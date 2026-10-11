/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// 8 octobre 2026 : flouter_zone et flous_lister (Agents in-app/outils/montage.json). Un nom, un numéro,
// un visage ou une conversation lisible à l'écran se cache par un rectangle flouté qui suit ce qu'il
// cache. Le moyen est l'effet « Motion Tracker » de l'application (filtre MLT opencv.tracker) : son
// analyse est lancée ici sans fenêtre, par melt, dans un processus à part, et ses images clés sont
// écrites dans l'effet, sans cadre dessiné.
//
// Trois choses à savoir :
// - le filtre ne suit bien un petit texte que si on lui donne de quoi s'accrocher : l'analyse suit une
//   zone plus large que la zone à cacher, et le flou reprend la place de la petite zone dedans ;
// - les effets du plan placés avant le flou (Transformer, étalonnage) sont rejoués pendant l'analyse,
//   à leurs positions d'origine : le fichier d'analyse reprend la forme d'un clip de projet ;
// - aucun producteur n'est ouvert dans l'application : le cache des décodeurs de MLT est commun à
//   tout le processus (document 6, 2 octobre 2026).

#include "koracutagenttools.h"
#include "koracutagenttools_p.h"
#include "koracutvisible.h"

#include "assets/keyframes/model/keyframemodel.hpp"
#include "assets/keyframes/model/keyframemodellist.hpp"
#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "effects/effectsrepository.hpp"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "kdenlivesettings.h"
#include "macros.hpp"
#include "profiles/profilemodel.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "xml/xml.hpp"

#include <KLocalizedString>
#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QPointer>
#include <QProcess>
#include <QRectF>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QXmlStreamWriter>
#include <cmath>
#include <mlt++/MltFilter.h>
#include <mlt++/MltProducer.h>

using namespace KoraCutAgentToolsPrivate;

namespace {

const int kMaxSeconds = 30;
const char *kMark = "kdenlive:koracut_flou";
const QString kTracker = QStringLiteral("opencv.tracker");
const QString kTransform = QStringLiteral("qtblend");

// Une image source posée dans un cadre : X = ox + x × sx.
struct Placement
{
    double sx = 1.;
    double sy = 1.;
    double ox = 0.;
    double oy = 0.;
    QRectF map(const QRectF &r) const { return QRectF(ox + r.x() * sx, oy + r.y() * sy, r.width() * sx, r.height() * sy); }
    QRectF unmap(const QRectF &r) const { return QRectF((r.x() - ox) / sx, (r.y() - oy) / sy, r.width() / sx, r.height() / sy); }
};

// Comme le Transformer de MLT : l'image garde ses proportions et se centre dans le cadre, sauf « distort ».
// @p sar : la forme des pixels du projet (1 pour des pixels carrés) ; le cadre est en pixels du projet.
Placement fit(const QSizeF &source, const QRectF &box, double sar, bool distort = false)
{
    Placement p;
    if (source.width() <= 0 || source.height() <= 0 || box.width() <= 0 || box.height() <= 0 || sar <= 0) {
        return p;
    }
    p.sx = box.width() / source.width();
    p.sy = box.height() / source.height();
    if (!distort) {
        const double shown = std::min(box.width() * sar / source.width(), box.height() / source.height());
        p.sx = shown / sar;
        p.sy = shown;
    }
    p.ox = box.x() + (box.width() - source.width() * p.sx) / 2.;
    p.oy = box.y() + (box.height() - source.height() * p.sy) / 2.;
    return p;
}

// « x y l h o » (ou « N=x y l h o ») vers un rectangle ; faux si le texte ne se lit pas.
bool parseRect(QString text, QRectF &rect)
{
    if (text.contains(QLatin1Char('='))) {
        text = text.section(QLatin1Char('='), 1);
    }
    const QStringList parts = text.section(QLatin1Char(';'), 0, 0).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 4 || text.contains(QLatin1Char('%'))) {
        return false;
    }
    bool ok[4];
    const double x = parts.at(0).toDouble(&ok[0]);
    const double y = parts.at(1).toDouble(&ok[1]);
    const double w = parts.at(2).toDouble(&ok[2]);
    const double h = parts.at(3).toDouble(&ok[3]);
    if (!ok[0] || !ok[1] || !ok[2] || !ok[3] || w <= 0 || h <= 0) {
        return false;
    }
    rect = QRectF(x, y, w, h);
    return true;
}

QJsonObject rectJson(const QRectF &r)
{
    return QJsonObject{{QStringLiteral("x_px"), qRound(r.x())},
                       {QStringLiteral("y_px"), qRound(r.y())},
                       {QStringLiteral("largeur_px"), qRound(r.width())},
                       {QStringLiteral("hauteur_px"), qRound(r.height())}};
}

// Le Transformer du plan, vu du flou : où il pose l'image source à une image donnée du média.
struct Transform
{
    std::shared_ptr<EffectItemModel> effect;
    bool distort = false;
    bool rotated = false;
    bool animated = false;

    QRectF rectAt(int sourceFrame, const QRectF &fallback) const
    {
        if (!effect) {
            return fallback;
        }
        QString value;
        const QModelIndex index = effect->getParamIndexFromName(QStringLiteral("rect"));
        const std::shared_ptr<KeyframeModelList> models = effect->getKeyframeModel();
        if (KeyframeModel *keys = models ? models->getKeyModel(QPersistentModelIndex(index)) : nullptr) {
            value = keys->getInterpolatedValue(GenTime(sourceFrame, pCore->getCurrentFps())).toString();
        }
        QRectF rect;
        if (parseRect(value, rect) || parseRect(effect->getParamFromName(QStringLiteral("rect")).toString(), rect)) {
            return rect;
        }
        return fallback;
    }
};

Transform readTransform(const std::shared_ptr<EffectItemModel> &effect)
{
    Transform t;
    t.effect = effect;
    if (!effect) {
        return t;
    }
    t.distort = effect->getParamFromName(QStringLiteral("distort")).toInt() == 1;
    // Une rotation, fixe ou animée : tout nombre non nul dans la valeur.
    static const QRegularExpression nonZero(QStringLiteral("(?:^|[=;\\s])-?(?:0*[1-9]\\d*(?:\\.\\d+)?|0+\\.0*[1-9]\\d*)(?:;|$)"));
    t.rotated = nonZero.match(effect->getParamFromName(QStringLiteral("rotation")).toString()).hasMatch();
    t.animated = effect->getParamFromName(QStringLiteral("rect")).toString().contains(QLatin1Char(';'));
    return t;
}

// Où va le flou dans la pile, et ce qu'il voit avant lui.
struct StackPlan
{
    /// Le flou passe avant ce Transformer (nul : il reste en fin de pile).
    std::shared_ptr<EffectItemModel> before;
    /// Le Transformer actif du plan, qu'il soit avant ou après le flou.
    Transform transform;
    /// Les effets actifs rejoués pendant l'analyse, dans l'ordre de la pile.
    std::vector<std::shared_ptr<EffectItemModel>> preceding;
    /// Un autre effet qui déplace l'image : les coordonnées données en pixels de la source sont approchées.
    bool otherGeometry = false;
};

bool movesImage(const QString &effectId)
{
    static const QStringList ids{QStringLiteral("affine"),     QStringLiteral("pan_zoom"),  QStringLiteral("crop"),     QStringLiteral("qtcrop"),
                                 QStringLiteral("mirror"),     QStringLiteral("avfilter.hflip"), QStringLiteral("avfilter.vflip"), QStringLiteral("frei0r.defish0r"),
                                 QStringLiteral("frei0r.lenscorrection"), QStringLiteral("frei0r.c0rners"), QStringLiteral("avfilter.rotate"), QStringLiteral("rotation")};
    return ids.contains(effectId);
}

StackPlan planStack(const std::shared_ptr<EffectStackModel> &stack, bool sameAspect)
{
    StackPlan plan;
    const auto effects = visibleEffects(stack);
    std::shared_ptr<EffectItemModel> first;
    for (const auto &effect : effects) {
        if (effect->isAssetEnabled() && effect->getAssetId() == kTransform) {
            first = effect;
            break;
        }
    }
    plan.transform = readTransform(first);
    // Avant le Transformer, le flou reste collé à la source quoi qu'on fasse ensuite du cadrage. C'est
    // possible quand le Transformer est un effet ordinaire (l'effet intégré reste en tête de pile) et
    // que la source a les proportions du projet (sinon le filtre reçoit une image d'une autre forme
    // selon qu'un Transformer le suit ou non).
    if (first && !first->isBuiltIn() && sameAspect) {
        plan.before = first;
    }
    for (const auto &effect : effects) {
        if (plan.before && effect == plan.before) {
            break;
        }
        if (!effect->isAssetEnabled() || effect->isAudio()) {
            continue;
        }
        plan.preceding.push_back(effect);
        if (movesImage(effect->getAssetId()) || (effect->getAssetId() == kTransform && effect != first)) {
            plan.otherGeometry = true;
        }
    }
    return plan;
}

QString meltProgram()
{
    const QString forced = qEnvironmentVariable("KORACUT_MELT");
    if (!forced.isEmpty()) {
        return QFileInfo(forced).isExecutable() ? forced : QString();
    }
    const QString configured = KdenliveSettings::meltpath();
    if (!configured.isEmpty() && QFileInfo(configured).isExecutable()) {
        return configured;
    }
    const QString dir = QCoreApplication::applicationDirPath();
    for (const QString &candidate : {dir + QStringLiteral("/melt"), dir + QStringLiteral("/melt-7"), dir + QStringLiteral("/melt.exe")}) {
        if (QFileInfo(candidate).isExecutable()) {
            return candidate;
        }
    }
    const QString found = QStandardPaths::findExecutable(QStringLiteral("melt"));
    return found.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("melt-7")) : found;
}

void writeProperty(QXmlStreamWriter &xml, const QString &name, const QString &value)
{
    xml.writeStartElement(QStringLiteral("property"));
    xml.writeAttribute(QStringLiteral("name"), name);
    xml.writeCharacters(value);
    xml.writeEndElement();
}

void writeProfile(QXmlStreamWriter &xml)
{
    const std::unique_ptr<ProfileModel> &profile = pCore->getCurrentProfile();
    xml.writeStartElement(QStringLiteral("profile"));
    xml.writeAttribute(QStringLiteral("description"), QStringLiteral("koracut"));
    xml.writeAttribute(QStringLiteral("width"), QString::number(profile->width()));
    xml.writeAttribute(QStringLiteral("height"), QString::number(profile->height()));
    xml.writeAttribute(QStringLiteral("progressive"), QString::number(profile->progressive() ? 1 : 0));
    xml.writeAttribute(QStringLiteral("sample_aspect_num"), QString::number(profile->sample_aspect_num()));
    xml.writeAttribute(QStringLiteral("sample_aspect_den"), QString::number(profile->sample_aspect_den()));
    xml.writeAttribute(QStringLiteral("display_aspect_num"), QString::number(profile->display_aspect_num()));
    xml.writeAttribute(QStringLiteral("display_aspect_den"), QString::number(profile->display_aspect_den()));
    xml.writeAttribute(QStringLiteral("frame_rate_num"), QString::number(profile->frame_rate_num()));
    xml.writeAttribute(QStringLiteral("frame_rate_den"), QString::number(profile->frame_rate_den()));
    xml.writeAttribute(QStringLiteral("colorspace"), QString::number(profile->colorspace()));
    xml.writeEndElement();
}

// Le plan tel qu'il est dans le projet : le média entier, une entrée de playlist bornée au plan, et
// sur cette entrée les effets qui précèdent le flou. Leurs images clés gardent ainsi leurs positions.
bool writeClipFile(const QString &path, const std::shared_ptr<ProjectClip> &binClip, int in, int out, const std::vector<std::shared_ptr<EffectItemModel>> &preceding)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    QXmlStreamWriter xml(&file);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("mlt"));
    xml.writeAttribute(QStringLiteral("LC_NUMERIC"), QStringLiteral("C"));
    writeProfile(xml);
    xml.writeStartElement(QStringLiteral("producer"));
    xml.writeAttribute(QStringLiteral("id"), QStringLiteral("source"));
    writeProperty(xml, QStringLiteral("resource"), binClip->url());
    // Ce que la personne a forcé sur le média (rotation, cadence, flux vidéo choisi).
    if (const std::shared_ptr<Mlt::Producer> original = binClip->originalProducer()) {
        for (const char *name : {"rotate", "autorotate", "force_aspect_ratio", "force_fps", "force_progressive", "force_tff", "force_colorspace", "set.force_full_luma",
                                 "vstream", "video_index"}) {
            if (original->property_exists(name)) {
                const QString value = QString::fromUtf8(original->get(name));
                if (!value.isEmpty()) {
                    writeProperty(xml, QString::fromLatin1(name), value);
                }
            }
        }
    }
    xml.writeEndElement();
    xml.writeStartElement(QStringLiteral("playlist"));
    xml.writeAttribute(QStringLiteral("id"), QStringLiteral("plan"));
    xml.writeStartElement(QStringLiteral("entry"));
    xml.writeAttribute(QStringLiteral("producer"), QStringLiteral("source"));
    xml.writeAttribute(QStringLiteral("in"), QString::number(in));
    xml.writeAttribute(QStringLiteral("out"), QString::number(out));
    for (const auto &effect : preceding) {
        Mlt::Filter &filter = effect->filter();
        xml.writeStartElement(QStringLiteral("filter"));
        if (filter.get_int("out") > 0) {
            xml.writeAttribute(QStringLiteral("in"), QString::number(filter.get_int("in")));
            xml.writeAttribute(QStringLiteral("out"), QString::number(filter.get_int("out")));
        }
        for (int i = 0; i < filter.count(); ++i) {
            const QString name = QString::fromUtf8(filter.get_name(i));
            const char *value = filter.get(i);
            if (name.isEmpty() || name.startsWith(QLatin1Char('_')) || !value || name == QLatin1String("in") || name == QLatin1String("out") ||
                name == QLatin1String("mlt_type")) {
                continue;
            }
            writeProperty(xml, name, QString::fromUtf8(value));
        }
        xml.writeEndElement();
    }
    xml.writeEndElement(); // entry
    xml.writeEndElement(); // playlist
    xml.writeEndElement(); // mlt
    xml.writeEndDocument();
    return file.flush();
}

// Par-dessus, le plan relu comme un seul producteur, avec le filtre de suivi : ses positions vont de 0
// à @p last, et le consommateur xml recopie le filtre avec ses résultats dans @p result.
bool writeAnalysisFile(const QString &path, const QString &clipFile, const QString &result, int last, const QRect &rect)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    QXmlStreamWriter xml(&file);
    xml.writeStartDocument();
    xml.writeStartElement(QStringLiteral("mlt"));
    xml.writeAttribute(QStringLiteral("LC_NUMERIC"), QStringLiteral("C"));
    writeProfile(xml);
    xml.writeStartElement(QStringLiteral("consumer"));
    xml.writeAttribute(QStringLiteral("mlt_service"), QStringLiteral("xml"));
    xml.writeAttribute(QStringLiteral("resource"), result);
    xml.writeAttribute(QStringLiteral("all"), QStringLiteral("1"));
    xml.writeAttribute(QStringLiteral("terminate_on_pause"), QStringLiteral("1"));
    xml.writeAttribute(QStringLiteral("audio_off"), QStringLiteral("1"));
    xml.writeAttribute(QStringLiteral("no_meta"), QStringLiteral("1"));
    xml.writeAttribute(QStringLiteral("real_time"), QStringLiteral("-1"));
    xml.writeAttribute(QStringLiteral("store"), QStringLiteral("kdenlive"));
    xml.writeEndElement();
    xml.writeStartElement(QStringLiteral("producer"));
    xml.writeAttribute(QStringLiteral("id"), QStringLiteral("analyse"));
    xml.writeAttribute(QStringLiteral("in"), QStringLiteral("0"));
    xml.writeAttribute(QStringLiteral("out"), QString::number(last));
    writeProperty(xml, QStringLiteral("mlt_service"), QStringLiteral("xml"));
    writeProperty(xml, QStringLiteral("resource"), clipFile);
    xml.writeStartElement(QStringLiteral("filter"));
    // La sortie du filtre dit à l'analyse où elle finit : c'est là qu'elle écrit ses résultats.
    xml.writeAttribute(QStringLiteral("out"), QString::number(last));
    writeProperty(xml, QStringLiteral("mlt_service"), kTracker);
    writeProperty(xml, QStringLiteral("rect"), QStringLiteral("%1 %2 %3 %4").arg(rect.x()).arg(rect.y()).arg(rect.width()).arg(rect.height()));
    // KCF : rapide, et sa boîte garde sa taille (CSRT la rétrécit sur un texte clair, MIL décroche).
    writeProperty(xml, QStringLiteral("algo"), QStringLiteral("KCF"));
    writeProperty(xml, QStringLiteral("steps"), QStringLiteral("1"));
    writeProperty(xml, QStringLiteral("shape_width"), QStringLiteral("0"));
    // Sans cadre ni flou, le filtre ne regarde pas l'image : un flou de type « aucun » le réveille.
    writeProperty(xml, QStringLiteral("blur"), QStringLiteral("1"));
    writeProperty(xml, QStringLiteral("blur_type"), QStringLiteral("4"));
    writeProperty(xml, QStringLiteral("kdenlive:id"), QStringLiteral("kdenlive-analysis"));
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndElement();
    xml.writeEndDocument();
    return file.flush();
}

struct Key
{
    int frame = 0;
    QRectF rect;
};

// « 0~=302 1234 220 140 0;1~=… » vers une liste triée.
QList<Key> parseResults(const QString &results)
{
    QList<Key> keys;
    const QStringList parts = results.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    static const QRegularExpression head(QStringLiteral("^\\s*(-?\\d+)[^=\\d]*=(.*)$"));
    for (const QString &part : parts) {
        const QRegularExpressionMatch m = head.match(part);
        Key key;
        if (!m.hasMatch() || !parseRect(m.captured(2), key.rect)) {
            continue;
        }
        key.frame = m.captured(1).toInt();
        keys.append(key);
    }
    std::sort(keys.begin(), keys.end(), [](const Key &a, const Key &b) { return a.frame < b.frame; });
    return keys;
}

// Ne garde que les images clés dont on ne peut pas se passer : entre deux images gardées, la ligne
// droite ne s'écarte pas de plus de @p tolerance pixels des valeurs d'origine (le suivi tremble d'un
// pixel d'une image à l'autre, la marge du flou le couvre).
QList<Key> simplify(const QList<Key> &keys, double tolerance)
{
    if (keys.size() < 3) {
        return keys;
    }
    QList<Key> kept{keys.first()};
    int anchor = 0;
    while (anchor < keys.size() - 1) {
        int reach = anchor + 1;
        for (int candidate = anchor + 2; candidate < keys.size(); ++candidate) {
            const Key &a = keys.at(anchor);
            const Key &b = keys.at(candidate);
            bool fits = true;
            for (int i = anchor + 1; i < candidate && fits; ++i) {
                const Key &k = keys.at(i);
                const double t = double(k.frame - a.frame) / double(b.frame - a.frame);
                fits = std::abs(a.rect.x() + (b.rect.x() - a.rect.x()) * t - k.rect.x()) <= tolerance &&
                       std::abs(a.rect.y() + (b.rect.y() - a.rect.y()) * t - k.rect.y()) <= tolerance &&
                       std::abs(a.rect.width() + (b.rect.width() - a.rect.width()) * t - k.rect.width()) <= tolerance &&
                       std::abs(a.rect.height() + (b.rect.height() - a.rect.height()) * t - k.rect.height()) <= tolerance;
            }
            if (!fits) {
                break;
            }
            reach = candidate;
        }
        kept.append(keys.at(reach));
        anchor = reach;
    }
    return kept;
}

QString serialize(const QList<Key> &keys)
{
    QStringList parts;
    for (const Key &key : keys) {
        parts << QStringLiteral("%1=%2 %3 %4 %5 0").arg(key.frame).arg(qRound(key.rect.x())).arg(qRound(key.rect.y())).arg(qRound(key.rect.width())).arg(qRound(key.rect.height()));
    }
    return parts.join(QLatin1Char(';'));
}

QRectF clampTo(const QRectF &rect, const QSizeF &frame)
{
    return rect.intersected(QRectF(QPointF(0, 0), frame));
}

struct Analysis
{
    QList<Key> keys; ///< la zone suivie, positions comptées depuis l'image de départ
    QString failure; ///< vide si melt a rendu des résultats
    bool unavailable = false;
    qint64 ms = 0;
};

// Lance melt sur le plan, sans bloquer l'interface, et rend les rectangles suivis.
Analysis track(const std::shared_ptr<ProjectClip> &binClip, const std::vector<std::shared_ptr<EffectItemModel>> &preceding, int in, int out, const QRect &rect,
               const QString &label)
{
    Analysis analysis;
    const QString melt = meltProgram();
    if (melt.isEmpty()) {
        analysis.unavailable = true;
        analysis.failure = QStringLiteral("Le programme melt est introuvable.");
        return analysis;
    }
    QTemporaryDir dir(QDir::temp().absoluteFilePath(QStringLiteral("koracut-flou-XXXXXX")));
    if (!dir.isValid()) {
        analysis.failure = QStringLiteral("Le dossier temporaire n'a pas pu être créé.");
        return analysis;
    }
    const QString clipFile = dir.filePath(QStringLiteral("plan.mlt"));
    const QString analysisFile = dir.filePath(QStringLiteral("analyse.mlt"));
    const QString resultFile = dir.filePath(QStringLiteral("resultat.mlt"));
    if (!writeClipFile(clipFile, binClip, in, out, preceding) || !writeAnalysisFile(analysisFile, clipFile, resultFile, out - in, rect)) {
        analysis.failure = QStringLiteral("Le fichier d'analyse n'a pas pu être écrit.");
        return analysis;
    }
    QProcess process;
    // Un chemin avec des accents ne s'ouvre pas dans une locale sans UTF-8.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString locale = env.value(QStringLiteral("LC_ALL"), env.value(QStringLiteral("LC_CTYPE"), env.value(QStringLiteral("LANG"))));
    if (!locale.contains(QLatin1String("UTF-8"), Qt::CaseInsensitive) && !locale.contains(QLatin1String("utf8"), Qt::CaseInsensitive)) {
#ifdef Q_OS_MACOS
        env.insert(QStringLiteral("LC_ALL"), QStringLiteral("en_US.UTF-8"));
#elif !defined(Q_OS_WIN)
        env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
#endif
    }
    process.setProcessEnvironment(env);
    process.setProgram(melt);
    process.setArguments({QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("progress=1"), analysisFile});
    QEventLoop loop;
    QByteArray errors;
    QObject::connect(&process, &QProcess::readyReadStandardError, &loop, [&process, &errors, label]() {
        errors += process.readAllStandardError();
        static const QRegularExpression counter(QStringLiteral("percentage:\\s*(\\d+)"));
        QRegularExpressionMatchIterator it = counter.globalMatch(QString::fromUtf8(errors.right(200)));
        QRegularExpressionMatch last;
        while (it.hasNext()) {
            last = it.next();
        }
        if (last.hasMatch()) {
            KoraCutVisible::self()->progressLine(QString(), i18nc("@info:status area tracking progress, %1 percent, %2 clip name", "Tracking the area to blur: %1% · %2", last.captured(1).toInt(), label));
        }
    });
    QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
    QObject::connect(&process, &QProcess::errorOccurred, &loop, [&loop](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            loop.quit();
        }
    });
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    // Vingt fois la durée du plan, et au moins une minute : le calcul mesuré tient dans la durée du plan.
    timeout.start(std::max(60, int(20 * (out - in + 1) / projectFps())) * 1000);
    QElapsedTimer timer;
    timer.start();
    KoraCutVisible::self()->progressLine(QString(), i18nc("@info:status area tracking progress, %1 percent, %2 clip name", "Tracking the area to blur: %1% · %2", 0, label));
    process.start();
    loop.exec();
    analysis.ms = timer.elapsed();
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(3000);
        analysis.failure = QStringLiteral("Le suivi n'a pas abouti dans le temps prévu.");
        return analysis;
    }
    if (process.error() == QProcess::FailedToStart) {
        analysis.unavailable = true;
        analysis.failure = QStringLiteral("Le programme melt ne se lance pas : %1").arg(melt);
        return analysis;
    }
    QDomDocument dom;
    QString results;
    if (Xml::docContentFromFile(dom, resultFile, false)) {
        const QDomNodeList filters = dom.elementsByTagName(QStringLiteral("filter"));
        for (int i = 0; i < filters.count() && results.isEmpty(); ++i) {
            const QDomElement filter = filters.item(i).toElement();
            if (Xml::getXmlProperty(filter, QStringLiteral("kdenlive:id")) == QLatin1String("kdenlive-analysis")) {
                results = Xml::getXmlProperty(filter, QStringLiteral("results"));
            }
        }
    }
    analysis.keys = parseResults(results);
    if (analysis.keys.isEmpty()) {
        const QString detail = QString::fromUtf8(errors).remove(QRegularExpression(QStringLiteral("Current Frame:[^\\n\\r]*"))).trimmed().section(QLatin1Char('\n'), -1).left(200);
        analysis.failure = detail.isEmpty() ? QStringLiteral("L'analyse n'a rien rendu.") : QStringLiteral("L'analyse n'a rien rendu. %1").arg(detail);
    }
    return analysis;
}

int blurTypeCode(const QString &type)
{
    // Les codes du filtre : 0 médian, 1 gaussien, 2 pixels, 3 aplat, 4 rien.
    if (type == QLatin1String("pixels")) {
        return 2;
    }
    if (type == QLatin1String("plein")) {
        return 3;
    }
    return 1;
}

QString blurTypeName(int code)
{
    switch (code) {
    case 2:
        return QStringLiteral("pixels");
    case 3:
        return QStringLiteral("plein");
    case 4:
        return QStringLiteral("aucun");
    default:
        return QStringLiteral("flou");
    }
}

QJsonObject flouterZone(const Timeline &timeline, const Args &args)
{
    int clipId = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = clipIdCheck(timeline, clipId); !e.isEmpty()) {
        return e;
    }
    // Le son d'un plan : son image groupée est prise.
    if (timeline->isAudioTrack(timeline->getClipTrackId(clipId))) {
        const int partner = timeline->getClipSplitPartner(clipId);
        if (partner < 0 || !timeline->isClip(partner) || timeline->isAudioTrack(timeline->getClipTrackId(partner))) {
            return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("Le clip %1 est un son : il n'a pas d'image à flouter.").arg(clipId),
                         QStringLiteral("Donne l'id du clip vidéo du plan (champ k = « v » dans etat_timeline)."));
        }
        clipId = partner;
    }
    if (!EffectsRepository::get()->exists(kTracker)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le module de suivi (OpenCV) n'est pas installé avec cette KoraCut : flouter_zone ne peut rien poser."),
                     QStringLiteral("Dis-le à la personne. À la place : recadrer le plan pour sortir la zone de l'image (transformer_clip), ou choisir un autre plan."));
    }
    const QJsonObject zoneArg = args.value(QStringLiteral("zone")).toObject();
    QRectF zone(zoneArg.value(QStringLiteral("x_px")).toDouble(), zoneArg.value(QStringLiteral("y_px")).toDouble(), zoneArg.value(QStringLiteral("largeur_px")).toDouble(),
                zoneArg.value(QStringLiteral("hauteur_px")).toDouble());
    if (zone.width() < 4 || zone.height() < 4) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("zone doit donner x_px, y_px, largeur_px et hauteur_px, avec au moins 4 pixels de côté."),
                     QStringLiteral("Regarde l'image (voir_image) et donne le rectangle qui entoure ce qu'il faut cacher."));
    }
    const QString type = args.value(QStringLiteral("type")).toString(QStringLiteral("flou"));
    if (!QStringList{QStringLiteral("flou"), QStringLiteral("pixels"), QStringLiteral("plein")}.contains(type)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("type vaut flou, pixels ou plein."));
    }
    const QString frameOfReference = args.value(QStringLiteral("repere")).toString(QStringLiteral("source"));
    if (frameOfReference != QLatin1String("source") && frameOfReference != QLatin1String("image")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("repere vaut source ou image."));
    }
    const bool imageReference = frameOfReference == QLatin1String("image");
    const int force = std::clamp(args.value(QStringLiteral("force")).toInt(40), 10, 200);
    const bool wantTracking = args.value(QStringLiteral("suivre")).toBool(true);
    const QString label = args.value(QStringLiteral("etiquette")).toString().trimmed().left(80);

    const int in = timeline->getClipIn(clipId);
    const int position = timeline->getClipPosition(clipId);
    const int length = timeline->getClipPlaytime(clipId);
    const int start = args.value(QStringLiteral("position_dans_clip")).toInt(0);
    if (start < 0 || start >= length) {
        return error(QStringLiteral("hors_clip"), QStringLiteral("position_dans_clip (%1) est hors du plan, qui dure %2 images.").arg(start).arg(length),
                     QStringLiteral("0 est la première image du plan."));
    }
    const auto binClip = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(clipId));
    if (!binClip) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Le média du clip %1 est introuvable.").arg(clipId));
    }
    const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(clipId);
    if (!stack) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le clip %1 n'a pas de pile d'effets.").arg(clipId));
    }
    const QSizeF profileSize = pCore->getCurrentFrameSize();
    const QRectF profileRect(QPointF(0, 0), profileSize);
    QSizeF sourceSize = binClip->getFrameSize();
    if (sourceSize.width() < 2 || sourceSize.height() < 2) {
        sourceSize = profileSize;
    }
    const double sar = pCore->getCurrentProfile()->sar() > 0 ? pCore->getCurrentProfile()->sar() : 1.;
    const double sourceRatio = sourceSize.width() / sourceSize.height();
    const double profileRatio = profileSize.width() * sar / profileSize.height();
    const bool sameAspect = std::abs(sourceRatio - profileRatio) < 0.01 * profileRatio;
    const StackPlan plan = planStack(stack, sameAspect);

    // Trois repères : la source (pixels du média), l'image du projet (ce que montre voir_image) et
    // celui du filtre, qui est l'image du projet quand le flou vient après le Transformer, la source
    // ramenée à la taille du projet quand il vient avant.
    const Placement sourceInProfile = fit(sourceSize, profileRect, sar);
    const auto toImage = [&](int frameInClip) -> Placement { return fit(sourceSize, plan.transform.rectAt(in + frameInClip, profileRect), sar, plan.transform.distort); };
    const auto toFilter = [&](int frameInClip) -> Placement { return plan.before ? sourceInProfile : toImage(frameInClip); };

    const QRectF sourceZone = clampTo(imageReference ? toImage(start).unmap(zone) : zone, sourceSize);
    if (sourceZone.width() < 2 || sourceZone.height() < 2) {
        return error(QStringLiteral("entree_invalide"),
                     imageReference ? QStringLiteral("La zone est hors de l'image du plan (image du projet : %1 × %2 pixels).").arg(profileSize.width()).arg(profileSize.height())
                                    : QStringLiteral("La zone est hors de l'image source, qui fait %1 × %2 pixels.").arg(sourceSize.width()).arg(sourceSize.height()),
                     QStringLiteral("Vérifie le repère : source (pixels du média, défaut) ou image (pixels de l'image du projet, celle de voir_image)."));
    }
    // Une petite marge : le suivi tremble d'un ou deux pixels, et un nom qui dépasse reste lisible.
    QRectF filterZone = toFilter(start).map(sourceZone);
    const double margin = std::clamp(0.08 * std::min(filterZone.width(), filterZone.height()), 3., 12.);
    filterZone = clampTo(filterZone.adjusted(-margin, -margin, margin, margin), profileSize);
    if (filterZone.width() < 2 || filterZone.height() < 2) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("À cette image, la zone est hors du cadre du projet : il n'y a rien à cacher."),
                     QStringLiteral("Choisis une image du plan où la zone est visible (position_dans_clip)."));
    }

    const bool isVideo = binClip->clipType() == ClipType::AV || binClip->clipType() == ClipType::Video;
    QStringList notes;
    bool tracking = wantTracking;
    if (tracking && !isVideo) {
        // Une image, un titre, une couleur ne bougent pas ; une séquence imbriquée n'est pas suivie.
        tracking = false;
        if (binClip->clipType() == ClipType::Timeline || binClip->clipType() == ClipType::Playlist) {
            notes << QStringLiteral("Ce clip est une séquence imbriquée : la zone n'est pas suivie, le rectangle reste fixe. Pour un suivi, floute le plan dans sa séquence d'origine.");
        }
    }
    if (tracking) {
        if (!QFileInfo::exists(binClip->url())) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le fichier du média est introuvable : %1").arg(binClip->url()),
                         QStringLiteral("Rebranche le disque du média, ou passe suivre à false pour un rectangle fixe."));
        }
        if (!qFuzzyCompare(timeline->getClipSpeed(clipId), 1.0)) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("Remets la vitesse de ce plan à 100 % avant de suivre une zone."),
                         QStringLiteral("changer_vitesse_clip avec vitesse_pourcent 100, puis flouter_zone. Ou suivre à false pour un rectangle fixe."));
        }
        if (length - start > kMaxSeconds * projectFps()) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Ce plan dure plus de %1 secondes : le suivi serait long à calculer.").arg(kMaxSeconds),
                         QStringLiteral("Coupe le plan autour du moment où la zone est visible (couper_clip), puis floute ce morceau seul."));
        }
    }

    // ---- Le suivi -------------------------------------------------------------------------------
    QList<Key> keys; // positions comptées depuis le début du plan, rectangles dans le repère du filtre
    qint64 elapsed = 0;
    bool lost = false;
    QString lostReason;
    if (tracking) {
        // De quoi s'accrocher : la zone suivie déborde de la zone à cacher.
        const double pad = std::clamp(0.6 * std::max(filterZone.width(), filterZone.height()), 40., 200.);
        const QRectF context = clampTo(filterZone.adjusted(-pad, -pad, pad, pad), profileSize);
        const QRect contextPx = context.toAlignedRect().intersected(QRect(0, 0, int(profileSize.width()), int(profileSize.height())));
        const QPointer<KdenliveDoc> guard(pCore->currentDoc());
        KdenliveDoc *doc = pCore->currentDoc();
        const Analysis analysis = track(binClip, plan.preceding, in + start, in + length - 1, contextPx, QFileInfo(binClip->url()).completeBaseName());
        elapsed = analysis.ms;
        if (analysis.unavailable) {
            return error(QStringLiteral("non_disponible"), analysis.failure, QStringLiteral("Passe suivre à false pour un rectangle fixe, ou dis-le à la personne."));
        }
        if (!guard || pCore->currentDoc() != doc) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le projet a été fermé pendant le suivi : rien n'a été posé."));
        }
        if (!timeline->isClip(clipId) || timeline->getClipIn(clipId) != in || timeline->getClipPlaytime(clipId) != length || timeline->getClipPosition(clipId) != position) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le plan a changé pendant le suivi : rien n'a été posé."),
                         QStringLiteral("Relis etat_timeline puis rappelle flouter_zone sur le plan."));
        }
        if (analysis.keys.size() < 2) {
            lost = true;
            lostReason = analysis.failure.isEmpty() ? QStringLiteral("l'analyse n'a rendu qu'une image") : analysis.failure;
        } else {
            // La zone à cacher garde sa place dans la zone suivie. Sous un Transformer animé, elle
            // grandit ou rétrécit avec le cadrage (la boîte du suivi, elle, garde sa taille).
            const double scale0 = toFilter(start).sx;
            const QRectF context0(contextPx);
            const QPointF offset = filterZone.center() - context0.center();
            for (const Key &tracked : analysis.keys) {
                // La boîte du suivi garde sa taille (KCF) ; le filtre la rogne quand elle touche un bord
                // de l'image, et c'est alors le côté opposé au bord qui dit où elle est.
                QRectF box(QPointF(0, 0), context0.size());
                if (tracked.rect.x() <= 0.5 && tracked.rect.width() < context0.width()) {
                    box.moveRight(tracked.rect.right());
                } else if (tracked.rect.right() >= profileSize.width() - 1.5 && tracked.rect.width() < context0.width()) {
                    box.moveLeft(tracked.rect.left());
                } else {
                    box.moveLeft(tracked.rect.center().x() - box.width() / 2.);
                }
                if (tracked.rect.y() <= 0.5 && tracked.rect.height() < context0.height()) {
                    box.moveBottom(tracked.rect.bottom());
                } else if (tracked.rect.bottom() >= profileSize.height() - 1.5 && tracked.rect.height() < context0.height()) {
                    box.moveTop(tracked.rect.top());
                } else {
                    box.moveTop(tracked.rect.center().y() - box.height() / 2.);
                }
                const double zoom = plan.before || scale0 <= 0 ? 1. : toFilter(start + tracked.frame).sx / scale0;
                const QPointF centre = box.center() + offset * zoom;
                QRectF rect(QPointF(0, 0), filterZone.size() * zoom);
                rect.moveCenter(centre);
                keys.append(Key{start + tracked.frame, rect});
                if (tracked.rect.width() < context0.width() / 3. || tracked.rect.height() < context0.height() / 3. || !profileRect.contains(centre)) {
                    lost = true;
                    lostReason = QStringLiteral("la zone est sortie de l'image à l'image %1 du plan").arg(start + tracked.frame);
                    break;
                }
            }
        }
    }
    const bool tracked = tracking && !lost;
    if (!tracked) {
        // Un rectangle fixe par rapport à la source : sous un Transformer animé placé avant le flou,
        // il suit le cadrage.
        keys.clear();
        if (!plan.before && plan.transform.animated) {
            for (int frame = 0; frame < length; frame += 2) {
                keys.append(Key{frame, clampTo(toFilter(frame).map(sourceZone).adjusted(-margin, -margin, margin, margin), profileSize)});
            }
        } else {
            keys.append(Key{0, filterZone});
        }
        if (keys.last().frame != length - 1) {
            keys.append(Key{length - 1, plan.before || !plan.transform.animated
                                            ? keys.last().rect
                                            : clampTo(toFilter(length - 1).map(sourceZone).adjusted(-margin, -margin, margin, margin), profileSize)});
        }
    } else if (start > 0) {
        // Avant l'image de départ, la zone reste où elle était à cette image.
        keys.prepend(Key{0, keys.first().rect});
    }
    // Sans bouger depuis la moitié du plan alors qu'elle bougeait avant : le suivi a peut-être décroché.
    bool frozenTail = false;
    bool neverMoved = false;
    if (tracked && keys.size() >= 8) {
        const QRectF last = keys.last().rect;
        int tail = 0;
        for (int i = keys.size() - 1; i >= 0 && keys.at(i).rect == last; --i) {
            ++tail;
        }
        neverMoved = tail == keys.size();
        frozenTail = !neverMoved && tail >= std::max(8, int(keys.size() * 0.5));
    }
    const int rawCount = keys.size();
    keys = simplify(keys, 1.5);
    if (keys.size() < 2) {
        keys.append(Key{length - 1, keys.first().rect});
    }
    const QRectF firstRect = keys.first().rect;
    const QRectF lastRect = keys.last().rect;
    // Les images clés de l'effet se comptent depuis le début du média.
    for (Key &key : keys) {
        key.frame += in;
    }

    // ---- La pose : une seule étape d'annulation ---------------------------------------------------
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    const int rowsBefore = stack->rowCount();
    if (!stack->appendEffectWithParams(kTracker, {{QString::fromLatin1(kMark), label.isEmpty() ? QStringLiteral("flou") : label}}, undo, redo) || stack->rowCount() <= rowsBefore) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("L'effet de flou ne peut pas se poser sur le clip %1.").arg(clipId));
    }
    const auto effect = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(stack->rowCount() - 1));
    if (!effect || effect->getAssetId() != kTracker) {
        undo();
        return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet de flou n'a pas été retrouvé dans la pile du clip %1.").arg(clipId));
    }
    const int typeCode = blurTypeCode(type);
    setParametersWithUndo(effect,
                          {{QStringLiteral("rect"), QStringLiteral("%1 %2 %3 %4").arg(qRound(firstRect.x())).arg(qRound(firstRect.y())).arg(qRound(firstRect.width())).arg(qRound(firstRect.height()))},
                           {QStringLiteral("algo"), QStringLiteral("KCF")},
                           {QStringLiteral("shape"), QStringLiteral("0")},
                           {QStringLiteral("shape_width"), QStringLiteral("0")},
                           {QStringLiteral("shape_color"), QStringLiteral("0x000000ff")},
                           {QStringLiteral("blur"), QString::number(force)},
                           {QStringLiteral("blur_type"), QString::number(typeCode)}},
                          undo, redo);
    // Les résultats en dernier : c'est eux qui font passer le filtre de l'analyse à l'application.
    setParametersWithUndo(effect, {{QStringLiteral("results"), serialize(keys)}}, undo, redo);
    QString place = plan.transform.effect ? QStringLiteral("apres_transformer") : QStringLiteral("sans_transformer");
    if (plan.before && plan.before->row() >= 0 && plan.before->row() < effect->row()) {
        if (stack->moveEffectWithUndo(plan.before->row(), effect, undo, redo)) {
            place = QStringLiteral("avant_transformer");
        }
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Blur area"));

    // ---- La réponse -----------------------------------------------------------------------------
    const auto report = [&](const QRectF &rect, int frameInClip) -> QJsonObject {
        const QRectF source = toFilter(frameInClip).unmap(rect);
        return rectJson(imageReference ? toImage(frameInClip).map(source) : source);
    };
    QJsonObject out{{QStringLiteral("ok"), true},
                    {QStringLiteral("clip_id"), clipId},
                    {QStringLiteral("effet_index"), effectRank(stack, effect)},
                    {QStringLiteral("effet_id"), kTracker},
                    {QStringLiteral("images_cles"), int(keys.size())},
                    {QStringLiteral("suivi"), tracked},
                    {QStringLiteral("zone_debut"), report(firstRect, 0)},
                    {QStringLiteral("zone_fin"), report(lastRect, length - 1)},
                    {QStringLiteral("repere"), frameOfReference},
                    {QStringLiteral("type"), type},
                    {QStringLiteral("force"), force},
                    {QStringLiteral("place"), place},
                    {QStringLiteral("duree_calcul_ms"), int(elapsed)}};
    if (!label.isEmpty()) {
        out.insert(QStringLiteral("etiquette"), label);
    }
    if (lost) {
        out.insert(QStringLiteral("suivi_perdu"), true);
        notes.prepend(QStringLiteral("suivi_perdu : %1. Un rectangle fixe a été posé à la place : vérifie la fin du plan avec voir_image, et s'il ne couvre plus la zone, "
                                     "supprime-le (supprimer_effet) et coupe le plan en morceaux plus courts, ou donne une zone plus large.")
                             .arg(lostReason));
    } else if (frozenTail) {
        notes << QStringLiteral("La zone ne bouge plus sur la fin du plan alors qu'elle bougeait avant : le suivi a peut-être décroché. Regarde la dernière image avec voir_image.");
    } else if (neverMoved && rawCount > 8) {
        notes << QStringLiteral("La zone n'a pas bougé pendant le plan (plan fixe, ou suivi qui n'a pas accroché) : regarde la dernière image avec voir_image.");
    }
    if (tracked && start > 0) {
        notes << QStringLiteral("Avant l'image %1 du plan, le rectangle reste où il est à cette image.").arg(start);
    }
    if (plan.transform.rotated) {
        notes << QStringLiteral("Le Transformer du plan tourne l'image : la rotation n'est pas comptée dans la conversion des coordonnées. Vérifie avec voir_image, et donne au besoin la zone avec repere image.");
    }
    if (plan.otherGeometry) {
        notes << QStringLiteral("Un autre effet du plan déplace l'image avant le flou : donne la zone avec repere image (ce que montre voir_image) si le rectangle tombe à côté.");
    }
    if (place == QLatin1String("apres_transformer")) {
        notes << QStringLiteral("Le flou est posé après le Transformer du plan : si le cadrage change ensuite, supprime ce flou et rappelle flouter_zone.");
    }
    if (!notes.isEmpty()) {
        out.insert(QStringLiteral("avertissement"), notes.join(QLatin1Char(' ')));
    }
    out.insert(QStringLiteral("conseil"), QStringLiteral("Vérifie le début et la fin du plan avec voir_image. Pour retirer ce flou : supprimer_effet avec ce clip_id et cet effet_index (flous_lister les retrouve)."));
    return out;
}

// flous_lister : les flous posés par flouter_zone (et les effets de suivi qui floutent), pour les
// retrouver et les retirer.
QJsonObject flousLister(const Timeline &timeline, const Args &args)
{
    const bool oneClip = args.contains(QStringLiteral("clip_id"));
    const int wanted = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (oneClip) {
        if (QJsonObject e = clipIdCheck(timeline, wanted); !e.isEmpty()) {
            return e;
        }
    }
    QJsonArray list;
    for (int trackId : tracksOnScreen(timeline)) {
        if (timeline->isAudioTrack(trackId)) {
            continue;
        }
        for (int clipId : clipsOnTrack(timeline, trackId)) {
            if (oneClip && clipId != wanted) {
                continue;
            }
            const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(clipId);
            const auto effects = visibleEffects(stack);
            const int in = timeline->getClipIn(clipId);
            for (size_t i = 0; i < effects.size(); ++i) {
                const auto &effect = effects.at(i);
                if (effect->getAssetId() != kTracker) {
                    continue;
                }
                const QString mark = QString::fromUtf8(effect->filter().get(kMark));
                const int blur = effect->getParamFromName(QStringLiteral("blur")).toInt();
                const int typeCode = effect->getParamFromName(QStringLiteral("blur_type")).toInt();
                if (mark.isEmpty() && (blur <= 0 || typeCode == 4)) {
                    continue; // un suivi qui ne cache rien
                }
                const QList<Key> keys = parseResults(effect->getParamFromName(QStringLiteral("results")).toString());
                bool moves = false;
                for (const Key &key : keys) {
                    moves = moves || key.rect != keys.first().rect;
                }
                QJsonObject o{{QStringLiteral("clip_id"), clipId},
                              {QStringLiteral("piste"), trackLabel(timeline, trackId)},
                              {QStringLiteral("position"), time(timeline->getClipPosition(clipId))},
                              {QStringLiteral("effet_index"), int(i)},
                              {QStringLiteral("type"), blurTypeName(typeCode)},
                              {QStringLiteral("force"), blur},
                              {QStringLiteral("suivi"), moves},
                              {QStringLiteral("images_cles"), int(keys.size())},
                              {QStringLiteral("actif"), effect->isAssetEnabled()}};
                if (!mark.isEmpty() && mark != QLatin1String("flou")) {
                    o.insert(QStringLiteral("etiquette"), mark);
                }
                if (!keys.isEmpty()) {
                    // Dans le repère du filtre : l'image du projet, ou la source à la taille du projet.
                    o.insert(QStringLiteral("zone_debut_image"), rectJson(keys.first().rect));
                    o.insert(QStringLiteral("zone_fin_image"), rectJson(keys.last().rect));
                    o.insert(QStringLiteral("debut_dans_clip"), std::max(0, keys.first().frame - in));
                }
                list.append(o);
            }
        }
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("flous"), list}, {QStringLiteral("nombre"), int(list.size())}};
}

} // namespace

namespace KoraCutAgentToolsPrivate {

QMap<QString, Tool> flouTools()
{
    return {
        {QStringLiteral("flouter_zone"), flouterZone},
        {QStringLiteral("flous_lister"), flousLister},
    };
}

} // namespace KoraCutAgentToolsPrivate
