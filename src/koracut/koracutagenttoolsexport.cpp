/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Chantier 3, lot B (24 septembre 2026) : les huit outils d'export. Contrat : export.json et le banc
// (Agents in-app/banc/src/outils/export.ts). Le rendu passe par RenderRequest, comme la fenêtre Export
// (RenderWidget::slotPrepareExport2), mais sans ses boîtes de dialogue : c'est l'outil qui répond
// `fichier_existe`, et `ecraser` est un accord donné avant l'appel. Le processus koracut_render est
// lancé et suivi ici (file d'attente, progression par la même prise locale que le serveur de rendu),
// et la fenêtre Export, quand elle existe, est tenue au courant.

#include "koracutagenttools.h"
#include "koracut/koracuthwdecode.h"
#include "koracutagenttools_p.h"
#include "koracutloudness.h"
#include "export/koracutexportqueue.h"
#include "export/koracutdeliverpage.h"
#include "export/koracutexport.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "dialogs/renderwidget.h"
#include "doc/kdenlivedoc.h"
#include "doc/kthumb.h"
#include "jobs/taskmanager.h"
#include "kdenlivesettings.h"
#include "mainwindow.h"
#include "monitor/monitor.h"
#include "profiles/profilemodel.hpp"
#include "render/renderrequest.h"
#include "renderpresets/renderpresetmodel.hpp"
#include "renderpresets/renderpresetrepository.hpp"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace KoraCutAgentToolsPrivate;

namespace {

// ---------------------------------------------------------------------------------------------
// Les préréglages

// Ce qu'un préréglage nous dit pour vérifier et estimer.
struct Preset
{
    QString id;                // youtube_1080p, personnalise...
    QString name;              // le nom dans la bibliothèque de rendu
    QString extension;
    double ratio = 0.;         // rapport d'image attendu (0 : celui du projet)
    int lufs = 0;              // cible sonore du préréglage KoraCut (0 : aucune)
    bool video = true;
    bool audio = true;
    double videoKbps = 0.;
    double audioKbps = 0.;
};

struct KoraCutPreset
{
    const char *id;
    const char *name;
    double ratio;
    int lufs;
};

// Les cinq préréglages KoraCut, définis dans data/profiles.xml (groupe « KoraCut »).
const std::vector<KoraCutPreset> &koraCutPresets()
{
    static const std::vector<KoraCutPreset> list = {
        {"master_4k_prores", "KoraCut Master ProRes", 0., 0},
        {"youtube_1080p", "KoraCut YouTube 1080p", 16. / 9., -14},
        {"reels_tiktok_9x16", "KoraCut Reels TikTok 9:16", 9. / 16., -14},
        {"audio_wav", "KoraCut Audio WAV", 0., 0},
        {"apercu_gif", "KoraCut Aperçu GIF", 0., 0},
    };
    return list;
}

// « 8000k », « 8M », « 8000 » : des kbit/s.
double kbpsOf(const QString &text, double fallback)
{
    QString t = text.trimmed().toLower();
    if (t.isEmpty() || t.contains(QLatin1Char('%'))) {
        return fallback;
    }
    double factor = 1. / 1000.;
    if (t.endsWith(QLatin1Char('k'))) {
        factor = 1.;
        t.chop(1);
    } else if (t.endsWith(QLatin1Char('m'))) {
        factor = 1000.;
        t.chop(1);
    }
    bool ok = false;
    const double v = t.toDouble(&ok);
    return ok ? v * factor : fallback;
}

void fillEstimates(Preset &p, const std::unique_ptr<RenderPresetModel> &model)
{
    const RenderPresetParams params = model->params();
    p.video = !(params.contains(QStringLiteral("vn")) || params.contains(QStringLiteral("video_off")));
    p.audio = !(params.contains(QStringLiteral("an")) || params.contains(QStringLiteral("audio_off")));
    int width = 1920;
    int height = 1080;
    double fps = projectFps();
    if (const auto &profile = pCore->getCurrentProfile()) {
        width = profile->width();
        height = profile->height();
    }
    if (p.video) {
        const QString vcodec = params.value(QStringLiteral("vcodec")).toLower();
        double bitsPerPixelFrame = 0.16; // H.264 à qualité constante, l'ordre de grandeur courant
        if (vcodec.contains(QLatin1String("prores"))) {
            bitsPerPixelFrame = 4.2;
        } else if (vcodec.contains(QLatin1String("ffv1")) || vcodec.contains(QLatin1String("rawvideo")) || vcodec == QLatin1String("png")) {
            bitsPerPixelFrame = 8.;
        } else if (vcodec == QLatin1String("gif")) {
            bitsPerPixelFrame = 1.5;
        } else if (vcodec.contains(QLatin1String("265")) || vcodec.contains(QLatin1String("hevc")) || vcodec.contains(QLatin1String("vp9")) || vcodec.contains(QLatin1String("av1"))) {
            bitsPerPixelFrame = 0.1;
        }
        const double byCodec = bitsPerPixelFrame * width * height * fps / 1000.;
        p.videoKbps = params.contains(QStringLiteral("vb")) ? kbpsOf(params.value(QStringLiteral("vb")), kbpsOf(model->defaultVBitrate(), byCodec)) : byCodec;
    }
    if (p.audio) {
        const QString acodec = params.value(QStringLiteral("acodec")).toLower();
        const bool pcm = acodec.startsWith(QLatin1String("pcm")) || p.extension == QLatin1String("wav") || acodec == QLatin1String("flac") || acodec == QLatin1String("alac");
        const double byCodec = pcm ? 1536. : 160.;
        p.audioKbps = params.contains(QStringLiteral("ab")) ? kbpsOf(params.value(QStringLiteral("ab")), kbpsOf(model->defaultABitrate(), byCodec)) : byCodec;
    }
}

// Le préréglage demandé par `preset` (et `nom_preset_kdenlive`), ou une erreur.
bool resolvePreset(const Args &args, Preset &out, QJsonObject &failure)
{
    const QString id = args.value(QStringLiteral("preset")).toString();
    QString name;
    if (id == QLatin1String("personnalise")) {
        name = args.value(QStringLiteral("nom_preset_kdenlive")).toString().trimmed();
        if (name.isEmpty()) {
            failure = error(QStringLiteral("entree_invalide"), QStringLiteral("nom_preset_kdenlive est requis quand preset vaut « personnalise »."),
                            QStringLiteral("Appelle lister_prereglages et recopie le nom exact."));
            return false;
        }
    } else {
        for (const KoraCutPreset &k : koraCutPresets()) {
            if (id == QLatin1String(k.id)) {
                name = QString::fromUtf8(k.name);
                out.ratio = k.ratio;
                out.lufs = k.lufs;
            }
        }
        if (name.isEmpty()) {
            failure = error(QStringLiteral("entree_invalide"), QStringLiteral("preset vaut master_4k_prores, youtube_1080p, reels_tiktok_9x16, audio_wav, apercu_gif ou personnalise."));
            return false;
        }
    }
    if (!RenderPresetRepository::get()->presetExists(name)) {
        failure = error(QStringLiteral("id_inconnu"), QStringLiteral("Le préréglage « %1 » n'existe pas sur cette machine.").arg(name),
                        QStringLiteral("Appelle lister_prereglages et recopie le nom exact."));
        return false;
    }
    const std::unique_ptr<RenderPresetModel> &model = RenderPresetRepository::get()->getPreset(name);
    if (!model->error().isEmpty()) {
        failure = error(QStringLiteral("non_disponible"), QStringLiteral("Le préréglage « %1 » ne marche pas sur cette machine : %2").arg(name, model->error()),
                        QStringLiteral("Choisis un préréglage marqué disponible dans lister_prereglages."));
        return false;
    }
    out.id = id;
    out.name = name;
    out.extension = model->extension().toLower();
    fillEstimates(out, model);
    return true;
}

// ---------------------------------------------------------------------------------------------
// La plage exportée

struct Range
{
    int in = 0;
    int out = 0; // dernière image comprise
    bool guides = false;
    QString kind = QStringLiteral("projet_entier");
    int frames() const { return out - in + 1; }
};

bool readRange(const Timeline &timeline, const QJsonValue &value, Range &range, QJsonObject &failure)
{
    const int total = timeline->duration();
    range.in = 0;
    range.out = total - 1;
    if (value.isUndefined() || value.isNull()) {
        return true;
    }
    const QJsonObject plage = value.toObject();
    const QString type = plage.value(QStringLiteral("type")).toString(QStringLiteral("projet_entier"));
    range.kind = type;
    if (type == QLatin1String("projet_entier")) {
        return true;
    }
    if (type == QLatin1String("entre_marqueurs")) {
        const auto guides = timeline->getGuideModel();
        if (!guides || guides->getAllMarkers().isEmpty()) {
            failure = error(QStringLiteral("entree_invalide"), QStringLiteral("Le projet n'a aucun marqueur : rien ne délimite les sections."),
                            QStringLiteral("Pose des marqueurs avec ajouter_marqueur, ou exporte le projet entier."));
            return false;
        }
        range.guides = true;
        return true;
    }
    if (type != QLatin1String("zone")) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("plage.type vaut projet_entier, zone ou entre_marqueurs."));
        return false;
    }
    const bool hasIn = plage.contains(QStringLiteral("debut"));
    const bool hasOut = plage.contains(QStringLiteral("fin"));
    if (!hasIn && !hasOut) {
        // Sans bornes : la zone posée sur le moniteur.
        Monitor *monitor = pCore->window() ? pCore->getMonitor(Kdenlive::ProjectMonitor) : nullptr;
        if (!monitor || monitor->getZoneEnd() <= monitor->getZoneStart()) {
            failure = error(QStringLiteral("entree_invalide"), QStringLiteral("La plage « zone » demande debut et fin (la personne n'a pas posé de zone sur la timeline)."),
                            QStringLiteral("Donne debut et fin, ou exporte le projet entier."));
            return false;
        }
        range.in = monitor->getZoneStart();
        range.out = monitor->getZoneEnd() - 1;
    } else {
        const double fps = projectFps();
        range.in = hasIn ? KoraCutAgentTools::parseTime(plage.value(QStringLiteral("debut")), fps) : 0;
        range.out = hasOut ? KoraCutAgentTools::parseTime(plage.value(QStringLiteral("fin")), fps) : total - 1;
        if (range.in < 0 || range.out < 0) {
            failure = error(QStringLiteral("entree_invalide"), QStringLiteral("plage.debut et plage.fin se donnent en images ou en timecode HH:MM:SS:II."));
            return false;
        }
    }
    if (range.out < range.in || range.in >= total) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("La plage à exporter est vide."), QStringLiteral("Le projet va de 0 à %1.").arg(total - 1));
        return false;
    }
    range.out = std::min(range.out, total - 1);
    return true;
}

// ---------------------------------------------------------------------------------------------
// La sortie

QString expandUser(QString path)
{
    if (path.startsWith(QLatin1String("~/"))) {
        path = QDir::homePath() + path.mid(1);
    }
    return path;
}

bool isProjectMedia(const QString &path)
{
    const QString wanted = QFileInfo(path).canonicalFilePath().isEmpty() ? QFileInfo(path).absoluteFilePath() : QFileInfo(path).canonicalFilePath();
    for (const QString &id : pCore->projectItemModel()->getAllClipIds()) {
        const auto clip = pCore->projectItemModel()->getClipByBinID(id);
        if (!clip || clip->url().isEmpty()) {
            continue;
        }
        const QFileInfo info(clip->url());
        const QString media = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
        if (media == wanted) {
            return true;
        }
    }
    return false;
}

KoraCutExportQueue &queue()
{
    return KoraCutExportQueue::self();
}

bool queueHolds(const QString &path)
{
    return queue().holds(path);
}

// Tout ce qui interdit d'écrire à ce chemin. `overwrite` ne lève que `fichier_existe`.
bool checkOutput(const QString &path, const QString &extension, bool overwrite, const QSet<QString> &taken, QJsonObject &failure)
{
    if (!QDir::isAbsolutePath(path)) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("« %1 » n'est pas un chemin absolu.").arg(path),
                        QStringLiteral("Pars du dossier du projet : le dossier Exports à côté du fichier projet."));
        return false;
    }
    const QFileInfo info(path);
    if (!extension.isEmpty() && info.suffix().compare(extension, Qt::CaseInsensitive) != 0) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("Ce préréglage produit un fichier .%1 : « %2 » n'a pas cette extension.").arg(extension, info.fileName()),
                        QStringLiteral("Termine le nom du fichier par .%1.").arg(extension));
        return false;
    }
    if (!info.dir().exists()) {
        failure = error(QStringLiteral("fichier_introuvable"), QStringLiteral("Le dossier %1 n'existe pas.").arg(info.absolutePath()),
                        QStringLiteral("L'outil ne crée pas de dossier. Propose le dossier Exports à côté du fichier projet."));
        return false;
    }
    if (isProjectMedia(path)) {
        failure = error(QStringLiteral("fichier_protege"), QStringLiteral("%1 est un média du projet : un export n'écrit jamais par-dessus un original.").arg(info.fileName()),
                        QStringLiteral("Choisis un autre nom, dans le dossier Exports."));
        return false;
    }
    if (taken.contains(path) || queueHolds(path)) {
        failure = error(QStringLiteral("fichier_existe"), QStringLiteral("Un rendu vers %1 est déjà dans la file.").arg(info.fileName()),
                        QStringLiteral("Choisis un autre nom, ou attends la fin de ce rendu."));
        return false;
    }
    if (info.exists() && !overwrite) {
        failure = error(QStringLiteral("fichier_existe"), QStringLiteral("%1 existe déjà dans %2.").arg(info.fileName(), info.absolutePath()),
                        QStringLiteral("Demande à la personne : un autre nom (par exemple avec « v2 »), ou remplacer l'ancien. Ne mets ecraser à true que si elle l'a dit pour ce fichier."));
        return false;
    }
    return true;
}

double megabytes(const Preset &p, int frames)
{
    const double seconds = frames / projectFps();
    return std::max(0.1, std::round((p.videoKbps + p.audioKbps) * seconds / 8. / 1000. * 10.) / 10.);
}

QJsonValue estimatedSeconds(const Preset &p, int frames)
{
    if (!KoraCutExportQueue::speedCache().contains(p.name)) {
        return QJsonValue::Null;
    }
    return std::max(1, int(std::lround(frames / projectFps() * KoraCutExportQueue::speedCache().value(p.name))));
}

// ---------------------------------------------------------------------------------------------
// Le rendu lui-même : une RenderRequest comme celle de la fenêtre Export, sans boîte de dialogue.

struct Prepared
{
    Preset preset;
    Range range;
    QString path;
    int lufs = 0;
    bool overwrite = false;
    bool subtitles = true; // sous_titres : incruster (défaut) ou sans
    std::vector<RenderRequest::RenderJob> jobs;
    KoraCutLoudness::Plan loudness; // ce que le nivellement a posé (gain mesuré, limiteur, ou repli)
};

bool buildRequest(const Timeline &timeline, Prepared &p, QJsonObject &failure)
{
    RenderRequest request;
    request.setOutputFile(p.path);
    request.loadPresetParams(p.preset.name);
    request.setProxyRendering(false);
    request.setDelayedRendering(false);
    request.setEmbedSubtitles(false);
    request.setBurnSubtitles(p.subtitles);
    request.setTwoPass(false);
    request.setAudioFilePerTrack(false);
    request.setLoudnessTarget(p.lufs);
    if (p.range.guides) {
        request.setGuideParams(timeline->getGuideModel(), true, -1);
    } else {
        request.setBounds(p.range.in, p.range.out);
    }
    p.jobs = request.process();
    p.loudness = request.loudnessPlan();
    if (!request.errorMessages().isEmpty() || p.jobs.empty()) {
        failure = error(QStringLiteral("rendu_echoue"), QStringLiteral("Le rendu n'a pas pu être préparé : %1").arg(request.errorMessages().join(QLatin1String(" ; "))),
                        QStringLiteral("Vérifie que le projet a un fichier et que ses médias sont là (verifier_avant_export)."));
        return false;
    }
    // Les sections entre marqueurs donnent leur propre nom de fichier : on les contrôle ici.
    for (const RenderRequest::RenderJob &job : p.jobs) {
        if (job.outputFile != p.path && QFileInfo::exists(job.outputFile) && !p.overwrite) {
            for (const RenderRequest::RenderJob &other : p.jobs) {
                QFile::remove(other.playlistPath);
            }
            failure = error(QStringLiteral("fichier_existe"), QStringLiteral("%1 existe déjà.").arg(job.outputFile),
                            QStringLiteral("Choisis un autre nom de base, ou demande l'accord pour ecraser."));
            return false;
        }
    }
    return true;
}

// Lit preset, plage, chemin et niveau sonore d'un export, sans rien lancer.
bool prepareExport(const Timeline &timeline, const QJsonObject &args, bool overwrite, const QSet<QString> &taken, Prepared &p, QJsonObject &failure)
{
    if (!resolvePreset(args, p.preset, failure)) {
        return false;
    }
    if (timeline->duration() < 1) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("La timeline est vide : il n'y a rien à exporter."), QStringLiteral("Monte d'abord quelque chose."));
        return false;
    }
    if (!readRange(timeline, args.value(QStringLiteral("plage")), p.range, failure)) {
        return false;
    }
    p.path = expandUser(args.value(QStringLiteral("chemin")).toString().trimmed());
    if (p.path.isEmpty()) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("chemin est vide."));
        return false;
    }
    p.overwrite = overwrite;
    if (!checkOutput(p.path, p.preset.extension, overwrite, taken, failure)) {
        return false;
    }
    // Une version sans sous-titres sans toucher à la piste (avant : désactiver la piste, disable_subtitle).
    const QString subtitles = args.value(QStringLiteral("sous_titres")).toString(QStringLiteral("incruster"));
    if (subtitles != QLatin1String("incruster") && subtitles != QLatin1String("sans")) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("sous_titres vaut incruster ou sans."));
        return false;
    }
    p.subtitles = subtitles == QLatin1String("incruster");
    if (args.contains(QStringLiteral("niveau_lufs"))) {
        const int lufs = args.value(QStringLiteral("niveau_lufs")).toInt(0);
        if (lufs > -6 || lufs < -30) {
            failure = error(QStringLiteral("entree_invalide"), QStringLiteral("niveau_lufs va de -30 à -6 (par exemple -14 pour YouTube, -16 pour un podcast)."));
            return false;
        }
        if (!p.preset.audio) {
            failure = error(QStringLiteral("entree_invalide"), QStringLiteral("Ce préréglage n'a pas de son : niveau_lufs ne s'applique pas."));
            return false;
        }
        p.lufs = lufs;
    }
    return true;
}

QJsonObject queued(const Prepared &p, KoraCutExportQueue::JobPtr &first)
{
    QJsonArray list;
    for (const RenderRequest::RenderJob &render : p.jobs) {
        const KoraCutExportQueue::JobPtr job = queue().add(render, p.preset.id, p.preset.name, p.range.frames() / int(p.jobs.size()), p.lufs, QFileInfo::exists(render.outputFile));
        if (!first) {
            first = job;
        }
        list.append(QJsonObject{{QStringLiteral("export_id"), job->id}, {QStringLiteral("chemin"), job->path}, {QStringLiteral("preset"), p.preset.id}});
    }
    queue().startNext();
    QJsonObject o{{QStringLiteral("ok"), true},
                  {QStringLiteral("export_id"), first->id},
                  {QStringLiteral("tache_id"), first->path},
                  {QStringLiteral("chemin"), first->path},
                  {QStringLiteral("preset"), p.preset.id},
                  {QStringLiteral("preset_kdenlive"), p.preset.name},
                  {QStringLiteral("etat"), first->state},
                  {QStringLiteral("plage"), p.range.kind},
                  {QStringLiteral("duree_exportee"), time(p.range.frames())},
                  {QStringLiteral("taille_estimee_mo"), megabytes(p.preset, p.range.frames())},
                  {QStringLiteral("temps_estime_secondes"), estimatedSeconds(p.preset, p.range.frames())},
                  {QStringLiteral("avec_proxys"), false},
                  {QStringLiteral("remplace_un_fichier"), first->replaces}};
    if (p.lufs != 0) {
        o.insert(QStringLiteral("niveau_lufs"), p.lufs);
        o.insert(QStringLiteral("nivellement"), KoraCutLoudness::planJson(p.loudness));
    }
    o.insert(QStringLiteral("sous_titres"), p.subtitles ? QStringLiteral("incruster") : QStringLiteral("sans"));
    if (list.size() > 1) {
        o.insert(QStringLiteral("exports"), list);
    }
    o.insert(QStringLiteral("conseil"), QStringLiteral("etat_exports suit l'avancement ; ne boucle pas dessus, l'application affiche déjà la progression."));
    return o;
}

// ---------------------------------------------------------------------------------------------
// lister_prereglages

QJsonObject listerPrereglages(const Timeline &, const Args &args)
{
    const QString wanted = args.value(QStringLiteral("categorie")).toString().trimmed().toLower();
    const QMap<QString, QString> categories = RenderPresetRepository::get()->getAllCategories();
    QJsonArray list;
    const QVector<QString> names = RenderPresetRepository::get()->getAllPresets();
    for (const QString &name : names) {
        const std::unique_ptr<RenderPresetModel> &model = RenderPresetRepository::get()->getPreset(name);
        if (!model) {
            continue;
        }
        const QString category = categories.value(model->groupId(), model->groupId());
        if (!wanted.isEmpty() && category.toLower() != wanted && model->groupId().toLower() != wanted) {
            continue;
        }
        QJsonObject o{{QStringLiteral("nom"), name},
                      {QStringLiteral("categorie"), category},
                      {QStringLiteral("extension"), model->extension().toLower()},
                      {QStringLiteral("disponible"), model->error().isEmpty()},
                      {QStringLiteral("koracut"), false}};
        for (const KoraCutPreset &k : koraCutPresets()) {
            if (name == QString::fromUtf8(k.name)) {
                o.insert(QStringLiteral("koracut"), true);
                o.insert(QStringLiteral("preset"), QLatin1String(k.id));
            }
        }
        if (!model->error().isEmpty()) {
            o.insert(QStringLiteral("raison"), model->error());
        }
        list.append(o);
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("prereglages"), list}, {QStringLiteral("total"), list.size()}};
}

// ---------------------------------------------------------------------------------------------
// verifier_avant_export

QJsonObject problem(const QString &severity, const QString &code, const QString &message, int position = -1)
{
    QJsonObject o{{QStringLiteral("gravite"), severity}, {QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
    if (position >= 0) {
        o.insert(QStringLiteral("position"), time(position));
    }
    return o;
}

QJsonObject verifierAvantExport(const Timeline &timeline, const Args &args)
{
    Preset preset;
    QJsonObject failure;
    if (!resolvePreset(args, preset, failure)) {
        return failure;
    }
    QJsonArray problems;
    const int total = timeline->duration();
    if (total < 1) {
        problems.append(problem(QStringLiteral("bloquant"), QStringLiteral("timeline_vide"), QStringLiteral("La timeline est vide : il n'y a rien à exporter.")));
    }
    // Les médias : manquants sur le disque, ou dont le proxy se fabrique encore.
    QSet<QString> seen;
    QList<QPair<int, int>> covered; // les plages d'image visibles
    for (int tid : tracksOnScreen(timeline)) {
        const bool audio = timeline->isAudioTrack(tid);
        const bool masked = trackMasked(timeline, tid);
        for (int cid : clipsOnTrack(timeline, tid)) {
            const QString binId = timeline->getClipBinId(cid);
            if (!audio && !masked) {
                covered << qMakePair(timeline->getClipPosition(cid), timeline->getClipPosition(cid) + timeline->getClipPlaytime(cid));
            }
            if (seen.contains(binId)) {
                continue;
            }
            seen.insert(binId);
            const auto clip = pCore->projectItemModel()->getClipByBinID(binId);
            if (!clip) {
                continue;
            }
            if (clip->clipStatus() == FileStatus::StatusMissing) {
                problems.append(problem(QStringLiteral("bloquant"), QStringLiteral("media_manquant"), QStringLiteral("Le fichier de %1 a disparu du disque.").arg(clip->clipName()),
                                        timeline->getClipPosition(cid)));
            }
            if (pCore->taskManager.hasPendingJob(ObjectId(KdenliveObjectType::BinClip, binId.toInt(), QUuid()), AbstractTask::PROXYJOB)) {
                problems.append(problem(QStringLiteral("attention"), QStringLiteral("proxy_en_cours"),
                                        QStringLiteral("Le proxy de %1 est encore en création (le rendu utilise les originaux, mais la machine est occupée).").arg(clip->clipName())));
            }
        }
    }
    if (preset.video && total > 0) {
        std::sort(covered.begin(), covered.end());
        int cursor = 0;
        int gaps = 0;
        for (const auto &range : std::as_const(covered)) {
            if (range.first > cursor) {
                if (++gaps <= 10) {
                    problems.append(problem(QStringLiteral("attention"), QStringLiteral("vide_video"),
                                            QStringLiteral("Image noire de %1 à %2 (%3 images).").arg(cursor).arg(range.first - 1).arg(range.first - cursor), cursor));
                }
            }
            cursor = std::max(cursor, range.second);
        }
        if (cursor < total && ++gaps <= 10) {
            problems.append(problem(QStringLiteral("attention"), QStringLiteral("vide_video"), QStringLiteral("Image noire de %1 à %2 (%3 images).").arg(cursor).arg(total - 1).arg(total - cursor), cursor));
        }
        if (gaps > 10) {
            problems.append(problem(QStringLiteral("attention"), QStringLiteral("vide_video"), QStringLiteral("%1 autres vides d'image.").arg(gaps - 10)));
        }
        if (preset.ratio > 0) {
            if (const auto &profile = pCore->getCurrentProfile()) {
                const double projectRatio = double(profile->width()) / std::max(1, profile->height());
                if (std::abs(projectRatio - preset.ratio) > 0.01) {
                    problems.append(problem(QStringLiteral("attention"), QStringLiteral("rapport_image"),
                                            QStringLiteral("Le projet est en %1 x %2, le préréglage vise le %3 : le fichier gardera le format du projet.")
                                                .arg(profile->width())
                                                .arg(profile->height())
                                                .arg(preset.ratio > 1 ? QStringLiteral("16:9") : QStringLiteral("9:16"))));
                }
            }
        }
    }
    // Le son : mesuré si le projet reste court (moins de quinze minutes), sinon on le dit.
    const int lufsTarget = args.contains(QStringLiteral("niveau_lufs")) ? args.value(QStringLiteral("niveau_lufs")).toInt() : preset.lufs;
    if (preset.audio && total > 0) {
        if (total <= 15 * 60 * projectFps()) {
            const QJsonObject sound = KoraCutAgentTools::call(timeline, QStringLiteral("mesurer_audio"), {{QStringLiteral("cible"), QStringLiteral("mixage")}});
            if (sound.value(QStringLiteral("ok")).toBool()) {
                if (sound.value(QStringLiteral("silence")).toBool()) {
                    problems.append(problem(QStringLiteral("attention"), QStringLiteral("silence_total"), QStringLiteral("Le mixage est muet d'un bout à l'autre.")));
                } else {
                    const double lufs = sound.value(QStringLiteral("lufs_integre")).toDouble();
                    const double peak = sound.value(QStringLiteral("crete_vraie_dbtp")).toDouble();
                    if (lufsTarget != 0 && std::abs(lufs - lufsTarget) > 1.) {
                        problems.append(problem(QStringLiteral("attention"), QStringLiteral("niveau_sonore"),
                                                QStringLiteral("Le mixage est à %1 LUFS, la cible est %2 LUFS (niveau_lufs dans exporter le mettra au niveau).").arg(lufs).arg(lufsTarget)));
                    }
                    if (peak > -1.) {
                        problems.append(problem(QStringLiteral("attention"), QStringLiteral("crete"), QStringLiteral("Crête vraie à %1 dBTP, au-dessus de -1 dBTP.").arg(peak)));
                    }
                }
            }
        } else {
            problems.append(problem(QStringLiteral("attention"), QStringLiteral("niveau_sonore_non_mesure"), QStringLiteral("Projet long : le niveau sonore n'a pas été mesuré (mesurer_audio le fait à la demande).")));
        }
    }
    // Le fichier de sortie et l'espace disque.
    QString folder;
    if (args.contains(QStringLiteral("chemin"))) {
        const QString path = expandUser(args.value(QStringLiteral("chemin")).toString().trimmed());
        const QFileInfo info(path);
        if (!QDir::isAbsolutePath(path)) {
            problems.append(problem(QStringLiteral("bloquant"), QStringLiteral("chemin_relatif"), QStringLiteral("« %1 » n'est pas un chemin absolu.").arg(path)));
        } else {
            folder = info.absolutePath();
            if (!info.dir().exists()) {
                problems.append(problem(QStringLiteral("bloquant"), QStringLiteral("dossier_introuvable"), QStringLiteral("Le dossier %1 n'existe pas.").arg(folder)));
            }
            if (info.suffix().compare(preset.extension, Qt::CaseInsensitive) != 0) {
                problems.append(problem(QStringLiteral("attention"), QStringLiteral("extension"), QStringLiteral("Ce préréglage écrit un .%1, le chemin finit par .%2.").arg(preset.extension, info.suffix())));
            }
            if (info.exists()) {
                problems.append(problem(QStringLiteral("attention"), QStringLiteral("fichier_existe"), QStringLiteral("%1 existe déjà : exporter refusera sans ecraser.").arg(info.fileName())));
            }
            if (isProjectMedia(path)) {
                problems.append(problem(QStringLiteral("bloquant"), QStringLiteral("fichier_protege"), QStringLiteral("%1 est un média du projet.").arg(info.fileName())));
            }
        }
    }
    if (folder.isEmpty() || !QDir(folder).exists()) {
        folder = pCore->currentDoc() && !pCore->currentDoc()->url().isEmpty() ? QFileInfo(pCore->currentDoc()->url().toLocalFile()).absolutePath() : QDir::homePath();
    }
    const double sizeMb = megabytes(preset, std::max(1, total));
    const QStorageInfo storage(folder);
    const double freeMb = storage.isValid() ? storage.bytesAvailable() / 1000000. : -1.;
    if (freeMb >= 0 && sizeMb > freeMb) {
        problems.append(problem(QStringLiteral("bloquant"), QStringLiteral("espace_disque"), QStringLiteral("Il faut environ %1 Mo, il en reste %2 sur %3.").arg(sizeMb).arg(std::round(freeMb)).arg(folder)));
    }
    bool ready = true;
    for (const QJsonValue &v : std::as_const(problems)) {
        if (v.toObject().value(QStringLiteral("gravite")).toString() == QLatin1String("bloquant")) {
            ready = false;
        }
    }
    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("pret"), ready},
                       {QStringLiteral("preset"), preset.id},
                       {QStringLiteral("preset_kdenlive"), preset.name},
                       {QStringLiteral("duree"), time(total)},
                       {QStringLiteral("problemes"), problems},
                       {QStringLiteral("taille_estimee_mo"), sizeMb}};
    if (freeMb >= 0) {
        result.insert(QStringLiteral("espace_libre_mo"), std::round(freeMb));
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// estimer_export

// Mesure la vitesse de rendu du préréglage sur deux secondes de la plage, une fois par session.
// -1 si le rendu d'essai n'a pas abouti.
double measureSpeed(const Timeline &timeline, const Preset &preset, const Range &range, int lufs)
{
    if (KoraCutExportQueue::speedCache().contains(preset.name)) {
        return KoraCutExportQueue::speedCache().value(preset.name);
    }
    const QString renderer = KoraCutExportQueue::rendererPath();
    if (renderer.isEmpty() || !pCore->currentDoc()) {
        return -1.;
    }
    const int fps = std::max(1, qRound(projectFps()));
    Range sample;
    sample.in = range.in;
    sample.out = std::min(range.out, range.in + 2 * fps - 1);
    const QString output = QDir::temp().absoluteFilePath(QStringLiteral("koracut-estimation-%1.%2").arg(QCoreApplication::applicationPid()).arg(preset.extension));
    QFile::remove(output);
    Prepared p;
    p.preset = preset;
    p.range = sample;
    p.path = output;
    p.lufs = lufs;
    p.overwrite = true;
    QJsonObject failure;
    if (!buildRequest(timeline, p, failure) || p.jobs.empty()) {
        return -1.;
    }
    QProcess process;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // KoraCut : les essais en cours finissent d'abord, puis jamais la puce si un clip ne se décode pas avec elle.
    KoraCutHwDecode::waitForTrials(3000);
    KoraCutHwDecode::applyTo(env);
    process.setProcessEnvironment(env);
    process.setProgram(renderer);
    process.setArguments(RenderRequest::argsByJob(p.jobs.front(), false));
    QElapsedTimer timer;
    timer.start();
    process.start();
    const bool done = process.waitForFinished(120000);
    const double elapsed = timer.elapsed() / 1000.;
    if (!done) {
        process.kill();
    }
    for (const RenderRequest::RenderJob &job : p.jobs) {
        QFile::remove(job.playlistPath);
    }
    const bool produced = QFileInfo(output).size() > 0;
    QFile::remove(output);
    if (!done || !produced || elapsed <= 0) {
        return -1.;
    }
    // Le lancement du processus compte pour environ une seconde, qu'on retire de la mesure.
    const double speed = std::max(0.05, (elapsed - 1.) / (sample.frames() / double(fps)));
    KoraCutExportQueue::speedCache().insert(preset.name, speed);
    return speed;
}

QJsonObject estimerExport(const Timeline &timeline, const Args &args)
{
    Preset preset;
    QJsonObject failure;
    if (!resolvePreset(args, preset, failure)) {
        return failure;
    }
    if (timeline->duration() < 1) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("La timeline est vide : il n'y a rien à estimer."), QStringLiteral("Monte d'abord quelque chose."));
    }
    Range range;
    if (!readRange(timeline, args.value(QStringLiteral("plage")), range, failure)) {
        return failure;
    }
    const double speed = args.value(QStringLiteral("mesurer_temps")).toBool(true) ? measureSpeed(timeline, preset, range, 0) : (KoraCutExportQueue::speedCache().contains(preset.name) ? KoraCutExportQueue::speedCache().value(preset.name) : -1.);
    const double sizeMb = megabytes(preset, range.frames());
    const QString folder = pCore->currentDoc() && !pCore->currentDoc()->url().isEmpty() ? QFileInfo(pCore->currentDoc()->url().toLocalFile()).absolutePath() : QDir::homePath();
    const QStorageInfo storage(folder);
    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("preset"), preset.id},
                       {QStringLiteral("preset_kdenlive"), preset.name},
                       {QStringLiteral("duree_exportee"), time(range.frames())},
                       {QStringLiteral("taille_estimee_mo"), sizeMb},
                       {QStringLiteral("espace_libre_mo"), storage.isValid() ? std::round(storage.bytesAvailable() / 1000000.) : -1.}};
    if (speed > 0) {
        const int seconds = std::max(1, int(std::lround(range.frames() / projectFps() * speed)));
        result.insert(QStringLiteral("temps_estime_secondes"), seconds);
        result.insert(QStringLiteral("temps"), QStringLiteral("mesure"));
        result.insert(QStringLiteral("rendu_long"), seconds > 120);
    } else {
        result.insert(QStringLiteral("temps_estime_secondes"), QJsonValue::Null);
        result.insert(QStringLiteral("temps"), QStringLiteral("non_disponible"));
        result.insert(QStringLiteral("rendu_long"), false);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// exporter, exporter_serie

QJsonObject exporter(const Timeline &timeline, const Args &args)
{
    if (!pCore->currentDoc()) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucun projet ouvert."));
    }
    Prepared p;
    QJsonObject failure;
    if (!prepareExport(timeline, args, args.value(QStringLiteral("ecraser")).toBool(false), {}, p, failure)) {
        return failure;
    }
    if (!buildRequest(timeline, p, failure)) {
        return failure;
    }
    KoraCutExportQueue::JobPtr first;
    return queued(p, first);
}

QJsonObject exporterSerie(const Timeline &timeline, const Args &args)
{
    if (!pCore->currentDoc()) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucun projet ouvert."));
    }
    const QJsonArray items = args.value(QStringLiteral("exports")).toArray();
    if (items.size() < 2 || items.size() > 8) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("exports compte de 2 à 8 exports."), QStringLiteral("Pour un seul, appelle exporter."));
    }
    const bool overwrite = args.value(QStringLiteral("ecraser")).toBool(false);
    // Tout est contrôlé avant de mettre quoi que ce soit en file : la série part entière ou ne part pas.
    std::vector<Prepared> prepared;
    QSet<QString> taken;
    QJsonObject failure;
    for (const QJsonValue &v : items) {
        Prepared p;
        // sous_titres au niveau de la série vaut pour chaque export qui ne dit rien.
        QJsonObject item = v.toObject();
        if (!item.contains(QStringLiteral("sous_titres")) && args.contains(QStringLiteral("sous_titres"))) {
            item.insert(QStringLiteral("sous_titres"), args.value(QStringLiteral("sous_titres")));
        }
        if (!prepareExport(timeline, item, overwrite, taken, p, failure)) {
            return failure;
        }
        taken.insert(p.path);
        prepared.push_back(p);
    }
    for (Prepared &p : prepared) {
        if (!buildRequest(timeline, p, failure)) {
            for (const Prepared &other : prepared) {
                for (const RenderRequest::RenderJob &job : other.jobs) {
                    QFile::remove(job.playlistPath);
                }
            }
            return failure;
        }
    }
    QJsonArray list;
    int totalSeconds = 0;
    bool timeKnown = true;
    for (const Prepared &p : prepared) {
        KoraCutExportQueue::JobPtr first;
        const QJsonObject one = queued(p, first);
        list.append(QJsonObject{{QStringLiteral("export_id"), one.value(QStringLiteral("export_id"))},
                                {QStringLiteral("chemin"), one.value(QStringLiteral("chemin"))},
                                {QStringLiteral("preset"), one.value(QStringLiteral("preset"))},
                                {QStringLiteral("sous_titres"), one.value(QStringLiteral("sous_titres"))},
                                {QStringLiteral("etat"), one.value(QStringLiteral("etat"))}});
        const QJsonValue seconds = one.value(QStringLiteral("temps_estime_secondes"));
        if (seconds.isDouble()) {
            totalSeconds += seconds.toInt();
        } else {
            timeKnown = false;
        }
    }
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("exports"), list}};
    result.insert(QStringLiteral("temps_estime_total_secondes"), timeKnown ? QJsonValue(totalSeconds) : QJsonValue::Null);
    result.insert(QStringLiteral("conseil"), QStringLiteral("Les rendus s'enchaînent un par un ; etat_exports suit la file."));
    return result;
}

// ---------------------------------------------------------------------------------------------
// etat_exports, annuler_export

QJsonObject etatExports(const Timeline &, const Args &args)
{
    const QString wanted = args.value(QStringLiteral("export_id")).toString();
    QJsonArray list;
    for (const auto &job : queue().jobs()) {
        if (wanted.isEmpty() || job->id == wanted || job->path == wanted) {
            list.append(queue().json(job));
        }
    }
    if (!wanted.isEmpty() && list.isEmpty()) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun export ne porte l'id %1.").arg(wanted), QStringLiteral("Appelle etat_exports sans export_id pour voir toute la file."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("exports"), list}};
}

QJsonObject annulerExport(const Timeline &, const Args &args)
{
    const QString wanted = args.value(QStringLiteral("export_id")).toString();
    KoraCutExportQueue::JobPtr job = queue().find(wanted);
    if (!job) {
        for (const auto &j : queue().jobs()) {
            if (j->path == wanted && (j->state == QLatin1String("en_cours") || j->state == QLatin1String("en_attente"))) {
                job = j;
            }
        }
    }
    if (!job) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun export ne porte l'id %1.").arg(wanted), QStringLiteral("Appelle etat_exports pour voir la file."));
    }
    if (job->state != QLatin1String("en_cours") && job->state != QLatin1String("en_attente")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Cet export est déjà %1 : il n'y a rien à arrêter.").arg(job->state),
                     QStringLiteral("Le fichier reste sur le disque : l'outil ne supprime rien."));
    }
    const bool wasRunning = job->state == QLatin1String("en_cours");
    queue().abort(job);
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("export_id"), job->id}, {QStringLiteral("chemin"), job->path}, {QStringLiteral("etat"), QStringLiteral("annule")}};
    result.insert(QStringLiteral("fichier_partiel"), QJsonValue::Null);
    if (wasRunning) {
        result.insert(QStringLiteral("remarque"), QStringLiteral("Le rendu s'arrête et KoraCut retire le fichier partiel : rien ne reste sur le disque."));
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// exporter_image

QJsonObject exporterImage(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    const int position = requiredTime(args, QStringLiteral("position"), failure);
    if (position < 0) {
        return failure;
    }
    if (position >= std::max(1, timeline->duration())) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("La position %1 est après la fin du projet (%2).").arg(position).arg(timeline->duration() - 1),
                     QStringLiteral("Choisis une image à l'intérieur du montage."));
    }
    const QString path = expandUser(args.value(QStringLiteral("chemin")).toString().trimmed());
    if (path.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("chemin est vide."));
    }
    if (!checkOutput(path, QStringLiteral("png"), false, {}, failure)) {
        return failure;
    }
    int width = 1920;
    int height = 1080;
    if (const auto &profile = pCore->getCurrentProfile()) {
        width = profile->width();
        height = profile->height();
    }
    const QString xml = timeline->sceneList(QString());
    QImage image;
    if (!xml.isEmpty()) {
        Mlt::Producer producer(pCore->getProjectProfile(), "xml-string", xml.toUtf8().constData());
        if (producer.is_valid()) {
            image = KThumb::getFrame(producer, position, width, height);
        }
    }
    if (image.isNull() || !image.save(path, "PNG")) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucune image n'a pu être écrite à cette position."));
    }
    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("chemin"), path},
                       {QStringLiteral("position"), time(position)},
                       {QStringLiteral("largeur_px"), image.width()},
                       {QStringLiteral("hauteur_px"), image.height()}};
    if (args.value(QStringLiteral("ajouter_au_chutier")).toBool(false)) {
        const QJsonObject imported = KoraCutAgentTools::call(timeline, QStringLiteral("importer_media"), {{QStringLiteral("chemins"), QJsonArray{path}}});
        const QJsonArray medias = imported.value(QStringLiteral("medias")).toArray();
        if (imported.value(QStringLiteral("ok")).toBool() && !medias.isEmpty()) {
            result.insert(QStringLiteral("media_id"), medias.first().toObject().value(QStringLiteral("id")).toString());
        } else {
            result.insert(QStringLiteral("remarque"), QStringLiteral("L'image est écrite mais n'a pas pu entrer dans le chutier."));
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// modeles_export (document 23) : les modèles du module Export, comme les tuiles de la page.

QJsonObject modelJson(const KoraCutExport::Model &m, const QSize &sequence)
{
    QJsonObject o{{QStringLiteral("id"), m.id},
                  {QStringLiteral("nom"), m.name},
                  {QStringLiteral("detail"), m.detail},
                  {QStringLiteral("plateforme"), m.platform},
                  {QStringLiteral("extension"), m.container},
                  {QStringLiteral("video"), m.video},
                  {QStringLiteral("audio"), m.audio},
                  {QStringLiteral("va_avec_la_sequence"), KoraCutExport::modelFits(m, sequence)}};
    if (m.width > 0 && m.height > 0) {
        o.insert(QStringLiteral("taille"), QStringLiteral("%1x%2").arg(m.width).arg(m.height));
    }
    if (m.bitrateKbps > 0) {
        o.insert(QStringLiteral("debit_max_mbps"), m.bitrateKbps / 1000.);
    }
    if (m.lufs != 0) {
        o.insert(QStringLiteral("lufs"), m.lufs);
    }
    if (m.maxSeconds > 0) {
        o.insert(QStringLiteral("duree_max_secondes"), m.maxSeconds);
    }
    return o;
}

QJsonObject settingsJson(const KoraCutExport::Settings &s)
{
    QJsonObject o = s.toJson();
    o.insert(QStringLiteral("fichier_prevu"), KoraCutExport::outputPath(s, KoraCutExport::currentProjectName(), KoraCutExport::currentSequenceName()));
    return o;
}

QJsonObject modelesExport(const Timeline &timeline, const Args &args)
{
    Q_UNUSED(timeline)
    const QString action = args.value(QStringLiteral("action")).toString(QStringLiteral("lister"));
    if (!pCore->currentDoc()) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucun projet ouvert."));
    }
    const QSize sequence = pCore->getCurrentFrameSize();
    const double fps = pCore->getCurrentFps();
    const QString uuid = pCore->currentTimelineId().toString();
    KoraCutExport::Settings current = KoraCutExport::documentSettings(uuid, sequence, fps);
    const QString type = pCore->currentDoc()->getDocumentProperty(QStringLiteral("koracut:videotype"));
    const QString recommended = type.isEmpty() ? KoraCutExport::modelForSize(sequence) : KoraCutExport::modelForVideoType(type, sequence);
    if (action == QLatin1String("lister")) {
        QJsonArray list;
        for (const KoraCutExport::Model &m : KoraCutExport::models()) {
            list.append(modelJson(m, sequence));
        }
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("modeles"), list},
                           {QStringLiteral("recommande"), recommended},
                           {QStringLiteral("sequence"), QStringLiteral("%1x%2").arg(sequence.width()).arg(sequence.height())},
                           {QStringLiteral("reglages"), settingsJson(current)}};
    }
    if (action == QLatin1String("lancer_file")) {
        int waiting = 0;
        for (const auto &j : KoraCutExportQueue::self().jobs()) {
            waiting += j->state == QLatin1String("en_attente") ? 1 : 0;
        }
        if (waiting == 0) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("La file n'a aucun export en attente."), QStringLiteral("Ajoute d'abord des exports (action ajouter)."));
        }
        KoraCutExportQueue::self().startAll();
        QJsonArray jobs;
        for (const auto &j : KoraCutExportQueue::self().jobs()) {
            jobs.append(KoraCutExportQueue::self().json(j));
        }
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("lances"), waiting}, {QStringLiteral("exports"), jobs},
                           {QStringLiteral("conseil"), QStringLiteral("etat_exports suit l'avancement ; ne boucle pas dessus.")}};
    }
    if (action != QLatin1String("appliquer") && action != QLatin1String("ajouter") && action != QLatin1String("exporter") && action != QLatin1String("lire")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("action vaut lister, lire, appliquer, ajouter, exporter ou lancer_file."));
    }
    KoraCutExport::Settings s = current;
    if (args.contains(QStringLiteral("modele"))) {
        const KoraCutExport::Model *m = KoraCutExport::model(args.value(QStringLiteral("modele")).toString());
        if (!m) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun modèle « %1 ».").arg(args.value(QStringLiteral("modele")).toString()),
                         QStringLiteral("Appelle modeles_export avec action lister."));
        }
        s = KoraCutExport::Settings::forModel(*m, sequence, fps);
        s.folder = current.folder;
        s.name = current.name;
        s.inOut = current.inOut;
    }
    // Les réglages changés à la main : les mêmes clés que la page (onglets Vidéo, Audio, Fichier).
    const QJsonObject changes = args.value(QStringLiteral("reglages")).toObject();
    if (!changes.isEmpty()) {
        QJsonObject o = s.toJson();
        for (auto it = changes.begin(); it != changes.end(); ++it) {
            if (!o.contains(it.key())) {
                return error(QStringLiteral("entree_invalide"), QStringLiteral("Réglage inconnu : %1.").arg(it.key()),
                             QStringLiteral("Les clés sont celles de reglages dans la réponse de lister."));
            }
            o.insert(it.key(), it.value());
        }
        s = KoraCutExport::Settings::fromJson(o);
        s.customized = true;
        if (s.lufs < -30 || s.lufs > -6) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("lufs va de -30 à -6."));
        }
    }
    if (action == QLatin1String("lire")) {
        return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("reglages"), settingsJson(current)}, {QStringLiteral("recommande"), recommended}};
    }
    // Appliquer : la page Export et le projet gardent ces réglages pour la séquence.
    if (KoraCutDeliverPage *page = KoraCutDeliverPage::instance()) {
        page->setSettings(s, s.customized);
        page->reload();
    } else {
        KoraCutExport::saveDocumentSettings(uuid, s);
    }
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("reglages"), settingsJson(s)}};
    if (const KoraCutExport::Model *m = KoraCutExport::model(s.model); m && !KoraCutExport::modelFits(*m, sequence) && s.video) {
        result.insert(QStringLiteral("attention"), QStringLiteral("%1 vise %2x%3, la séquence est en %4x%5 : le fichier garde la forme de la séquence (il faut une séquence verticale pour un vrai 9:16).")
                                                       .arg(m->name).arg(m->width).arg(m->height).arg(sequence.width()).arg(sequence.height()));
    }
    if (action == QLatin1String("appliquer")) {
        return result;
    }
    const QString wanted = expandUser(args.value(QStringLiteral("chemin")).toString().trimmed());
    if (!wanted.isEmpty()) {
        QJsonObject failure;
        if (!checkOutput(wanted, s.extension(), false, {}, failure)) {
            return failure;
        }
    }
    const KoraCutExport::Enqueued r = KoraCutExport::enqueue(s, action == QLatin1String("ajouter"), wanted);
    if (!r.ok) {
        return error(r.code.isEmpty() ? QStringLiteral("rendu_echoue") : r.code, r.message);
    }
    result.insert(QStringLiteral("chemin"), r.path);
    result.insert(QStringLiteral("duree_exportee"), time(r.frames));
    if (!r.subtitleFile.isEmpty()) {
        result.insert(QStringLiteral("sous_titres_srt"), r.subtitleFile);
    }
    if (r.still) {
        result.insert(QStringLiteral("etat"), QStringLiteral("termine"));
        return result;
    }
    result.insert(QStringLiteral("export_id"), r.jobIds.value(0));
    result.insert(QStringLiteral("tache_id"), r.path);
    const auto job = KoraCutExportQueue::self().find(r.jobIds.value(0));
    result.insert(QStringLiteral("etat"), job ? job->state : QStringLiteral("en_attente"));
    result.insert(QStringLiteral("conseil"), action == QLatin1String("ajouter") ? QStringLiteral("Retenu dans la file : action lancer_file le démarre (avec l'accord de la personne).")
                                                                                  : QStringLiteral("etat_exports suit l'avancement ; ne boucle pas dessus."));
    return result;
}

} // namespace

namespace KoraCutAgentToolsPrivate {

QMap<QString, Tool> exportTools()
{
    return {
        {QStringLiteral("lister_prereglages"), listerPrereglages}, {QStringLiteral("verifier_avant_export"), verifierAvantExport},
        {QStringLiteral("estimer_export"), estimerExport},         {QStringLiteral("exporter"), exporter},
        {QStringLiteral("exporter_serie"), exporterSerie},         {QStringLiteral("etat_exports"), etatExports},
        {QStringLiteral("annuler_export"), annulerExport},         {QStringLiteral("exporter_image"), exporterImage},
        {QStringLiteral("modeles_export"), modelesExport},
    };
}

} // namespace KoraCutAgentToolsPrivate
