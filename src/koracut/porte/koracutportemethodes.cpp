/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutportemethodes.h"
#include "koracut/export/koracutexportqueue.h"
#include "koracutporte.h"

#include "bin/clipcreator.hpp"
#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "dialogs/renderwidget.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "doc/kthumb.h"
#include "kdenlive_debug.h"
#include "kdenlivesettings.h"
#include "koracut/koracutagenttools.h"
#include "koracut/koracutautocut.h"
#include "koracut/koracutducking.h"
#include "lib/audio/audioStreamInfo.h"
#include "koracut/koracutmodules.h"
#include "koracut/koracutnewprojectdialog.h"
#include "koracut/koracutpanelfocus.h"
#include "koracut/motion/koracutmotionrenderer.h"
#include "koracut/terminal/koracutterminalpanel.h"
#include "koracut/koracuttopbar.h"
#include "koracut/koracuttranscriber.h"
#include "koracut/koracutvisible.h"
#include "koracut/koracutvideotypes.h"
#include "layouts/layoutswitcher.h"
#include "mainwindow.h"
#include "monitor/monitor.h"
#include "monitor/monitormanager.h"
#include "profiles/profilemodel.hpp"
#include "project/projectmanager.h"
#include "renderpresets/renderpresetmodel.hpp"
#include "renderpresets/renderpresetrepository.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinetabs.hpp"
#include "timeline2/view/timelinewidget.h"
#include "utils/machineprofile.h"
#include "koracut/koracutperfpanel.h"
#include "utils/thumbnailcache.hpp"

#include <KActionCollection>
#include <KLocalizedString>

#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/qtcommon/View.h>

#include <QThread>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QAbstractSlider>
#include <QColor>
#include <QSpinBox>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>

#include <cmath>

#include <mlt++/MltFrame.h>
#include <mlt++/MltProducer.h>
#include <mlt++/MltProfile.h>
#include <framework/mlt_version.h>

using Reply = KoraCutPorte::Reply;
using Call = KoraCutPorte::Call;
using Param = KoraCutPorte::Param;

namespace {

// ---------------------------------------------------------------------------------------------
// Petits outils

Param p(const QString &name, const QString &type, const QString &description, bool required = false)
{
    return Param{name, type, description, required};
}

double fps()
{
    return pCore ? pCore->getCurrentFps() : 25.;
}

QJsonObject timeObject(int frames)
{
    return QJsonObject{{QStringLiteral("i"), frames}, {QStringLiteral("tc"), KoraCutAgentTools::timecode(frames, fps())}};
}

// La taille et la cadence d'un média quand elles ne sont pas celles du projet ; vide quand il a le format du
// projet, pour un son, et tant que le média n'est pas chargé.
QJsonObject formatDifference(const std::shared_ptr<ProjectClip> &clip)
{
    if (!clip || clip->clipType() == ClipType::Audio) {
        return {};
    }
    const QSize size = clip->frameSize();
    const double fps = clip->getOriginalFps();
    const auto &profile = pCore->getCurrentProfile();
    if (!profile || size.isEmpty()) {
        return {};
    }
    const bool otherSize = size != QSize(profile->width(), profile->height());
    const bool otherFps = fps > 0 && qAbs(fps - profile->fps()) > 0.01 && clip->clipType() != ClipType::Image;
    if (!otherSize && !otherFps) {
        return {};
    }
    return QJsonObject{{QStringLiteral("largeur"), size.width()}, {QStringLiteral("hauteur"), size.height()}, {QStringLiteral("ips"), fps > 0 ? QJsonValue(fps) : QJsonValue()}};
}

Reply noWindow()
{
    return Reply::fail(QStringLiteral("non_disponible"), QStringLiteral("Cette méthode a besoin de la fenêtre de KoraCut."),
                       QStringLiteral("Elle ne marche pas dans un test hors écran."));
}

Reply noProject()
{
    return Reply::fail(QStringLiteral("projet_absent"), QStringLiteral("Aucun projet ouvert."), QStringLiteral("projet.nouveau ou projet.ouvrir d'abord."));
}

QString cleanLabel(QString text)
{
    return text.remove(QLatin1Char('&'));
}

QString producerTypeName(ClipType::ProducerType type)
{
    switch (type) {
    case ClipType::AV:
        return QStringLiteral("video_son");
    case ClipType::Video:
        return QStringLiteral("video");
    case ClipType::Audio:
        return QStringLiteral("audio");
    case ClipType::Image:
        return QStringLiteral("image");
    case ClipType::Color:
        return QStringLiteral("couleur");
    case ClipType::Text:
    case ClipType::TextTemplate:
        return QStringLiteral("titre");
    case ClipType::Playlist:
        return QStringLiteral("playlist");
    case ClipType::Timeline:
        return QStringLiteral("sequence");
    case ClipType::SlideShow:
        return QStringLiteral("diaporama");
    default:
        return QStringLiteral("autre");
    }
}

QString fileKind(const QString &path)
{
    static const QStringList video = {QStringLiteral("mp4"), QStringLiteral("mov"), QStringLiteral("mkv"), QStringLiteral("avi"), QStringLiteral("mts"),
                                      QStringLiteral("m4v"), QStringLiteral("webm"), QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("mxf"),
                                      QStringLiteral("hevc"), QStringLiteral("ts")};
    static const QStringList audio = {QStringLiteral("wav"), QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("flac"),
                                      QStringLiteral("ogg"), QStringLiteral("aif"), QStringLiteral("aiff"), QStringLiteral("opus")};
    static const QStringList image = {QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("webp"), QStringLiteral("tif"),
                                      QStringLiteral("tiff"), QStringLiteral("heic"), QStringLiteral("gif"), QStringLiteral("svg"), QStringLiteral("bmp"),
                                      QStringLiteral("dng"), QStringLiteral("raw")};
    const QString ext = QFileInfo(path).suffix().toLower();
    if (video.contains(ext)) {
        return QStringLiteral("video");
    }
    if (audio.contains(ext)) {
        return QStringLiteral("audio");
    }
    if (image.contains(ext)) {
        return QStringLiteral("image");
    }
    if (ext == QLatin1String("koracut") || ext == QLatin1String("kdenlive")) {
        return QStringLiteral("projet");
    }
    return QStringLiteral("autre");
}

// La carte chemin -> id des clips du chutier.
QHash<QString, QString> mediaByPath()
{
    QHash<QString, QString> map;
    if (!pCore || !pCore->projectItemModel()) {
        return map;
    }
    const std::vector<QString> ids = pCore->projectItemModel()->getAllClipIds();
    for (const QString &id : ids) {
        const auto clip = pCore->projectItemModel()->getClipByBinID(id);
        if (clip && !clip->url().isEmpty()) {
            map.insert(QFileInfo(clip->url()).absoluteFilePath(), id);
        }
    }
    return map;
}

// Un dossier du chutier par son nom ou son id.
std::shared_ptr<ProjectFolder> findBinFolder(const QString &nameOrId)
{
    auto model = pCore->projectItemModel();
    if (nameOrId.isEmpty()) {
        return model->getRootFolder();
    }
    if (auto byId = model->getFolderByBinId(nameOrId)) {
        return byId;
    }
    std::vector<std::shared_ptr<AbstractProjectItem>> queue{model->getRootFolder()};
    while (!queue.empty()) {
        auto item = queue.front();
        queue.erase(queue.begin());
        for (int i = 0; i < item->childCount(); ++i) {
            auto child = std::static_pointer_cast<AbstractProjectItem>(item->child(i));
            if (child->itemType() == AbstractProjectItem::FolderItem) {
                if (child->name().compare(nameOrId, Qt::CaseInsensitive) == 0) {
                    return std::static_pointer_cast<ProjectFolder>(child);
                }
                queue.push_back(child);
            }
        }
    }
    return nullptr;
}

// Renomme ou déplace sur le disque. Windows refuse de toucher à un fichier ouvert (Mac et Linux le
// permettent) : en cas de refus, MLT relâche les médias concernés et on réessaie.
bool renameOnDisk(const QString &from, const QString &to, bool folder)
{
    bool renamed = QFile::rename(from, to);
#ifdef Q_OS_WIN
    for (int attempt = 0; !renamed && attempt < 5 && pCore && pCore->projectItemModel(); ++attempt) {
        const QString prefix = from + QLatin1Char('/');
        const std::vector<QString> ids = pCore->projectItemModel()->getAllClipIds();
        for (const QString &id : ids) {
            const auto clip = pCore->projectItemModel()->getClipByBinID(id);
            if (!clip || clip->url().isEmpty()) {
                continue;
            }
            const QString url = QFileInfo(clip->url()).absoluteFilePath();
            if ((folder && url.startsWith(prefix)) || (!folder && url == from)) {
                KoraCutAgentTools::releaseMediaFile(id);
            }
        }
        QThread::msleep(60 * (attempt + 1));
        renamed = QFile::rename(from, to);
    }
#else
    Q_UNUSED(folder)
#endif
    return renamed;
}

// Après un renommage ou un déplacement sur le disque, les clips du chutier suivent.
void retargetClips(const QString &from, const QString &to, bool folder)
{
    if (!pCore || !pCore->projectItemModel()) {
        return;
    }
    const QString prefix = from + QLatin1Char('/');
    const std::vector<QString> ids = pCore->projectItemModel()->getAllClipIds();
    for (const QString &id : ids) {
        const auto clip = pCore->projectItemModel()->getClipByBinID(id);
        if (!clip || clip->url().isEmpty()) {
            continue;
        }
        const QString url = QFileInfo(clip->url()).absoluteFilePath();
        QString target;
        if (folder && url.startsWith(prefix)) {
            target = to + url.mid(from.size());
        } else if (!folder && url == from) {
            target = to;
        } else {
            continue;
        }
        // Le chemin suit tout de suite (pas de rechargement : le fichier est le même), et le nom du
        // chutier aussi quand il était celui du fichier. Les producteurs de piste des séquences
        // ouvertes suivent aussi (25 septembre 2026) : ce sont eux que le projet enregistre, et un
        // projet rouvert les reprend tels quels.
        KoraCutAgentTools::relocateMedia(id, url, target);
        if (clip->clipName() == QFileInfo(url).fileName()) {
            QMap<QString, QString> props;
            props.insert(QStringLiteral("kdenlive:clipname"), QFileInfo(target).fileName());
            clip->setProperties(props, true);
        }
    }
}

// Le fichier du projet, s'il est vraiment sur le disque. Un projet neuf porte déjà l'URL proposée
// pour son premier enregistrement (« ~/Movies/Nom.koracut ») : tant que ce fichier n'existe pas, le
// projet n'a pas de dossier, et rien ne doit lire ni écrire dans ~/Movies à sa place.
QString documentFile()
{
    if (!pCore || !pCore->currentDoc() || pCore->currentDoc()->url().isEmpty()) {
        return QString();
    }
    const QString path = pCore->currentDoc()->url().toLocalFile();
    return QFileInfo::exists(path) ? path : QString();
}

QString projectFolderOf(const KoraCutPorteContext &ctx)
{
    if (ctx.projectFolder) {
        return ctx.projectFolder();
    }
    const QString file = documentFile();
    return file.isEmpty() ? QString() : QFileInfo(file).absolutePath();
}

// Un chemin donné par le client : « ~ » est son dossier personnel, un chemin relatif part du dossier
// du projet (sans projet enregistré, il reste relatif au dossier courant, mais on le dit).
QString expandUserPath(const KoraCutPorteContext &ctx, const QString &given)
{
    QString path = given.trimmed();
    if (path == QLatin1String("~")) {
        return QDir::homePath();
    }
    if (path.startsWith(QLatin1String("~/"))) {
        return QDir::homePath() + path.mid(1);
    }
    if (path.isEmpty() || QDir::isAbsolutePath(path)) {
        return path;
    }
    const QString folder = projectFolderOf(ctx);
    if (folder.isEmpty()) {
        // Pas de dossier de projet : le chemin reste relatif, et la méthode le refuse (jamais le
        // dossier courant de l'application, qui n'est celui de personne).
        return path;
    }
    return QDir::cleanPath(QDir(folder).absoluteFilePath(path));
}

// Un chemin resté relatif faute de projet enregistré : l'erreur à rendre.
bool relativeWithoutProject(const QString &path, Reply &error)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path)) {
        return false;
    }
    error = Reply::fail(QStringLiteral("projet_absent"), QStringLiteral("« %1 » est un chemin relatif, et le projet n'est pas encore enregistré : il n'a pas de dossier.").arg(path),
                        QStringLiteral("Donne un chemin absolu (ou ~), ou enregistre d'abord le projet (projet.enregistrer chemin=…)."));
    return true;
}

// Le clip de séquence (chutier) et son uuid, à partir d'un uuid, d'un nom, ou de rien (la courante).
std::shared_ptr<ProjectClip> findSequence(const QString &given, QUuid *uuidOut)
{
    if (!pCore || !pCore->currentDoc() || !pCore->projectItemModel()) {
        return nullptr;
    }
    KdenliveDoc *doc = pCore->currentDoc();
    const QString wanted = given.trimmed();
    const QList<QUuid> uuids = doc->getTimelinesUuids();
    QUuid found;
    if (wanted.isEmpty()) {
        if (auto tl = pCore->projectManager()->getTimeline()) {
            found = tl->uuid();
        }
    } else {
        for (const QUuid &uuid : uuids) {
            if (uuid.toString(QUuid::WithoutBraces).compare(wanted, Qt::CaseInsensitive) == 0 || uuid.toString().compare(wanted, Qt::CaseInsensitive) == 0) {
                found = uuid;
                break;
            }
        }
        if (found.isNull()) {
            for (const QUuid &uuid : uuids) {
                const QString binId = pCore->projectItemModel()->getSequenceId(uuid);
                auto clip = binId.isEmpty() ? nullptr : pCore->projectItemModel()->getClipByBinID(binId);
                if (clip && clip->clipName().compare(wanted, Qt::CaseInsensitive) == 0) {
                    found = uuid;
                    break;
                }
            }
        }
    }
    if (found.isNull()) {
        return nullptr;
    }
    const QString binId = pCore->projectItemModel()->getSequenceId(found);
    if (binId.isEmpty()) {
        return nullptr;
    }
    if (uuidOut) {
        *uuidOut = found;
    }
    return pCore->projectItemModel()->getClipByBinID(binId);
}

QJsonObject sequenceEntry(const QUuid &uuid)
{
    KdenliveDoc *doc = pCore->currentDoc();
    auto tl = doc->getTimeline(uuid, true);
    QString name;
    const QString binId = pCore->projectItemModel()->getSequenceId(uuid);
    if (!binId.isEmpty()) {
        if (auto clip = pCore->projectItemModel()->getClipByBinID(binId)) {
            name = clip->clipName();
        }
    }
    QUuid current;
    if (auto active = pCore->projectManager()->getTimeline()) {
        current = active->uuid();
    }
    QJsonObject s{{QStringLiteral("uuid"), uuid.toString(QUuid::WithoutBraces)}, {QStringLiteral("nom"), name}, {QStringLiteral("media_id"), binId}, {QStringLiteral("courante"), uuid == current}};
    if (tl) {
        s.insert(QStringLiteral("duree"), timeObject(tl->duration()));
        s.insert(QStringLiteral("pistes_video"), int(tl->getTracksIds(false).size()));
        s.insert(QStringLiteral("pistes_audio"), int(tl->getTracksIds(true).size()));
    }
    const auto &profile = pCore->getCurrentProfile();
    s.insert(QStringLiteral("profil"), QJsonObject{{QStringLiteral("largeur"), profile->width()}, {QStringLiteral("hauteur"), profile->height()}, {QStringLiteral("ips"), profile->fps()}});
    return s;
}

// ---- L'analyse d'un média (lot 3)

// Le chemin de ffmpeg : celui des réglages, sinon celui du système.
QString ffmpegPath()
{
    QString path = KdenliveSettings::ffmpegpath();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        path = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    }
    return path;
}

// Le niveau crête de chaque image d'un fichier, en dBFS, lu par MLT (son seul, la vidéo n'est pas
// décodée). C'est la même mesure que les formes d'onde du chutier (AudioLevelsTask), calculée ici
// tout de suite, dans un fil à part, sans attendre la tâche de fond.
QVector<double> frameLevelsFromFile(const QString &path, double fps, int stream)
{
    QVector<double> levels;
    Mlt::Profile profile;
    profile.set_frame_rate(int(std::lround(fps * 1000.)), 1000);
    Mlt::Producer producer(profile, path.toUtf8().constData());
    if (!producer.is_valid()) {
        return levels;
    }
    producer.set("video_index", -1);
    if (stream >= 0) {
        producer.set("audio_index", stream);
    }
    const int length = producer.get_length();
    if (length <= 0) {
        return levels;
    }
    levels.reserve(length);
    const int frequency = 48000;
    for (int i = 0; i < length; ++i) {
        std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
        if (!frame || !frame->is_valid()) {
            break;
        }
        mlt_audio_format format = mlt_audio_s16;
        int freq = frequency;
        int channels = 2;
        int samples = mlt_audio_calculate_frame_samples(float(fps), frequency, i);
        const auto *data = static_cast<const int16_t *>(frame->get_audio(format, freq, channels, samples));
        int peak = 0;
        if (data) {
            for (int k = 0; k < samples * channels; ++k) {
                peak = qMax(peak, qAbs(int(data[k])));
            }
        }
        levels.append(KoraCutDucking::peakToDb(peak));
    }
    return levels;
}

// Les niveaux déjà calculés pour les formes d'onde du chutier, si la tâche de fond a fini.
QVector<double> frameLevelsFromCache(const std::shared_ptr<ProjectClip> &clip, int stream)
{
    QVector<double> levels;
    if (!clip->audioInfo()) {
        return levels;
    }
    const QVector<int16_t> cache = clip->audioFrameCache(stream);
    const int channels = qMax(1, clip->audioInfo()->channelsForStream(stream));
    const int block = AUDIOLEVELS_POINTS_PER_FRAME * channels;
    if (cache.size() < block) {
        return levels;
    }
    levels.reserve(cache.size() / block);
    for (int offset = 0; offset + block <= cache.size(); offset += block) {
        int peak = 0;
        for (int k = 0; k < block; ++k) {
            peak = qMax(peak, qAbs(int(cache.at(offset + k))));
        }
        levels.append(KoraCutDucking::peakToDb(peak));
    }
    return levels;
}

QJsonArray intervalsJson(const QList<KoraCutDucking::Interval> &intervals)
{
    QJsonArray list;
    for (const KoraCutDucking::Interval &in : intervals) {
        list.append(QJsonObject{{QStringLiteral("debut"), timeObject(in.first)}, {QStringLiteral("fin"), timeObject(in.second)}, {QStringLiteral("duree"), timeObject(in.second - in.first)}});
    }
    return list;
}

// Lance ffmpeg sans bloquer et rend sa sortie (stderr et stdout mêlés) à @p done.
void runFfmpeg(QObject *parent, const QStringList &args, const std::function<void(bool, const QString &)> &done)
{
    auto *process = new QProcess(parent);
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto answered = std::make_shared<bool>(false);
    const auto finish = [process, done, answered](bool ok) {
        if (*answered) {
            return;
        }
        *answered = true;
        const QString out = QString::fromUtf8(process->readAll());
        process->deleteLater();
        done(ok, out.isEmpty() && !ok ? process->errorString() : out);
    };
    QObject::connect(process, &QProcess::finished, parent, [finish](int code, QProcess::ExitStatus status) { finish(status == QProcess::NormalExit && code == 0); });
    QObject::connect(process, &QProcess::errorOccurred, parent, [process, finish](QProcess::ProcessError) {
        if (process->state() == QProcess::NotRunning) {
            finish(false);
        }
    });
    process->start(ffmpegPath(), args);
}

// Résout un chemin donné (relatif au projet ou absolu) et vérifie qu'il reste dans le projet.
bool resolveInProject(const KoraCutPorteContext &ctx, const QString &given, QString &absolute, Reply &error)
{
    const QString folder = projectFolderOf(ctx);
    if (folder.isEmpty()) {
        error = Reply::fail(QStringLiteral("projet_absent"), QStringLiteral("Le projet n'est pas enregistré : il n'a pas encore de dossier."),
                            QStringLiteral("projet.enregistrer avec un chemin, puis réessaie."));
        return false;
    }
    const QString cleaned = QDir::cleanPath(QDir(folder).absoluteFilePath(given.isEmpty() ? QStringLiteral(".") : given));
    if (!KoraCutPorteMethodes::isInsideFolder(folder, cleaned)) {
        error = Reply::fail(QStringLiteral("hors_projet"), QStringLiteral("« %1 » sort du dossier du projet (%2).").arg(given, folder),
                            QStringLiteral("La porte ne touche qu'au dossier du projet et à ses sous-dossiers."));
        return false;
    }
    absolute = cleaned;
    return true;
}

QString relativeToProject(const KoraCutPorteContext &ctx, const QString &absolute)
{
    const QString folder = projectFolderOf(ctx);
    return folder.isEmpty() ? absolute : QDir(folder).relativeFilePath(absolute);
}

QJsonObject fileEntry(const KoraCutPorteContext &ctx, const QFileInfo &info, const QHash<QString, QString> &media)
{
    QJsonObject o{{QStringLiteral("nom"), info.fileName()},
                  {QStringLiteral("chemin"), relativeToProject(ctx, info.absoluteFilePath())},
                  {QStringLiteral("type"), info.isDir() ? QStringLiteral("dossier") : fileKind(info.fileName())},
                  {QStringLiteral("modifie"), info.lastModified().toString(Qt::ISODate)}};
    if (!info.isDir()) {
        o.insert(QStringLiteral("taille"), double(info.size()));
        const QString id = media.value(info.absoluteFilePath());
        if (!id.isEmpty()) {
            o.insert(QStringLiteral("media_id"), id);
        }
    }
    return o;
}

// La fenêtre secondaire visée par `titre` ou `classe` (ou la première ouverte).
QWidget *findWindow(const QJsonObject &params, QWidget *main)
{
    const QString title = params.value(QStringLiteral("titre")).toString();
    const QString klass = params.value(QStringLiteral("classe")).toString();
    QWidget *first = nullptr;
    const QList<QWidget *> tops = QApplication::topLevelWidgets();
    for (QWidget *w : tops) {
        if (w == main || !w->isVisible() || !w->isWindow() || w->width() < 50) {
            continue;
        }
        if (!title.isEmpty() && w->windowTitle().contains(title, Qt::CaseInsensitive)) {
            return w;
        }
        if (!klass.isEmpty() && QString::fromLatin1(w->metaObject()->className()) == klass) {
            return w;
        }
        if (!first) {
            first = w;
        }
    }
    if (!title.isEmpty() || !klass.isEmpty()) {
        return nullptr;
    }
    // Sans cible : la fenêtre modale active (la dernière ouverte, celle qui attend une réponse),
    // sinon la première fenêtre ouverte.
    if (QWidget *modal = QApplication::activeModalWidget()) {
        if (modal != main && modal->isVisible() && modal->isWindow()) {
            return modal;
        }
    }
    return first;
}

// Les définitions d'outils d'agents (nom, description, schéma), si la ressource est compilée.
QJsonArray toolDefinitions()
{
    QJsonArray list;
    const QDir dir(QStringLiteral(":/koracut/agents"));
    if (!dir.exists()) {
        return list;
    }
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &name : files) {
        QFile file(dir.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonArray tools = QJsonDocument::fromJson(file.readAll()).array();
        for (const QJsonValue &v : tools) {
            list.append(v);
        }
    }
    return list;
}

// Contrôle léger des paramètres d'un outil contre son schéma (25 septembre 2026) : un champ
// inconnu, un champ obligatoire absent ou une valeur hors de la liste permise (enum, depuis le
// 12 octobre 2026), au premier niveau et dans les objets d'une liste. Sans
// lui, un programme qui se trompe de nom de champ (« nouveau_nom » au lieu de « nom ») recevait
// « ok » et un résultat qui ne faisait pas ce qu'il croyait. Rend un message vide si tout va bien.
QString checkToolArgs(const QJsonObject &schema, const QJsonObject &args, const QString &where)
{
    const QJsonObject props = schema.value(QStringLiteral("properties")).toObject();
    if (props.isEmpty()) {
        return {};
    }
    const QStringList allowed = props.keys();
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        if (!props.contains(it.key())) {
            return QStringLiteral("Champ inconnu « %1 »%2. Champs attendus : %3.").arg(it.key(), where, allowed.join(QStringLiteral(", ")));
        }
        const QJsonObject sub = props.value(it.key()).toObject();
        // Une valeur hors de la liste permise (12 octobre 2026) : sans ce contrôle, un mode mal écrit arrivait
        // jusqu'à l'outil, qui répondait à côté ou prenait son comportement par défaut. Seulement pour un texte :
        // c'est là que les listes de valeurs servent.
        const QJsonArray permitted = sub.value(QStringLiteral("enum")).toArray();
        if (!permitted.isEmpty() && it.value().isString() && !permitted.contains(it.value())) {
            QStringList names;
            for (const QJsonValue &v : permitted) {
                names << v.toString();
            }
            return QStringLiteral("Valeur « %1 » refusée pour « %2 »%3. Valeurs permises : %4.").arg(it.value().toString(), it.key(), where, names.join(QStringLiteral(", ")));
        }
        const QJsonObject items = sub.value(QStringLiteral("items")).toObject();
        if (it.value().isArray() && items.value(QStringLiteral("type")).toString() == QLatin1String("object")) {
            const QJsonArray array = it.value().toArray();
            for (int i = 0; i < array.size(); ++i) {
                const QString inner = checkToolArgs(items, array.at(i).toObject(), QStringLiteral(" dans %1[%2]").arg(it.key()).arg(i));
                if (!inner.isEmpty()) {
                    return inner;
                }
            }
        }
    }
    const QJsonArray required = schema.value(QStringLiteral("required")).toArray();
    for (const QJsonValue &r : required) {
        if (!args.contains(r.toString())) {
            return QStringLiteral("Il manque « %1 »%2. Champs attendus : %3.").arg(r.toString(), where, allowed.join(QStringLiteral(", ")));
        }
    }
    return {};
}

QString checkToolCall(const QString &name, const QJsonObject &args)
{
    static const QJsonArray defs = toolDefinitions();
    for (const QJsonValue &v : defs) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("name")).toString() == name) {
            return checkToolArgs(o.value(QStringLiteral("input_schema")).toObject(), args, QString());
        }
    }
    return {};
}

} // namespace

// ---------------------------------------------------------------------------------------------

bool KoraCutPorteMethodes::quitHasSomethingToLose(MainWindow *window)
{
    if (pCore && pCore->currentDoc() && pCore->currentDoc()->isModified()) {
        return true;
    }
    // Les deux files de rendu n'ont pas les mêmes mots (« attente » pour l'une, « en_attente » pour l'autre).
    const auto running = [](const QString &state) {
        return state == QLatin1String("attente") || state == QLatin1String("en_attente") || state == QLatin1String("demarrage") || state == QLatin1String("en_cours");
    };
    if (window) {
        if (RenderWidget *rw = window->koraCutRenderWidget(false)) {
            const QJsonArray jobs = rw->koraCutJobs();
            for (const QJsonValue &j : jobs) {
                if (running(j.toObject().value(QStringLiteral("etat")).toString())) {
                    return true;
                }
            }
        }
    }
    for (const auto &j : KoraCutExportQueue::self().jobs()) {
        if (running(j->state)) {
            return true;
        }
    }
    return false;
}

bool KoraCutPorteMethodes::isInsideFolder(const QString &folder, const QString &path)
{
    if (folder.isEmpty() || path.isEmpty()) {
        return false;
    }
    // Tout se compare en chemin canonique (macOS : /var est un lien vers /private/var), et un lien
    // symbolique qui pointe dehors sort du projet : on résout la partie du chemin qui existe.
    // Type de retour explicite : avec QStringBuilder, « a + b » n'est pas un QString.
    const auto canonicalize = [](const QString &given) -> QString {
        const QString cleaned = QDir::cleanPath(given);
        QString existing = cleaned;
        QString rest;
        while (!existing.isEmpty() && existing != QLatin1String("/") && !QFileInfo::exists(existing)) {
            rest = QLatin1Char('/') + QFileInfo(existing).fileName() + rest;
            existing = QFileInfo(existing).absolutePath();
        }
        const QString canonical = QFileInfo(existing).canonicalFilePath();
        return (canonical.isEmpty() ? existing : canonical) + rest;
    };
    const QString root = canonicalize(QFileInfo(folder).absoluteFilePath());
    const QString target = canonicalize(QDir(folder).absoluteFilePath(path));
    return target == root || target.startsWith(root + QLatin1Char('/'));
}

QSet<QString> KoraCutPorteMethodes::confirmationTools()
{
    QSet<QString> set;
    const QJsonArray defs = toolDefinitions();
    for (const QJsonValue &v : defs) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("x_confirmation")).toBool(false)) {
            set.insert(o.value(QStringLiteral("name")).toString());
        }
    }
    if (set.isEmpty()) {
        set = {QStringLiteral("supprimer_clip"), QStringLiteral("supprimer_piste"), QStringLiteral("exporter"), QStringLiteral("exporter_serie")};
    }
    return set;
}

// ---------------------------------------------------------------------------------------------

void KoraCutPorteMethodes::registerAll(KoraCutPorte *porte, const KoraCutPorteContext &context)
{
    const KoraCutPorteContext ctx = context;
    const auto timeline = [ctx]() -> std::shared_ptr<TimelineItemModel> {
        if (ctx.timeline) {
            return ctx.timeline();
        }
        return pCore && pCore->projectManager() ? pCore->projectManager()->getTimeline() : nullptr;
    };
    const auto undoStack = [ctx]() -> std::shared_ptr<DocUndoStack> {
        if (ctx.undoStack) {
            return ctx.undoStack();
        }
        // Pendant un chargement, le document n'existe pas encore : ProjectManager::undoStack() le lirait.
        return pCore && pCore->currentDoc() ? pCore->undoStack() : nullptr;
    };
    const auto actions = [ctx]() -> KActionCollection * {
        if (ctx.actions) {
            return ctx.actions;
        }
        return ctx.window ? ctx.window->actionCollection() : nullptr;
    };
    porte->setUndoStackProvider(undoStack);
    porte->setProjectProvider([]() { return pCore && pCore->currentDoc() ? pCore->currentDoc()->url().toLocalFile() : QString(); });
    const QSet<QString> confirmTools = confirmationTools();
    MainWindow *window = ctx.window;
    // Dans l'application (pas dans les tests, qui n'ont pas de fenêtre) : tant que le document ou sa
    // timeline n'existent pas, un projet se charge.
    if (window) {
        porte->setBusyProvider([window]() { return !pCore || !pCore->currentDoc() || !window->getCurrentTimeline(); });
    }

    // ---- app -------------------------------------------------------------------------------
    porte->registerMethod(QStringLiteral("app.version"), QStringLiteral("Qui répond : application, version, Qt, MLT, système, profil machine."), {}, [](const Call &) {
        return Reply::value(QJsonObject{{QStringLiteral("application"), QStringLiteral("KoraCut")},
                                        {QStringLiteral("version"), QCoreApplication::applicationVersion()},
                                        {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
                                        {QStringLiteral("mlt"), QString::fromLatin1(mlt_version_get_string())},
                                        {QStringLiteral("systeme"), QSysInfo::prettyProductName()},
                                        {QStringLiteral("profil_machine"), MachineProfile::levelName(MachineProfile::effectiveLevel())},
                                        // Un seul HyperFrames, celui de l'application : sa version et sa commande, pour tout agent du dehors.
                                        {QStringLiteral("hyperframes"), QJsonObject{{QStringLiteral("version"), KoraCutMotionHost::installedVersion()},
                                                                                    {QStringLiteral("commande"), KoraCutMotionHost::launcher()}}}});
    });
    porte->registerMethod(QStringLiteral("app.performance"),
                          QStringLiteral("Le profil de la machine et ce que la lecture fait maintenant : i/s, images sautées, temps par image, aperçu, décodeur, proxys, mémoire (panneau Diagnostic)."),
                          {}, [](const Call &) {
                              return Reply::value(KoraCutPerfPanel::snapshot());
                          });
    porte->registerMethod(QStringLiteral("app.ping"), QStringLiteral("Répond tout de suite, avec l'heure."), {}, [](const Call &) {
        return Reply::value(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("heure"), QDateTime::currentDateTime().toString(Qt::ISODate)}});
    });
    porte->registerMethod(
        QStringLiteral("app.quitter"),
        QStringLiteral("Ferme KoraCut (à confirmation ; un programme de confiance passe sans question quand rien n'est à perdre : projet non modifié, aucun rendu en cours)."),
        {p(QStringLiteral("enregistrer"), QStringLiteral("boolean"), QStringLiteral("Enregistrer le projet avant, défaut true."))},
        [porte, window](const Call &call) {
            Reply gate;
            // Quitter demande toujours quand il y a quelque chose à perdre ou à écrire. Sinon, la confiance déjà
            // accordée par la personne (case de la Console, KORACUT_PORTE_CONFIANCE) suffit : fermer une instance
            // d'essai intacte n'a pas à attendre quelqu'un devant l'écran.
            if (porte->needsConfirmation(call, QStringLiteral("quitter KoraCut"), KoraCutPorteMethodes::quitHasSomethingToLose(window), gate)) {
                return gate;
            }
            if (!window) {
                return noWindow();
            }
            const bool save = call.params.value(QStringLiteral("enregistrer")).toBool(true);
            QTimer::singleShot(0, window, [window, save]() {
                if (save && pCore->currentDoc() && pCore->currentDoc()->isModified() && !pCore->currentDoc()->url().isEmpty()) {
                    pCore->projectManager()->saveFile();
                }
                // Demandé par la porte : pas de question « un agent est en train de monter » (c'est lui qui ferme).
                qApp->setProperty("koracut_quitter_porte", true);
                window->close();
                qApp->setProperty("koracut_quitter_porte", false);
            });
            return Reply::value(QJsonObject{{QStringLiteral("ok"), true}});
        },
        true);

    // ---- terminal --------------------------------------------------------------------------
    // Le terminal intégré vu de la porte (3 octobre 2026) : une IA du dehors ouvre Claude Code dans
    // KoraCut, lui écrit ses demandes et lit ses réponses, sous les yeux de la personne.
    // Par le dock et pas par la fenêtre : un panneau fermé n'est plus un enfant de la fenêtre.
    const auto terminalPanel = [window]() -> KoraCutTerminalPanel * {
        if (!window) {
            return nullptr;
        }
        if (KDDockWidgets::Core::DockWidget *dock = KDDockWidgets::Core::DockWidget::byName(QStringLiteral("terminal"))) {
            if (QWidget *guest = KDDockWidgets::QtCommon::View_qt::asQWidget(dock->guestView().get())) {
                if (auto *panel = qobject_cast<KoraCutTerminalPanel *>(guest)) {
                    return panel;
                }
                return guest->findChild<KoraCutTerminalPanel *>();
            }
        }
        return window->findChild<KoraCutTerminalPanel *>();
    };
    const auto terminalShow = [window]() {
        // La même place que par le bouton (à côté de la timeline) : ouvert par son dock seul, un terminal qui n'a
        // jamais été rangé s'ouvrait en fenêtre flottante (5 octobre 2026).
        if (window && QMetaObject::invokeMethod(window, "koraCutRevealTerminalDock", Qt::DirectConnection)) {
            return;
        }
        if (KDDockWidgets::Core::DockWidget *dock = KDDockWidgets::Core::DockWidget::byName(QStringLiteral("terminal"))) {
            dock->open();
            dock->setAsCurrentTab();
        }
    };
    const auto terminalFail = [](const QString &code) {
        if (code == QLatin1String("touche_inconnue")) {
            return Reply::fail(code, QStringLiteral("Touche inconnue."), QStringLiteral("entree, echap, tab, maj-tab, haut, bas, gauche, droite, retour ou ctrl-c."));
        }
        return Reply::fail(QStringLiteral("aucun_onglet"), QStringLiteral("Aucun onglet du terminal ne correspond, ou plus rien n'y tourne."),
                           QStringLiteral("terminal.ouvrir lance Claude Code ; terminal.etat liste les onglets."));
    };
    porte->registerMethod(
        QStringLiteral("terminal.etat"),
        QStringLiteral("Le terminal intégré : onglets, Claude Code installé et ouvert, écran de l'onglet visé, agent_travaille et question_ouverte (indicatifs : l'agent attend une réponse à un choix). Avec historique, les dernières lignes de tout son texte."),
        {p(QStringLiteral("onglet"), QStringLiteral("string"), QStringLiteral("Vide : l'onglet de Claude Code s'il tourne, sinon celui de devant. Ou « claude », un rang (1 au début), un titre.")),
         p(QStringLiteral("historique"), QStringLiteral("number"), QStringLiteral("Nombre de lignes du défilement à rendre en plus de l'écran, défaut 0."))},
        [terminalPanel](const Call &call) {
            KoraCutTerminalPanel *panel = terminalPanel();
            if (!panel) {
                return noWindow();
            }
            return Reply::value(panel->porteState(call.params.value(QStringLiteral("onglet")).toVariant().toString(),
                                                  std::clamp(call.params.value(QStringLiteral("historique")).toInt(0), 0, 5000)));
        });
    porte->registerMethod(
        QStringLiteral("terminal.ouvrir"),
        QStringLiteral("Ouvre le terminal dans le dossier du projet et y lance l'agent qui monte : Claude Code par défaut (compétences posées, consignes du projet écrites, KoraCut branchée), un autre agent du catalogue, ou un simple shell. Une session Claude Code déjà ouverte est ramenée devant. Le projet doit être enregistré. Rend la main tout de suite : terminal.etat dit quand l'agent est prêt."),
        {p(QStringLiteral("agent"), QStringLiteral("string"), QStringLiteral("claude (défaut), shell, systeme (le terminal du système, sans agent), ou l'identifiant d'un agent du catalogue."))},
        [window, terminalPanel, terminalShow](const Call &call) {
            KoraCutTerminalPanel *panel = terminalPanel();
            if (!panel || !window) {
                return noWindow();
            }
            const QString agent = call.params.value(QStringLiteral("agent")).toString(QStringLiteral("claude")).trimmed().toLower();
            if (agent == QLatin1String("claude") || agent == QLatin1String("claude-code")) {
                // Claude Code nommément (l'action « Monter avec un agent » lance le dernier agent choisi). Au tour de
                // boucle suivant : le panneau lit le projet par la porte, qui doit d'abord rendre la main.
                terminalShow();
                QTimer::singleShot(0, panel, [panel]() { panel->editWithClaudeCode(); });
            } else {
                terminalShow();
                // Au tour de boucle suivant, comme l'action : le panneau lit le projet par la porte.
                QTimer::singleShot(0, panel, [panel, agent]() {
                    if (agent == QLatin1String("shell")) {
                        panel->newShell();
                    } else if (agent == QLatin1String("systeme")) {
                        // Le terminal du système (Terminal.app, konsole, Windows Terminal) sur ce projet, avec la porte et koracut-ctl.
                        panel->openInSystemTerminal(QStringLiteral("-"));
                    } else {
                        panel->launchAgent(agent, false);
                    }
                });
            }
            return Reply::value(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("agent"), agent}, {QStringLiteral("conseil"), QStringLiteral("terminal.etat dit quand l'agent est prêt à lire une demande.")}});
        });
    porte->registerMethod(
        QStringLiteral("terminal.ecrire"),
        QStringLiteral("Écrit un texte dans le terminal comme s'il était tapé, puis Entrée : la demande à l'agent qui monte, un retour, une réponse à sa question. À confirmation (ce qui s'écrit dans un terminal s'exécute)."),
        {p(QStringLiteral("texte"), QStringLiteral("string"), QStringLiteral("Le texte, sur une ou plusieurs lignes."), true),
         p(QStringLiteral("entree"), QStringLiteral("boolean"), QStringLiteral("Valider par Entrée, défaut true.")),
         p(QStringLiteral("onglet"), QStringLiteral("string"), QStringLiteral("Comme terminal.etat."))},
        [porte, terminalPanel, terminalShow, terminalFail](const Call &call) {
            KoraCutTerminalPanel *panel = terminalPanel();
            if (!panel) {
                return noWindow();
            }
            // « texte=4 » arrive comme un nombre (koracut-ctl lit le JSON) : c'est quand même ce qu'il faut taper.
            const QString text = call.params.value(QStringLiteral("texte")).toVariant().toString();
            if (text.isEmpty()) {
                return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("texte est vide."));
            }
            Reply gate;
            if (porte->needsConfirmation(call, QStringLiteral("écrire dans le terminal"), false, gate)) {
                return gate;
            }
            terminalShow();
            QString error;
            if (!panel->porteWrite(call.params.value(QStringLiteral("onglet")).toVariant().toString(), text, call.params.value(QStringLiteral("entree")).toBool(true), error)) {
                return terminalFail(error);
            }
            return Reply::value(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("caracteres"), text.size()}});
        },
        true);
    porte->registerMethod(
        QStringLiteral("terminal.touche"),
        QStringLiteral("Une touche dans le terminal : entree, echap (interrompt l'agent), tab, maj-tab, haut, bas, gauche, droite, retour, ctrl-c. À confirmation."),
        {p(QStringLiteral("touche"), QStringLiteral("string"), QStringLiteral("Le nom de la touche."), true),
         p(QStringLiteral("onglet"), QStringLiteral("string"), QStringLiteral("Comme terminal.etat."))},
        [porte, terminalPanel, terminalFail](const Call &call) {
            KoraCutTerminalPanel *panel = terminalPanel();
            if (!panel) {
                return noWindow();
            }
            Reply gate;
            if (porte->needsConfirmation(call, QStringLiteral("appuyer sur une touche dans le terminal"), false, gate)) {
                return gate;
            }
            QString error;
            if (!panel->porteKey(call.params.value(QStringLiteral("onglet")).toVariant().toString(), call.params.value(QStringLiteral("touche")).toString(), error)) {
                return terminalFail(error);
            }
            return Reply::value(QJsonObject{{QStringLiteral("ok"), true}});
        },
        true);

    // ---- actions ---------------------------------------------------------------------------
    porte->registerMethod(
        QStringLiteral("actions.lister"), QStringLiteral("Toutes les actions nommées de KoraCut (menus, raccourcis), avec leur état."),
        {p(QStringLiteral("filtre"), QStringLiteral("string"), QStringLiteral("Un mot cherché dans le nom ou le libellé.")),
         p(QStringLiteral("activees"), QStringLiteral("boolean"), QStringLiteral("true : seulement les actions disponibles maintenant."))},
        [actions](const Call &call) {
            KActionCollection *collection = actions();
            if (!collection) {
                return noWindow();
            }
            const QString filter = call.params.value(QStringLiteral("filtre")).toString().trimmed();
            const bool onlyEnabled = call.params.value(QStringLiteral("activees")).toBool(false);
            QJsonArray list;
            const QList<QAction *> all = collection->actions();
            for (QAction *a : all) {
                if (a->objectName().isEmpty()) {
                    continue;
                }
                const QString label = cleanLabel(a->text());
                if (!filter.isEmpty() && !a->objectName().contains(filter, Qt::CaseInsensitive) && !label.contains(filter, Qt::CaseInsensitive)) {
                    continue;
                }
                if (onlyEnabled && !a->isEnabled()) {
                    continue;
                }
                QStringList keys;
                const QList<QKeySequence> shortcuts = a->shortcuts();
                for (const QKeySequence &k : shortcuts) {
                    keys << k.toString(QKeySequence::PortableText);
                }
                QJsonObject o{{QStringLiteral("nom"), a->objectName()},
                              {QStringLiteral("libelle"), label},
                              {QStringLiteral("raccourci"), keys.join(QStringLiteral(", "))},
                              {QStringLiteral("activee"), a->isEnabled()},
                              {QStringLiteral("cochable"), a->isCheckable()}};
                if (a->isCheckable()) {
                    o.insert(QStringLiteral("cochee"), a->isChecked());
                }
                const QList<QObject *> owners = a->associatedObjects();
                for (QObject *owner : owners) {
                    if (auto *menu = qobject_cast<QMenu *>(owner)) {
                        o.insert(QStringLiteral("menu"), cleanLabel(menu->title()));
                        break;
                    }
                }
                list.append(o);
            }
            return Reply::value(list);
        });
    porte->registerMethod(QStringLiteral("actions.etat"), QStringLiteral("L'état d'une action : activée, cochée."),
                          {p(QStringLiteral("nom"), QStringLiteral("string"), QStringLiteral("Le nom stable de l'action (actions.lister)."), true)},
                          [actions](const Call &call) {
                              KActionCollection *collection = actions();
                              if (!collection) {
                                  return noWindow();
                              }
                              QAction *a = collection->action(call.params.value(QStringLiteral("nom")).toString().trimmed());
                              if (!a) {
                                  return Reply::fail(QStringLiteral("action_inconnue"), QStringLiteral("Aucune action ne s'appelle ainsi."),
                                                     QStringLiteral("actions.lister filtre=<mot> pour la trouver."));
                              }
                              return Reply::value(QJsonObject{{QStringLiteral("nom"), a->objectName()},
                                                              {QStringLiteral("libelle"), cleanLabel(a->text())},
                                                              {QStringLiteral("activee"), a->isEnabled()},
                                                              {QStringLiteral("cochable"), a->isCheckable()},
                                                              {QStringLiteral("cochee"), a->isCheckable() ? QJsonValue(a->isChecked()) : QJsonValue()}});
                          });
    porte->registerMethod(
        QStringLiteral("actions.declencher"), QStringLiteral("Déclenche une action par son nom, comme un clic dans le menu."),
        {p(QStringLiteral("nom"), QStringLiteral("string"), QStringLiteral("Le nom stable de l'action."), true),
         p(QStringLiteral("cocher"), QStringLiteral("boolean"), QStringLiteral("Pour une action à cocher : l'état voulu."))},
        [actions](const Call &call) {
            KActionCollection *collection = actions();
            if (!collection) {
                return noWindow();
            }
            const QString name = call.params.value(QStringLiteral("nom")).toString().trimmed();
            QAction *a = collection->action(name);
            if (!a) {
                return Reply::fail(QStringLiteral("action_inconnue"), QStringLiteral("Aucune action ne s'appelle « %1 ».").arg(name),
                                   QStringLiteral("actions.lister filtre=<mot> pour la trouver."));
            }
            if (!a->isEnabled()) {
                return Reply::fail(QStringLiteral("action_desactivee"), QStringLiteral("« %1 » est grisée en ce moment.").arg(cleanLabel(a->text())),
                                   QStringLiteral("Elle a besoin d'une sélection, d'un clip ou d'un projet : lis timeline.etat."));
            }
            if (a->isCheckable() && call.params.contains(QStringLiteral("cocher"))) {
                const bool wanted = call.params.value(QStringLiteral("cocher")).toBool();
                if (a->isChecked() != wanted) {
                    a->trigger();
                }
            } else {
                a->trigger();
            }
            QJsonObject o{{QStringLiteral("ok"), true}, {QStringLiteral("nom"), name}};
            if (a->isCheckable()) {
                o.insert(QStringLiteral("cochee"), a->isChecked());
            }
            return Reply::value(o);
        });

    // ---- ui --------------------------------------------------------------------------------
    porte->registerMethod(
        QStringLiteral("ui.module"), QStringLiteral("Le module affiché (montage, etalonnage, audio, vfx, photo, illustration, export) ; avec id, y va."),
        {p(QStringLiteral("id"), QStringLiteral("string"), QStringLiteral("Le module voulu ; sans id, lit seulement."))},
        [window](const Call &call) {
            if (!window) {
                return noWindow();
            }
            QJsonArray available;
            const QList<KoraCutModules::Module> modules = KoraCutModules::all();
            for (const KoraCutModules::Module &m : modules) {
                available.append(QJsonObject{{QStringLiteral("id"), m.id}, {QStringLiteral("nom"), m.name}, {QStringLiteral("pret"), m.ready}});
            }
            const QString id = call.params.value(QStringLiteral("id")).toString().trimmed();
            if (!id.isEmpty()) {
                if (!KoraCutModules::byId(id)) {
                    return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Aucun module ne s'appelle « %1 ».").arg(id));
                }
                window->koraCutSetModule(id);
            }
            return Reply::value(QJsonObject{{QStringLiteral("courant"), window->koraCutCurrentModule()}, {QStringLiteral("disponibles"), available}});
        });
    porte->registerMethod(QStringLiteral("ui.panneaux"), QStringLiteral("Les panneaux : nom interne, titre, ouvert, visible (onglet devant), flottant."), {},
                          [window](const Call &) {
                              if (!window) {
                                  return noWindow();
                              }
                              QJsonArray list;
                              const auto docks = KDDockWidgets::DockRegistry::self()->dockwidgets();
                              for (KDDockWidgets::Core::DockWidget *d : docks) {
                                  list.append(QJsonObject{{QStringLiteral("nom"), d->uniqueName()},
                                                          {QStringLiteral("titre"), d->title()},
                                                          {QStringLiteral("ouvert"), d->isOpen()},
                                                          {QStringLiteral("visible"), d->isOpen() && d->isCurrentTab()},
                                                          {QStringLiteral("flottant"), d->isFloating()}});
                              }
                              return Reply::value(list);
                          });
    porte->registerMethod(
        QStringLiteral("ui.panneau"), QStringLiteral("Montre, cache, donne le focus, agrandit ou remet un panneau."),
        {p(QStringLiteral("nom"), QStringLiteral("string"), QStringLiteral("Nom interne (ui.panneaux)."), true),
         p(QStringLiteral("faire"), QStringLiteral("string"), QStringLiteral("montrer, cacher, focus, agrandir, restaurer."), true)},
        [window](const Call &call) {
            if (!window) {
                return noWindow();
            }
            const QString name = call.params.value(QStringLiteral("nom")).toString();
            const QString verb = call.params.value(QStringLiteral("faire")).toString();
            KDDockWidgets::Core::DockWidget *dock = KDDockWidgets::Core::DockWidget::byName(name);
            if (!dock) {
                return Reply::fail(QStringLiteral("panneau_inconnu"), QStringLiteral("Aucun panneau ne s'appelle « %1 ».").arg(name),
                                   QStringLiteral("ui.panneaux donne les noms."));
            }
            if (verb == QLatin1String("montrer")) {
                dock->open();
                dock->setAsCurrentTab();
            } else if (verb == QLatin1String("cacher")) {
                dock->close();
            } else if (verb == QLatin1String("focus")) {
                dock->open();
                dock->setAsCurrentTab();
                if (QWidget *w = KDDockWidgets::QtCommon::View_qt::asQWidget(dock->guestView().get())) {
                    w->setFocus(Qt::OtherFocusReason);
                }
            } else if (verb == QLatin1String("agrandir")) {
                if (!KoraCutPanelFocus::isActive()) {
                    KoraCutPanelFocus::toggle(dock);
                }
            } else if (verb == QLatin1String("restaurer")) {
                if (KoraCutPanelFocus::isActive()) {
                    KoraCutPanelFocus::toggle(nullptr);
                }
            } else {
                return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("faire = montrer, cacher, focus, agrandir ou restaurer."));
            }
            return Reply::value(QJsonObject{{QStringLiteral("nom"), name}, {QStringLiteral("ouvert"), dock->isOpen()}, {QStringLiteral("visible"), dock->isOpen() && dock->isCurrentTab()}});
        });
    porte->registerMethod(QStringLiteral("ui.fenetres"), QStringLiteral("Les fenêtres secondaires ouvertes, leurs boutons et leurs champs."), {}, [window](const Call &) {
        if (!window) {
            return noWindow();
        }
        QJsonArray list;
        const QList<QWidget *> tops = QApplication::topLevelWidgets();
        for (QWidget *w : tops) {
            if (w != window && w->isVisible() && w->isWindow() && w->width() >= 50) {
                list.append(KoraCutPorte::describeWindow(w));
            }
        }
        return Reply::value(list);
    });
    porte->registerMethod(
        QStringLiteral("ui.fenetre"), QStringLiteral("Répond à une fenêtre ouverte : clique un bouton par son libellé, change d'onglet, remplit un champ, ferme."),
        {p(QStringLiteral("titre"), QStringLiteral("string"), QStringLiteral("Un morceau du titre de la fenêtre (sinon la fenêtre modale active, à défaut la première ouverte ; un bouton est alors cherché dans toutes).")),
         p(QStringLiteral("classe"), QStringLiteral("string"), QStringLiteral("Ou sa classe (ui.fenetres).")),
         p(QStringLiteral("faire"), QStringLiteral("string"), QStringLiteral("bouton:<libellé>, fermer, onglet:<n>, section:<id> ou recherche:<texte> (fenêtre à sections, Préférences).")),
         p(QStringLiteral("champ"), QStringLiteral("string"), QStringLiteral("Nom d'un champ à remplir (ui.fenetres).")),
         p(QStringLiteral("valeur"), QStringLiteral("any"), QStringLiteral("La valeur du champ."))},
        [window](const Call &call) {
            if (!window) {
                return noWindow();
            }
            QWidget *w = findWindow(call.params, window);
            if (!w) {
                return Reply::fail(QStringLiteral("fenetre_absente"), QStringLiteral("Aucune fenêtre ne correspond."), QStringLiteral("ui.fenetres dit ce qui est ouvert."));
            }
            const QString field = call.params.value(QStringLiteral("champ")).toString();
            if (!field.isEmpty()) {
                const QJsonValue value = call.params.value(QStringLiteral("valeur"));
                bool done = false;
                // Un champ se désigne par son objectName ou par son nom stable (koracutId, Préférences).
                const auto named = [&field](QWidget *f) {
                    return f->objectName() == field || (!field.isEmpty() && f->property("koracutId").toString() == field);
                };
                for (QLineEdit *e : w->findChildren<QLineEdit *>()) {
                    if (named(e)) {
                        e->setText(value.toVariant().toString());
                        // Un champ de texte n'enregistre qu'à la fin de la saisie (Préférences) : on la signale.
                        Q_EMIT e->editingFinished();
                        done = true;
                    }
                }
                for (QSpinBox *s : w->findChildren<QSpinBox *>()) {
                    if (named(s)) {
                        s->setValue(value.toInt());
                        done = true;
                    }
                }
                for (QDoubleSpinBox *s : w->findChildren<QDoubleSpinBox *>()) {
                    if (named(s)) {
                        s->setValue(value.toDouble());
                        done = true;
                    }
                }
                for (QAbstractSlider *s : w->findChildren<QAbstractSlider *>()) {
                    if (named(s)) {
                        s->setValue(value.toInt());
                        done = true;
                    }
                }
                for (QComboBox *c : w->findChildren<QComboBox *>()) {
                    if (named(c)) {
                        const int ix = c->findText(value.toVariant().toString());
                        if (ix >= 0) {
                            c->setCurrentIndex(ix);
                            done = true;
                        }
                    }
                }
                for (QCheckBox *c : w->findChildren<QCheckBox *>()) {
                    if (named(c) || cleanLabel(c->text()) == field || (!c->accessibleName().isEmpty() && c->accessibleName() == field)) {
                        c->setChecked(value.toBool());
                        done = true;
                    }
                }
                // Une couleur (KColorButton) : « #RRGGBB ».
                for (QWidget *c : w->findChildren<QWidget *>()) {
                    if (!done && named(c) && c->metaObject()->indexOfProperty("color") >= 0) {
                        const QColor color(value.toString());
                        if (color.isValid()) {
                            c->setProperty("color", color);
                            done = true;
                        }
                    }
                }
                if (!done) {
                    return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Aucun champ « %1 » dans cette fenêtre.").arg(field));
                }
            }
            const QString verb = call.params.value(QStringLiteral("faire")).toString();
            if (verb.startsWith(QLatin1String("bouton:"))) {
                const QString label = verb.mid(7);
                // La fenêtre visée d'abord ; sans titre ni classe, on cherche ensuite dans toutes les
                // fenêtres ouvertes (une fenêtre de chargement peut en cacher une autre).
                QList<QWidget *> candidates{w};
                if (!call.params.contains(QStringLiteral("titre")) && !call.params.contains(QStringLiteral("classe"))) {
                    const QList<QWidget *> tops = QApplication::topLevelWidgets();
                    for (QWidget *other : tops) {
                        if (other != w && other != window && other->isVisible() && other->isWindow() && other->width() >= 50) {
                            candidates << other;
                        }
                    }
                }
                for (QWidget *candidate : std::as_const(candidates)) {
                    for (QAbstractButton *b : candidate->findChildren<QAbstractButton *>()) {
                        if (b->isVisible() && cleanLabel(b->text()) == label) {
                            b->click();
                            return Reply::value(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("fenetre"), candidate->windowTitle()}});
                        }
                    }
                }
                return Reply::fail(QStringLiteral("bouton_introuvable"), QStringLiteral("Aucun bouton « %1 » dans « %2 ».").arg(label, w->windowTitle()),
                                   QStringLiteral("ui.fenetres liste les boutons visibles."));
            }
            if (verb == QLatin1String("fermer")) {
                w->close();
            } else if (verb.startsWith(QLatin1String("section:")) || verb.startsWith(QLatin1String("recherche:"))) {
                // Une fenêtre à sections (Préférences) : aller à une section, filtrer les réglages.
                const bool isSection = verb.startsWith(QLatin1String("section:"));
                const QString arg = verb.mid(isSection ? 8 : 10);
                if (!w->property("koracutSections").isValid()) {
                    return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Cette fenêtre n'a pas de sections."));
                }
                if (isSection) {
                    bool ok = false;
                    QMetaObject::invokeMethod(w, "koraCutShowSection", Q_RETURN_ARG(bool, ok), Q_ARG(QString, arg));
                    if (!ok) {
                        return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Aucune section « %1 ».").arg(arg),
                                           QStringLiteral("ui.fenetres donne les sections."));
                    }
                } else {
                    int found = 0;
                    QMetaObject::invokeMethod(w, "koraCutSearch", Q_RETURN_ARG(int, found), Q_ARG(QString, arg));
                    return Reply::value(QJsonObject{{QStringLiteral("ok"), true},
                                                    {QStringLiteral("trouves"), found},
                                                    {QStringLiteral("fenetre"), KoraCutPorte::describeWindow(w)}});
                }
                return Reply::value(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("fenetre"), KoraCutPorte::describeWindow(w)}});
            } else if (verb.startsWith(QLatin1String("onglet:"))) {
                QTabWidget *tabs = nullptr;
                for (QTabWidget *candidate : w->findChildren<QTabWidget *>()) {
                    if (candidate->isVisible()) {
                        tabs = candidate;
                        break;
                    }
                }
                if (!tabs) {
                    return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Cette fenêtre n'a pas d'onglets."));
                }
                tabs->setCurrentIndex(verb.mid(7).toInt());
            } else if (!verb.isEmpty()) {
                return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("faire = bouton:<libellé>, fermer, onglet:<n>, section:<id> ou recherche:<texte>."));
            }
            return Reply::value(QJsonObject{{QStringLiteral("ok"), true}});
        });
    porte->registerMethod(
        QStringLiteral("ui.capture"), QStringLiteral("Une capture PNG pour vérifier (jamais pour cliquer) : la fenêtre, le moniteur, la timeline ou un panneau."),
        {p(QStringLiteral("chemin"), QStringLiteral("string"), QStringLiteral("Fichier PNG à écrire."), true),
         p(QStringLiteral("quoi"), QStringLiteral("string"), QStringLiteral("fenetre (défaut), moniteur, timeline, ou un nom de panneau."))},
        [window, ctx](const Call &call) {
            if (!window) {
                return noWindow();
            }
            const QString path = expandUserPath(ctx, call.params.value(QStringLiteral("chemin")).toString());
            Reply relative;
            if (relativeWithoutProject(path, relative)) {
                return relative;
            }
            const QString what = call.params.value(QStringLiteral("quoi")).toString(QStringLiteral("fenetre"));
            QWidget *target = window;
            if (what != QLatin1String("fenetre")) {
                const QString name = what == QLatin1String("moniteur") ? QStringLiteral("projectmonitor") : what;
                KDDockWidgets::Core::DockWidget *dock = KDDockWidgets::Core::DockWidget::byName(name);
                target = dock ? KDDockWidgets::QtCommon::View_qt::asQWidget(dock->guestView().get()) : nullptr;
                if (!target) {
                    return Reply::fail(QStringLiteral("panneau_inconnu"), QStringLiteral("Aucun panneau ne s'appelle « %1 ».").arg(what));
                }
            }
            const QPixmap pix = target->grab();
            QDir().mkpath(QFileInfo(path).absolutePath());
            if (!pix.save(path, "PNG")) {
                return Reply::fail(QStringLiteral("ecriture_impossible"), QStringLiteral("La capture ne s'écrit pas dans %1.").arg(path));
            }
            return Reply::value(QJsonObject{{QStringLiteral("chemin"), path}, {QStringLiteral("largeur"), pix.width()}, {QStringLiteral("hauteur"), pix.height()}});
        });
    porte->registerMethod(QStringLiteral("ui.message"), QStringLiteral("Un message dans la barre d'état de KoraCut."),
                          {p(QStringLiteral("texte"), QStringLiteral("string"), QStringLiteral("Le message."), true),
                           p(QStringLiteral("duree_ms"), QStringLiteral("number"), QStringLiteral("Durée d'affichage, défaut 4000."))},
                          [window](const Call &call) {
                              if (!window || !pCore) {
                                  return noWindow();
                              }
                              pCore->displayMessage(call.params.value(QStringLiteral("texte")).toString(), InformationMessage,
                                                    call.params.value(QStringLiteral("duree_ms")).toInt(4000));
                              return Reply::value(QJsonObject{{QStringLiteral("ok"), true}});
                          });

    // ---- theme et reglages (25 septembre 2026) -------------------------------------------------
    // Les mêmes outils que les agents (koracutagenttoolsreglages.cpp) : thème de l'interface et
    // liste blanche des réglages de l'application. Rien ne touche au projet.
    const auto reglage = [timeline](const QString &tool, const QJsonObject &args) {
        return Reply::fromTool(KoraCutAgentTools::call(timeline(), tool, args));
    };
    porte->registerMethod(QStringLiteral("theme.lister"), QStringLiteral("Les thèmes (livrés, créés), le thème actif, le précédent et les jetons de couleur."),
                          {p(QStringLiteral("avec_palette"), QStringLiteral("boolean"), QStringLiteral("Ajoute la palette complète du thème actif et son contrôle de contraste."))},
                          [reglage](const Call &call) { return reglage(QStringLiteral("theme_lister"), call.params); });
    porte->registerMethod(QStringLiteral("theme.appliquer"),
                          QStringLiteral("Change le thème tout de suite : clair, sombre, systeme, precedent, ou un nom (matrix, spider-man, punk…)."),
                          {p(QStringLiteral("nom"), QStringLiteral("string"), QStringLiteral("clair, sombre, systeme, precedent, ou le nom d'un thème."), true)},
                          [reglage](const Call &call) { return reglage(QStringLiteral("theme_appliquer"), call.params); });
    porte->registerMethod(
        QStringLiteral("theme.creer"),
        QStringLiteral("Crée un thème depuis quelques couleurs (au moins fond ou accent), le vérifie (contraste WCAG, corrigé si besoin) et l'applique."),
        {p(QStringLiteral("nom"), QStringLiteral("string"), QStringLiteral("Le nom du thème."), true),
         p(QStringLiteral("couleurs"), QStringLiteral("object"), QStringLiteral("Jeton vers « #RRGGBB » (theme.lister donne les jetons)."), true),
         p(QStringLiteral("inspiration"), QStringLiteral("string"), QStringLiteral("La demande d'origine, en une phrase.")),
         p(QStringLiteral("police"), QStringLiteral("string"), QStringLiteral("Vide, « mono » ou une famille installée.")),
         p(QStringLiteral("rayon"), QStringLiteral("number"), QStringLiteral("Rayon des coins, 0 à 12 (4 = la charte)."))},
        [reglage](const Call &call) { return reglage(QStringLiteral("theme_creer"), call.params); });
    porte->registerMethod(
        QStringLiteral("theme.modifier"), QStringLiteral("Change des jetons, la police ou le rayon d'un thème (un thème livré donne une copie « perso »), et l'applique."),
        {p(QStringLiteral("nom"), QStringLiteral("string"), QStringLiteral("Le thème ; absent : le thème actif.")),
         p(QStringLiteral("couleurs"), QStringLiteral("object"), QStringLiteral("Les jetons à changer.")),
         p(QStringLiteral("police"), QStringLiteral("string"), QStringLiteral("Nouvelle police.")),
         p(QStringLiteral("rayon"), QStringLiteral("number"), QStringLiteral("Nouveau rayon."))},
        [reglage](const Call &call) { return reglage(QStringLiteral("theme_modifier"), call.params); });
    porte->registerMethod(
        QStringLiteral("theme.supprimer"), QStringLiteral("Supprime un thème créé (à confirmation ; jamais un thème livré)."),
        {p(QStringLiteral("nom"), QStringLiteral("string"), QStringLiteral("Le nom du thème créé."), true)},
        [reglage, porte](const Call &call) {
            Reply gate;
            if (porte->needsConfirmation(call, QStringLiteral("supprimer le thème « %1 »").arg(call.params.value(QStringLiteral("nom")).toString()), false, gate)) {
                return gate;
            }
            return reglage(QStringLiteral("theme_supprimer"), call.params);
        },
        true);
    porte->registerMethod(QStringLiteral("reglages.lister"), QStringLiteral("Les réglages de l'application accessibles aux programmes, par section, avec valeur, bornes et effet."),
                          {p(QStringLiteral("section"), QStringLiteral("string"),
                             QStringLiteral("sauvegarde, timeline, durees, lecture, proxy, nouveau_projet, agents, apercu ; absent : toutes."))},
                          [reglage](const Call &call) { return reglage(QStringLiteral("reglages_lister"), call.params); });
    porte->registerMethod(QStringLiteral("reglages.lire"), QStringLiteral("La valeur d'un ou de plusieurs réglages."),
                          {p(QStringLiteral("cle"), QStringLiteral("string"), QStringLiteral("Un réglage.")),
                           p(QStringLiteral("cles"), QStringLiteral("array"), QStringLiteral("Plusieurs réglages."))},
                          [reglage](const Call &call) { return reglage(QStringLiteral("reglages_lire"), call.params); });
    porte->registerMethod(QStringLiteral("reglages.ecrire"),
                          QStringLiteral("Change un réglage de la liste blanche, enregistré et appliqué tout de suite quand c'est possible ; rend avant et après."),
                          {p(QStringLiteral("cle"), QStringLiteral("string"), QStringLiteral("Le réglage (reglages.lister)."), true),
                           p(QStringLiteral("valeur"), QStringLiteral("any"), QStringLiteral("Booléen, entier, durée HH:MM:SS:II ou un des choix."), true)},
                          [reglage](const Call &call) { return reglage(QStringLiteral("reglages_ecrire"), call.params); });

    // ---- projet ----------------------------------------------------------------------------
    porte->registerMethod(QStringLiteral("projet.info"), QStringLiteral("Le projet ouvert : chemin, profil, type de vidéo, séquences, dossiers."), {}, [ctx](const Call &) {
        if (!pCore || !pCore->currentDoc()) {
            return noProject();
        }
        KdenliveDoc *doc = pCore->currentDoc();
        const auto &profile = pCore->getCurrentProfile();
        QJsonArray sequences;
        const QList<QUuid> uuids = doc->getTimelinesUuids();
        for (const QUuid &uuid : uuids) {
            auto tl = doc->getTimeline(uuid, true);
            QString name;
            const QString binId = pCore->projectItemModel()->getSequenceId(uuid);
            if (!binId.isEmpty()) {
                if (auto clip = pCore->projectItemModel()->getClipByBinID(binId)) {
                    name = clip->clipName();
                }
            }
            QJsonObject s{{QStringLiteral("uuid"), uuid.toString(QUuid::WithoutBraces)}, {QStringLiteral("nom"), name}};
            if (tl) {
                s.insert(QStringLiteral("duree"), timeObject(tl->duration()));
            }
            sequences.append(s);
        }
        const QString folder = projectFolderOf(ctx);
        QJsonObject folders;
        if (!folder.isEmpty()) {
            folders.insert(QStringLiteral("projet"), folder);
            const QStringList entries = QDir(folder).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &e : entries) {
                const QString low = e.toLower();
                if ((low.startsWith(QLatin1String("rush")) || low == QLatin1String("sources") || low == QLatin1String("footage")) && !folders.contains(QStringLiteral("rushs"))) {
                    folders.insert(QStringLiteral("rushs"), QDir(folder).absoluteFilePath(e));
                } else if (low.startsWith(QLatin1String("export")) && !folders.contains(QStringLiteral("exports"))) {
                    folders.insert(QStringLiteral("exports"), QDir(folder).absoluteFilePath(e));
                } else if (low == QLatin1String("montage") && !folders.contains(QStringLiteral("montage"))) {
                    folders.insert(QStringLiteral("montage"), QDir(folder).absoluteFilePath(e));
                }
            }
        }
        folders.insert(QStringLiteral("cache"), doc->projectDataFolder());
        QJsonObject props;
        const QString videoType = doc->getDocumentProperty(QStringLiteral("koracut:videotype"));
        if (!videoType.isEmpty()) {
            props.insert(QStringLiteral("koracut:videotype"), videoType);
        }
        QString current;
        if (auto tl = pCore->projectManager()->getTimeline()) {
            current = tl->uuid().toString(QUuid::WithoutBraces);
        }
        QString typeName;
        if (const KoraCutVideoTypes::VideoType *t = KoraCutVideoTypes::byId(videoType)) {
            typeName = t->name;
        }
        const QString file = documentFile();
        return Reply::value(QJsonObject{{QStringLiteral("chemin"), file.isEmpty() ? QJsonValue() : QJsonValue(file)},
                                        {QStringLiteral("chemin_propose"), (file.isEmpty() && !doc->url().isEmpty()) ? QJsonValue(doc->url().toLocalFile()) : QJsonValue()},
                                        {QStringLiteral("nom"), doc->projectName()},
                                        {QStringLiteral("modifie"), doc->isModified()},
                                        {QStringLiteral("profil"), QJsonObject{{QStringLiteral("largeur"), profile->width()}, {QStringLiteral("hauteur"), profile->height()}, {QStringLiteral("ips"), profile->fps()}}},
                                        {QStringLiteral("type_video"), videoType.isEmpty() ? QJsonValue() : QJsonValue(videoType)},
                                        {QStringLiteral("type_video_nom"), typeName.isEmpty() ? QJsonValue() : QJsonValue(typeName)},
                                        {QStringLiteral("sequences"), sequences},
                                        {QStringLiteral("sequence_courante"), current},
                                        {QStringLiteral("dossier"), folder.isEmpty() ? QJsonValue() : QJsonValue(folder)},
                                        {QStringLiteral("dossiers"), folders},
                                        {QStringLiteral("dossier_rendus"), doc->projectRenderFolder()},
                                        {QStringLiteral("proprietes"), props}});
    });
    porte->registerMethod(
        QStringLiteral("projet.nouveau"), QStringLiteral("Un projet neuf depuis un plan : type de vidéo (suffit), nom, format, pistes. À confirmation si le projet courant est modifié."),
        {p(QStringLiteral("plan"), QStringLiteral("object"),
           QStringLiteral("{ type_video, nom, largeur, hauteur, ips (24, 25, 30, 50, 60, ou 23.976, 29.97, 59.94), pistes_video: [noms], pistes_audio: [noms] }"), true),
         p(QStringLiteral("enregistrer"), QStringLiteral("boolean"),
           QStringLiteral("Quand le projet courant a des changements : true l'enregistre avant, false les abandonne. Obligatoire dans ce cas."))},
        [porte, window](const Call &call) {
            if (!window) {
                return noWindow();
            }
            const QJsonObject plan = call.params.value(QStringLiteral("plan")).toObject();
            KoraCutProjectPlan out;
            const QString typeId = plan.value(QStringLiteral("type_video")).toString();
            const KoraCutVideoTypes::VideoType *type = typeId.isEmpty() ? nullptr : KoraCutVideoTypes::byId(typeId);
            if (!typeId.isEmpty() && !type) {
                return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Aucun type de vidéo « %1 ».").arg(typeId),
                                   QStringLiteral("Les ids sont ceux du guide (F01-01, F04-13…), voir la compétence koracut-montage."));
            }
            if (type) {
                out.typeId = type->id;
                out.name = type->name;
                out.frame = KoraCutVideoTypes::frameSize(type->format);
                out.fps = type->fps;
                out.videoTracks = type->videoTracks;
                out.audioTracks = type->audioTracks;
                out.exportLufs = type->exportLufs;
                out.exportCodec = type->exportCodec;
            } else {
                out.frame = QSize(1920, 1080);
                out.fps = 25;
                out.videoTracks = {QStringLiteral("V1"), QStringLiteral("V2")};
                out.audioTracks = {QStringLiteral("A1"), QStringLiteral("A2")};
            }
            if (plan.contains(QStringLiteral("nom"))) {
                out.name = plan.value(QStringLiteral("nom")).toString();
            }
            if (plan.contains(QStringLiteral("largeur")) && plan.contains(QStringLiteral("hauteur"))) {
                out.frame = QSize(plan.value(QStringLiteral("largeur")).toInt(), plan.value(QStringLiteral("hauteur")).toInt());
            }
            if (plan.contains(QStringLiteral("ips"))) {
                // Un entier, un décimal (29.97, 23.976, 59.94) ou une fraction écrite « 30000/1001 ».
                const QJsonValue ips = plan.value(QStringLiteral("ips"));
                bool read = false;
                if (ips.isString() && ips.toString().contains(QLatin1Char('/'))) {
                    const int num = ips.toString().section(QLatin1Char('/'), 0, 0).trimmed().toInt();
                    const int den = ips.toString().section(QLatin1Char('/'), 1, 1).trimmed().toInt();
                    read = num > 0 && den > 0 && out.setRate(double(num) / den);
                } else {
                    read = out.setRate(ips.isString() ? ips.toString().replace(QLatin1Char(','), QLatin1Char('.')).toDouble() : ips.toDouble());
                }
                if (!read) {
                    return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Images par seconde illisibles : « %1 ».").arg(ips.toVariant().toString()),
                                       QStringLiteral("ips est un nombre : 24, 25, 30, 50, 60, ou 23.976, 29.97, 59.94 (ou « 30000/1001 »)."));
                }
            }
            const auto names = [](const QJsonValue &v) {
                QStringList list;
                for (const QJsonValue &n : v.toArray()) {
                    list << n.toString();
                }
                return list;
            };
            if (plan.contains(QStringLiteral("pistes_video"))) {
                out.videoTracks = names(plan.value(QStringLiteral("pistes_video")));
            }
            if (plan.contains(QStringLiteral("pistes_audio"))) {
                out.audioTracks = names(plan.value(QStringLiteral("pistes_audio")));
            }
            if (out.frame.width() < 16 || out.frame.height() < 16 || out.fps < 1) {
                return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Format ou images par seconde illisibles."),
                                   QStringLiteral("largeur et hauteur d'au moins 16, ips au moins 1 (24, 25, 29.97, 30…)."));
            }
            if (pCore->currentDoc() && pCore->currentDoc()->isModified()) {
                // 12 octobre 2026, comme projet.ouvrir : sans ce choix, newFileFromPlan ouvrait la fenêtre « Avertissement »
                // (Enregistrer / Ne pas enregistrer), et une commande de la porte n'ouvre jamais de fenêtre (PORTE.md § 2 bis).
                if (!call.params.contains(QStringLiteral("enregistrer"))) {
                    return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Le projet courant a des changements non enregistrés."),
                                       QStringLiteral("Dis enregistrer=true pour l'enregistrer avant, ou enregistrer=false pour abandonner ses changements."));
                }
                const bool save = call.params.value(QStringLiteral("enregistrer")).toBool();
                if (save && documentFile().isEmpty()) {
                    return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Le projet courant n'a pas encore de fichier : il ne peut pas être enregistré tout seul."),
                                       QStringLiteral("projet.enregistrer chemin=… d'abord, ou enregistrer=false pour abandonner ses changements."));
                }
                if (!save) {
                    Reply gate;
                    if (porte->needsConfirmation(call, QStringLiteral("abandonner les changements non enregistrés du projet courant pour en créer un neuf"), false, gate)) {
                        return gate;
                    }
                    pCore->currentDoc()->setModified(false);
                } else if (!pCore->projectManager()->saveFile()) {
                    return Reply::fail(QStringLiteral("ecriture_impossible"), QStringLiteral("Le projet courant n'a pas pu être enregistré : rien n'a été créé."));
                }
            }
            pCore->projectManager()->newFileFromPlan(out);
            KdenliveDoc *doc = pCore->currentDoc();
            porte->publish(QStringLiteral("projet"), QJsonObject{{QStringLiteral("quoi"), QStringLiteral("nouveau")}, {QStringLiteral("nom"), out.name}});
            return Reply::value(QJsonObject{{QStringLiteral("chemin"), QJsonValue()},
                                            {QStringLiteral("chemin_propose"), (doc && !doc->url().isEmpty()) ? QJsonValue(doc->url().toLocalFile()) : QJsonValue()},
                                            {QStringLiteral("nom"), out.name},
                                            {QStringLiteral("type_video"), out.typeId.isEmpty() ? QJsonValue() : QJsonValue(out.typeId)}});
        },
        true);
    porte->registerMethod(
        QStringLiteral("projet.ouvrir"), QStringLiteral("Ouvre un projet .koracut. À confirmation si le projet courant est modifié."),
        {p(QStringLiteral("chemin"), QStringLiteral("string"), QStringLiteral("Le fichier du projet (absolu, ~, ou relatif au dossier du projet ouvert)."), true),
         p(QStringLiteral("enregistrer"), QStringLiteral("boolean"),
           QStringLiteral("Quand le projet courant a des changements : true l'enregistre avant, false les abandonne. Obligatoire dans ce cas."))},
        [porte, window, ctx](const Call &call) {
            if (!window) {
                return noWindow();
            }
            const QString path = expandUserPath(ctx, call.params.value(QStringLiteral("chemin")).toString());
            Reply relative;
            if (relativeWithoutProject(path, relative)) {
                return relative;
            }
            if (!QFileInfo::exists(path)) {
                return Reply::fail(QStringLiteral("fichier_introuvable"), QStringLiteral("%1 n'existe pas.").arg(path));
            }
            if (pCore->currentDoc() && pCore->currentDoc()->isModified()) {
                // 12 octobre 2026 : sans ce choix, openFile ouvrait la fenêtre « Avertissement » (Enregistrer / Ne pas
                // enregistrer), et une commande de la porte n'ouvre jamais de fenêtre (PORTE.md § 2 bis).
                if (!call.params.contains(QStringLiteral("enregistrer"))) {
                    return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Le projet courant a des changements non enregistrés."),
                                       QStringLiteral("Dis enregistrer=true pour l'enregistrer avant, ou enregistrer=false pour abandonner ses changements."));
                }
                const bool save = call.params.value(QStringLiteral("enregistrer")).toBool();
                if (save && documentFile().isEmpty()) {
                    return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Le projet courant n'a pas encore de fichier : il ne peut pas être enregistré tout seul."),
                                       QStringLiteral("projet.enregistrer chemin=… d'abord, ou enregistrer=false pour abandonner ses changements."));
                }
                if (!save) {
                    Reply gate;
                    if (porte->needsConfirmation(call, QStringLiteral("abandonner les changements non enregistrés du projet courant pour ouvrir %1").arg(QFileInfo(path).fileName()), false, gate)) {
                        return gate;
                    }
                    pCore->currentDoc()->setModified(false);
                } else if (!pCore->projectManager()->saveFile()) {
                    return Reply::fail(QStringLiteral("ecriture_impossible"), QStringLiteral("Le projet courant n'a pas pu être enregistré : rien n'a été ouvert."));
                }
            }
            pCore->projectManager()->openFile(QUrl::fromLocalFile(path));
            porte->publish(QStringLiteral("projet"), QJsonObject{{QStringLiteral("quoi"), QStringLiteral("ouvert")}, {QStringLiteral("chemin"), path}});
            return Reply::value(QJsonObject{{QStringLiteral("chemin"), path}});
        },
        true);
    // projet.format (12 octobre 2026, relevé au montage du 2 octobre) : changer la taille ou la cadence du projet
    // ouvert sans passer par la fenêtre des réglages du projet. La taille se change à tout moment ; la cadence
    // seulement tant qu'aucune séquence n'a de clip (la changer déplacerait chaque coupe).
    porte->registerMethod(
        QStringLiteral("projet.format"),
        QStringLiteral("Change le format du projet ouvert : taille de l'image, images par seconde. Ce qui n'est pas donné reste tel quel. La cadence ne se change que sur un projet sans clip dans ses séquences."),
        {p(QStringLiteral("largeur"), QStringLiteral("integer"), QStringLiteral("Largeur de l'image, en pixels (avec hauteur).")),
         p(QStringLiteral("hauteur"), QStringLiteral("integer"), QStringLiteral("Hauteur de l'image, en pixels (avec largeur).")),
         p(QStringLiteral("ips"), QStringLiteral("number"), QStringLiteral("Images par seconde : 24, 25, 30, 50, 60, ou 23.976, 29.97, 59.94."))},
        [porte, window](const Call &call) {
            if (!window) {
                return noWindow();
            }
            KdenliveDoc *doc = pCore->currentDoc();
            const auto &current = pCore->getCurrentProfile();
            if (!doc || !current) {
                return Reply::fail(QStringLiteral("etat_invalide"), QStringLiteral("Aucun projet ouvert."));
            }
            const bool hasWidth = call.params.contains(QStringLiteral("largeur"));
            const bool hasHeight = call.params.contains(QStringLiteral("hauteur"));
            const bool hasRate = call.params.contains(QStringLiteral("ips"));
            if (hasWidth != hasHeight || (!hasWidth && !hasRate)) {
                return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Donne largeur et hauteur ensemble, ou ips, ou les trois."));
            }
            KoraCutProjectPlan wanted;
            wanted.frame = QSize(current->width(), current->height());
            if (current->frame_rate_den() > 1) {
                wanted.setRate(double(current->frame_rate_num()) / current->frame_rate_den());
            } else {
                wanted.setRate(current->frame_rate_num());
            }
            if (hasWidth) {
                wanted.frame = QSize(call.params.value(QStringLiteral("largeur")).toInt(), call.params.value(QStringLiteral("hauteur")).toInt());
                if (wanted.frame.width() < 16 || wanted.frame.height() < 16 || wanted.frame.width() > 16384 || wanted.frame.height() > 16384) {
                    return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Taille illisible : %1 × %2.").arg(wanted.frame.width()).arg(wanted.frame.height()),
                                       QStringLiteral("largeur et hauteur entre 16 et 16384."));
                }
                if (wanted.frame.width() % 2 != 0 || wanted.frame.height() % 2 != 0) {
                    return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("largeur et hauteur doivent être paires (les encodeurs le demandent)."));
                }
            }
            const double rateBefore = double(current->frame_rate_num()) / current->frame_rate_den();
            if (hasRate) {
                const QJsonValue ips = call.params.value(QStringLiteral("ips"));
                if (!wanted.setRate(ips.isString() ? ips.toString().replace(QLatin1Char(','), QLatin1Char('.')).toDouble() : ips.toDouble())) {
                    return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Images par seconde illisibles : « %1 ».").arg(ips.toVariant().toString()),
                                       QStringLiteral("ips est un nombre : 24, 25, 30, 50, 60, ou 23.976, 29.97, 59.94."));
                }
            }
            const auto profileJson = []() {
                const auto &now = pCore->getCurrentProfile();
                return QJsonObject{{QStringLiteral("largeur"), now->width()}, {QStringLiteral("hauteur"), now->height()}, {QStringLiteral("ips"), now->fps()}};
            };
            if (qAbs(wanted.rate() - rateBefore) > 0.0005) {
                int clips = 0;
                for (const QUuid &uuid : doc->getTimelinesUuids()) {
                    if (auto tl = doc->getTimeline(uuid, true)) {
                        clips += tl->getClipsCount();
                    }
                }
                if (clips > 0) {
                    return Reply::fail(QStringLiteral("etat_invalide"),
                                       QStringLiteral("La cadence ne se change pas sur un projet qui a déjà %1 clip(s) dans ses séquences : chaque coupe changerait de place.").arg(clips),
                                       QStringLiteral("Change seulement la taille (largeur, hauteur), ou crée un projet neuf à la bonne cadence avant de monter."));
                }
            }
            const QString path = ProjectManager::koraCutProfileFor(wanted);
            if (path.isEmpty()) {
                return Reply::fail(QStringLiteral("ecriture_impossible"), QStringLiteral("Le profil %1 × %2 n'a pas pu être créé.").arg(wanted.frame.width()).arg(wanted.frame.height()));
            }
            if (path == pCore->getCurrentProfilePath()) {
                return Reply::value(QJsonObject{{QStringLiteral("profil"), profileJson()}, {QStringLiteral("inchange"), true}});
            }
            // slotSwitchProfile est le slot du bouton « Basculer » du chutier : même chemin qu'à la souris.
            if (!QMetaObject::invokeMethod(doc, "slotSwitchProfile", Qt::DirectConnection, Q_ARG(QString, path), Q_ARG(bool, true))) {
                return Reply::fail(QStringLiteral("etat_invalide"), QStringLiteral("Le format n'a pas pu être changé."));
            }
            // Un format demandé est un format choisi : un rush importé ensuite ne le change pas.
            doc->setDocumentProperty(QStringLiteral("koracut:format_choisi"), QStringLiteral("1"));
            porte->publish(QStringLiteral("projet"), QJsonObject{{QStringLiteral("quoi"), QStringLiteral("format")}, {QStringLiteral("profil"), profileJson()}});
            return Reply::value(QJsonObject{{QStringLiteral("profil"), profileJson()}, {QStringLiteral("inchange"), false}});
        });
    porte->registerMethod(QStringLiteral("projet.enregistrer"), QStringLiteral("Enregistre le projet (avec chemin : enregistre sous)."),
                          {p(QStringLiteral("chemin"), QStringLiteral("string"), QStringLiteral("Fichier .koracut (absolu, ~, ou relatif au dossier du projet), facultatif si le projet en a déjà un."))},
                          [porte, window, ctx](const Call &call) {
                              if (!window || !pCore->currentDoc()) {
                                  return window ? noProject() : noWindow();
                              }
                              QString path = expandUserPath(ctx, call.params.value(QStringLiteral("chemin")).toString());
                              bool ok = false;
                              if (path.isEmpty() && documentFile().isEmpty() && !pCore->currentDoc()->url().isEmpty()) {
                                  // Le projet neuf a un chemin proposé (projet.nouveau) : sans chemin, c'est lui.
                                  path = pCore->currentDoc()->url().toLocalFile();
                              }
                              Reply relative;
                              if (relativeWithoutProject(path, relative)) {
                                  return relative;
                              }
                              if (!path.isEmpty()) {
                                  if (!path.endsWith(QLatin1String(".koracut"), Qt::CaseInsensitive) && !path.endsWith(QLatin1String(".kdenlive"), Qt::CaseInsensitive)) {
                                      path += QStringLiteral(".koracut");
                                  }
                                  QDir().mkpath(QFileInfo(path).absolutePath());
                                  ok = pCore->projectManager()->saveFileAs(path, true, false);
                              } else if (pCore->currentDoc()->url().isEmpty()) {
                                  return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Le projet n'a pas encore de fichier : donne un chemin."));
                              } else {
                                  ok = pCore->projectManager()->saveFile();
                              }
                              if (!ok) {
                                  return Reply::fail(QStringLiteral("ecriture_impossible"), QStringLiteral("L'enregistrement a échoué."));
                              }
                              const QString saved = pCore->currentDoc()->url().toLocalFile();
                              porte->publish(QStringLiteral("projet"), QJsonObject{{QStringLiteral("quoi"), QStringLiteral("enregistre")}, {QStringLiteral("chemin"), saved}});
                              return Reply::value(QJsonObject{{QStringLiteral("chemin"), saved}});
                          });
    porte->registerMethod(
        QStringLiteral("projet.reglages"), QStringLiteral("Lit ou écrit une propriété koracut:* ou kdenlive:* du document."),
        {p(QStringLiteral("clef"), QStringLiteral("string"), QStringLiteral("La propriété."), true), p(QStringLiteral("valeur"), QStringLiteral("string"), QStringLiteral("À écrire ; absent : lecture."))},
        [](const Call &call) {
            if (!pCore || !pCore->currentDoc()) {
                return noProject();
            }
            const QString key = call.params.value(QStringLiteral("clef")).toString();
            if (!key.startsWith(QLatin1String("koracut:")) && !key.startsWith(QLatin1String("kdenlive:"))) {
                return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Seules les propriétés koracut:* et kdenlive:* sont accessibles."));
            }
            if (call.params.contains(QStringLiteral("valeur"))) {
                pCore->currentDoc()->setDocumentProperty(key, call.params.value(QStringLiteral("valeur")).toVariant().toString());
                pCore->currentDoc()->setModified(true);
            }
            return Reply::value(QJsonObject{{QStringLiteral("clef"), key}, {QStringLiteral("valeur"), pCore->currentDoc()->getDocumentProperty(key)}});
        });

    // ---- projet.sequences (lot 3) ------------------------------------------------------------
    porte->registerMethod(QStringLiteral("projet.sequences"), QStringLiteral("Les séquences du projet : uuid, nom, media_id (le clip du chutier), durée, pistes, profil, courante."), {}, [](const Call &) {
        if (!pCore || !pCore->currentDoc() || !pCore->projectItemModel()) {
            return noProject();
        }
        QJsonArray list;
        const QList<QUuid> uuids = pCore->currentDoc()->getTimelinesUuids();
        for (const QUuid &uuid : uuids) {
            list.append(sequenceEntry(uuid));
        }
        const auto &profile = pCore->getCurrentProfile();
        return Reply::value(QJsonObject{{QStringLiteral("sequences"), list},
                                        {QStringLiteral("profil"), QJsonObject{{QStringLiteral("largeur"), profile->width()}, {QStringLiteral("hauteur"), profile->height()}, {QStringLiteral("ips"), profile->fps()}}},
                                        {QStringLiteral("note"), QStringLiteral("Un projet KoraCut a un seul profil (format et cadence) : toutes ses séquences le partagent.")}});
    });
    porte->registerMethod(
        QStringLiteral("projet.sequence"),
        QStringLiteral("Crée, renomme, active ou supprime une séquence. creer : nom, pistes_video, pistes_audio (sans nombre de pistes : celles de la séquence en cours, noms compris ; largeur, hauteur, ips sont lus mais le projet n'a qu'un profil, la réponse le dit). renommer : sequence + nom. activer : sequence. supprimer : sequence, à confirmation, pas annulable. Une étape d'annulation pour creer et renommer."),
        {p(QStringLiteral("faire"), QStringLiteral("string"), QStringLiteral("creer, renommer, activer, supprimer."), true),
         p(QStringLiteral("sequence"), QStringLiteral("string"), QStringLiteral("L'uuid ou le nom de la séquence visée (renommer, activer, supprimer) ; absent : la courante.")),
         p(QStringLiteral("nom"), QStringLiteral("string"), QStringLiteral("Le nom (creer, renommer).")),
         p(QStringLiteral("pistes_video"), QStringLiteral("number"), QStringLiteral("creer : nombre de pistes vidéo ; sans pistes_video ni pistes_audio, la séquence reprend les pistes de la séquence en cours, noms compris.")),
         p(QStringLiteral("pistes_audio"), QStringLiteral("number"), QStringLiteral("creer : nombre de pistes audio.")),
         p(QStringLiteral("largeur"), QStringLiteral("number"), QStringLiteral("creer : format souhaité, pour mémoire.")),
         p(QStringLiteral("hauteur"), QStringLiteral("number"), QStringLiteral("creer : format souhaité, pour mémoire.")),
         p(QStringLiteral("ips"), QStringLiteral("number"), QStringLiteral("creer : cadence souhaitée, pour mémoire."))},
        [porte](const Call &call) {
            if (!pCore || !pCore->currentDoc() || !pCore->projectItemModel()) {
                return noProject();
            }
            const QString verb = call.params.value(QStringLiteral("faire")).toString();
            auto model = pCore->projectItemModel();
            const auto &profile = pCore->getCurrentProfile();
            if (verb == QLatin1String("creer")) {
                // Le même code que l'outil creer_sequence : sans nombre de pistes, la séquence neuve reprend
                // celles de la séquence en cours, noms compris (« Plans », « Voix »…), au lieu de dix pistes
                // sans nom. La séquence d'avant reste devant.
                auto tl = pCore->projectManager() ? pCore->projectManager()->getTimeline() : nullptr;
                if (!tl) {
                    return noProject();
                }
                QJsonObject args{{QStringLiteral("activer"), false}};
                for (const QString &key : {QStringLiteral("nom"), QStringLiteral("pistes_video"), QStringLiteral("pistes_audio")}) {
                    if (call.params.contains(key)) {
                        args.insert(key, call.params.value(key));
                    }
                }
                const QJsonObject made = KoraCutAgentTools::call(tl, QStringLiteral("creer_sequence"), args);
                if (!made.value(QStringLiteral("ok")).toBool()) {
                    return Reply::fromTool(made);
                }
                const QJsonObject seq = made.value(QStringLiteral("sequence")).toObject();
                const QString binId = seq.value(QStringLiteral("media_id")).toString();
                const QString name = seq.value(QStringLiteral("nom")).toString();
                auto clip = model->getClipByBinID(binId);
                const QUuid uuid = clip ? clip->getSequenceUuid() : QUuid();
                QJsonObject o = uuid.isNull() ? QJsonObject{{QStringLiteral("media_id"), binId}} : sequenceEntry(uuid);
                o.insert(QStringLiteral("ok"), true);
                o.insert(QStringLiteral("pistes_reprises"), !call.params.contains(QStringLiteral("pistes_video")) && !call.params.contains(QStringLiteral("pistes_audio")));
                const int w = call.params.value(QStringLiteral("largeur")).toInt();
                const int h = call.params.value(QStringLiteral("hauteur")).toInt();
                const double ips = call.params.value(QStringLiteral("ips")).toDouble();
                if ((w > 0 && w != profile->width()) || (h > 0 && h != profile->height()) || (ips > 0 && qAbs(ips - profile->fps()) > 0.01)) {
                    o.insert(QStringLiteral("note"), QStringLiteral("Un projet KoraCut a un seul profil : la séquence est en %1 × %2 à %3 i/s comme le projet, pas en %4 × %5 à %6. Pour un autre format, un autre projet (projet.nouveau).")
                                                         .arg(profile->width()).arg(profile->height()).arg(profile->fps()).arg(w > 0 ? w : profile->width()).arg(h > 0 ? h : profile->height()).arg(ips > 0 ? ips : profile->fps()));
                }
                porte->publish(QStringLiteral("projet"), QJsonObject{{QStringLiteral("quoi"), QStringLiteral("sequence")}, {QStringLiteral("faire"), verb}, {QStringLiteral("nom"), name}});
                return Reply::value(o);
            }
            if (verb == QLatin1String("supprimer") && pCore->currentDoc()->getTimelinesUuids().size() <= 1) {
                return Reply::fail(QStringLiteral("sequence_unique"), QStringLiteral("Un projet garde au moins une séquence."));
            }
            QUuid uuid;
            auto clip = findSequence(call.params.value(QStringLiteral("sequence")).toString(), &uuid);
            if (!clip) {
                return Reply::fail(QStringLiteral("sequence_introuvable"), QStringLiteral("Aucune séquence ne correspond à « %1 ».").arg(call.params.value(QStringLiteral("sequence")).toString()),
                                   QStringLiteral("projet.sequences donne les uuid et les noms."));
            }
            if (verb == QLatin1String("renommer")) {
                const QString name = call.params.value(QStringLiteral("nom")).toString().trimmed();
                if (name.isEmpty()) {
                    return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("nom est vide."));
                }
                const QString binId = clip->clipId();
                const QString before = clip->clipName();
                const auto apply = [binId](const QString &value) {
                    auto c = pCore->projectItemModel()->getClipByBinID(binId);
                    if (!c) {
                        return false;
                    }
                    QMap<QString, QString> props;
                    props.insert(QStringLiteral("kdenlive:clipname"), value);
                    c->setProperties(props, true);
                    return true;
                };
                apply(name);
                Fun undo = [apply, before]() { return apply(before); };
                Fun redo = [apply, name]() { return apply(name); };
                pCore->pushUndo(undo, redo, i18nc("@action undo text, %1 old name, %2 new name", "Rename sequence %1 to %2", before, name));
                porte->publish(QStringLiteral("projet"), QJsonObject{{QStringLiteral("quoi"), QStringLiteral("sequence")}, {QStringLiteral("faire"), verb}, {QStringLiteral("nom"), name}});
                QJsonObject o = sequenceEntry(uuid);
                o.insert(QStringLiteral("avant"), before);
                return Reply::value(o);
            }
            if (verb == QLatin1String("activer")) {
                if (pCore->window()) {
                    pCore->projectManager()->openTimeline(clip->clipId(), -1, uuid);
                } else {
                    pCore->projectManager()->setActiveTimeline(uuid);
                }
                porte->watchTimeline(pCore->projectManager()->getTimeline());
                return Reply::value(sequenceEntry(uuid));
            }
            if (verb == QLatin1String("supprimer")) {
                Reply gate;
                if (porte->needsConfirmation(call, QStringLiteral("supprimer la séquence « %1 » (pas annulable, l'historique est vidé)").arg(clip->clipName()), false, gate)) {
                    return gate;
                }
                const QString name = clip->clipName();
                // Comme le chutier : fermer la timeline, vider l'historique (il pointe sur elle), retirer le clip.
                pCore->projectManager()->closeTimeline(uuid, true, true, true);
                Fun undo = []() { return true; };
                Fun redo = []() { return true; };
                if (!model->requestBinClipDeletion(clip, undo, redo)) {
                    return Reply::fail(QStringLiteral("media_utilise"), QStringLiteral("La séquence « %1 » ne se retire pas du chutier (utilisée sur une piste verrouillée ?).").arg(name));
                }
                if (auto stack = pCore->undoStack()) {
                    stack->clear();
                }
                porte->watchTimeline(pCore->projectManager()->getTimeline());
                porte->publish(QStringLiteral("projet"), QJsonObject{{QStringLiteral("quoi"), QStringLiteral("sequence")}, {QStringLiteral("faire"), verb}, {QStringLiteral("nom"), name}});
                return Reply::value(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("nom"), name}, {QStringLiteral("annulable"), false}});
            }
            return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("faire = creer, renommer, activer ou supprimer."));
        },
        true);

    // ---- medias ----------------------------------------------------------------------------
    porte->registerMethod(
        QStringLiteral("medias.lister"),
        QStringLiteral("Les médias du chutier (comme l'outil lister_medias). Sans limite, tout le chutier ; avec limite, une page, et suite donne le debut de la suivante."),
        {p(QStringLiteral("filtre"), QStringLiteral("string"), QStringLiteral("Un mot du nom, du dossier ou des étiquettes (recherche est accepté aussi).")),
         p(QStringLiteral("type"), QStringLiteral("string"), QStringLiteral("tous (défaut), video, audio, image, titre, sequence.")),
         p(QStringLiteral("dossier"), QStringLiteral("string"), QStringLiteral("Un dossier du chutier.")),
         p(QStringLiteral("limite"), QStringLiteral("number"), QStringLiteral("Nombre de médias par page (1 à 200) ; absent : tous.")),
         p(QStringLiteral("debut"), QStringLiteral("number"), QStringLiteral("Rang du premier média rendu (la suite d'une page)."))},
        [timeline](const Call &call) {
            auto tl = timeline();
            if (!tl) {
                return noProject();
            }
            // La porte ne coupe pas en silence : sans limite, les pages sont mises bout à bout.
            QJsonObject args = call.params;
            if (args.contains(QStringLiteral("filtre")) && !args.contains(QStringLiteral("recherche"))) {
                args.insert(QStringLiteral("recherche"), args.value(QStringLiteral("filtre")));
            }
            args.remove(QStringLiteral("filtre"));
            if (args.contains(QStringLiteral("limite"))) {
                return Reply::fromTool(KoraCutAgentTools::call(tl, QStringLiteral("lister_medias"), args));
            }
            args.insert(QStringLiteral("limite"), 200);
            QJsonObject all = KoraCutAgentTools::call(tl, QStringLiteral("lister_medias"), args);
            QJsonArray medias = all.value(QStringLiteral("medias")).toArray();
            while (all.value(QStringLiteral("ok")).toBool() && all.contains(QStringLiteral("suite"))) {
                args.insert(QStringLiteral("debut"), all.value(QStringLiteral("suite")));
                const QJsonObject page = KoraCutAgentTools::call(tl, QStringLiteral("lister_medias"), args);
                const QJsonArray more = page.value(QStringLiteral("medias")).toArray();
                if (!page.value(QStringLiteral("ok")).toBool() || more.isEmpty()) {
                    break;
                }
                for (const QJsonValue &m : more) {
                    medias.append(m);
                }
                all = page;
            }
            all.insert(QStringLiteral("medias"), medias);
            all.remove(QStringLiteral("suite"));
            all.remove(QStringLiteral("tronque"));
            all.remove(QStringLiteral("conseil"));
            return Reply::fromTool(all);
        });
    porte->registerMethod(QStringLiteral("medias.info"), QStringLiteral("Tout sur un média : chemin, type, durée, format, son, proxy, utilisations."),
                          {p(QStringLiteral("media_id"), QStringLiteral("string"), QStringLiteral("L'id du chutier (medias.lister)."), true)},
                          [](const Call &call) {
                              const QString id = call.params.value(QStringLiteral("media_id")).toVariant().toString();
                              if (!pCore || !pCore->projectItemModel() || !pCore->projectItemModel()->hasClip(id)) {
                                  return Reply::fail(QStringLiteral("media_introuvable"), QStringLiteral("Aucun média n'a l'identifiant %1.").arg(id),
                                                     QStringLiteral("medias.lister donne les ids."));
                              }
                              auto clip = pCore->projectItemModel()->getClipByBinID(id);
                              const QSize size = clip->getFrameSize();
                              const int num = clip->getProducerIntProperty(QStringLiteral("meta.media.frame_rate_num"));
                              const int den = clip->getProducerIntProperty(QStringLiteral("meta.media.frame_rate_den"));
                              QJsonObject o{{QStringLiteral("id"), id},
                                            {QStringLiteral("nom"), clip->clipName()},
                                            {QStringLiteral("chemin"), clip->url()},
                                            {QStringLiteral("type"), producerTypeName(clip->clipType())},
                                            {QStringLiteral("duree"), timeObject(int(clip->frameDuration()))},
                                            {QStringLiteral("largeur"), size.width()},
                                            {QStringLiteral("hauteur"), size.height()},
                                            {QStringLiteral("ips"), den > 0 ? double(num) / den : 0.},
                                            {QStringLiteral("son"), clip->hasAudio()},
                                            {QStringLiteral("video"), clip->hasVideo()},
                                            {QStringLiteral("proxy"), clip->hasProxy()},
                                            {QStringLiteral("utilisations"), int(clip->refCount())},
                                            {QStringLiteral("manquant"), clip->clipStatus() == FileStatus::StatusMissing}};
                              // Le chemin complet du dossier du chutier, comme medias.lister ; dossier_nom : le dernier nom.
                              const QString folderPath = KoraCutAgentTools::binFolderPath(id);
                              if (!folderPath.isEmpty()) {
                                  o.insert(QStringLiteral("dossier"), folderPath);
                                  o.insert(QStringLiteral("dossier_nom"), folderPath.section(QLatin1Char('/'), -1));
                              }
                              return Reply::value(o);
                          });
    porte->registerAsyncMethod(
        QStringLiteral("medias.importer"),
        QStringLiteral("Importe des fichiers dans le chutier (copie ou lie, ne déplace jamais). Un fichier déjà dans le chutier n'est pas réimporté : sa ligne rend son media_id avec existait=true. Rend la main quand les clips sont prêts, sans jamais ouvrir de fenêtre."),
        {p(QStringLiteral("chemins"), QStringLiteral("array"), QStringLiteral("Les fichiers ou dossiers à importer (absolus, ~, ou relatifs au dossier du projet)."), true),
         p(QStringLiteral("dossier"), QStringLiteral("string"), QStringLiteral("Dossier du chutier (nom ou id), créé s'il manque.")),
         p(QStringLiteral("dupliquer"), QStringLiteral("boolean"), QStringLiteral("true : un fichier déjà dans le chutier y entre une deuxième fois (défaut false).")),
         p(QStringLiteral("attendre"), QStringLiteral("boolean"), QStringLiteral("Défaut true : attend que les clips soient prêts (60 s au plus)."))},
        [porte, ctx](const Call &call, const KoraCutPorte::Done &done) {
            if (!pCore || !pCore->projectItemModel() || !pCore->currentDoc()) {
                done(noProject());
                return;
            }
            QList<QUrl> urls;
            QJsonArray others; // introuvables et déjà présents : répondus sans import
            const bool duplicate = call.params.value(QStringLiteral("dupliquer")).toBool(false);
            const QHash<QString, QString> known = mediaByPath();
            const QJsonArray paths = call.params.value(QStringLiteral("chemins")).toArray();
            for (const QJsonValue &v : paths) {
                const QString path = expandUserPath(ctx, v.toString());
                Reply relative;
                if (relativeWithoutProject(path, relative)) {
                    others.append(QJsonObject{{QStringLiteral("chemin"), v.toString()}, {QStringLiteral("ok"), false}, {QStringLiteral("erreur"), QStringLiteral("projet_absent")}});
                    continue;
                }
                if (!QFileInfo::exists(path)) {
                    others.append(QJsonObject{{QStringLiteral("chemin"), v.toString()}, {QStringLiteral("ok"), false}, {QStringLiteral("erreur"), QStringLiteral("fichier_introuvable")}});
                    continue;
                }
                const QString absolute = QFileInfo(path).absoluteFilePath();
                const QString existing = known.value(absolute);
                if (!existing.isEmpty() && !duplicate && !QFileInfo(absolute).isDir()) {
                    QJsonObject o{{QStringLiteral("chemin"), absolute}, {QStringLiteral("media_id"), existing}, {QStringLiteral("ok"), true}, {QStringLiteral("existait"), true}};
                    if (auto clip = pCore->projectItemModel()->getClipByBinID(existing)) {
                        o.insert(QStringLiteral("nom"), clip->clipName());
                        o.insert(QStringLiteral("duree"), timeObject(int(clip->frameDuration())));
                        // Déjà là ne veut pas dire au bon format : le dire aussi pour lui (13 octobre 2026).
                        const QJsonObject other = formatDifference(clip);
                        if (!other.isEmpty()) {
                            o.insert(QStringLiteral("format_different"), other);
                        }
                    }
                    others.append(o);
                    continue;
                }
                urls << QUrl::fromLocalFile(absolute);
            }
            if (urls.isEmpty()) {
                if (others.isEmpty()) {
                    done(Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("chemins est vide.")));
                } else if (std::none_of(others.begin(), others.end(), [](const QJsonValue &o) { return o.toObject().value(QStringLiteral("ok")).toBool(); })) {
                    const bool unsaved = std::all_of(others.begin(), others.end(), [](const QJsonValue &o) { return o.toObject().value(QStringLiteral("erreur")).toString() == QLatin1String("projet_absent"); });
                    done(unsaved ? Reply::fail(QStringLiteral("projet_absent"), QStringLiteral("Chemins relatifs, et le projet n'est pas encore enregistré : il n'a pas de dossier."),
                                               QStringLiteral("Donne des chemins absolus (ou ~), ou enregistre d'abord le projet."))
                                 : Reply::fail(QStringLiteral("fichier_introuvable"), QStringLiteral("Aucun des chemins n'existe."), QStringLiteral("Vérifie avec fichiers.lister ; un chemin relatif part du dossier du projet.")));
                } else {
                    done(Reply::value(others));
                }
                return;
            }
            auto model = pCore->projectItemModel();
            QString folderId = model->getRootFolder()->clipId();
            const QString folderName = call.params.value(QStringLiteral("dossier")).toString().trimmed();
            if (!folderName.isEmpty()) {
                if (auto folder = findBinFolder(folderName)) {
                    folderId = folder->clipId();
                } else {
                    Fun undo = []() { return true; };
                    Fun redo = []() { return true; };
                    QString newId;
                    if (model->requestAddFolder(newId, folderName, model->getRootFolder()->clipId(), undo, redo)) {
                        pCore->pushUndo(undo, redo, i18nc("@action undo text", "Create bin folder %1", folderName));
                        folderId = newId;
                    }
                }
            }
            const bool wait = call.params.value(QStringLiteral("attendre")).toBool(true);
            const int expected = urls.size();
            auto ready = std::make_shared<QJsonArray>();
            auto finished = std::make_shared<bool>(false);
            auto timer = std::make_shared<QTimer>();
            auto answer = [done, ready, others, finished, timer]() {
                if (*finished) {
                    return;
                }
                *finished = true;
                timer->stop();
                QJsonArray list = *ready;
                for (const QJsonValue &m : others) {
                    list.append(m);
                }
                done(Reply::value(list));
            };
            Fun undo = []() { return true; };
            Fun redo = []() { return true; };
            const auto onReady = [ready, expected, answer, wait](const QString &binId) {
                QJsonObject o{{QStringLiteral("media_id"), binId}, {QStringLiteral("ok"), true}};
                if (auto clip = pCore->projectItemModel()->getClipByBinID(binId)) {
                    o.insert(QStringLiteral("chemin"), clip->url());
                    o.insert(QStringLiteral("nom"), clip->clipName());
                    o.insert(QStringLiteral("duree"), timeObject(int(clip->frameDuration())));
                    // Un rush qui n'a pas le format du projet : on le dit, le projet garde le sien (12 octobre 2026).
                    const QJsonObject other = formatDifference(clip);
                    if (!other.isEmpty()) {
                        o.insert(QStringLiteral("format_different"), other);
                    }
                }
                ready->append(o);
                if (wait && ready->size() >= expected) {
                    answer();
                }
            };
            QString last;
            if (duplicate) {
                // Fichier par fichier : ce chemin ne vérifie pas les doublons, donc n'ouvre pas la
                // fenêtre « Dupliquer ». Les dossiers passent par la liste (ils n'ont pas de doublon).
                QList<QUrl> folders;
                for (const QUrl &url : std::as_const(urls)) {
                    if (QFileInfo(url.toLocalFile()).isDir()) {
                        folders << url;
                    } else {
                        last = ClipCreator::createClipFromFile(url.toLocalFile(), folderId, model, undo, redo, onReady);
                    }
                }
                if (!folders.isEmpty()) {
                    last = ClipCreator::createClipsFromList(folders, false, folderId, model, undo, redo, onReady);
                }
            } else {
                // Les doublons ont été retirés au-dessus : la liste ne pose pas de question.
                last = ClipCreator::createClipsFromList(urls, false, folderId, model, undo, redo, onReady);
            }
            pCore->pushUndo(undo, redo, i18ncp("@action undo text", "Import %1 file", "Import %1 files", urls.size()));
            porte->publish(QStringLiteral("projet"), QJsonObject{{QStringLiteral("quoi"), QStringLiteral("import")}, {QStringLiteral("fichiers"), urls.size()}});
            if (!wait) {
                done(Reply::value(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("lances"), urls.size()}, {QStringLiteral("dernier_media_id"), last}}));
                return;
            }
            if (ready->size() >= expected) {
                answer();
                return;
            }
            // Un fichier qui ne se charge pas ne rappelle jamais : on répond avec ce qui est prêt.
            timer->setSingleShot(true);
            QObject::connect(timer.get(), &QTimer::timeout, porte, [answer]() { answer(); });
            timer->start(60000);
        });
    porte->registerMethod(QStringLiteral("medias.renommer"), QStringLiteral("Renomme un média dans le chutier (le nom affiché, pas le fichier sur le disque). Une étape d'annulation."),
                          {p(QStringLiteral("media_id"), QStringLiteral("string"), QStringLiteral("L'id du chutier."), true),
                           p(QStringLiteral("nom"), QStringLiteral("string"), QStringLiteral("Le nouveau nom."), true)},
                          [](const Call &call) {
                              const QString id = call.params.value(QStringLiteral("media_id")).toVariant().toString();
                              if (!pCore || !pCore->projectItemModel() || !pCore->projectItemModel()->hasClip(id)) {
                                  return Reply::fail(QStringLiteral("media_introuvable"), QStringLiteral("Aucun média n'a l'identifiant %1.").arg(id), QStringLiteral("medias.lister donne les ids."));
                              }
                              const QString name = call.params.value(QStringLiteral("nom")).toString().trimmed();
                              if (name.isEmpty()) {
                                  return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("nom est vide."));
                              }
                              auto clip = pCore->projectItemModel()->getClipByBinID(id);
                              const QString before = clip->clipName();
                              const auto apply = [id](const QString &value) {
                                  auto c = pCore->projectItemModel()->getClipByBinID(id);
                                  if (!c) {
                                      return false;
                                  }
                                  QMap<QString, QString> props;
                                  props.insert(QStringLiteral("kdenlive:clipname"), value);
                                  c->setProperties(props, true);
                                  return true;
                              };
                              if (!apply(name)) {
                                  return Reply::fail(QStringLiteral("media_introuvable"), QStringLiteral("Le média %1 a disparu.").arg(id));
                              }
                              Fun undo = [apply, before]() { return apply(before); };
                              Fun redo = [apply, name]() { return apply(name); };
                              pCore->pushUndo(undo, redo, i18nc("@action undo text, %1 old name, %2 new name", "Rename %1 to %2", before, name));
                              return Reply::value(QJsonObject{{QStringLiteral("media_id"), id}, {QStringLiteral("avant"), before}, {QStringLiteral("apres"), name}, {QStringLiteral("sequence"), clip->clipType() == ClipType::Timeline}});
                          });
    porte->registerAsyncMethod(
        QStringLiteral("medias.analyser"), QStringLiteral("Analyse un média : vignette (PNG à un temps donné), transcription (Whisper, en local), silences (les blancs à couper), niveaux (LUFS intégré et crête, par ffmpeg), plans (changements de plan, par ffmpeg). Les temps rendus sont en images et en timecode du projet."),
        {p(QStringLiteral("media_id"), QStringLiteral("string"), QStringLiteral("L'id du chutier."), true),
         p(QStringLiteral("quoi"), QStringLiteral("string"), QStringLiteral("vignette, transcription, silences, niveaux, plans."), true),
         p(QStringLiteral("options"), QStringLiteral("object"), QStringLiteral("vignette : { temps, chemin, largeur } ; transcription : { langue } ; silences : { seuil_db (sinon automatique), duree_min_ms (500), marge_ms, flux } ; plans : { seuil (0.4), ecart_min (images) }"))},
        [porte, ctx](const Call &call, const KoraCutPorte::Done &done) {
            const QString id = call.params.value(QStringLiteral("media_id")).toVariant().toString();
            if (!pCore || !pCore->projectItemModel() || !pCore->projectItemModel()->hasClip(id)) {
                done(Reply::fail(QStringLiteral("media_introuvable"), QStringLiteral("Aucun média n'a l'identifiant %1.").arg(id), QStringLiteral("medias.lister donne les ids.")));
                return;
            }
            auto clip = pCore->projectItemModel()->getClipByBinID(id);
            const QString what = call.params.value(QStringLiteral("quoi")).toString();
            const QJsonObject options = call.params.value(QStringLiteral("options")).toObject();
            if (what == QLatin1String("vignette")) {
                int frame = 0;
                if (options.contains(QStringLiteral("temps"))) {
                    frame = KoraCutAgentTools::parseTime(options.value(QStringLiteral("temps")), fps());
                    if (frame < 0) {
                        done(Reply::fail(QStringLiteral("entree_invalide"), QStringLiteral("temps se donne en images ou en timecode HH:MM:SS:II.")));
                        return;
                    }
                }
                frame = qBound(0, frame, qMax(0, int(clip->frameDuration()) - 1));
                const int width = qBound(64, options.value(QStringLiteral("largeur")).toInt(480), 1920);
                QString path = expandUserPath(ctx, options.value(QStringLiteral("chemin")).toString());
                if (!path.isEmpty() && !QDir::isAbsolutePath(path)) {
                    done(Reply::fail(QStringLiteral("projet_absent"), QStringLiteral("« %1 » est un chemin relatif, et le projet n'est pas encore enregistré.").arg(path), QStringLiteral("Donne un chemin absolu, ou aucun (le cache).")));
                    return;
                }
                if (path.isEmpty()) {
                    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/vignettes");
                    QDir().mkpath(dir);
                    path = QStringLiteral("%1/%2-%3.png").arg(dir, id).arg(frame);
                }
                QImage image;
                const QSize frameSize = clip->getFrameSize();
                const int height = frameSize.width() > 0 ? qMax(1, width * frameSize.height() / frameSize.width()) : width * 9 / 16;
                if (!clip->url().isEmpty() && QFileInfo::exists(clip->url())) {
                    Mlt::Producer producer(pCore->getProjectProfile(), clip->url().toUtf8().constData());
                    if (producer.is_valid()) {
                        image = KThumb::getFrame(producer, frame, width, height);
                    }
                }
                if (image.isNull()) {
                    image = ThumbnailCache::get()->getThumbnail(id, frame);
                }
                if (image.isNull() || !image.save(path, "PNG")) {
                    done(Reply::fail(QStringLiteral("non_disponible"), QStringLiteral("Aucune image n'a pu être tirée de ce média."),
                                     QStringLiteral("Un média sans image (son seul) n'a pas de vignette.")));
                    return;
                }
                done(Reply::value(QJsonObject{{QStringLiteral("chemin"), path},
                                              {QStringLiteral("temps"), timeObject(frame)},
                                              {QStringLiteral("largeur"), image.width()},
                                              {QStringLiteral("hauteur"), image.height()}}));
                return;
            }
            if (what == QLatin1String("transcription")) {
                if (clip->url().isEmpty() || !QFileInfo::exists(clip->url()) || !clip->hasAudio()) {
                    done(Reply::fail(QStringLiteral("non_disponible"), QStringLiteral("Ce média n'a pas de son à transcrire.")));
                    return;
                }
                auto *transcriber = new KoraCutTranscriber(porte);
                const QString file = clip->url();
                QObject::connect(transcriber, &KoraCutTranscriber::finished, porte, [done, transcriber, file](const QList<KoraCutAutoCut::Word> &words) {
                    QJsonArray list;
                    QStringList text;
                    const double f = fps();
                    for (const KoraCutAutoCut::Word &w : words) {
                        list.append(QJsonObject{{QStringLiteral("m"), w.text}, {QStringLiteral("d"), w.start}, {QStringLiteral("f"), w.end}});
                        text << w.text;
                    }
                    Q_UNUSED(f)
                    done(Reply::value(QJsonObject{{QStringLiteral("chemin"), file}, {QStringLiteral("mots"), list}, {QStringLiteral("texte"), text.join(QLatin1Char(' '))}}));
                    transcriber->deleteLater();
                });
                QObject::connect(transcriber, &KoraCutTranscriber::failed, porte, [done, transcriber](const QString &message) {
                    done(Reply::fail(QStringLiteral("non_disponible"), message,
                                     QStringLiteral("La reconnaissance vocale s'installe dans Préférences > Reconnaissance vocale (modèle « base » ou « small »).")));
                    transcriber->deleteLater();
                });
                transcriber->startFile(file, options.value(QStringLiteral("langue")).toString(QStringLiteral("fr")));
                return;
            }
            const QString file = clip->url();
            const bool onDisk = !file.isEmpty() && QFileInfo::exists(file);
            const double rate = fps();
            if (what == QLatin1String("silences")) {
                if (!onDisk || !clip->hasAudio()) {
                    done(Reply::fail(QStringLiteral("non_disponible"), QStringLiteral("Ce média n'a pas de son à analyser.")));
                    return;
                }
                KoraCutAutoCut::SilenceSettings settings = KoraCutAutoCut::SilenceSettings::forRythme(KoraCutAutoCut::Rythme::Normal);
                if (options.contains(QStringLiteral("seuil_db"))) {
                    settings.autoThreshold = false;
                    settings.thresholdDb = options.value(QStringLiteral("seuil_db")).toDouble(-40.);
                }
                if (options.contains(QStringLiteral("duree_min_ms"))) {
                    settings.minSilenceMs = qMax(0, options.value(QStringLiteral("duree_min_ms")).toInt());
                }
                if (options.contains(QStringLiteral("marge_ms"))) {
                    settings.padBeforeMs = settings.padAfterMs = qMax(0, options.value(QStringLiteral("marge_ms")).toInt());
                }
                const QMap<int, QString> streams = clip->audioStreams();
                const int stream = options.contains(QStringLiteral("flux")) ? options.value(QStringLiteral("flux")).toInt() : (streams.isEmpty() ? -1 : streams.firstKey());
                const auto answer = [done, settings, rate, file, stream](const QVector<double> &levels, const QString &source) {
                    if (levels.isEmpty()) {
                        done(Reply::fail(QStringLiteral("non_disponible"), QStringLiteral("Le son de %1 n'a pas pu être lu.").arg(QFileInfo(file).fileName())));
                        return;
                    }
                    double threshold = 0.;
                    const QList<KoraCutDucking::Interval> silences = KoraCutAutoCut::silenceIntervals(levels, settings, rate, &threshold);
                    done(Reply::value(QJsonObject{{QStringLiteral("chemin"), file},
                                                  {QStringLiteral("flux"), stream},
                                                  {QStringLiteral("seuil_db"), threshold},
                                                  {QStringLiteral("duree"), timeObject(levels.size())},
                                                  {QStringLiteral("silences"), intervalsJson(silences)},
                                                  {QStringLiteral("total_silence"), timeObject(KoraCutAutoCut::totalFrames(silences))},
                                                  {QStringLiteral("source"), source}}));
                };
                // Les niveaux des formes d'onde s'ils sont prêts, sinon lus du fichier dans un fil à part.
                const QVector<double> cached = stream >= 0 ? frameLevelsFromCache(clip, stream) : QVector<double>();
                if (!cached.isEmpty()) {
                    answer(cached, QStringLiteral("chutier"));
                    return;
                }
                auto *watcher = new QFutureWatcher<QVector<double>>(porte);
                QObject::connect(watcher, &QFutureWatcher<QVector<double>>::finished, porte, [watcher, answer]() {
                    answer(watcher->result(), QStringLiteral("fichier"));
                    watcher->deleteLater();
                });
                watcher->setFuture(QtConcurrent::run(frameLevelsFromFile, file, rate, stream));
                return;
            }
            if (what == QLatin1String("niveaux")) {
                if (!onDisk || !clip->hasAudio()) {
                    done(Reply::fail(QStringLiteral("non_disponible"), QStringLiteral("Ce média n'a pas de son à mesurer.")));
                    return;
                }
                if (ffmpegPath().isEmpty()) {
                    done(Reply::fail(QStringLiteral("non_disponible"), QStringLiteral("ffmpeg est introuvable."), QStringLiteral("Préférences > Environnement.")));
                    return;
                }
                runFfmpeg(porte, {QStringLiteral("-nostats"), QStringLiteral("-hide_banner"), QStringLiteral("-i"), file, QStringLiteral("-vn"), QStringLiteral("-filter_complex"), QStringLiteral("ebur128=peak=true"), QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")},
                          [done, file](bool ok, const QString &out) {
                              // Le résumé de ebur128, en fin de sortie : I (intégré), LRA, Peak (crête vraie).
                              const auto grab = [&out](const QString &key) -> QJsonValue {
                                  const QRegularExpression re(QStringLiteral("\\n\\s*%1:\\s*(-?[0-9.]+|-inf)").arg(key));
                                  const QRegularExpressionMatch m = re.match(out, qMax(0, int(out.lastIndexOf(QLatin1String("Summary:")))));
                                  if (!m.hasMatch() || m.captured(1) == QLatin1String("-inf")) {
                                      return QJsonValue();
                                  }
                                  return m.captured(1).toDouble();
                              };
                              const QJsonValue integrated = grab(QStringLiteral("I"));
                              if (!ok && integrated.isNull()) {
                                  done(Reply::fail(QStringLiteral("non_disponible"), QStringLiteral("ffmpeg n'a pas pu mesurer %1.").arg(QFileInfo(file).fileName()), out.right(400).trimmed()));
                                  return;
                              }
                              done(Reply::value(QJsonObject{{QStringLiteral("chemin"), file},
                                                            {QStringLiteral("lufs_integre"), integrated},
                                                            {QStringLiteral("plage_lu"), grab(QStringLiteral("LRA"))},
                                                            {QStringLiteral("crete_dbtp"), grab(QStringLiteral("Peak"))},
                                                            {QStringLiteral("seuil_db"), grab(QStringLiteral("Threshold"))}}));
                          });
                return;
            }
            if (what == QLatin1String("plans")) {
                if (!onDisk || !clip->hasVideo() || clip->clipType() == ClipType::Image) {
                    done(Reply::fail(QStringLiteral("non_disponible"), QStringLiteral("Ce média n'a pas d'image animée à analyser.")));
                    return;
                }
                if (ffmpegPath().isEmpty()) {
                    done(Reply::fail(QStringLiteral("non_disponible"), QStringLiteral("ffmpeg est introuvable."), QStringLiteral("Préférences > Environnement.")));
                    return;
                }
                const double threshold = qBound(0.05, options.value(QStringLiteral("seuil")).toDouble(0.4), 1.);
                const int minGap = qMax(0, options.value(QStringLiteral("ecart_min")).toInt(int(rate / 2)));
                runFfmpeg(porte, {QStringLiteral("-nostats"), QStringLiteral("-hide_banner"), QStringLiteral("-i"), file, QStringLiteral("-an"), QStringLiteral("-filter:v"), QStringLiteral("select='gt(scene,%1)',showinfo").arg(threshold), QStringLiteral("-fps_mode"), QStringLiteral("vfr"), QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")},
                          [done, file, rate, threshold, minGap](bool ok, const QString &out) {
                              const QRegularExpression re(QStringLiteral("pts_time:\\s*([0-9.]+)"));
                              QJsonArray cuts;
                              int last = -minGap - 1;
                              auto it = re.globalMatch(out);
                              while (it.hasNext()) {
                                  const int frame = int(std::lround(it.next().captured(1).toDouble() * rate));
                                  if (frame - last < minGap) {
                                      continue;
                                  }
                                  last = frame;
                                  cuts.append(timeObject(frame));
                              }
                              if (!ok && cuts.isEmpty()) {
                                  done(Reply::fail(QStringLiteral("non_disponible"), QStringLiteral("ffmpeg n'a pas pu analyser %1.").arg(QFileInfo(file).fileName()), out.right(400).trimmed()));
                                  return;
                              }
                              done(Reply::value(QJsonObject{{QStringLiteral("chemin"), file}, {QStringLiteral("seuil"), threshold}, {QStringLiteral("plans"), cuts}, {QStringLiteral("nombre"), cuts.size()}}));
                          });
                return;
            }
            done(Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("quoi = vignette, transcription, silences, niveaux ou plans.")));
        });

    // ---- fichiers --------------------------------------------------------------------------
    porte->registerMethod(
        QStringLiteral("fichiers.lister"), QStringLiteral("Les fichiers et dossiers du projet (jamais en dehors), avec l'id du média quand il est dans le chutier."),
        {p(QStringLiteral("dossier"), QStringLiteral("string"), QStringLiteral("Relatif au dossier du projet ; défaut : le projet lui-même.")),
         p(QStringLiteral("recursif"), QStringLiteral("boolean"), QStringLiteral("Descend dans les sous-dossiers (2000 entrées au plus)."))},
        [ctx](const Call &call) {
            QString abs;
            Reply err;
            if (!resolveInProject(ctx, call.params.value(QStringLiteral("dossier")).toString(), abs, err)) {
                return err;
            }
            if (!QFileInfo(abs).isDir()) {
                return Reply::fail(QStringLiteral("fichier_introuvable"), QStringLiteral("%1 n'est pas un dossier.").arg(relativeToProject(ctx, abs)));
            }
            const bool recursive = call.params.value(QStringLiteral("recursif")).toBool(false);
            const QHash<QString, QString> media = mediaByPath();
            QJsonArray list;
            QList<QString> queue{abs};
            while (!queue.isEmpty() && list.size() < 2000) {
                const QString dir = queue.takeFirst();
                const QFileInfoList entries = QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::NoSymLinks | QDir::Hidden, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
                for (const QFileInfo &info : entries) {
                    if (info.fileName().startsWith(QLatin1Char('.'))) {
                        continue;
                    }
                    list.append(fileEntry(ctx, info, media));
                    if (recursive && info.isDir()) {
                        queue << info.absoluteFilePath();
                    }
                }
            }
            return Reply::value(QJsonObject{{QStringLiteral("dossier"), relativeToProject(ctx, abs)}, {QStringLiteral("projet"), projectFolderOf(ctx)}, {QStringLiteral("entrees"), list}});
        });
    // Renommer et déplacer partagent la même commande annulable.
    const auto moveOnDisk = [ctx](const Call &call, const QString &fromGiven, const QString &toAbs, const QString &undoText) -> Reply {
        QString from;
        Reply err;
        if (!resolveInProject(ctx, fromGiven, from, err)) {
            return err;
        }
        if (!QFileInfo::exists(from)) {
            return Reply::fail(QStringLiteral("fichier_introuvable"), QStringLiteral("%1 n'existe pas.").arg(relativeToProject(ctx, from)));
        }
        if (!KoraCutPorteMethodes::isInsideFolder(projectFolderOf(ctx), toAbs)) {
            return Reply::fail(QStringLiteral("hors_projet"), QStringLiteral("La destination sort du dossier du projet."));
        }
        if (from == projectFolderOf(ctx) || QFileInfo(from).canonicalFilePath() == QFileInfo(projectFolderOf(ctx)).canonicalFilePath()) {
            return Reply::fail(QStringLiteral("hors_projet"), QStringLiteral("Le dossier du projet lui-même ne se déplace pas."));
        }
        if (QFileInfo::exists(toAbs)) {
            return Reply::fail(QStringLiteral("fichier_existe"), QStringLiteral("%1 existe déjà.").arg(relativeToProject(ctx, toAbs)));
        }
        if (pCore && pCore->currentDoc() && !pCore->currentDoc()->url().isEmpty() && QFileInfo(from).canonicalFilePath() == QFileInfo(pCore->currentDoc()->url().toLocalFile()).canonicalFilePath()) {
            return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Le fichier du projet ouvert ne se renomme pas par ici."), QStringLiteral("projet.enregistrer avec un autre chemin."));
        }
        const bool folder = QFileInfo(from).isDir();
        if (toAbs.startsWith(from + QLatin1Char('/'))) {
            return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Un dossier ne se déplace pas dans lui-même."));
        }
        if (!QDir().mkpath(QFileInfo(toAbs).absolutePath()) || !renameOnDisk(from, toAbs, folder)) {
            return Reply::fail(QStringLiteral("ecriture_impossible"), QStringLiteral("Le système a refusé de déplacer %1.").arg(relativeToProject(ctx, from)));
        }
        retargetClips(from, toAbs, folder);
        Fun undo = [from, toAbs, folder]() {
            if (!renameOnDisk(toAbs, from, folder)) {
                return false;
            }
            retargetClips(toAbs, from, folder);
            return true;
        };
        Fun redo = [from, toAbs, folder]() {
            if (!renameOnDisk(from, toAbs, folder)) {
                return false;
            }
            retargetClips(from, toAbs, folder);
            return true;
        };
        if (pCore) {
            pCore->pushUndo(undo, redo, undoText);
        }
        int medias = 0;
        const QHash<QString, QString> media = mediaByPath();
        for (auto it = media.begin(); it != media.end(); ++it) {
            if (it.key() == toAbs || (folder && it.key().startsWith(toAbs + QLatin1Char('/')))) {
                ++medias;
            }
        }
        return Reply::value(QJsonObject{{QStringLiteral("ok"), true},
                                        {QStringLiteral("avant"), relativeToProject(ctx, from)},
                                        {QStringLiteral("apres"), relativeToProject(ctx, toAbs)},
                                        {QStringLiteral("medias_suivis"), medias}});
    };
    porte->registerMethod(
        QStringLiteral("fichiers.renommer"), QStringLiteral("Renomme un fichier ou un dossier du projet ; les médias du chutier suivent. Une étape d'annulation."),
        {p(QStringLiteral("chemin"), QStringLiteral("string"), QStringLiteral("Relatif au projet."), true),
         p(QStringLiteral("nouveau_nom"), QStringLiteral("string"), QStringLiteral("Le nouveau nom, sans dossier."), true)},
        [ctx, moveOnDisk](const Call &call) {
            const QString newName = call.params.value(QStringLiteral("nouveau_nom")).toString().trimmed();
            if (newName.isEmpty() || newName.contains(QLatin1Char('/')) || newName == QLatin1String(".") || newName == QLatin1String("..")) {
                return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("nouveau_nom est un nom simple, sans dossier."));
            }
            QString from;
            Reply err;
            if (!resolveInProject(ctx, call.params.value(QStringLiteral("chemin")).toString(), from, err)) {
                return err;
            }
            const QString to = QFileInfo(from).absolutePath() + QLatin1Char('/') + newName;
            return moveOnDisk(call, call.params.value(QStringLiteral("chemin")).toString(), to,
                              i18nc("@action undo text, %1 old name, %2 new name", "Rename %1 to %2", QFileInfo(from).fileName(), newName));
        });
    porte->registerMethod(
        QStringLiteral("fichiers.deplacer"), QStringLiteral("Déplace un fichier ou un dossier dans un autre dossier du projet ; les médias suivent. Une étape d'annulation."),
        {p(QStringLiteral("chemin"), QStringLiteral("string"), QStringLiteral("Relatif au projet."), true),
         p(QStringLiteral("dossier"), QStringLiteral("string"), QStringLiteral("Le dossier d'arrivée, relatif au projet (créé s'il manque)."), true)},
        [ctx, moveOnDisk](const Call &call) {
            QString from, dir;
            Reply err;
            if (!resolveInProject(ctx, call.params.value(QStringLiteral("chemin")).toString(), from, err) ||
                !resolveInProject(ctx, call.params.value(QStringLiteral("dossier")).toString(), dir, err)) {
                return err;
            }
            const QString to = dir + QLatin1Char('/') + QFileInfo(from).fileName();
            return moveOnDisk(call, call.params.value(QStringLiteral("chemin")).toString(), to,
                              i18nc("@action undo text, %1 file name, %2 folder", "Move %1 to %2", QFileInfo(from).fileName(), relativeToProject(ctx, dir)));
        });
    porte->registerMethod(QStringLiteral("fichiers.creer_dossier"), QStringLiteral("Crée un dossier dans le projet (Rushs, Montage, Exports…). Une étape d'annulation."),
                          {p(QStringLiteral("chemin"), QStringLiteral("string"), QStringLiteral("Relatif au projet."), true)},
                          [ctx](const Call &call) {
                              QString abs;
                              Reply err;
                              if (!resolveInProject(ctx, call.params.value(QStringLiteral("chemin")).toString(), abs, err)) {
                                  return err;
                              }
                              if (QFileInfo::exists(abs)) {
                                  return Reply::value(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("chemin"), relativeToProject(ctx, abs)}, {QStringLiteral("existait"), true}});
                              }
                              if (!QDir().mkpath(abs)) {
                                  return Reply::fail(QStringLiteral("ecriture_impossible"), QStringLiteral("Le dossier ne se crée pas."));
                              }
                              Fun undo = [abs]() {
                                  QDir d(abs);
                                  return !d.exists() || d.isEmpty() ? QDir().rmdir(abs) || !d.exists() : true;
                              };
                              Fun redo = [abs]() { return QDir().mkpath(abs); };
                              if (pCore) {
                                  pCore->pushUndo(undo, redo, i18nc("@action undo text", "Create folder %1", QFileInfo(abs).fileName()));
                              }
                              return Reply::value(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("chemin"), relativeToProject(ctx, abs)}, {QStringLiteral("existait"), false}});
                          });
    porte->registerMethod(
        QStringLiteral("fichiers.corbeille"), QStringLiteral("Met un fichier ou un dossier du projet à la corbeille du système (à confirmation ; jamais de suppression définitive)."),
        {p(QStringLiteral("chemin"), QStringLiteral("string"), QStringLiteral("Relatif au projet."), true)},
        [ctx, porte](const Call &call) {
            QString abs;
            Reply err;
            if (!resolveInProject(ctx, call.params.value(QStringLiteral("chemin")).toString(), abs, err)) {
                return err;
            }
            if (!QFileInfo::exists(abs)) {
                return Reply::fail(QStringLiteral("fichier_introuvable"), QStringLiteral("%1 n'existe pas.").arg(relativeToProject(ctx, abs)));
            }
            if (QFileInfo(abs).canonicalFilePath() == QFileInfo(projectFolderOf(ctx)).canonicalFilePath()) {
                return Reply::fail(QStringLiteral("hors_projet"), QStringLiteral("Le dossier du projet lui-même ne va pas à la corbeille."));
            }
            const QHash<QString, QString> media = mediaByPath();
            for (auto it = media.begin(); it != media.end(); ++it) {
                if (it.key() == abs || it.key().startsWith(abs + QLatin1Char('/'))) {
                    return Reply::fail(QStringLiteral("media_utilise"), QStringLiteral("%1 est dans le chutier (média %2).").arg(relativeToProject(ctx, abs), it.value()),
                                       QStringLiteral("Retire-le du chutier d'abord (action delete_clip sur la sélection du chutier), ou laisse-le."));
                }
            }
            Reply gate;
            if (porte->needsConfirmation(call, QStringLiteral("mettre « %1 » à la corbeille").arg(relativeToProject(ctx, abs)), false, gate)) {
                return gate;
            }
            QString inTrash;
            if (!QFile::moveToTrash(abs, &inTrash)) {
                return Reply::fail(QStringLiteral("ecriture_impossible"), QStringLiteral("Le système a refusé de mettre %1 à la corbeille.").arg(relativeToProject(ctx, abs)));
            }
            return Reply::value(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("corbeille"), inTrash}, {QStringLiteral("annulable"), false}});
        },
        true);

    // ---- timeline --------------------------------------------------------------------------
    porte->registerMethod(QStringLiteral("timeline.etat"), QStringLiteral("L'état de la séquence (comme l'outil etat_timeline) : pistes, clips, sélection."),
                          {p(QStringLiteral("niveau"), QStringLiteral("string"), QStringLiteral("resume, normal (défaut) ou complet.")),
                           p(QStringLiteral("debut"), QStringLiteral("any"), QStringLiteral("Début de la plage regardée (images ou timecode).")),
                           p(QStringLiteral("fin"), QStringLiteral("any"), QStringLiteral("Fin de la plage."))},
                          [timeline](const Call &call) {
                              auto tl = timeline();
                              if (!tl) {
                                  return noProject();
                              }
                              QJsonObject args = call.params;
                              if (args.contains(QStringLiteral("niveau"))) {
                                  args.insert(QStringLiteral("detail"), args.take(QStringLiteral("niveau")));
                              }
                              QJsonObject state = KoraCutAgentTools::call(tl, QStringLiteral("etat_timeline"), args);
                              if (state.value(QStringLiteral("ok")).toBool() && pCore && pCore->monitorManager()) {
                                  state.insert(QStringLiteral("tete"), timeObject(pCore->getMonitorPosition(Kdenlive::ProjectMonitor)));
                              }
                              return Reply::fromTool(state);
                          });
    porte->registerMethod(QStringLiteral("timeline.outils"), QStringLiteral("Les outils de montage disponibles derrière timeline.outil, avec leur schéma."), {}, [confirmTools](const Call &) {
        const QStringList implemented = KoraCutAgentTools::implementedTools();
        QJsonArray list;
        const QJsonArray defs = toolDefinitions();
        QSet<QString> seen;
        for (const QJsonValue &v : defs) {
            const QJsonObject o = v.toObject();
            const QString name = o.value(QStringLiteral("name")).toString();
            if (!implemented.contains(name)) {
                continue;
            }
            seen.insert(name);
            list.append(QJsonObject{{QStringLiteral("nom"), name},
                                    {QStringLiteral("description"), o.value(QStringLiteral("description")).toString()},
                                    {QStringLiteral("schema"), o.value(QStringLiteral("input_schema")).toObject()},
                                    {QStringLiteral("confirmation"), confirmTools.contains(name)}});
        }
        for (const QString &name : implemented) {
            if (!seen.contains(name)) {
                list.append(QJsonObject{{QStringLiteral("nom"), name}, {QStringLiteral("confirmation"), confirmTools.contains(name)}});
            }
        }
        return Reply::value(list);
    });
    porte->registerAsyncMethod(
        QStringLiteral("timeline.outil"),
        QStringLiteral("Appelle un outil de montage ou d'organisation (couper_clip, ajouter_clip, renommer_media, organiser_rushs, voix_off_generer…) : mêmes noms, mêmes réponses que les agents. "
                       "Montage visible : l'écran montre chaque geste et la réponse part après une courte pause ; visible=false (koracut-ctl --rapide) répond tout de suite."),
        {p(QStringLiteral("nom"), QStringLiteral("string"), QStringLiteral("Le nom de l'outil (timeline.outils)."), true),
         p(QStringLiteral("args"), QStringLiteral("object"), QStringLiteral("Ses paramètres.")),
         p(QStringLiteral("visible"), QStringLiteral("boolean"), QStringLiteral("false : sans mise en scène ni pause (défaut : le réglage Montage visible)."))},
        [timeline, porte, confirmTools](const Call &call, const KoraCutPorte::Done &done) {
            auto tl = timeline();
            if (!tl) {
                done(noProject());
                return;
            }
            const QString name = call.params.value(QStringLiteral("nom")).toString();
            const QJsonObject args = call.params.value(QStringLiteral("args")).toObject();
            const QString invalid = checkToolCall(name, args);
            if (!invalid.isEmpty()) {
                done(Reply::fail(QStringLiteral("parametres_invalides"), invalid, QStringLiteral("timeline.outils donne le schéma de chaque outil.")));
                return;
            }
            if (confirmTools.contains(name) || args.value(QStringLiteral("ecraser")).toBool(false)) {
                Reply gate;
                const bool always = name == QLatin1String("exporter") || name == QLatin1String("exporter_serie") || args.value(QStringLiteral("ecraser")).toBool(false);
                if (porte->needsConfirmation(call, QStringLiteral("%1 %2").arg(name, QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact))), always, gate)) {
                    done(gate);
                    return;
                }
            }
            // Pendant la commande, l'application ne pose pas la question « Pistes audio manquantes » :
            // les pistes qui manquent au son du clip sont créées (le choix par défaut), et on le dit.
            // Montage visible (25 septembre 2026) : sans pause, le geste se fait ici même ; avec une
            // pause, il se fait un peu plus tard, dans le tour du client (runAsClient).
            const bool immediate = !call.params.value(QStringLiteral("visible")).toBool(true) || KoraCutVisible::self()->pauseMs() == 0;
            const auto gesture = [timeline, porte, call, name, args, immediate]() -> QJsonObject {
                QJsonObject result;
                const auto act = [&result, timeline, name, args]() {
                    auto now = timeline();
                    if (!now) {
                        result = QJsonObject{{QStringLiteral("ok"), false},
                                             {QStringLiteral("erreur"), QJsonObject{{QStringLiteral("code"), QStringLiteral("projet_absent")}, {QStringLiteral("message"), QStringLiteral("La séquence s'est fermée.")}}}};
                        return;
                    }
                    const int tracksBefore = now->getTracksCount();
                    result = KoraCutAgentTools::call(now, name, args);
                    if (result.value(QStringLiteral("ok")).toBool() && now->getTracksCount() > tracksBefore && name == QLatin1String("ajouter_clip")) {
                        result.insert(QStringLiteral("pistes_creees"), now->getTracksCount() - tracksBefore);
                    }
                };
                if (immediate) {
                    act();
                } else {
                    porte->runAsClient(call.client, act);
                }
                if (result.value(QStringLiteral("ok")).toBool()) {
                    porte->publish(QStringLiteral("timeline"), QJsonObject{{QStringLiteral("outil"), name}, {QStringLiteral("client"), call.client}}, call.clientId);
                }
                return result;
            };
            KoraCutVisible::self()->run(call.client, name, args, porte, gesture, [done](const QJsonObject &result) { done(Reply::fromTool(result)); },
                                        call.params.value(QStringLiteral("visible")).toBool(true));
        },
        true);
    porte->registerMethod(QStringLiteral("timeline.selection"), QStringLiteral("Lit ou pose la sélection de la timeline."),
                          {p(QStringLiteral("clip_ids"), QStringLiteral("array"), QStringLiteral("Les ids à sélectionner ; [] vide la sélection ; absent : lecture."))},
                          [timeline](const Call &call) {
                              auto tl = timeline();
                              if (!tl) {
                                  return noProject();
                              }
                              if (call.params.contains(QStringLiteral("clip_ids"))) {
                                  std::unordered_set<int> ids;
                                  for (const QJsonValue &v : call.params.value(QStringLiteral("clip_ids")).toArray()) {
                                      if (!tl->isItem(v.toInt())) {
                                          return Reply::fail(QStringLiteral("id_inconnu"), QStringLiteral("Aucun clip n'a l'identifiant %1.").arg(v.toInt()), QStringLiteral("Relis timeline.etat."));
                                      }
                                      ids.insert(v.toInt());
                                  }
                                  tl->requestSetSelection(ids);
                              }
                              QJsonArray sel;
                              for (int id : tl->getCurrentSelection()) {
                                  sel.append(id);
                              }
                              return Reply::value(QJsonObject{{QStringLiteral("clip_ids"), sel}});
                          });
    porte->registerMethod(QStringLiteral("timeline.tete"), QStringLiteral("Lit ou déplace la tête de lecture."),
                          {p(QStringLiteral("position"), QStringLiteral("any"), QStringLiteral("Images ou timecode ; absent : lecture."))},
                          [window, timeline](const Call &call) {
                              if (!window || !pCore->monitorManager()) {
                                  return noWindow();
                              }
                              if (call.params.contains(QStringLiteral("position"))) {
                                  const int pos = KoraCutAgentTools::parseTime(call.params.value(QStringLiteral("position")), fps());
                                  if (pos < 0) {
                                      return Reply::fail(QStringLiteral("entree_invalide"), QStringLiteral("position se donne en images ou en timecode HH:MM:SS:II."));
                                  }
                                  pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);
                                  if (window->getCurrentTimeline()) {
                                      window->getCurrentTimeline()->controller()->setPosition(pos);
                                  }
                              }
                              auto tl = timeline();
                              return Reply::value(QJsonObject{{QStringLiteral("position"), timeObject(pCore->getMonitorPosition(Kdenlive::ProjectMonitor))},
                                                              {QStringLiteral("sequence"), tl ? tl->uuid().toString(QUuid::WithoutBraces) : QString()}});
                          });
    porte->registerMethod(QStringLiteral("timeline.lecture"), QStringLiteral("Lecture du moniteur Programme : lire, pause, stop, image_suivante, image_precedente."),
                          {p(QStringLiteral("faire"), QStringLiteral("string"), QStringLiteral("lire, pause, stop, image_suivante, image_precedente ; absent : lecture de l'état."))},
                          [window](const Call &call) {
                              if (!window || !pCore->monitorManager() || !pCore->monitorManager()->projectMonitor()) {
                                  return noWindow();
                              }
                              Monitor *monitor = pCore->monitorManager()->projectMonitor();
                              const QString verb = call.params.value(QStringLiteral("faire")).toString();
                              if (!verb.isEmpty()) {
                                  pCore->monitorManager()->activateMonitor(Kdenlive::ProjectMonitor);
                              }
                              if (verb == QLatin1String("lire")) {
                                  if (!monitor->isPlaying()) {
                                      monitor->slotPlay();
                                  }
                              } else if (verb == QLatin1String("pause")) {
                                  monitor->pause();
                              } else if (verb == QLatin1String("stop")) {
                                  monitor->stop();
                              } else if (verb == QLatin1String("image_suivante")) {
                                  pCore->monitorManager()->slotForwardOneFrame();
                              } else if (verb == QLatin1String("image_precedente")) {
                                  pCore->monitorManager()->slotRewindOneFrame();
                              } else if (!verb.isEmpty()) {
                                  return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("faire = lire, pause, stop, image_suivante ou image_precedente."));
                              }
                              return Reply::value(QJsonObject{{QStringLiteral("lecture"), monitor->isPlaying()}, {QStringLiteral("position"), timeObject(pCore->getMonitorPosition(Kdenlive::ProjectMonitor))}});
                          });
    porte->registerMethod(QStringLiteral("timeline.zoom"), QStringLiteral("Le zoom de la timeline : niveau 0 (le plus près) à 13, ou ajuster à la fenêtre."),
                          {p(QStringLiteral("niveau"), QStringLiteral("number"), QStringLiteral("0 à 13.")), p(QStringLiteral("ajuster"), QStringLiteral("boolean"), QStringLiteral("true : toute la séquence visible."))},
                          [window](const Call &call) {
                              if (!window) {
                                  return noWindow();
                              }
                              if (call.params.value(QStringLiteral("ajuster")).toBool(false)) {
                                  window->koraCutSetZoom(-1, true);
                              } else if (call.params.contains(QStringLiteral("niveau"))) {
                                  window->koraCutSetZoom(qBound(0, call.params.value(QStringLiteral("niveau")).toInt(), 13), false);
                              }
                              return Reply::value(QJsonObject{{QStringLiteral("niveau"), window->koraCutZoomLevel()}});
                          });

    // ---- timeline.vues : plusieurs séquences visibles côte à côte (document 13, § 5) -----------
    const auto viewsReply = [](TimelineTabs *tabs) {
        return QJsonObject{{QStringLiteral("vues"), tabs->views()},
                           {QStringLiteral("decoupe"), tabs->layoutDescription()},
                           {QStringLiteral("note"), QStringLiteral("Une seule séquence est active (elle pilote le moniteur, les raccourcis et les outils) ; les autres restent affichées, figées.")}};
    };
    porte->registerMethod(QStringLiteral("timeline.vues"),
                          QStringLiteral("Les séquences ouvertes et leur place : vue (principale, vue-1…), onglet, affichée, active, place en pixels ; decoupe décrit les vues (H = côte à côte, V = l'une sous l'autre)."),
                          {}, [window, viewsReply](const Call &) {
                              auto *tabs = window ? window->findChild<TimelineTabs *>() : nullptr;
                              if (!tabs) {
                                  return noWindow();
                              }
                              return Reply::value(viewsReply(tabs));
                          });
    porte->registerMethod(
        QStringLiteral("timeline.vue"),
        QStringLiteral("Place une séquence : poser (à gauche, à droite, en haut ou en bas d'une autre, ou de toute la zone), onglet (la remet dans les onglets principaux), activer (elle pilote le moniteur). Une séquence fermée est d'abord ouverte. Rien d'annulable : c'est l'affichage, pas le montage."),
        {p(QStringLiteral("faire"), QStringLiteral("string"), QStringLiteral("poser, onglet, activer, orienter."), true),
         p(QStringLiteral("orientation"), QStringLiteral("string"), QStringLiteral("orienter : horizontale (1920 × 1080) ou verticale (1080 × 1920) ; active, le profil se retourne et l'espace se range.")),
         p(QStringLiteral("sequence"), QStringLiteral("string"), QStringLiteral("L'uuid ou le nom de la séquence à placer."), true),
         p(QStringLiteral("cote"), QStringLiteral("string"), QStringLiteral("poser : gauche, droite, haut, bas.")),
         p(QStringLiteral("a_cote_de"), QStringLiteral("string"), QStringLiteral("poser : l'uuid ou le nom de la séquence voisine ; absent : à côté de toute la zone."))},
        [window, viewsReply](const Call &call) {
            auto *tabs = window ? window->findChild<TimelineTabs *>() : nullptr;
            if (!tabs || !pCore || !pCore->currentDoc()) {
                return noWindow();
            }
            const QString verb = call.params.value(QStringLiteral("faire")).toString();
            const auto opened = [](const QString &wanted, QUuid *uuid) -> bool {
                auto clip = findSequence(wanted, uuid);
                if (!clip) {
                    return false;
                }
                // Une séquence qui n'est pas ouverte s'ouvre d'abord en onglet.
                if (!pCore->window()->getTimeline(*uuid)) {
                    pCore->projectManager()->openTimeline(clip->clipId(), -1, *uuid);
                }
                return pCore->window()->getTimeline(*uuid) != nullptr;
            };
            QUuid uuid;
            if (!opened(call.params.value(QStringLiteral("sequence")).toString(), &uuid)) {
                return Reply::fail(QStringLiteral("sequence_introuvable"), QStringLiteral("Aucune séquence ne correspond à « %1 ».").arg(call.params.value(QStringLiteral("sequence")).toString()),
                                   QStringLiteral("projet.sequences donne les uuid et les noms."));
            }
            QString error;
            bool done = false;
            if (verb == QLatin1String("poser")) {
                bool ok = false;
                const KoraCutSplit::Side side = KoraCutSplit::sideFromName(call.params.value(QStringLiteral("cote")).toString(), &ok);
                if (!ok || side == KoraCutSplit::Side::Center) {
                    return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("cote = gauche, droite, haut ou bas."));
                }
                QUuid anchor;
                const QString neighbour = call.params.value(QStringLiteral("a_cote_de")).toString();
                if (!neighbour.isEmpty() && !opened(neighbour, &anchor)) {
                    return Reply::fail(QStringLiteral("sequence_introuvable"), QStringLiteral("Aucune séquence ne correspond à « %1 ».").arg(neighbour),
                                       QStringLiteral("projet.sequences donne les uuid et les noms."));
                }
                done = tabs->placeSequence(uuid, anchor, side, &error);
            } else if (verb == QLatin1String("onglet")) {
                done = tabs->dockBackSequence(uuid);
            } else if (verb == QLatin1String("activer")) {
                done = tabs->activateSequence(uuid);
            } else if (verb == QLatin1String("orienter")) {
                const QString o = call.params.value(QStringLiteral("orientation")).toString();
                if (!o.startsWith(QLatin1Char('h')) && !o.startsWith(QLatin1Char('v'))) {
                    return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("orientation = horizontale ou verticale."));
                }
                tabs->setSequenceOrientation(uuid, o.startsWith(QLatin1Char('v')));
                done = true;
            } else {
                return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("faire = poser, onglet, activer ou orienter."));
            }
            if (!done) {
                const QString why = error == QLatin1String("seule_dans_sa_vue")    ? QStringLiteral("La séquence est seule dans sa vue : il n'y a rien à mettre à côté.")
                                    : error == QLatin1String("une_seule_sequence") ? QStringLiteral("Il faut au moins deux séquences ouvertes.")
                                    : error == QLatin1String("meme_sequence")      ? QStringLiteral("Une séquence ne se pose pas à côté d'elle-même.")
                                                                                   : QStringLiteral("Ce placement n'est pas possible (%1).").arg(error);
                return Reply::fail(error.isEmpty() ? QStringLiteral("non_disponible") : error, why, QStringLiteral("timeline.vues donne la place de chaque séquence."));
            }
            return Reply::value(viewsReply(tabs));
        });

    // ---- export ----------------------------------------------------------------------------
    porte->registerMethod(QStringLiteral("export.prereglages"), QStringLiteral("Les préréglages d'export : nom, groupe, extension, note."), {}, [](const Call &) {
        QJsonArray list;
        const QVector<QString> names = RenderPresetRepository::get()->getAllPresets();
        for (const QString &name : names) {
            if (!RenderPresetRepository::get()->presetExists(name)) {
                continue;
            }
            const auto &preset = RenderPresetRepository::get()->getPreset(name);
            list.append(QJsonObject{{QStringLiteral("nom"), name},
                                    {QStringLiteral("groupe"), preset->groupId()},
                                    {QStringLiteral("extension"), preset->extension()},
                                    {QStringLiteral("description"), preset->note()}});
        }
        return Reply::value(list);
    });
    porte->registerMethod(
        QStringLiteral("export.lancer"), QStringLiteral("Lance un rendu avec un préréglage vers un fichier, avec le niveau sonore, avec ou sans les sous-titres. À confirmation si le fichier existe (ecraser)."),
        {p(QStringLiteral("prereglage"), QStringLiteral("string"), QStringLiteral("Nom du préréglage (export.prereglages)."), true),
         p(QStringLiteral("chemin"), QStringLiteral("string"), QStringLiteral("Le fichier de sortie."), true),
         p(QStringLiteral("niveau_lufs"), QStringLiteral("number"),
           QStringLiteral("Niveau sonore cible, de -30 à -8 (-14 pour le web). Absent : le son du mixage tel quel, sans nivellement : le fichier contient ce que "
                          "mesurer_audio annonce (cible mixage). La cible du type de vidéo du projet n'est pas appliquée d'office : la réponse la rappelle.")),
         p(QStringLiteral("ecraser"), QStringLiteral("boolean"), QStringLiteral("true : écrase le fichier s'il existe (à confirmation).")),
         p(QStringLiteral("zone"), QStringLiteral("any"), QStringLiteral("projet (défaut, toute la séquence), zone (la zone du moniteur), ou { in, out } en images ou timecode, out exclu.")),
         p(QStringLiteral("sous_titres"), QStringLiteral("string"), QStringLiteral("incruster (défaut) ou sans : une version sans sous-titres, sans toucher à la piste."))},
        [porte, window, ctx](const Call &call) {
            if (!window) {
                return noWindow();
            }
            if (!pCore->currentDoc()) {
                return noProject();
            }
            const QString preset = call.params.value(QStringLiteral("prereglage")).toString();
            if (!RenderPresetRepository::get()->presetExists(preset)) {
                return Reply::fail(QStringLiteral("prereglage_inconnu"), QStringLiteral("Aucun préréglage « %1 ».").arg(preset), QStringLiteral("export.prereglages liste les noms."));
            }
            const QString path = expandUserPath(ctx, call.params.value(QStringLiteral("chemin")).toString());
            if (path.isEmpty()) {
                return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("chemin manque."));
            }
            Reply relative;
            if (relativeWithoutProject(path, relative)) {
                return relative;
            }
            // La zone rendue : tout, la zone du moniteur, ou des bornes données.
            int boundIn = -1;
            int boundOut = -1;
            QString zoneKind = QStringLiteral("projet");
            const QJsonValue zone = call.params.value(QStringLiteral("zone"));
            if (zone.isObject()) {
                const QJsonObject z = zone.toObject();
                boundIn = z.contains(QStringLiteral("in")) ? KoraCutAgentTools::parseTime(z.value(QStringLiteral("in")), fps()) : 0;
                boundOut = KoraCutAgentTools::parseTime(z.value(QStringLiteral("out")), fps());
                if (boundIn < 0 || boundOut <= boundIn) {
                    return Reply::fail(QStringLiteral("entree_invalide"), QStringLiteral("zone = { in, out } avec out strictement après in (images ou timecode)."));
                }
                zoneKind = QStringLiteral("bornes");
            } else if (zone.isString() && zone.toString() == QLatin1String("zone")) {
                Monitor *monitor = pCore->getMonitor(Kdenlive::ProjectMonitor);
                if (!monitor) {
                    return noWindow();
                }
                boundIn = monitor->getZoneStart();
                boundOut = monitor->getZoneEnd();
                if (boundOut <= boundIn) {
                    return Reply::fail(QStringLiteral("entree_invalide"), QStringLiteral("La zone du moniteur est vide."), QStringLiteral("Donne zone = { in, out }."));
                }
                zoneKind = QStringLiteral("zone");
            } else if (!zone.isUndefined() && !zone.isNull() && !(zone.isString() && zone.toString() == QLatin1String("projet"))) {
                return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("zone = projet, zone ou { in, out }."));
            }
            const QString subtitles = call.params.value(QStringLiteral("sous_titres")).toString(QStringLiteral("incruster"));
            if (subtitles != QLatin1String("incruster") && subtitles != QLatin1String("sans")) {
                return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("sous_titres = incruster ou sans."));
            }
            const bool levelling = call.params.contains(QStringLiteral("niveau_lufs")) && !call.params.value(QStringLiteral("niveau_lufs")).isNull();
            const int lufs = call.params.value(QStringLiteral("niveau_lufs")).toInt(0);
            if (levelling && (!call.params.value(QStringLiteral("niveau_lufs")).isDouble() || lufs < -30 || lufs > -8)) {
                return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("niveau_lufs est un nombre entier de -30 à -8 (-14 pour le web, -16 pour un podcast, -23 pour la télévision)."),
                                   QStringLiteral("Sans niveau_lufs, le son est rendu tel quel."));
            }
            if (QFileInfo::exists(path)) {
                if (!call.params.value(QStringLiteral("ecraser")).toBool(false)) {
                    return Reply::fail(QStringLiteral("fichier_existe"), QStringLiteral("%1 existe déjà.").arg(path), QStringLiteral("Donne un autre chemin, ou ecraser=true (à confirmation)."));
                }
                Reply gate;
                if (porte->needsConfirmation(call, QStringLiteral("écraser %1 par le rendu").arg(path), true, gate)) {
                    return gate;
                }
            }
            RenderWidget *rw = window->koraCutRenderWidget(true);
            if (!rw) {
                return Reply::fail(QStringLiteral("non_disponible"), QStringLiteral("La fenêtre d'export ne s'ouvre pas."));
            }
            rw->focusItem(preset);
            // Le niveau sonore : celui demandé, sinon aucun. Avant le 1er octobre 2026, sans niveau_lufs, la
            // case gardait l'état du projet : un projet de type publicité (cible du guide : -23 LUFS) sortait
            // à -23,0 LUFS alors que mesurer_audio annonçait -14 pour le mixage. La case de la fenêtre suit.
            for (QCheckBox *c : rw->findChildren<QCheckBox *>()) {
                if (c->text() == i18nc("@option:check", "Level the sound at")) {
                    c->setChecked(levelling);
                }
            }
            if (levelling) {
                for (QSpinBox *sp : rw->findChildren<QSpinBox *>()) {
                    if (sp->suffix().contains(QLatin1String("LUFS"))) {
                        sp->setValue(lufs);
                    }
                }
            }
            QDir().mkpath(QFileInfo(path).absolutePath());
            rw->koraCutSetBounds(boundIn, boundOut >= 0 ? boundOut - 1 : -1);
            rw->koraCutSetSubtitles(subtitles == QLatin1String("incruster"));
            rw->startRenderTo(path);
            porte->publish(QStringLiteral("taches"), QJsonObject{{QStringLiteral("quoi"), QStringLiteral("rendu_lance")}, {QStringLiteral("chemin"), path}});
            QJsonObject o{{QStringLiteral("tache_id"), path}, {QStringLiteral("prereglage"), rw->currentPresetName()}, {QStringLiteral("zone"), zoneKind}, {QStringLiteral("sous_titres"), subtitles}};
            if (boundIn >= 0) {
                o.insert(QStringLiteral("in"), timeObject(boundIn));
                o.insert(QStringLiteral("out"), timeObject(boundOut));
            }
            // Ce qui est fait du son, dit en clair : nivelé à la cible, ou rendu tel quel.
            o.insert(QStringLiteral("niveau_lufs"), levelling ? QJsonValue(lufs) : QJsonValue());
            if (!levelling) {
                const int typeTarget = pCore->currentDoc()->getDocumentProperty(QStringLiteral("koracut:export_lufs")).toInt();
                QString note = QStringLiteral("Son rendu tel quel, sans nivellement : le fichier contient ce que mesurer_audio annonce pour le mixage.");
                if (typeTarget != 0) {
                    o.insert(QStringLiteral("cible_du_type"), typeTarget);
                    note += QStringLiteral(" Le type de vidéo du projet vise %1 LUFS : niveau_lufs=%1 pour niveler à cette cible.").arg(typeTarget);
                }
                o.insert(QStringLiteral("note_son"), note);
            }
            o.insert(QStringLiteral("conseil"), QStringLiteral("export.etat tache_id=<tache_id> suit l'avancement ; l'événement taches prévient."));
            return Reply::value(o);
        },
        true);
    porte->registerMethod(QStringLiteral("export.etat"), QStringLiteral("Les rendus en cours et finis : avancement, état, erreur. Avec tache_id, ce rendu seul."),
                          {p(QStringLiteral("tache_id"), QStringLiteral("string"), QStringLiteral("Le tache_id rendu par export.lancer (le chemin de sortie)."))},
                          [window, ctx](const Call &call) {
                              if (!window) {
                                  return noWindow();
                              }
                              RenderWidget *rw = window->koraCutRenderWidget(false);
                              QJsonArray jobs = rw ? rw->koraCutJobs() : QJsonArray();
                              // Document 23 : les rendus du module Export et des outils d'agent (même forme de tache_id).
                              for (const auto &j : KoraCutExportQueue::self().jobs()) {
                                  QJsonObject o = KoraCutExportQueue::self().json(j);
                                  o.insert(QStringLiteral("avancement"), j->progress);
                                  jobs.append(o);
                              }
                              const QString wanted = expandUserPath(ctx, call.params.value(QStringLiteral("tache_id")).toString());
                              if (wanted.isEmpty()) {
                                  return Reply::value(jobs);
                              }
                              for (const QJsonValue &j : jobs) {
                                  if (j.toObject().value(QStringLiteral("tache_id")).toString() == wanted) {
                                      return Reply::value(j);
                                  }
                              }
                              return Reply::fail(QStringLiteral("tache_inconnue"), QStringLiteral("Aucun rendu vers %1.").arg(wanted), QStringLiteral("export.etat sans paramètre liste les rendus."));
                          });
    porte->registerMethod(QStringLiteral("export.annuler"), QStringLiteral("Arrête un rendu."),
                          {p(QStringLiteral("tache_id"), QStringLiteral("string"), QStringLiteral("Le chemin de sortie (export.etat)."), true)},
                          [window, ctx](const Call &call) {
                              if (!window) {
                                  return noWindow();
                              }
                              RenderWidget *rw = window->koraCutRenderWidget(false);
                              const QString dest = expandUserPath(ctx, call.params.value(QStringLiteral("tache_id")).toString());
                              for (const auto &j : KoraCutExportQueue::self().jobs()) {
                                  if ((j->path == dest || j->id == dest) && KoraCutExportQueue::self().abort(j)) {
                                      return Reply::value(QJsonObject{{QStringLiteral("ok"), true}});
                                  }
                              }
                              if (!rw || !rw->koraCutAbortJob(dest)) {
                                  return Reply::fail(QStringLiteral("tache_inconnue"), QStringLiteral("Aucun rendu vers %1.").arg(dest), QStringLiteral("export.etat liste les rendus."));
                              }
                              return Reply::value(QJsonObject{{QStringLiteral("ok"), true}});
                          });

    // Document 23 : les modèles du module Export (YouTube, TikTok, Reels, Master…), comme les tuiles de la page.
    porte->registerMethod(QStringLiteral("export.modeles"),
                          QStringLiteral("Les modèles d'export (plateformes et fichiers techniques), le modèle recommandé pour le type du projet et les réglages de la séquence."),
                          {}, [timeline](const Call &) {
                              auto tl = timeline();
                              if (!tl) {
                                  return noProject();
                              }
                              const QJsonObject r = KoraCutAgentTools::call(tl, QStringLiteral("modeles_export"), {{QStringLiteral("action"), QStringLiteral("lister")}});
                              return r.value(QStringLiteral("ok")).toBool() ? Reply::value(r)
                                                                            : Reply::fail(r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString(),
                                                                                          r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("message")).toString());
                          });
    porte->registerMethod(
        QStringLiteral("export.modele"),
        QStringLiteral("Choisit un modèle d'export et, au choix, l'ajoute à la file (retenu), l'exporte tout de suite, ou lance toute la file. Jamais par-dessus un fichier (v2, v3)."),
        {p(QStringLiteral("modele"), QStringLiteral("string"), QStringLiteral("Identifiant (export.modeles) : tiktok, youtube_1080p, youtube_shorts, master_h264…")),
         p(QStringLiteral("action"), QStringLiteral("string"), QStringLiteral("appliquer (défaut), ajouter, exporter, lancer_file.")),
         p(QStringLiteral("reglages"), QStringLiteral("object"), QStringLiteral("Réglages changés : debit_kbps, lufs, normaliser, sous_titres, dossier, nom, entree_sortie…")),
         p(QStringLiteral("chemin"), QStringLiteral("string"), QStringLiteral("Le fichier de sortie ; absent : Exports/ à côté du projet et le nom des réglages."))},
        [timeline, porte, ctx](const Call &call) {
            auto tl = timeline();
            if (!tl) {
                return noProject();
            }
            QJsonObject args{{QStringLiteral("action"), call.params.value(QStringLiteral("action")).toString(QStringLiteral("appliquer"))}};
            for (const QString &key : {QStringLiteral("modele"), QStringLiteral("reglages")}) {
                if (call.params.contains(key)) {
                    args.insert(key, call.params.value(key));
                }
            }
            if (call.params.contains(QStringLiteral("chemin"))) {
                args.insert(QStringLiteral("chemin"), expandUserPath(ctx, call.params.value(QStringLiteral("chemin")).toString()));
            }
            const QJsonObject r = KoraCutAgentTools::call(tl, QStringLiteral("modeles_export"), args);
            if (!r.value(QStringLiteral("ok")).toBool()) {
                const QJsonObject e = r.value(QStringLiteral("erreur")).toObject();
                return Reply::fail(e.value(QStringLiteral("code")).toString(), e.value(QStringLiteral("message")).toString(), e.value(QStringLiteral("conseil")).toString());
            }
            if (r.contains(QStringLiteral("tache_id"))) {
                porte->publish(QStringLiteral("taches"), QJsonObject{{QStringLiteral("quoi"), QStringLiteral("rendu_lance")}, {QStringLiteral("chemin"), r.value(QStringLiteral("tache_id"))}});
            }
            return Reply::value(r);
        },
        true);

    // ---- historique ------------------------------------------------------------------------
    porte->registerMethod(QStringLiteral("historique.lister"), QStringLiteral("Les dernières étapes d'annulation."),
                          {p(QStringLiteral("n"), QStringLiteral("number"), QStringLiteral("Combien, défaut 20."))},
                          [undoStack](const Call &call) {
                              auto stack = undoStack();
                              if (!stack) {
                                  return noProject();
                              }
                              const int n = qMax(1, call.params.value(QStringLiteral("n")).toInt(20));
                              QJsonArray list;
                              for (int i = qMax(0, stack->count() - n); i < stack->count(); ++i) {
                                  list.append(QJsonObject{{QStringLiteral("index"), i}, {QStringLiteral("texte"), stack->text(i)}, {QStringLiteral("fait"), i < stack->index()}});
                              }
                              return Reply::value(QJsonObject{{QStringLiteral("etapes"), list}, {QStringLiteral("index"), stack->index()}, {QStringLiteral("total"), stack->count()}});
                          });
    const auto undoRedo = [undoStack](bool redo) {
        return [undoStack, redo](const Call &call) {
            auto stack = undoStack();
            if (!stack) {
                return noProject();
            }
            const int n = qMax(1, call.params.value(QStringLiteral("n")).toInt(1));
            QStringList texts;
            for (int i = 0; i < n; ++i) {
                if (redo ? !stack->canRedo() : !stack->canUndo()) {
                    break;
                }
                texts << (redo ? stack->redoText() : stack->undoText());
                if (redo) {
                    stack->redo();
                } else {
                    stack->undo();
                }
            }
            if (texts.isEmpty()) {
                return Reply::fail(QStringLiteral("rien_a_annuler"), redo ? QStringLiteral("Rien à rétablir.") : QStringLiteral("Rien à annuler."));
            }
            return Reply::value(QJsonObject{{QStringLiteral("index"), stack->index()}, {QStringLiteral("texte"), texts.join(QStringLiteral(" ; "))}, {QStringLiteral("etapes"), texts.size()}});
        };
    };
    porte->registerMethod(QStringLiteral("historique.annuler"), QStringLiteral("Annule la dernière étape (ou n)."), {p(QStringLiteral("n"), QStringLiteral("number"), QStringLiteral("Défaut 1."))}, undoRedo(false));
    porte->registerMethod(QStringLiteral("historique.refaire"), QStringLiteral("Rétablit la dernière étape annulée (ou n)."), {p(QStringLiteral("n"), QStringLiteral("number"), QStringLiteral("Défaut 1."))}, undoRedo(true));
    porte->registerMethod(
        QStringLiteral("historique.tour"), QStringLiteral("Entre debut et fin, tout ce que ce client fait tient dans une seule étape « <client> : <titre> » ; abandon défait tout."),
        {p(QStringLiteral("faire"), QStringLiteral("string"), QStringLiteral("debut, fin ou abandon."), true), p(QStringLiteral("titre"), QStringLiteral("string"), QStringLiteral("Le titre de l'étape (avec debut)."))},
        [porte](const Call &call) {
            const QString verb = call.params.value(QStringLiteral("faire")).toString();
            QString error;
            if (verb == QLatin1String("debut")) {
                if (!porte->beginTurn(call.clientId, call.client, call.params.value(QStringLiteral("titre")).toString(), error)) {
                    return Reply::fail(QStringLiteral("tour_ouvert"), error);
                }
                return Reply::value(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("tour"), QStringLiteral("ouvert")}});
            }
            if (verb == QLatin1String("fin") || verb == QLatin1String("abandon")) {
                const int actions = porte->endTurn(call.clientId, error, verb == QLatin1String("abandon"));
                if (actions < 0) {
                    return Reply::fail(QStringLiteral("aucun_tour"), error);
                }
                return Reply::value(QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("tour"), verb == QLatin1String("fin") ? QStringLiteral("ferme") : QStringLiteral("abandonne")}, {QStringLiteral("actions"), actions}});
            }
            return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("faire = debut, fin ou abandon."));
        });

    // ---- evenements ------------------------------------------------------------------------
    porte->registerMethod(QStringLiteral("evenements.abonner"), QStringLiteral("Recevoir des événements sans demander : tete, selection, projet, timeline, taches, fenetre, journal."),
                          {p(QStringLiteral("types"), QStringLiteral("array"), QStringLiteral("Les types voulus."), true)},
                          [porte](const Call &call) {
                              QStringList types;
                              for (const QJsonValue &v : call.params.value(QStringLiteral("types")).toArray()) {
                                  types << v.toString();
                              }
                              QStringList accepted;
                              if (!porte->subscribe(call.clientId, types, accepted)) {
                                  return Reply::fail(QStringLiteral("non_disponible"), QStringLiteral("Les événements ne se reçoivent que par la prise."));
                              }
                              return Reply::value(QJsonObject{{QStringLiteral("abonne"), QJsonArray::fromStringList(accepted)}});
                          });
    porte->registerMethod(QStringLiteral("evenements.desabonner"), QStringLiteral("Ne plus recevoir ces événements."),
                          {p(QStringLiteral("types"), QStringLiteral("array"), QStringLiteral("Les types à retirer."), true)},
                          [porte](const Call &call) {
                              QStringList types;
                              for (const QJsonValue &v : call.params.value(QStringLiteral("types")).toArray()) {
                                  types << v.toString();
                              }
                              porte->unsubscribe(call.clientId, types);
                              return Reply::value(QJsonObject{{QStringLiteral("ok"), true}});
                          });

    // Les sources d'événements de l'application : la tête, les fenêtres modales, la sélection (la
    // fenêtre rappelle watchTimeline à chaque changement de séquence).
    if (window && pCore && pCore->monitorManager() && pCore->monitorManager()->projectMonitor()) {
        QObject::connect(pCore->monitorManager()->projectMonitor(), &Monitor::seekPosition, porte,
                         [porte](int pos) { porte->publish(QStringLiteral("tete"), QJsonObject{{QStringLiteral("position"), timeObject(pos)}}); });
    }
    if (window) {
        porte->watchWindows();
    }
    if (auto tl = timeline()) {
        porte->watchTimeline(tl);
    }
}
