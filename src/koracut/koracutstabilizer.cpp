/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutstabilizer.h"

#include "bin/clipcreator.hpp"
#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "mainwindow.h"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>

#include <QCryptographicHash>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#ifdef Q_OS_UNIX
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace {
const QString kSourceProperty = QStringLiteral("kdenlive:koracut_stabilise_source");
const QString kLevelProperty = QStringLiteral("kdenlive:koracut_stabilise_niveau");

/** Le dossier des fichiers de stabilisation : à côté du projet enregistré, sinon dans le dossier
 *  KoraCut des vidéos. Un projet neuf porte une URL sans fichier (voir CLAUDE.md) : on vérifie. */
QString stabilizationFolder()
{
    QString base;
    KdenliveDoc *doc = pCore->currentDoc();
    if (doc && !doc->url().isEmpty() && QFileInfo::exists(doc->url().toLocalFile())) {
        base = QFileInfo(doc->url().toLocalFile()).absolutePath();
    } else {
        base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/KoraCut");
    }
    const QString dir = base + QStringLiteral("/Stabilisation");
    QDir().mkpath(dir);
    return dir;
}

std::shared_ptr<TimelineItemModel> timelineFor(const QUuid &uuid)
{
    KdenliveDoc *doc = pCore->currentDoc();
    return doc ? doc->getTimeline(uuid, true) : nullptr;
}
} // namespace

KoraCutStabilizer *KoraCutStabilizer::instance()
{
    static QPointer<KoraCutStabilizer> self;
    if (!self) {
        self = new KoraCutStabilizer(qApp);
    }
    return self;
}

KoraCutStabilizer::KoraCutStabilizer(QObject *parent)
    : QObject(parent)
{
}

QList<KoraCutClipTools::Choice> KoraCutStabilizer::levels()
{
    return {
        {QStringLiteral("leger"), i18nc("@item stabilization level", "Light"), QString(),
         i18nc("@info:tooltip", "Smooths small shakes and crops the picture as little as possible.")},
        {QStringLiteral("moyen"), i18nc("@item stabilization level", "Medium"), QString(),
         i18nc("@info:tooltip", "Steady picture for a shot filmed by hand. Recommended.")},
        {QStringLiteral("fort"), i18nc("@item stabilization level", "Strong"), QString(),
         i18nc("@info:tooltip", "The steadiest picture, as if on a rail. Crops more of the edges.")},
    };
}

QMap<QString, QString> KoraCutStabilizer::analysisParams()
{
    // Une analyse fine (précision 10 sur 15) sert aux trois niveaux : elle ne se refait jamais.
    return {{QStringLiteral("shakiness"), QStringLiteral("6")}, {QStringLiteral("accuracy"), QStringLiteral("10")},
            {QStringLiteral("stepsize"), QStringLiteral("6")},  {QStringLiteral("mincontrast"), QStringLiteral("0.3")},
            {QStringLiteral("smoothing"), QStringLiteral("15")}, {QStringLiteral("optzoom"), QStringLiteral("1")}};
}

QMap<QString, QString> KoraCutStabilizer::levelParams(const QString &level)
{
    // smoothing : nombre d'images de chaque côté pour lisser le trajet de la caméra.
    // optzoom 2 : le zoom suit le besoin, image à peine recadrée ; 1 : un seul zoom pour tout le plan.
    if (level == QLatin1String("leger")) {
        return {{QStringLiteral("smoothing"), QStringLiteral("8")}, {QStringLiteral("optzoom"), QStringLiteral("2")}, {QStringLiteral("zoomspeed"), QStringLiteral("0.2")}};
    }
    if (level == QLatin1String("moyen")) {
        return {{QStringLiteral("smoothing"), QStringLiteral("15")}, {QStringLiteral("optzoom"), QStringLiteral("1")}, {QStringLiteral("zoomspeed"), QStringLiteral("0.25")}};
    }
    if (level == QLatin1String("fort")) {
        return {{QStringLiteral("smoothing"), QStringLiteral("30")}, {QStringLiteral("optzoom"), QStringLiteral("1")}, {QStringLiteral("zoomspeed"), QStringLiteral("0.25")}};
    }
    return {};
}

bool KoraCutStabilizer::writeLevelFile(const QString &analysisFile, const QString &levelFile, const QString &level)
{
    const QMap<QString, QString> params = levelParams(level);
    if (params.isEmpty()) {
        return false;
    }
    QFile in(analysisFile);
    if (!in.open(QIODevice::ReadOnly)) {
        return false;
    }
    QDomDocument doc;
    if (!doc.setContent(&in)) {
        return false;
    }
    in.close();
    bool found = false;
    const QDomNodeList filters = doc.elementsByTagName(QStringLiteral("filter"));
    for (int i = 0; i < filters.count(); ++i) {
        QDomElement filter = filters.at(i).toElement();
        QMap<QString, QDomElement> props;
        const QDomNodeList children = filter.elementsByTagName(QStringLiteral("property"));
        for (int j = 0; j < children.count(); ++j) {
            const QDomElement p = children.at(j).toElement();
            props.insert(p.attribute(QStringLiteral("name")), p);
        }
        if (props.value(QStringLiteral("mlt_service")).text() != QLatin1String("vidstab")) {
            continue;
        }
        found = true;
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            QDomElement p = props.value(it.key());
            if (p.isNull()) {
                p = doc.createElement(QStringLiteral("property"));
                p.setAttribute(QStringLiteral("name"), it.key());
                filter.appendChild(p);
            }
            while (p.hasChildNodes()) {
                p.removeChild(p.firstChild());
            }
            p.appendChild(doc.createTextNode(it.value()));
        }
    }
    if (!found) {
        return false;
    }
    QFile out(levelFile);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    out.write(doc.toByteArray(2));
    return true;
}

QString KoraCutStabilizer::sourceBinId(const std::shared_ptr<TimelineItemModel> &timeline, int clipId) const
{
    const QString binId = timeline->getClipBinId(clipId);
    const auto clip = pCore->projectItemModel()->getClipByBinID(binId);
    if (!clip) {
        return QString();
    }
    const QString source = clip->getProducerProperty(kSourceProperty);
    if (!source.isEmpty() && pCore->projectItemModel()->getClipByBinID(source)) {
        return source;
    }
    return binId;
}

QString KoraCutStabilizer::baseName(const QString &sourceBinId) const
{
    const auto clip = pCore->projectItemModel()->getClipByBinID(sourceBinId);
    const QString url = clip ? clip->url() : QString();
    // Le nom du média, plus une empreinte de son chemin : deux médias de même nom ne se mêlent pas.
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex().left(8));
    return stabilizationFolder() + QLatin1Char('/') + QFileInfo(url).completeBaseName() + QLatin1Char('-') + hash;
}

QString KoraCutStabilizer::currentLevel(const std::shared_ptr<TimelineItemModel> &timeline, int clipId) const
{
    const int cid = KoraCutClipTools::videoTarget(timeline, clipId);
    if (cid < 0) {
        return QString();
    }
    const auto clip = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(cid));
    if (!clip || clip->getProducerProperty(kSourceProperty).isEmpty()) {
        return QString();
    }
    return clip->getProducerProperty(kLevelProperty);
}

int KoraCutStabilizer::progress(const std::shared_ptr<TimelineItemModel> &timeline, int clipId) const
{
    const int cid = KoraCutClipTools::videoTarget(timeline, clipId);
    for (const Job *job : m_jobs) {
        for (const Waiting &w : job->waiting) {
            if (w.clipId == cid && w.timeline == timeline->uuid()) {
                return job->percent;
            }
        }
    }
    return -1;
}

QString KoraCutStabilizer::pendingLevel(const std::shared_ptr<TimelineItemModel> &timeline, int clipId) const
{
    const int cid = KoraCutClipTools::videoTarget(timeline, clipId);
    for (const Job *job : m_jobs) {
        for (const Waiting &w : job->waiting) {
            if (w.clipId == cid && w.timeline == timeline->uuid()) {
                return w.level;
            }
        }
    }
    return QString();
}

KoraCutStabilizer::Status KoraCutStabilizer::start(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, const QString &level)
{
    Status status;
    if (levelParams(level).isEmpty()) {
        status.code = QStringLiteral("entree_invalide");
        status.message = i18nc("@info", "The level is light, medium or strong.");
        return status;
    }
    const int cid = KoraCutClipTools::videoTarget(timeline, clipId);
    if (cid < 0) {
        status.code = QStringLiteral("mauvais_type_de_piste");
        status.message = i18nc("@info", "Only a video shot can be stabilized (not a sound, an image, a color or a title).");
        return status;
    }
    if (!qFuzzyCompare(timeline->getClipSpeed(cid), 1.)) {
        status.code = QStringLiteral("non_disponible");
        status.message = i18nc("@info", "Set the speed of this shot back to 100% before stabilizing it.");
        return status;
    }
    const QString source = sourceBinId(timeline, cid);
    const auto sourceClip = pCore->projectItemModel()->getClipByBinID(source);
    if (!sourceClip || sourceClip->url().isEmpty() || !QFileInfo::exists(sourceClip->url())) {
        status.code = QStringLiteral("non_disponible");
        status.message = i18nc("@info", "The file of this shot cannot be found.");
        return status;
    }
    const Waiting waiting{timeline->uuid(), cid, level};

    // Une analyse tourne déjà pour ce média : on attend avec elle.
    if (Job *job = m_jobs.value(source)) {
        job->waiting.erase(std::remove_if(job->waiting.begin(), job->waiting.end(),
                                          [&](const Waiting &w) { return w.clipId == cid && w.timeline == waiting.timeline; }),
                           job->waiting.end());
        job->waiting << waiting;
        status.ok = true;
        status.running = true;
        return status;
    }

    const QString base = baseName(source);
    const QString analysisFile = base + QStringLiteral("-analyse.mlt");
    const QString trfFile = base + QStringLiteral(".trf");
    if (QFileInfo::exists(analysisFile) && QFileInfo::exists(trfFile)) {
        // Déjà analysé : le niveau se pose tout de suite.
        applyLevel(waiting, source, analysisFile);
        status.ok = true;
        status.done = true;
        return status;
    }

    auto *job = new Job;
    job->sourceBinId = source;
    job->analysisFile = analysisFile;
    job->waiting << waiting;
    m_jobs.insert(source, job);

    QStringList args = {QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("progress=1"), QStringLiteral("-profile"), pCore->getCurrentProfilePath(),
                        sourceClip->url()};
    args << sourceClip->enforcedParams();
    args << QStringLiteral("-attach") << QStringLiteral("vidstab");
    const QMap<QString, QString> params = analysisParams();
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        args << QStringLiteral("%1=%2").arg(it.key(), it.value());
    }
    args << QStringLiteral("filename=%1").arg(trfFile);
    args << QStringLiteral("-consumer") << QStringLiteral("xml:%1").arg(analysisFile) << QStringLiteral("all=1") << QStringLiteral("terminate_on_pause=1");

    auto *process = new QProcess(this);
    job->process = process;
#ifdef Q_OS_UNIX
    // Basse priorité : le montage reste fluide pendant l'analyse.
    process->setChildProcessModifier([]() { [[maybe_unused]] int r = ::nice(10); });
#elif defined(Q_OS_WIN)
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) { a->flags |= BELOW_NORMAL_PRIORITY_CLASS; });
#endif
    connect(process, &QProcess::readyReadStandardError, this, [this, source, process]() {
        Job *j = m_jobs.value(source);
        if (!j) {
            return;
        }
        const QString buffer = QString::fromUtf8(process->readAllStandardError());
        j->log.append(buffer.right(2000));
        const int at = buffer.lastIndexOf(QLatin1String("percentage:"));
        if (at < 0) {
            return;
        }
        const int value = buffer.mid(at + 11).simplified().section(QLatin1Char(' '), 0, 0).toInt();
        if (value != j->percent) {
            j->percent = std::clamp(value, 0, 99);
            for (const Waiting &w : std::as_const(j->waiting)) {
                Q_EMIT progressChanged(w.clipId, j->percent);
            }
        }
    });
    connect(process, &QProcess::finished, this, [this, source, trfFile, analysisFile](int exitCode, QProcess::ExitStatus exitStatus) {
        Job *j = m_jobs.value(source);
        const bool ok = j && !j->canceled && exitStatus == QProcess::NormalExit && exitCode == 0 && QFileInfo::exists(analysisFile);
        if (!ok) {
            // Une analyse ratée ou annulée ne laisse rien derrière elle.
            QFile::remove(analysisFile);
            QFile::remove(trfFile);
        }
        finishJob(source, ok);
    });
    process->start(KdenliveSettings::meltpath(), args);
    status.ok = true;
    status.running = true;
    Q_EMIT progressChanged(cid, 0);
    return status;
}

void KoraCutStabilizer::finishJob(const QString &source, bool ok)
{
    Job *job = m_jobs.take(source);
    if (!job) {
        return;
    }
    if (job->process) {
        job->process->deleteLater();
    }
    for (const Waiting &w : std::as_const(job->waiting)) {
        if (ok) {
            applyLevel(w, source, job->analysisFile);
        } else {
            Q_EMIT finished(w.clipId, false, job->canceled ? i18nc("@info", "Stabilization canceled.") : i18nc("@info", "The stabilization failed."));
        }
    }
    if (!ok && !job->canceled) {
        qWarning() << "KoraCut stabilisation :" << job->log.right(600);
    }
    delete job;
}

void KoraCutStabilizer::applyLevel(const Waiting &waiting, const QString &source, const QString &analysisFile)
{
    const QString levelFile = analysisFile.left(analysisFile.size() - int(qstrlen("-analyse.mlt"))) + QLatin1Char('-') + waiting.level + QStringLiteral(".mlt");
    if (!QFileInfo::exists(levelFile) && !writeLevelFile(analysisFile, levelFile, waiting.level)) {
        Q_EMIT finished(waiting.clipId, false, i18nc("@info", "The stabilization failed."));
        return;
    }
    auto model = pCore->projectItemModel();
    // Le média du niveau est déjà dans le chutier : on repointe tout de suite.
    const QStringList existing = model->getClipByUrl(QFileInfo(levelFile));
    if (!existing.isEmpty()) {
        replace(waiting, existing.first());
        return;
    }
    // Sinon il rejoint le dossier « Stabilisation » du chutier (sans étape d'annulation : comme un
    // rendu, il peut rester), puis le plan est repointé quand il est chargé.
    QString folderId = model->getFolderIdByName(i18nc("@title bin folder", "Stabilization"));
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (folderId.isEmpty() || folderId == QLatin1String("-1")) {
        model->requestAddFolder(folderId, i18nc("@title bin folder", "Stabilization"), model->getRootFolder()->clipId(), undo, redo);
    }
    const QString level = waiting.level;
    auto ready = [this, waiting, source, level](const QString &binId) {
        if (binId.isEmpty() || binId == QLatin1String("-1")) {
            Q_EMIT finished(waiting.clipId, false, i18nc("@info", "The stabilization failed."));
            return;
        }
        if (auto clip = pCore->projectItemModel()->getClipByBinID(binId)) {
            clip->setProducerProperty(kSourceProperty, source);
            clip->setProducerProperty(kLevelProperty, level);
            // Un nom lisible dans le chutier et sur le plan, au lieu du nom du fichier .mlt.
            const auto original = pCore->projectItemModel()->getClipByBinID(source);
            QString levelName;
            for (const auto &c : levels()) {
                if (c.id == level) {
                    levelName = c.label.toLower();
                }
            }
            const QString name = i18nc("@item bin clip name, %1 is the original clip name, %2 the stabilization level", "%1 (stabilized, %2)",
                                       original ? original->clipName() : QString(), levelName);
            clip->setProducerProperty(QStringLiteral("kdenlive:clipname"), name);
            clip->setName(name);
        }
        replace(waiting, binId);
    };
    const QString id = ClipCreator::createClipFromFile(levelFile, folderId, model, undo, redo, ready);
    if (id == QLatin1String("-1")) {
        Q_EMIT finished(waiting.clipId, false, i18nc("@info", "The stabilization failed."));
    }
}

namespace {
// Le plan garde son identifiant mais change de média : l'Inspecteur, s'il le montre, se remet à jour (titre).
void refreshInspector(const std::shared_ptr<TimelineItemModel> &timeline, int clipId)
{
    if (!timeline || !pCore->window()) {
        return;
    }
    const ObjectId owner(KdenliveObjectType::TimelineClip, clipId, timeline->uuid());
    if (pCore->window()->effectStackOwner() == owner) {
        pCore->showEffectStackFromId(owner);
    }
}
} // namespace

void KoraCutStabilizer::replace(const Waiting &waiting, const QString &newBinId)
{
    auto timeline = timelineFor(waiting.timeline);
    if (!timeline || !timeline->isClip(waiting.clipId)) {
        Q_EMIT finished(waiting.clipId, false, i18nc("@info", "The shot was removed during the analysis."));
        return;
    }
    const QString currentId = timeline->getClipBinId(waiting.clipId);
    if (currentId != newBinId) {
        timeline->processTimelineReplacement({waiting.clipId}, currentId, newBinId, -1, false, true);
        refreshInspector(timeline, waiting.clipId);
    }
    if (auto clip = pCore->projectItemModel()->getClipByBinID(newBinId); clip && clip->getProducerProperty(kLevelProperty).isEmpty()) {
        clip->setProducerProperty(kLevelProperty, waiting.level);
    }
    Q_EMIT finished(waiting.clipId, true, QString());
}

KoraCutStabilizer::Status KoraCutStabilizer::remove(const std::shared_ptr<TimelineItemModel> &timeline, int clipId)
{
    Status status;
    const int cid = KoraCutClipTools::videoTarget(timeline, clipId);
    if (cid < 0) {
        status.code = QStringLiteral("mauvais_type_de_piste");
        status.message = i18nc("@info", "Only a video shot can be stabilized (not a sound, an image, a color or a title).");
        return status;
    }
    cancel(timeline, cid);
    const QString currentId = timeline->getClipBinId(cid);
    const auto clip = pCore->projectItemModel()->getClipByBinID(currentId);
    const QString source = clip ? clip->getProducerProperty(kSourceProperty) : QString();
    if (source.isEmpty()) {
        status.ok = true; // pas stabilisé : rien à faire
        return status;
    }
    if (!pCore->projectItemModel()->getClipByBinID(source)) {
        status.code = QStringLiteral("id_inconnu");
        status.message = i18nc("@info", "The original file of this shot is no longer in the bin.");
        return status;
    }
    timeline->processTimelineReplacement({cid}, currentId, source, -1, false, true);
    refreshInspector(timeline, cid);
    status.ok = true;
    status.done = true;
    return status;
}

void KoraCutStabilizer::cancel(const std::shared_ptr<TimelineItemModel> &timeline, int clipId)
{
    const int cid = KoraCutClipTools::videoTarget(timeline, clipId);
    for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
        Job *job = it.value();
        job->waiting.erase(std::remove_if(job->waiting.begin(), job->waiting.end(),
                                          [&](const Waiting &w) { return w.clipId == cid && w.timeline == timeline->uuid(); }),
                           job->waiting.end());
        if (job->waiting.isEmpty() && job->process) {
            // Plus personne n'attend cette analyse : on l'arrête (le processus fini la retire).
            job->canceled = true;
            job->process->kill();
        }
    }
    Q_EMIT finished(cid, false, QString());
}
