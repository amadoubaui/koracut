/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutagenttools.h"
#include "koracutagenttools_p.h"

#include "bin/model/markerlistmodel.hpp"
#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "doc/kdenlivedoc.h"
#include "koracut/motion/koracutmotionmirror.h"
#include "koracut/motion/koracutmotionproject.h"
#include "koracut/motion/koracutmotionrenderer.h"
#include "koracut/motion/koracutmotionsync.h"
#include "koracut/koracuttimelinecolors.h"
#include "profiles/profilemodel.hpp"
#include "timeline2/model/timelinefunctions.hpp"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

// Les aides partagées avec les autres fichiers d'outils (koracutagenttools_p.h).
namespace KoraCutAgentToolsPrivate {

double projectFps()
{
    const double fps = pCore->getCurrentFps();
    return fps > 0 ? fps : 25.;
}

QJsonObject error(const QString &code, const QString &message, const QString &advice)
{
    QJsonObject e{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
    if (!advice.isEmpty()) {
        e.insert(QStringLiteral("conseil"), advice);
    }
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("erreur"), e}};
}

QJsonObject time(int frames)
{
    return QJsonObject{{QStringLiteral("i"), frames}, {QStringLiteral("tc"), KoraCutAgentTools::timecode(frames, projectFps())}};
}

// Paramètre de temps obligatoire : -1 et une erreur `entree_invalide` s'il manque ou ne se lit pas.
int requiredTime(const Args &args, const QString &key, QJsonObject &failure)
{
    const int value = KoraCutAgentTools::parseTime(args.value(key), projectFps());
    if (value < 0) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("« %1 » manque ou ne se lit pas.").arg(key),
                        QStringLiteral("Donne un nombre d'images ou un timecode HH:MM:SS:II."));
    }
    return value;
}

QString trackLabel(const Timeline &timeline, int trackId)
{
    return timeline->getTrackTagById(trackId);
}

bool trackMasked(const Timeline &timeline, int trackId)
{
    // MLT : 1 cache l'image, 2 coupe le son, 3 les deux. Une piste audio vaut 1 en temps normal, une
    // piste vidéo 2 ; masquée, les deux valent 3 (TimelineController::hideTrack).
    return timeline->getTrackProperty(trackId, QStringLiteral("hide")).toInt() == 3;
}

// Les pistes dans l'ordre de l'écran, de haut en bas : position MLT décroissante.
QList<int> tracksOnScreen(const Timeline &timeline)
{
    QList<int> tracks = timeline->getTracksIds(false) + timeline->getTracksIds(true);
    std::sort(tracks.begin(), tracks.end(), [&timeline](int a, int b) { return timeline->getTrackPosition(a) > timeline->getTrackPosition(b); });
    return tracks;
}

// Les clips d'une piste, triés par position.
QList<int> clipsOnTrack(const Timeline &timeline, int trackId)
{
    QList<int> clips;
    for (int id : timeline->getItemsInRange(trackId, 0, -1, false)) {
        if (timeline->isClip(id)) {
            clips << id;
        }
    }
    std::sort(clips.begin(), clips.end(), [&timeline](int a, int b) { return timeline->getClipPosition(a) < timeline->getClipPosition(b); });
    return clips;
}

// Numéro de groupe stable pour un tour : le plus petit id du groupe. 0 si le clip est seul.
int groupNumber(const Timeline &timeline, int clipId)
{
    if (!timeline->isInGroup(clipId)) {
        return 0;
    }
    const std::unordered_set<int> elements = timeline->getGroupElements(clipId);
    if (elements.size() < 2) {
        return 0;
    }
    return *std::min_element(elements.begin(), elements.end());
}

QJsonObject clipIdCheck(const Timeline &timeline, int clipId)
{
    if (!timeline->isClip(clipId)) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun clip n'a l'identifiant %1.").arg(clipId),
                     QStringLiteral("Relis etat_timeline : les identifiants changent après une coupe ou une annulation."));
    }
    const int trackId = timeline->getClipTrackId(clipId);
    if (trackId != -1 && timeline->trackIsLocked(trackId)) {
        return error(QStringLiteral("piste_verrouillee"), QStringLiteral("La piste %1 est verrouillée.").arg(trackLabel(timeline, trackId)),
                     QStringLiteral("Déverrouille-la avec regler_piste, ou demande à la personne."));
    }
    return {};
}

QJsonObject trackIdCheck(const Timeline &timeline, int trackId, bool mustBeUnlocked)
{
    if (!timeline->isTrack(trackId)) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucune piste n'a l'identifiant %1.").arg(trackId),
                     QStringLiteral("Relis etat_timeline (champ id des pistes)."));
    }
    if (mustBeUnlocked && timeline->trackIsLocked(trackId)) {
        return error(QStringLiteral("piste_verrouillee"), QStringLiteral("La piste %1 est verrouillée.").arg(trackLabel(timeline, trackId)),
                     QStringLiteral("Déverrouille-la avec regler_piste, ou demande à la personne."));
    }
    return {};
}

// Durée du fondu d'entrée ou de sortie d'un clip, 0 s'il n'y en a pas.
int currentFade(const Timeline &timeline, int clipId, bool fadeIn)
{
    return timeline->data(timeline->makeClipIndexFromID(clipId), fadeIn ? TimelineModel::FadeInRole : TimelineModel::FadeOutRole).toInt();
}

QString mediaTypeName(ClipType::ProducerType type)
{
    switch (type) {
    case ClipType::Audio:
        return QStringLiteral("audio");
    case ClipType::Image:
    case ClipType::SlideShow:
        return QStringLiteral("image");
    case ClipType::Text:
    case ClipType::TextTemplate:
    case ClipType::Animation:
        return QStringLiteral("titre");
    case ClipType::Color:
        return QStringLiteral("couleur");
    case ClipType::Timeline:
    case ClipType::Playlist:
        return QStringLiteral("sequence");
    default:
        return QStringLiteral("video");
    }
}

QString resolveUserPath(const QString &path, QJsonObject &failure)
{
    QString out = path.trimmed();
    if (out == QLatin1String("~") || out.startsWith(QLatin1String("~/"))) {
        return QDir::cleanPath(QDir::homePath() + out.mid(1));
    }
    if (QFileInfo(out).isAbsolute()) {
        return QDir::cleanPath(out);
    }
    // Relatif au dossier du projet, s'il est vraiment sur le disque (un projet neuf porte une URL sans fichier).
    KdenliveDoc *doc = pCore ? pCore->currentDoc() : nullptr;
    const QString file = doc && !doc->url().isEmpty() ? doc->url().toLocalFile() : QString();
    if (file.isEmpty() || !QFileInfo::exists(file)) {
        failure = error(QStringLiteral("projet_absent"), QStringLiteral("Le chemin « %1 » est relatif, mais le projet n'est pas encore enregistré.").arg(path),
                        QStringLiteral("Donne un chemin absolu, ou enregistre le projet d'abord."));
        return QString();
    }
    return QDir::cleanPath(QFileInfo(file).absolutePath() + QLatin1Char('/') + out);
}

QJsonObject copyMotionAssets(const Args &args, const QString &compositionDir, QJsonArray &copied, bool checkOnly)
{
    if (!args.contains(QStringLiteral("fichiers"))) {
        return {};
    }
    // Tout est vérifié avant la première copie : une liste fausse ne laisse rien à moitié.
    QList<QFileInfo> sources;
    QSet<QString> names;
    const QJsonArray wanted = args.value(QStringLiteral("fichiers")).toArray();
    for (const QJsonValue &v : wanted) {
        QJsonObject failure;
        const QString path = resolveUserPath(v.toString(), failure);
        if (path.isEmpty()) {
            return failure.isEmpty() ? error(QStringLiteral("entree_invalide"), QStringLiteral("fichiers contient un chemin vide.")) : failure;
        }
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile()) {
            return error(QStringLiteral("fichier_introuvable"), QStringLiteral("Le fichier %1 n'existe pas (ou c'est un dossier).").arg(path),
                         QStringLiteral("fichiers prend des fichiers : absolus, « ~ », ou relatifs au dossier du projet."));
        }
        if (names.contains(info.fileName().toLower())) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Deux fichiers s'appellent « %1 » : ils iraient au même endroit dans assets/.").arg(info.fileName()),
                         QStringLiteral("Renomme l'un des deux avant de le donner."));
        }
        names.insert(info.fileName().toLower());
        sources << info;
    }
    if (checkOnly) {
        return {};
    }
    const QString assets = compositionDir + QStringLiteral("/assets");
    if (!sources.isEmpty() && !QDir().mkpath(assets)) {
        return error(QStringLiteral("ecriture_impossible"), QStringLiteral("Le dossier %1 n'a pas pu être créé.").arg(assets));
    }
    for (const QFileInfo &info : std::as_const(sources)) {
        const QString target = assets + QLatin1Char('/') + info.fileName();
        if (info.absoluteFilePath() != QFileInfo(target).absoluteFilePath()) {
            QFile::remove(target);
            if (!QFile::copy(info.absoluteFilePath(), target)) {
                return error(QStringLiteral("ecriture_impossible"), QStringLiteral("Le fichier %1 n'a pas pu être copié dans %2.").arg(info.fileName(), assets));
            }
        }
        copied.append(QString(QStringLiteral("assets/") + info.fileName()));
    }
    return {};
}

bool releaseMotionName(KoraCutMotionMirror *mirror, const QString &name, QString &trashed)
{
    if (!mirror->projects().contains(name)) {
        return true;
    }
    if (!mirror->projectState(name).retired) {
        return false;
    }
    trashed = mirror->trashProject(name);
    return !trashed.isEmpty();
}

} // namespace KoraCutAgentToolsPrivate

namespace {

using namespace KoraCutAgentToolsPrivate;

// ---------------------------------------------------------------------------------------------
// etat_timeline (projet.json)

QJsonObject etatTimeline(const Timeline &timeline, const Args &args)
{
    const double fps = projectFps();
    const QString detail = args.value(QStringLiteral("detail")).toString(QStringLiteral("normal"));
    if (detail != QLatin1String("normal") && detail != QLatin1String("resume") && detail != QLatin1String("complet")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("detail vaut « resume », « normal » ou « complet »."));
    }
    const bool complete = detail == QLatin1String("complet");
    const bool resume = detail == QLatin1String("resume");
    const bool withTimecodes = args.value(QStringLiteral("avec_timecodes")).toBool(false);
    int rangeStart = 0;
    int rangeEnd = -1;
    if (args.contains(QStringLiteral("debut"))) {
        rangeStart = KoraCutAgentTools::parseTime(args.value(QStringLiteral("debut")), fps);
    }
    if (args.contains(QStringLiteral("fin"))) {
        rangeEnd = KoraCutAgentTools::parseTime(args.value(QStringLiteral("fin")), fps);
    }
    if (rangeStart < 0 || (args.contains(QStringLiteral("fin")) && rangeEnd < rangeStart)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("La plage debut / fin ne se lit pas."));
    }
    QSet<int> onlyTracks;
    for (const QJsonValue &v : args.value(QStringLiteral("piste_ids")).toArray()) {
        onlyTracks.insert(v.toInt());
    }

    QJsonObject projet;
    if (pCore->currentDoc()) {
        projet.insert(QStringLiteral("nom"), QFileInfo(pCore->currentDoc()->url().toLocalFile()).fileName());
    }
    if (const auto &profile = pCore->getCurrentProfile()) {
        projet.insert(QStringLiteral("l"), profile->width());
        projet.insert(QStringLiteral("h"), profile->height());
    }
    projet.insert(QStringLiteral("ips"), fps);
    const int duration = timeline->duration();
    projet.insert(QStringLiteral("dur"), duration);
    projet.insert(QStringLiteral("dur_tc"), KoraCutAgentTools::timecode(duration, fps));
    QJsonArray selection;
    for (int id : timeline->getCurrentSelection()) {
        selection.append(id);
    }
    if (!selection.isEmpty()) {
        projet.insert(QStringLiteral("sel"), selection);
    }

    QJsonArray pistes;
    const QList<int> tracks = tracksOnScreen(timeline);
    for (int rank = 0; rank < tracks.size(); ++rank) {
        const int tid = tracks.at(rank);
        if (!onlyTracks.isEmpty() && !onlyTracks.contains(tid)) {
            continue;
        }
        const bool audio = timeline->isAudioTrack(tid);
        QJsonObject piste{{QStringLiteral("id"), tid},
                          {QStringLiteral("n"), trackLabel(timeline, tid)},
                          {QStringLiteral("t"), audio ? QStringLiteral("a") : QStringLiteral("v")},
                          {QStringLiteral("rang"), rank}};
        const QString name = timeline->getTrackProperty(tid, QStringLiteral("kdenlive:track_name")).toString();
        if (!name.isEmpty()) {
            piste.insert(QStringLiteral("nom"), name);
        }
        const int mirror = audio ? timeline->getMirrorVideoTrackId(tid) : timeline->getMirrorAudioTrackId(tid);
        if (mirror != -1) {
            piste.insert(QStringLiteral("av"), mirror);
        }
        if (timeline->trackIsLocked(tid)) {
            piste.insert(QStringLiteral("verr"), true);
        }
        if (trackMasked(timeline, tid)) {
            piste.insert(QStringLiteral("masq"), true);
        }
        // La couleur choisie pour la piste (document 13) : un nom de la palette ou « #RRGGBB ».
        if (const QString coul = timeline->getTrackProperty(tid, QStringLiteral("kdenlive:koracut_color")).toString(); !coul.isEmpty()) {
            piste.insert(QStringLiteral("coul"), KoraCutTimelineColors::nameOf(coul));
        }
        // Les effets posés sur la piste entière (lot A, 24 septembre 2026).
        if (const QJsonArray trackFx = effectsJson(timeline->getTrackEffectStackModel(tid), complete, 0); !trackFx.isEmpty()) {
            piste.insert(QStringLiteral("fx"), trackFx);
        }
        const QList<int> clips = clipsOnTrack(timeline, tid);
        if (resume) {
            int last = -1;
            for (int cid : clips) {
                last = std::max(last, timeline->getClipPosition(cid) + timeline->getClipPlaytime(cid) - 1);
            }
            piste.insert(QStringLiteral("nb"), clips.size());
            if (last >= 0) {
                piste.insert(QStringLiteral("fin"), last);
            }
        } else {
            QJsonArray list;
            for (int cid : clips) {
                const int pos = timeline->getClipPosition(cid);
                const int dur = timeline->getClipPlaytime(cid);
                if (pos + dur - 1 < rangeStart || (rangeEnd >= 0 && pos > rangeEnd)) {
                    continue;
                }
                const std::pair<int, int> inOut = timeline->getClipInOut(cid);
                QJsonObject clip{{QStringLiteral("id"), cid},
                                 {QStringLiteral("m"), timeline->getClipBinId(cid)},
                                 {QStringLiteral("nom"), timeline->getClipName(cid)},
                                 {QStringLiteral("pos"), pos},
                                 {QStringLiteral("in"), inOut.first},
                                 // Kdenlive : out inclus. Outils : out exclu.
                                 {QStringLiteral("out"), inOut.second + 1},
                                 {QStringLiteral("dur"), dur}};
                const ClipType::ProducerType type = timeline->getClipState(cid).second;
                QString kind = audio ? QStringLiteral("a") : QStringLiteral("v");
                if (type == ClipType::Image) {
                    kind = QStringLiteral("i");
                } else if (type == ClipType::Text || type == ClipType::TextTemplate || type == ClipType::Animation) {
                    kind = QStringLiteral("t");
                } else if (type == ClipType::Color) {
                    kind = QStringLiteral("c");
                } else if (type == ClipType::Timeline) {
                    kind = QStringLiteral("s");
                }
                clip.insert(QStringLiteral("k"), kind);
                if (withTimecodes) {
                    clip.insert(QStringLiteral("pos_tc"), KoraCutAgentTools::timecode(pos, fps));
                }
                if (const int grp = groupNumber(timeline, cid)) {
                    clip.insert(QStringLiteral("grp"), grp);
                }
                const int fadeIn = currentFade(timeline, cid, true);
                const int fadeOut = currentFade(timeline, cid, false);
                if (fadeIn > 0 || fadeOut > 0) {
                    clip.insert(QStringLiteral("fondu"), QJsonArray{fadeIn, fadeOut});
                }
                const double speed = timeline->getClipSpeed(cid);
                if (!qFuzzyCompare(speed, 1.)) {
                    clip.insert(QStringLiteral("vit"), qRound(speed * 100));
                }
                // Le texte d'un titre, coupé à 60 caractères.
                if (kind == QLatin1String("t")) {
                    if (const auto binClip = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(cid))) {
                        const QString text = binClip->getProducerProperty(QStringLiteral("kdenlive:koracut_titre_texte"));
                        if (!text.isEmpty()) {
                            clip.insert(QStringLiteral("txt"), text.left(60));
                        }
                    }
                }
                // Le fondu enchaîné avec le clip précédent (lot A).
                if (const int mix = timeline->getMixDuration(cid); mix > 0) {
                    clip.insert(QStringLiteral("mix"), mix);
                }
                // Les effets, sans les fondus (lot A) ; `i` est le rang à passer à effet_index.
                const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(cid);
                if (const QJsonArray fx = effectsJson(stack, complete, inOut.first); !fx.isEmpty()) {
                    clip.insert(QStringLiteral("fx"), fx);
                }
                if (stack && !stack->isStackEnabled()) {
                    clip.insert(QStringLiteral("fx_off"), true);
                }
                if (const QString coul = TimelineFunctions::koraCutClipColor(timeline, cid); !coul.isEmpty()) {
                    clip.insert(QStringLiteral("coul"), KoraCutTimelineColors::nameOf(coul));
                }
                list.append(clip);
            }
            piste.insert(QStringLiteral("clips"), list);
        }
        pistes.append(piste);
    }
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("v"), 1}, {QStringLiteral("projet"), projet}, {QStringLiteral("pistes"), pistes}};
    // Les effets de la sortie générale (piste_id -1 dans les outils d'effets).
    if (const QJsonArray master = effectsJson(timeline->getMasterEffectStackModel(), complete, 0); !master.isEmpty()) {
        result.insert(QStringLiteral("master_fx"), master);
    }
    // Les guides de la timeline, dans la plage demandée.
    if (!resume) {
        QJsonArray markers;
        if (const auto guides = timeline->getGuideModel()) {
            const QList<CommentedTime> all = guides->getAllMarkers();
            for (const CommentedTime &m : all) {
                const int frame = m.time().frames(fps);
                if (frame < rangeStart || (rangeEnd >= 0 && frame > rangeEnd)) {
                    continue;
                }
                markers.append(QJsonObject{{QStringLiteral("i"), frame}, {QStringLiteral("txt"), m.comment()}, {QStringLiteral("cat"), m.markerType()}});
            }
        }
        if (!markers.isEmpty()) {
            result.insert(QStringLiteral("marqueurs"), markers);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// Montage (montage.json)

QJsonObject couperClip(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    QJsonObject failure;
    const int position = requiredTime(args, QStringLiteral("position"), failure);
    if (position < 0) {
        return failure;
    }
    const int start = timeline->getClipPosition(cid);
    const int end = start + timeline->getClipPlaytime(cid);
    if (position <= start || position >= end) {
        return error(QStringLiteral("hors_clip"), QStringLiteral("Le clip %1 va de %2 à %3 : %4 n'est pas à l'intérieur.").arg(cid).arg(start).arg(end - 1).arg(position),
                     QStringLiteral("Coupe strictement entre la première et la dernière image du clip."));
    }
    const int tid = timeline->getClipTrackId(cid);
    if (!TimelineFunctions::requestClipCut(timeline, cid, position)) {
        return error(QStringLiteral("hors_clip"), QStringLiteral("La coupe du clip %1 a échoué.").arg(cid));
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("clip_gauche_id"), cid},
                       {QStringLiteral("clip_droit_id"), timeline->getClipByPosition(tid, position)},
                       {QStringLiteral("position"), time(position)}};
}

QJsonObject couperToutesPistes(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    const int position = requiredTime(args, QStringLiteral("position"), failure);
    if (position < 0) {
        return failure;
    }
    // Les clips qui seront coupés : ceux qui chevauchent la position sur une piste non verrouillée.
    QList<std::pair<int, int>> spanning;
    for (int tid : tracksOnScreen(timeline)) {
        if (timeline->trackIsLocked(tid)) {
            continue;
        }
        const int cid = timeline->getClipByPosition(tid, position);
        if (cid != -1 && timeline->getClipPosition(cid) < position) {
            spanning << std::make_pair(cid, tid);
        }
    }
    if (spanning.isEmpty()) {
        return error(QStringLiteral("hors_clip"), QStringLiteral("Aucun clip ne passe par %1 sur une piste non verrouillée.").arg(position));
    }
    if (!TimelineFunctions::requestClipCutAll(timeline, position)) {
        return error(QStringLiteral("hors_clip"), QStringLiteral("La coupe à %1 a échoué.").arg(position));
    }
    QJsonArray cuts;
    for (const auto &[cid, tid] : std::as_const(spanning)) {
        cuts.append(QJsonObject{{QStringLiteral("clip_gauche_id"), cid},
                                {QStringLiteral("clip_droit_id"), timeline->getClipByPosition(tid, position)},
                                {QStringLiteral("piste_id"), tid}});
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("position"), time(position)}, {QStringLiteral("clips_coupes"), cuts}};
}

QJsonObject deplacerClip(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    const int tid = args.value(QStringLiteral("piste_id")).toInt(-1);
    if (QJsonObject e = trackIdCheck(timeline, tid); !e.isEmpty()) {
        return e;
    }
    QJsonObject failure;
    const int position = requiredTime(args, QStringLiteral("position"), failure);
    if (position < 0) {
        return failure;
    }
    if (timeline->isAudioTrack(tid) != timeline->isAudioTrack(timeline->getClipTrackId(cid))) {
        return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("Un clip ne passe pas d'une piste vidéo à une piste audio, ni l'inverse."));
    }
    if (args.value(QStringLiteral("seul")).toBool(false) && timeline->isInGroup(cid)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Déplacer un clip sans son groupe n'est pas encore écrit."),
                     QStringLiteral("Déplace le groupe entier, ou demande à la personne de dégrouper."));
    }
    if (!timeline->requestClipMove(cid, tid, position, true, true, true)) {
        return error(QStringLiteral("place_occupee"), QStringLiteral("La place est prise sur la piste %1 à %2, ou le groupe ne peut pas suivre.").arg(trackLabel(timeline, tid)).arg(position),
                     QStringLiteral("Choisis une place libre (etat_timeline) ou une autre piste."));
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("clip_id"), cid},
                       {QStringLiteral("piste_id"), timeline->getClipTrackId(cid)},
                       {QStringLiteral("position"), time(timeline->getClipPosition(cid))}};
}

QJsonObject supprimerClip(const Timeline &timeline, const Args &args)
{
    const QJsonArray ids = args.value(QStringLiteral("clip_ids")).toArray();
    if (ids.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("clip_ids est vide."));
    }
    const bool close = args.value(QStringLiteral("refermer")).toBool(false);
    const bool keepGroup = args.value(QStringLiteral("garder_groupe")).toBool(false);
    // Tout vérifier avant de toucher à quoi que ce soit : un lot à moitié supprimé est pire qu'un refus.
    std::set<int> toDelete;
    for (const QJsonValue &v : ids) {
        const int cid = v.toInt(-1);
        if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
            return e;
        }
        if (keepGroup && timeline->isInGroup(cid)) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("Retirer un clip sans son groupe n'est pas encore écrit."),
                         QStringLiteral("Retire le groupe entier, ou demande à la personne de dégrouper."));
        }
        if (timeline->isInGroup(cid)) {
            for (int partner : timeline->getGroupElements(cid)) {
                toDelete.insert(partner);
            }
        } else {
            toDelete.insert(cid);
        }
    }
    // Les places libérées, pour refermer ensuite de droite à gauche (fermer à gauche décalerait la droite).
    QList<std::pair<int, int>> holes; // position, piste
    for (int cid : toDelete) {
        if (timeline->isClip(cid)) {
            holes << std::make_pair(timeline->getClipPosition(cid), timeline->getClipTrackId(cid));
        }
    }
    // duree_retiree : la somme des plages libérées, fusionnées quand elles se touchent (l'image et le
    // son d'un même plan comptent une fois), que le vide soit refermé ou non. C'est la règle du banc.
    QList<std::pair<int, int>> ranges; // début, fin exclue
    for (int cid : toDelete) {
        if (timeline->isClip(cid)) {
            const int start = timeline->getClipPosition(cid);
            ranges << std::make_pair(start, start + timeline->getClipPlaytime(cid));
        }
    }
    std::sort(ranges.begin(), ranges.end());
    int removedDuration = 0;
    std::pair<int, int> current{-1, -1};
    for (const auto &r : std::as_const(ranges)) {
        if (current.first == -1 || r.first > current.second) {
            if (current.first != -1) {
                removedDuration += current.second - current.first;
            }
            current = r;
        } else {
            current.second = std::max(current.second, r.second);
        }
    }
    if (current.first != -1) {
        removedDuration += current.second - current.first;
    }
    QJsonArray deleted;
    std::set<int> done;
    for (int cid : toDelete) {
        if (done.count(cid) || !timeline->isClip(cid)) {
            continue;
        }
        if (timeline->isInGroup(cid)) {
            for (int partner : timeline->getGroupElements(cid)) {
                done.insert(partner);
            }
        }
        done.insert(cid);
        if (!timeline->requestItemDeletion(cid, true)) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Le clip %1 n'a pas pu être retiré.").arg(cid));
        }
    }
    for (int cid : toDelete) {
        deleted.append(cid);
    }
    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("supprimes"), deleted},
                       {QStringLiteral("duree_retiree"), time(removedDuration)},
                       {QStringLiteral("referme"), close}};
    if (close) {
        std::sort(holes.begin(), holes.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
        std::set<int> closedPositions;
        int closedDuration = 0;
        bool somethingFollowed = false;
        for (const auto &[position, tid] : std::as_const(holes)) {
            if (!closedPositions.insert(position).second) {
                continue;
            }
            if (timeline->getClipByPosition(tid, position) == -1 && !clipsOnTrack(timeline, tid).isEmpty() &&
                timeline->getClipPosition(clipsOnTrack(timeline, tid).last()) > position) {
                somethingFollowed = true;
            }
            const int before = timeline->duration();
            if (TimelineFunctions::requestDeleteBlankAt(timeline, tid, position, true)) {
                closedDuration += std::max(0, before - timeline->duration());
            }
        }
        if (closedDuration == 0 && somethingFollowed) {
            result.insert(QStringLiteral("remarque"), QStringLiteral("Le vide n'a pas pu être refermé : une autre piste a quelque chose à cet endroit."));
        }
    }
    return result;
}

QJsonObject rognerClip(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    const QString side = args.value(QStringLiteral("cote")).toString();
    if (side != QLatin1String("debut") && side != QLatin1String("fin")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("cote vaut « debut » ou « fin »."));
    }
    QJsonObject failure;
    const int duration = requiredTime(args, QStringLiteral("duree"), failure);
    if (duration < 1) {
        return failure.isEmpty() ? error(QStringLiteral("entree_invalide"), QStringLiteral("La durée doit faire au moins une image.")) : failure;
    }
    if (args.value(QStringLiteral("refermer")).toBool(false)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le rognage en ripple n'est pas encore écrit."),
                     QStringLiteral("Rogne sans refermer, puis retire le vide avec supprimer_espace."));
    }
    const bool alone = args.value(QStringLiteral("seul")).toBool(false);
    const int result = timeline->requestItemResize(cid, duration, side == QLatin1String("fin"), true, -1, alone);
    if (result != duration) {
        return error(QStringLiteral("hors_media"), QStringLiteral("Le clip %1 ne peut pas faire %2 images de ce côté.").arg(cid).arg(duration),
                     QStringLiteral("On ne peut pas allonger un clip au-delà de son média, ni par-dessus un clip voisin."));
    }
    const std::pair<int, int> inOut = timeline->getClipInOut(cid);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("clip_id"), cid},
                       {QStringLiteral("position"), time(timeline->getClipPosition(cid))},
                       {QStringLiteral("duree"), time(timeline->getClipPlaytime(cid))},
                       {QStringLiteral("entree"), time(inOut.first)},
                       {QStringLiteral("sortie"), time(inOut.second + 1)}};
}

QJsonObject ajouterPiste(const Timeline &timeline, const Args &args)
{
    const QString type = args.value(QStringLiteral("type")).toString();
    if (type != QLatin1String("video") && type != QLatin1String("audio")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("type vaut « video » ou « audio »."));
    }
    const bool audio = type == QLatin1String("audio");
    const QString name = args.value(QStringLiteral("nom")).toString();
    const int rank = args.value(QStringLiteral("rang")).toInt(-1);
    const QList<int> tracks = tracksOnScreen(timeline);
    int position;
    if (rank < 0) {
        // Défaut : au-dessus des pistes vidéo, ou en dessous des pistes audio.
        position = audio ? 0 : tracks.size();
    } else if (rank >= tracks.size()) {
        position = 0;
    } else {
        // Posée au rang demandé : juste au-dessus de la piste qui l'occupe aujourd'hui.
        position = timeline->getTrackPosition(tracks.at(rank)) + 1;
    }
    int newId = -1;
    if (!timeline->requestTrackInsertion(position, newId, name, audio)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("La piste n'a pas pu être ajoutée à ce rang."),
                     QStringLiteral("Une piste vidéo se pose au-dessus des pistes audio : essaie rang -1."));
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("piste_id"), newId},
                       {QStringLiteral("nom"), name.isEmpty() ? trackLabel(timeline, newId) : name},
                       {QStringLiteral("type"), type},
                       {QStringLiteral("rang"), tracksOnScreen(timeline).indexOf(newId)}};
}

// supprimer_piste (11 octobre 2026) : seulement une piste vide. Retirer des clips reste l'affaire de
// supprimer_clip, qui demande l'accord de la personne ; cet outil ne doit pas devenir un moyen de s'en passer.
QJsonObject supprimerPiste(const Timeline &timeline, const Args &args)
{
    const int tid = args.value(QStringLiteral("piste_id")).toInt(-1);
    if (QJsonObject e = trackIdCheck(timeline, tid, true); !e.isEmpty()) {
        return e;
    }
    const int clips = timeline->getTrackClipsCount(tid);
    const int compositions = timeline->getTrackCompositionsCount(tid);
    if (clips > 0 || compositions > 0) {
        QJsonObject e = error(QStringLiteral("place_occupee"),
                              QStringLiteral("La piste %1 n'est pas vide : %2 clip(s), %3 composition(s).").arg(trackLabel(timeline, tid)).arg(clips).arg(compositions),
                              QStringLiteral("Déplace-les (deplacer_clip) ou retire-les (supprimer_clip), puis rappelle supprimer_piste."));
        e.insert(QStringLiteral("clips"), clips);
        e.insert(QStringLiteral("compositions"), compositions);
        return e;
    }
    if (timeline->getTracksCount() < 2) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("La dernière piste de la timeline ne se retire pas."));
    }
    // Le nom donné à la piste s'il y en a un (« Musique »), son étiquette sinon (« A3 »), comme ajouter_piste.
    const QString given = timeline->getTrackProperty(tid, QStringLiteral("kdenlive:track_name")).toString();
    const QString name = given.isEmpty() ? trackLabel(timeline, tid) : given;
    const bool audio = timeline->isAudioTrack(tid);
    if (!timeline->requestTrackDeletion(tid)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("La piste %1 n'a pas pu être retirée.").arg(name));
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("piste_id"), tid},
                       {QStringLiteral("nom"), name},
                       {QStringLiteral("type"), audio ? QStringLiteral("audio") : QStringLiteral("video")},
                       {QStringLiteral("pistes_restantes"), timeline->getTracksCount()}};
}

QJsonObject reglerPiste(const Timeline &timeline, const Args &args)
{
    const int tid = args.value(QStringLiteral("piste_id")).toInt(-1);
    if (QJsonObject e = trackIdCheck(timeline, tid, false); !e.isEmpty()) {
        return e;
    }
    if (args.contains(QStringLiteral("nom"))) {
        timeline->setTrackName(tid, args.value(QStringLiteral("nom")).toString());
    }
    if (args.contains(QStringLiteral("masquee"))) {
        const bool audio = timeline->isAudioTrack(tid);
        const QString before = timeline->getTrackProperty(tid, QStringLiteral("hide")).toString();
        const QString after = args.value(QStringLiteral("masquee")).toBool() ? QStringLiteral("3") : (audio ? QStringLiteral("1") : QStringLiteral("2"));
        std::weak_ptr<TimelineItemModel> weak = timeline;
        Fun redo = [weak, tid, after]() {
            if (auto t = weak.lock()) {
                t->setTrackProperty(tid, QStringLiteral("hide"), after);
            }
            return true;
        };
        Fun undo = [weak, tid, before]() {
            if (auto t = weak.lock()) {
                t->setTrackProperty(tid, QStringLiteral("hide"), before);
            }
            return true;
        };
        redo();
        pCore->pushUndo(undo, redo, QStringLiteral("regler_piste"));
    }
    if (args.contains(QStringLiteral("verrouillee"))) {
        timeline->setTrackLockedState(tid, args.value(QStringLiteral("verrouillee")).toBool());
    }
    const QString name = timeline->getTrackProperty(tid, QStringLiteral("kdenlive:track_name")).toString();
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("piste_id"), tid},
                       {QStringLiteral("nom"), name.isEmpty() ? trackLabel(timeline, tid) : name},
                       {QStringLiteral("verrouillee"), timeline->trackIsLocked(tid)},
                       {QStringLiteral("masquee"), trackMasked(timeline, tid)}};
}

QJsonObject ajouterClip(const Timeline &timeline, const Args &args)
{
    const QString mediaId = args.value(QStringLiteral("media_id")).toString();
    if (mediaId.isEmpty() || !pCore->projectItemModel()->hasClip(mediaId)) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun média n'a l'identifiant « %1 » dans le chutier.").arg(mediaId),
                     QStringLiteral("Relis lister_medias (champ id)."));
    }
    const int tid = args.value(QStringLiteral("piste_id")).toInt(-1);
    if (QJsonObject e = trackIdCheck(timeline, tid); !e.isEmpty()) {
        return e;
    }
    QJsonObject failure;
    const int position = requiredTime(args, QStringLiteral("position"), failure);
    if (position < 0) {
        return failure;
    }
    const QString mode = args.value(QStringLiteral("mode")).toString(QStringLiteral("libre"));
    const bool freeTrack = mode == QLatin1String("piste_libre");
    if (mode != QLatin1String("libre") && !freeTrack && mode != QLatin1String("ecraser") && mode != QLatin1String("inserer")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("mode « %1 » inconnu.").arg(mode),
                     QStringLiteral("mode vaut « libre » ou « piste_libre » (« ecraser » et « inserer » ne sont pas encore écrits)."));
    }
    if (mode != QLatin1String("libre") && !freeTrack) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Les modes « ecraser » et « inserer » ne sont pas encore écrits."),
                     QStringLiteral("Pose le clip en mode libre à une place vide, ou en mode piste_libre pour une seule partie."));
    }
    const QString part = args.value(QStringLiteral("partie")).toString(QStringLiteral("video_et_audio"));
    QString binId = mediaId;
    if (part == QLatin1String("audio")) {
        binId.prepend(QLatin1Char('A'));
    } else if (part == QLatin1String("video")) {
        binId.prepend(QLatin1Char('V'));
    } else if (part != QLatin1String("video_et_audio")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("partie vaut « video_et_audio », « video » ou « audio »."));
    }
    if (args.contains(QStringLiteral("entree")) || args.contains(QStringLiteral("sortie"))) {
        // Une image, un titre ou une couleur n'ont pas de fin.
        const std::shared_ptr<ProjectClip> media = pCore->projectItemModel()->getClipByBinID(mediaId);
        const int mediaLength = media ? int(media->frameDuration()) : 0;
        const bool limited = media && media->hasLimitedDuration() && mediaLength > 0;
        // L'un sans l'autre (11 octobre 2026) : sortie seule part du début du média, entree seule va jusqu'à sa fin.
        const int in = args.contains(QStringLiteral("entree")) ? KoraCutAgentTools::parseTime(args.value(QStringLiteral("entree")), projectFps()) : 0;
        if (!args.contains(QStringLiteral("sortie")) && !limited) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Ce média n'a pas de fin : entree seule ne dit pas où s'arrêter."),
                         QStringLiteral("Donne aussi sortie (sortie - entree = durée du clip)."));
        }
        const int out = args.contains(QStringLiteral("sortie")) ? KoraCutAgentTools::parseTime(args.value(QStringLiteral("sortie")), projectFps()) : mediaLength;
        if (in < 0 || out <= in) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("sortie (%1) doit être strictement après entree (%2).").arg(out).arg(in),
                         QStringLiteral("sortie est la première image qui n'est pas posée : sortie - entree = durée du clip."));
        }
        // Au-delà de la fin du média : le dire avec sa durée (avant le 1er octobre 2026, la pose échouait
        // et répondait place_occupee, un message faux).
        if (limited && out > mediaLength) {
            QJsonObject e = error(QStringLiteral("hors_media"),
                                  QStringLiteral("Le média %1 dure %2 images (%3) : sortie = %4 dépasse sa fin.")
                                      .arg(mediaId)
                                      .arg(mediaLength)
                                      .arg(KoraCutAgentTools::timecode(mediaLength, projectFps()))
                                      .arg(out),
                                  QStringLiteral("Donne une sortie de %1 au plus (sortie est exclue : %1 prend le média jusqu'à sa dernière image).").arg(mediaLength));
            e.insert(QStringLiteral("duree_media"), time(mediaLength));
            return e;
        }
        // Kdenlive : « id/entrée/sortie », sortie incluse.
        binId += QStringLiteral("/%1/%2").arg(in).arg(out - 1);
    }
    const bool audioTrack = timeline->isAudioTrack(tid);
    if ((part == QLatin1String("audio") && !audioTrack) || (part == QLatin1String("video") && audioTrack)) {
        return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("La partie « %1 » ne se pose pas sur la piste %2.").arg(part, trackLabel(timeline, tid)));
    }
    int newId = -1;
    bool trackCreated = false;
    if (freeTrack) {
        // piste_libre (12 octobre 2026) : deux effets sonores qui se chevauchent ne se posent pas sur la même piste.
        // On essaie piste_id, puis les autres pistes du même type dans l'ordre de l'écran, puis on en crée une.
        // Une seule partie : avec image et son, il faudrait deux pistes libres à la fois.
        if (part == QLatin1String("video_et_audio")) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Le mode piste_libre pose une seule partie."),
                         QStringLiteral("Donne partie « audio » ou « video »."));
        }
        QList<int> candidates{tid};
        for (int other : tracksOnScreen(timeline)) {
            if (other != tid && timeline->isAudioTrack(other) == audioTrack && !timeline->trackIsLocked(other)) {
                candidates << other;
            }
        }
        Fun undo = []() { return true; };
        Fun redo = []() { return true; };
        for (int candidate : std::as_const(candidates)) {
            Fun tryUndo = []() { return true; };
            Fun tryRedo = []() { return true; };
            int id = -1;
            if (timeline->requestClipInsertion(binId, candidate, position, id, true, true, false, tryUndo, tryRedo) && id != -1) {
                undo = tryUndo;
                redo = tryRedo;
                newId = id;
                break;
            }
        }
        if (newId == -1) {
            int newTrack = -1;
            const int trackPosition = audioTrack ? 0 : tracksOnScreen(timeline).size();
            if (!timeline->requestTrackInsertion(trackPosition, newTrack, QString(), audioTrack, undo, redo) || newTrack == -1 ||
                !timeline->requestClipInsertion(binId, newTrack, position, newId, true, true, false, undo, redo) || newId == -1) {
                undo();
                return error(QStringLiteral("place_occupee"), QStringLiteral("Aucune piste n'a pu recevoir le clip à %1, même une piste neuve.").arg(position));
            }
            trackCreated = true;
        }
        pCore->pushUndo(undo, redo, i18nc("@action undo text", "Insert clip on a free track"));
    } else if (!timeline->requestClipInsertion(binId, tid, position, newId, true, true, false) || newId == -1) {
        return error(QStringLiteral("place_occupee"), QStringLiteral("Pas de place sur la piste %1 à %2 (ou sur sa piste associée pour le son).").arg(trackLabel(timeline, tid)).arg(position),
                     QStringLiteral("Choisis une place libre, une autre piste, partie « video », ou mode piste_libre pour une seule partie."));
    }
    QJsonValue audioId = QJsonValue::Null;
    if (timeline->isInGroup(newId)) {
        for (int partner : timeline->getGroupElements(newId)) {
            if (partner != newId && timeline->isClip(partner)) {
                audioId = partner;
            }
        }
    }
    QJsonObject placed{{QStringLiteral("ok"), true},
                       {QStringLiteral("clip_id"), newId},
                       {QStringLiteral("clip_audio_id"), audioId},
                       {QStringLiteral("piste_id"), timeline->getClipTrackId(newId)},
                       {QStringLiteral("position"), time(timeline->getClipPosition(newId))},
                       {QStringLiteral("duree"), time(timeline->getClipPlaytime(newId))}};
    if (freeTrack) {
        placed.insert(QStringLiteral("piste_creee"), trackCreated);
    }
    return placed;
}

// ---------------------------------------------------------------------------------------------
// Lot 2 (23 septembre 2026) : lister_medias (projet.json), supprimer_espace, changer_vitesse_clip

// Pour chercher sans se soucier des accents ni des majuscules : « Séquence » trouve « sequence ».
QString plain(const QString &text)
{
    QString out;
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    for (const QChar c : decomposed) {
        if (c.category() != QChar::Mark_NonSpacing) {
            out.append(c.toLower());
        }
    }
    return out;
}

QJsonObject listerMedias(const Timeline &, const Args &args)
{
    const QString search = plain(args.value(QStringLiteral("recherche")).toString());
    const QString wantedType = args.value(QStringLiteral("type")).toString(QStringLiteral("tous"));
    const QString wantedFolder = plain(args.value(QStringLiteral("dossier")).toString());
    const int limit = std::clamp(args.value(QStringLiteral("limite")).toInt(50), 1, 200);
    // La suite d'une liste coupée : un gros chutier ne tient pas en une page (les plans Motion de la V3
    // d'AFEBABA, ids 64 à 71, tombaient au-delà des 50 premiers sans que la réponse le dise).
    const int from = std::max(0, args.value(QStringLiteral("debut")).toInt(0));
    QJsonArray medias;
    int total = 0;
    std::vector<QString> ids = pCore->projectItemModel()->getAllClipIds();
    std::sort(ids.begin(), ids.end(), [](const QString &a, const QString &b) { return a.toInt() < b.toInt(); });
    for (const QString &id : ids) {
        const std::shared_ptr<ProjectClip> clip = pCore->projectItemModel()->getClipByBinID(id);
        if (!clip) {
            continue;
        }
        const QString type = mediaTypeName(clip->clipType());
        // Le chemin complet du dossier (« Script 01/01 Rushs ») : quatre dossiers « 01 Rushs » rangés dans
        // quatre dossiers différents ne se distinguaient pas par leur seul dernier nom (1er octobre 2026).
        const QString folder = KoraCutAgentTools::binFolderPath(id);
        const QString folderName = folder.section(QLatin1Char('/'), -1);
        const QString tags = clip->tags();
        if (wantedType != QLatin1String("tous") && type != wantedType) {
            continue;
        }
        // Le filtre accepte le chemin complet ou le dernier nom (comme avant).
        if (!wantedFolder.isEmpty() && plain(folder) != wantedFolder && plain(folderName) != wantedFolder) {
            continue;
        }
        if (!search.isEmpty() && !plain(clip->name()).contains(search) && !plain(folder).contains(search) && !plain(tags).contains(search)) {
            continue;
        }
        ++total;
        if (total <= from || medias.size() >= limit) {
            continue;
        }
        QJsonObject media{{QStringLiteral("id"), id}, {QStringLiteral("nom"), clip->name()}, {QStringLiteral("type"), type},
                          {QStringLiteral("duree"), time(int(clip->frameDuration()))}, {QStringLiteral("a_du_son"), clip->hasAudio()},
                          {QStringLiteral("proxy"), clip->hasProxy() ? QStringLiteral("pret") : QStringLiteral("aucun")},
                          {QStringLiteral("utilise"), int(clip->timelineInstances().size())}};
        if (!folder.isEmpty()) {
            media.insert(QStringLiteral("dossier"), folder);
            if (folderName != folder) {
                media.insert(QStringLiteral("dossier_nom"), folderName);
            }
        }
        const QSize size = clip->frameSize();
        if (size.isValid() && type != QLatin1String("audio")) {
            media.insert(QStringLiteral("l"), size.width());
            media.insert(QStringLiteral("h"), size.height());
        }
        if (type == QLatin1String("video")) {
            media.insert(QStringLiteral("ips"), clip->getOriginalFps());
        }
        if (clip->clipStatus() == FileStatus::StatusMissing) {
            media.insert(QStringLiteral("manquant"), true);
        }
        if (!tags.isEmpty()) {
            media.insert(QStringLiteral("etiquettes"), QJsonArray::fromStringList(tags.split(QLatin1Char(','), Qt::SkipEmptyParts)));
        }
        medias.append(media);
    }
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("medias"), medias}, {QStringLiteral("total"), total}};
    const int next = from + int(medias.size());
    if (next < total) {
        result.insert(QStringLiteral("tronque"), true);
        result.insert(QStringLiteral("suite"), next);
        result.insert(QStringLiteral("conseil"), QStringLiteral("La liste est coupée : %1 médias sur %2. Relance avec debut = %3 pour la suite, ou affine avec recherche, type ou dossier.")
                                                    .arg(medias.size())
                                                    .arg(total)
                                                    .arg(next));
    }
    return result;
}

QJsonObject supprimerEspace(const Timeline &timeline, const Args &args)
{
    const int tid = args.value(QStringLiteral("piste_id")).toInt(-1);
    if (QJsonObject e = trackIdCheck(timeline, tid); !e.isEmpty()) {
        return e;
    }
    QJsonObject failure;
    const int position = requiredTime(args, QStringLiteral("position"), failure);
    if (position < 0) {
        return failure;
    }
    if (timeline->getClipByPosition(tid, position) != -1) {
        return error(QStringLiteral("pas_de_vide"), QStringLiteral("Il y a un clip sur la piste %1 à %2, pas un vide.").arg(trackLabel(timeline, tid)).arg(position),
                     QStringLiteral("Donne une position à l'intérieur du vide (etat_timeline)."));
    }
    // La durée refermée : la place du premier clip qui suit, avant et après.
    int next = -1;
    for (int cid : clipsOnTrack(timeline, tid)) {
        if (timeline->getClipPosition(cid) > position) {
            next = cid;
            break;
        }
    }
    if (next == -1) {
        return error(QStringLiteral("pas_de_vide"), QStringLiteral("Rien ne suit la position %1 sur la piste %2 : il n'y a rien à rapprocher.").arg(position).arg(trackLabel(timeline, tid)));
    }
    const int before = timeline->getClipPosition(next);
    if (!TimelineFunctions::requestDeleteBlankAt(timeline, tid, position, args.value(QStringLiteral("toutes_pistes")).toBool(false))) {
        return error(QStringLiteral("pas_de_vide"), QStringLiteral("Le vide n'a pas pu être refermé à %1.").arg(position),
                     QStringLiteral("Avec toutes_pistes, chaque piste doit avoir un vide au moins aussi grand à cet endroit."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("duree_refermee"), time(before - timeline->getClipPosition(next))}};
}

QJsonObject changerVitesseClip(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    const double percent = args.value(QStringLiteral("vitesse_pourcent")).toDouble(0);
    if (qFuzzyIsNull(percent) || percent < -1000 || percent > 1000) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("vitesse_pourcent va de -1000 à 1000, sans 0."));
    }
    const bool keepPitch = args.value(QStringLiteral("conserver_hauteur")).toBool(true);
    // Attention : cette version de requestClipTimeWarp prend un pourcentage et divise elle-même par 100.
    if (!timeline->requestClipTimeWarp(cid, percent, keepPitch, true)) {
        return error(QStringLiteral("place_occupee"), QStringLiteral("La nouvelle durée du clip %1 ne tient pas : un clip voisin est dans le passage.").arg(cid),
                     QStringLiteral("Libère la place après le clip avant un ralenti."));
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("clip_id"), cid},
                       {QStringLiteral("vitesse_pourcent"), qRound(timeline->getClipSpeed(cid) * 100)},
                       {QStringLiteral("duree"), time(timeline->getClipPlaytime(cid))}};
}

// ---------------------------------------------------------------------------------------------
// Lot 3 (23 septembre 2026) : ajouter_fondu

QJsonObject ajouterFondu(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    const QString side = args.value(QStringLiteral("cote")).toString();
    if (side != QLatin1String("entree") && side != QLatin1String("sortie")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("cote vaut « entree » ou « sortie »."));
    }
    const int duration = KoraCutAgentTools::parseTime(args.value(QStringLiteral("duree")), projectFps());
    if (duration < 0) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("« duree » manque ou ne se lit pas."), QStringLiteral("0 retire le fondu."));
    }
    const int clipLength = timeline->getClipPlaytime(cid);
    if (duration > clipLength) {
        return error(QStringLiteral("hors_clip"), QStringLiteral("Le clip %1 ne dure que %2 images.").arg(cid).arg(clipLength),
                     QStringLiteral("Un fondu ne dépasse pas la durée du clip."));
    }
    const bool fadeIn = side == QLatin1String("entree");
    const bool audio = timeline->isAudioTrack(timeline->getClipTrackId(cid));
    const int before = currentFade(timeline, cid, fadeIn);
    // Fixer un côté à N images, sans qu'aucune fonction de la pile d'effets n'inscrive sa propre étape
    // d'annulation : removeEffect et appendEffect le font, et un seul appel d'outil en laissait deux
    // (constaté au test du 23 septembre 2026). On passe par les variantes « WithUndo » dont on jette
    // les étapes, et c'est notre propre étape, « mettre ce fondu à N », qui s'inscrit.
    std::weak_ptr<TimelineItemModel> weak = timeline;
    auto apply = [weak, cid, fadeIn, audio](int length) {
        auto t = weak.lock();
        if (!t || !t->isClip(cid)) {
            return false;
        }
        const std::shared_ptr<EffectStackModel> stack = t->getClipEffectStackModel(cid);
        if (!stack) {
            return false;
        }
        const QStringList ids = fadeIn ? QStringList{QStringLiteral("fadein"), QStringLiteral("fade_from_black")}
                                       : QStringList{QStringLiteral("fadeout"), QStringLiteral("fade_to_black")};
        std::vector<std::shared_ptr<EffectItemModel>> existing;
        for (int row = 0; row < stack->rowCount(); ++row) {
            auto effect = std::static_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
            if (effect && ids.contains(effect->getAssetId())) {
                existing.push_back(effect);
            }
        }
        Fun ignoredUndo = []() { return true; };
        Fun ignoredRedo = []() { return true; };
        if (length <= 0) {
            for (auto it = existing.rbegin(); it != existing.rend(); ++it) {
                QString name;
                stack->removeEffectWithUndo(*it, name, ignoredUndo, ignoredRedo);
            }
            return true;
        }
        if (existing.empty()) {
            const QString effectId = audio ? ids.at(0) : ids.at(1);
            if (!stack->appendEffectWithUndo(effectId, ignoredUndo, ignoredRedo).first) {
                return false;
            }
        }
        return stack->adjustFadeLength(length, fadeIn, audio, !audio, false);
    };
    if (!apply(duration)) {
        return error(QStringLiteral("hors_clip"), QStringLiteral("Le fondu n'a pas pu être posé sur le clip %1.").arg(cid));
    }
    Fun redo = [apply, duration]() { return apply(duration); };
    Fun undo = [apply, before]() { return apply(before); };
    pCore->pushUndo(undo, redo, QStringLiteral("ajouter_fondu"));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("clip_id"), cid},
                       {QStringLiteral("cote"), side},
                       {QStringLiteral("duree"), time(currentFade(timeline, cid, fadeIn))}};
}


// ---------------------------------------------------------------------------------------------
// Lot 3 du module Motion (24 septembre 2026) : motion_* (motion.json). Les compositions HyperFrames du
// projet, lues et écrites par le miroir (document 19). Les temps de ces outils sont ceux du HTML : des
// secondes ; seule motion_poser prend une position de timeline (images ou timecode).

KoraCutMotionMirror *g_motionMirror = nullptr;

// Le miroir et le projet Motion visé, ou une erreur prête à rendre.
KoraCutMotionMirror *motionOrFail(const Args &args, QString &project, QJsonObject &failure, bool needProject = true)
{
    if (!g_motionMirror || !g_motionMirror->document()) {
        failure = error(QStringLiteral("non_disponible"), QStringLiteral("Le module Motion n'est pas disponible (aucun projet ouvert)."));
        return nullptr;
    }
    g_motionMirror->rescan();
    // Un clip supprimé ou une annulation de l'instant d'avant : l'état rendu est à jour.
    g_motionMirror->flush();
    if (!needProject) {
        return g_motionMirror;
    }
    project = KoraCutMotionProject::sanitizeName(args.value(QStringLiteral("nom")).toString());
    if (project.isEmpty() || !g_motionMirror->projects().contains(project)) {
        failure = error(QStringLiteral("projet_inconnu"), QStringLiteral("Aucune composition Motion ne s'appelle « %1 ».").arg(args.value(QStringLiteral("nom")).toString()),
                        QStringLiteral("Relis motion_lister (champ nom), ou crée-la avec motion_creer."));
        return nullptr;
    }
    return g_motionMirror;
}

QString hostStateName()
{
    switch (KoraCutMotionHost::state()) {
    case KoraCutMotionHost::State::Ready:
        return QStringLiteral("pret");
    case KoraCutMotionHost::State::NotInstalled:
        return QStringLiteral("a_installer");
    case KoraCutMotionHost::State::NoNode:
        return QStringLiteral("sans_node");
    }
    return QString();
}

QJsonObject elementJson(const Timeline &timeline, const KoraCutMotionMirror::ElementState &e, double fps)
{
    QJsonObject o{{QStringLiteral("id"), e.id},
                  {QStringLiteral("sorte"), e.kind},
                  {QStringLiteral("nom"), e.name},
                  {QStringLiteral("debut"), e.start},
                  {QStringLiteral("duree"), e.duration},
                  {QStringLiteral("piste_index"), e.trackIndex}};
    if (!e.text.isEmpty()) {
        o.insert(QStringLiteral("texte"), e.text);
    }
    if (!e.src.isEmpty()) {
        o.insert(QStringLiteral("fichier"), e.src);
    }
    QString render = QStringLiteral("aucun");
    if (e.kind == QLatin1String("audio")) {
        render = QStringLiteral("son");
    } else if (e.pending) {
        render = QStringLiteral("en_cours");
    } else if (e.failed) {
        render = QStringLiteral("echec");
    } else if (!e.renderPath.isEmpty()) {
        render = QStringLiteral("pret");
    }
    o.insert(QStringLiteral("rendu"), render);
    if (e.clipId >= 0 && timeline && timeline->isClip(e.clipId)) {
        o.insert(QStringLiteral("clip_id"), e.clipId);
        o.insert(QStringLiteral("piste_id"), timeline->getClipTrackId(e.clipId));
        o.insert(QStringLiteral("pos"), timeline->getClipPosition(e.clipId));
        o.insert(QStringLiteral("pos_tc"), KoraCutAgentTools::timecode(timeline->getClipPosition(e.clipId), fps));
        o.insert(QStringLiteral("len"), timeline->getClipPlaytime(e.clipId));
    }
    return o;
}

QJsonObject projectJson(const Timeline &, KoraCutMotionMirror *mirror, const QString &name)
{
    const KoraCutMotionMirror::ProjectState state = mirror->projectState(name);
    const KoraCutMotionSync *sync = mirror->sync(name);
    const double fps = projectFps();
    QJsonObject o{{QStringLiteral("nom"), name}, {QStringLiteral("dossier"), state.dir}, {QStringLiteral("fichier"), state.indexPath}};
    // La séquence où vit la composition : ses clips sont là, quelle que soit la séquence devant. Les
    // clip_id, piste_id et pos des éléments sont ceux de CETTE séquence.
    const QString sequence = mirror->sequenceOf(name);
    const Timeline timeline = mirror->timelineFor(name);
    o.insert(QStringLiteral("sequence"), sequence);
    if (pCore->projectItemModel()) {
        const QString binId = pCore->projectItemModel()->getSequenceId(QUuid(sequence));
        if (auto clip = binId.isEmpty() ? nullptr : pCore->projectItemModel()->getClipByBinID(binId)) {
            o.insert(QStringLiteral("sequence_nom"), clip->clipName());
        }
    }
    o.insert(QStringLiteral("sequence_active"), sequence == pCore->currentTimelineId().toString(QUuid::WithoutBraces));
    if (!timeline) {
        o.insert(QStringLiteral("sequence_ouverte"), false);
    }
    // Une composition d'un seul élément (lot 5) : sa sorte, son nom lisible, la coupe qu'elle suit, son retrait.
    if (!state.element.isEmpty()) {
        o.insert(QStringLiteral("element_seul"), state.element);
        o.insert(QStringLiteral("affichage"), state.display);
        if (!state.style.isEmpty()) {
            o.insert(QStringLiteral("style"), state.style);
        }
        if (state.cut >= 0) {
            o.insert(QStringLiteral("coupe"), time(int(std::lround(state.cut * projectFps()))));
        }
        if (state.retired) {
            o.insert(QStringLiteral("retiree"), true);
        }
        // Posé sous une transition : il la suit quand la coupe bouge.
        if (const QString followed = mirror->followedTransition(name); !followed.isEmpty()) {
            o.insert(QStringLiteral("suit_transition"), followed);
        }
    }
    if (sync) {
        const KoraCutMotion::Composition &comp = sync->composition();
        o.insert(QStringLiteral("largeur"), comp.width);
        o.insert(QStringLiteral("hauteur"), comp.height);
        o.insert(QStringLiteral("ips"), comp.fps);
        o.insert(QStringLiteral("duree"), comp.end());
    }
    QJsonArray elements;
    for (const KoraCutMotionMirror::ElementState &e : mirror->elements(name)) {
        elements.append(elementJson(timeline, e, fps));
    }
    o.insert(QStringLiteral("elements"), elements);
    o.insert(QStringLiteral("rendus"), state.rendered);
    o.insert(QStringLiteral("en_cours"), state.pending);
    o.insert(QStringLiteral("echecs"), state.failed);
    if (!state.errors.isEmpty()) {
        o.insert(QStringLiteral("erreurs"), QJsonArray::fromStringList(state.errors));
    }
    return o;
}

QJsonObject motionLister(const Timeline &timeline, const Args &args)
{
    QString project;
    QJsonObject failure;
    KoraCutMotionMirror *mirror = motionOrFail(args, project, failure, false);
    if (!mirror) {
        return failure;
    }
    // Une composition retirée (son clip a quitté la timeline : supprimé, ou création annulée) n'est plus
    // montrée ; son dossier reste, pour que Rétablir la remette. retirees = true les montre.
    const bool withRetired = args.value(QStringLiteral("retirees")).toBool(false);
    const QString onlySequence = args.value(QStringLiteral("sequence")).toString().trimmed();
    QJsonArray list;
    int hidden = 0;
    for (const QString &name : mirror->projects()) {
        if (!withRetired && mirror->projectState(name).retired) {
            ++hidden;
            continue;
        }
        const QJsonObject o = projectJson(timeline, mirror, name);
        if (!onlySequence.isEmpty() && onlySequence != QLatin1String("active") && o.value(QStringLiteral("sequence")).toString() != onlySequence &&
            o.value(QStringLiteral("sequence_nom")).toString().compare(onlySequence, Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (onlySequence == QLatin1String("active") && !o.value(QStringLiteral("sequence_active")).toBool()) {
            continue;
        }
        list.append(o);
    }
    QJsonObject host{{QStringLiteral("etat"), hostStateName()}};
    const QString version = KoraCutMotionHost::installedVersion();
    if (!version.isEmpty()) {
        host.insert(QStringLiteral("version"), version);
    }
    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("hyperframes"), host},
                       {QStringLiteral("dossier"), mirror->motionRoot()},
                       {QStringLiteral("compositions"), list}};
    if (hidden > 0) {
        result.insert(QStringLiteral("retirees_masquees"), hidden);
    }
    // La charte du client : un tokens.json à côté du fichier projet (ou dans motion/), rendu tel quel.
    QStringList candidates{mirror->motionRoot() + QStringLiteral("/tokens.json")};
    if (mirror->document() && !mirror->document()->url().isEmpty()) {
        candidates.prepend(QFileInfo(mirror->document()->url().toLocalFile()).absolutePath() + QStringLiteral("/tokens.json"));
    }
    for (const QString &candidate : std::as_const(candidates)) {
        QFile file(candidate);
        if (file.open(QIODevice::ReadOnly)) {
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isObject()) {
                QJsonObject charte = doc.object();
                charte.insert(QStringLiteral("fichier"), candidate);
                result.insert(QStringLiteral("charte"), charte);
                break;
            }
        }
    }
    return result;
}

QJsonObject motionCreer(const Timeline &timeline, const Args &args)
{
    QString project;
    QJsonObject failure;
    KoraCutMotionMirror *mirror = motionOrFail(args, project, failure, false);
    if (!mirror) {
        return failure;
    }
    const QString wanted = args.value(QStringLiteral("nom")).toString();
    const QString clean = KoraCutMotionProject::sanitizeName(wanted);
    if (clean.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le nom « %1 » ne donne aucun nom de dossier utilisable.").arg(wanted),
                     QStringLiteral("Des lettres, des chiffres et des tirets : « bandeau-awa »."));
    }
    if (mirror->projects().contains(clean) && !mirror->projectState(clean).retired) {
        return error(QStringLiteral("existe_deja"), QStringLiteral("Une composition « %1 » existe déjà.").arg(clean),
                     QStringLiteral("Écris dedans avec motion_ecrire, ou choisis un autre nom."));
    }
    const int width = args.value(QStringLiteral("largeur")).toInt(0);
    const int height = args.value(QStringLiteral("hauteur")).toInt(0);
    const double fps = args.value(QStringLiteral("ips")).toDouble(0);
    if (width < 0 || height < 0 || fps < 0) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("largeur, hauteur et ips sont positifs (ou absents : ceux du projet)."));
    }
    // Une composition déjà écrite sur le disque (un dossier HyperFrames avec son index.html, ou un .html
    // seul) : elle est importée avec ses fichiers (polices, images, scripts), comme par le panneau Motion.
    const QString source = args.value(QStringLiteral("source")).toString().trimmed();
    if (!source.isEmpty()) {
        if (args.contains(QStringLiteral("html"))) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Donne html ou source, pas les deux."));
        }
        const QFileInfo info(source);
        if (!info.isAbsolute() || !info.exists()) {
            return error(QStringLiteral("fichier_introuvable"), QStringLiteral("La source « %1 » n'existe pas.").arg(source),
                         QStringLiteral("Un chemin absolu vers un dossier qui contient index.html, ou vers un fichier .html."));
        }
        if (info.isDir() ? !QFileInfo::exists(info.absoluteFilePath() + QStringLiteral("/index.html")) : info.suffix().toLower() != QLatin1String("html")) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("La source doit être un dossier avec un index.html, ou un fichier .html."));
        }
        // Les fichiers en plus sont vérifiés avant l'import, et copiés juste après (la source apporte déjà les siens).
        QJsonArray copied;
        if (QJsonObject e = copyMotionAssets(args, QString(), copied, true); !e.isEmpty()) {
            return e;
        }
        QString trashed;
        if (!releaseMotionName(mirror, clean, trashed)) {
            return error(QStringLiteral("existe_deja"), QStringLiteral("Une composition « %1 » existe déjà.").arg(clean));
        }
        QString why;
        const QString name = mirror->importProject(info.absoluteFilePath(), clean, &why);
        if (name.isEmpty()) {
            return error(QStringLiteral("entree_invalide"), why.isEmpty() ? QStringLiteral("La composition n'a pas pu être importée.") : why);
        }
        if (QJsonObject e = copyMotionAssets(args, mirror->projectState(name).dir, copied); !e.isEmpty()) {
            return e;
        }
        QJsonObject result = projectJson(timeline, mirror, name);
        result.insert(QStringLiteral("ok"), true);
        result.insert(QStringLiteral("importee_de"), info.absoluteFilePath());
        if (!copied.isEmpty()) {
            result.insert(QStringLiteral("fichiers_copies"), copied);
        }
        if (!trashed.isEmpty()) {
            result.insert(QStringLiteral("ancienne_retiree"), trashed);
        }
        return result;
    }
    const QString html = args.value(QStringLiteral("html")).toString();
    if (!html.isEmpty() && !html.contains(QLatin1String("data-composition-id"))) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le HTML n'a pas de racine data-composition-id."),
                     QStringLiteral("Lis la compétence hyperframes-core : la racine porte data-composition-id, data-width, data-height, data-fps, data-duration."));
    }
    QString trashed;
    if (!releaseMotionName(mirror, clean, trashed)) {
        return error(QStringLiteral("existe_deja"), QStringLiteral("Une composition « %1 » existe déjà.").arg(clean));
    }
    // Les polices et les images sont dans assets/ avant la création : le premier rendu les trouve.
    QJsonArray copied;
    if (QJsonObject e = copyMotionAssets(args, mirror->motionRoot() + QLatin1Char('/') + clean, copied); !e.isEmpty()) {
        return e;
    }
    QString why;
    const QString name = mirror->createProject(clean, &why, width, height, fps, html);
    if (name.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), why.isEmpty() ? QStringLiteral("La composition n'a pas pu être créée.") : why);
    }
    QJsonObject result = projectJson(timeline, mirror, name);
    result.insert(QStringLiteral("ok"), true);
    if (!copied.isEmpty()) {
        result.insert(QStringLiteral("fichiers_copies"), copied);
    }
    if (!trashed.isEmpty()) {
        result.insert(QStringLiteral("ancienne_retiree"), trashed);
    }
    return result;
}

QJsonObject motionLire(const Timeline &, const Args &args)
{
    QString project;
    QJsonObject failure;
    KoraCutMotionMirror *mirror = motionOrFail(args, project, failure);
    if (!mirror) {
        return failure;
    }
    const KoraCutMotionSync *sync = mirror->sync(project);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("nom"), project},
                       {QStringLiteral("fichier"), sync->indexPath()},
                       {QStringLiteral("html"), sync->html()}};
}

QJsonObject motionEcrire(const Timeline &timeline, const Args &args)
{
    QString project;
    QJsonObject failure;
    KoraCutMotionMirror *mirror = motionOrFail(args, project, failure);
    if (!mirror) {
        return failure;
    }
    if (args.value(QStringLiteral("html")).toString().trimmed().isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("html est vide."), QStringLiteral("Pour retirer une composition, laisse-la : rien ne supprime un fichier."));
    }
    if (!args.value(QStringLiteral("html")).toString().contains(QLatin1String("data-composition-id"))) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le HTML n'a pas de racine data-composition-id."),
                     QStringLiteral("Lis la compétence hyperframes-core : la racine porte data-composition-id, data-width, data-height, data-fps, data-duration."));
    }
    const KoraCutMotionSync *sync = mirror->sync(project);
    QMap<QString, QString> before;
    for (const KoraCutMotion::Element &e : sync->composition().elements) {
        before.insert(e.id, e.hash);
    }
    // Les fichiers d'abord : le nouveau rendu les trouve dans assets/.
    QJsonArray copied;
    if (QJsonObject e = copyMotionAssets(args, mirror->projectState(project).dir, copied); !e.isEmpty()) {
        return e;
    }
    // Ce que KoraCut a écrit sur la racine ne se perd pas parce que le HTML réécrit l'a oublié : la séquence où la
    // composition est posée, et pour un élément seul sa sorte, sa place et son style. Sans eux (12 octobre 2026,
    // relevé sur le projet ICONOA), l'élément n'était plus reconnu : son clip repartait à l'image 0 sur une piste
    // neuve. Un attribut que le nouveau HTML donne lui-même n'est pas touché.
    QString html = args.value(QStringLiteral("html")).toString();
    {
        static const QStringList kept{QStringLiteral("data-koracut-sequence"), QStringLiteral("data-koracut-element"), QStringLiteral("data-koracut-start"),
                                      QStringLiteral("data-koracut-style")};
        const auto rootOf = [](const QString &text, KoraCutMotion::Tag &root) {
            const QList<KoraCutMotion::Tag> tags = KoraCutMotion::tokenize(text);
            for (const KoraCutMotion::Tag &t : tags) {
                if (!t.closing && t.has(QStringLiteral("data-composition-id"))) {
                    root = t;
                    return true;
                }
            }
            return false;
        };
        KoraCutMotion::Tag oldRoot;
        if (rootOf(sync->html(), oldRoot)) {
            for (const QString &name : kept) {
                const QString value = oldRoot.attr(name).trimmed();
                KoraCutMotion::Tag newRoot;
                // La racine est relue à chaque tour : poser un attribut déplace les suivants.
                if (value.isEmpty() || !rootOf(html, newRoot) || newRoot.has(name)) {
                    continue;
                }
                html = KoraCutMotion::setAttribute(html, newRoot, name, value);
            }
        }
    }
    if (!mirror->replaceHtml(project, html, i18nc("@action undo text", "Motion: rewrite %1", project))) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le fichier de « %1 » n'a pas pu être écrit.").arg(project));
    }
    QJsonArray added, removed, rerendered;
    QSet<QString> seen;
    for (const KoraCutMotion::Element &e : sync->composition().elements) {
        seen.insert(e.id);
        if (!before.contains(e.id)) {
            added.append(e.id);
        } else if (before.value(e.id) != e.hash && e.kind != KoraCutMotion::Kind::Audio) {
            rerendered.append(e.id);
        }
    }
    for (auto it = before.constBegin(); it != before.constEnd(); ++it) {
        if (!seen.contains(it.key())) {
            removed.append(it.key());
        }
    }
    QJsonObject result = projectJson(timeline, mirror, project);
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("ajoutes"), added);
    result.insert(QStringLiteral("retires"), removed);
    result.insert(QStringLiteral("rendus_relances"), rerendered);
    if (!copied.isEmpty()) {
        result.insert(QStringLiteral("fichiers_copies"), copied);
    }
    return result;
}

QJsonObject motionElement(const Timeline &, const Args &args)
{
    QString project;
    QJsonObject failure;
    KoraCutMotionMirror *mirror = motionOrFail(args, project, failure);
    if (!mirror) {
        return failure;
    }
    const QString id = args.value(QStringLiteral("id")).toString();
    const KoraCutMotionSync *sync = mirror->sync(project);
    const KoraCutMotion::Element *e = sync->composition().element(id);
    if (!e) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun élément « %1 » dans « %2 ».").arg(id, project), QStringLiteral("Relis motion_lister (champ elements[].id)."));
    }
    // Une composition d'un seul élément (lot 5) se change ici : texte, place (data-koracut-start), durée.
    const bool single = e->kind == KoraCutMotion::Kind::Whole && sync->composition().isElement();
    if (e->kind == KoraCutMotion::Kind::Whole && !single) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("« %1 » est une composition d'un seul tenant : modifie son HTML avec motion_ecrire.").arg(project));
    }
    QMap<QString, QString> attributes;
    if (args.contains(QStringLiteral("debut"))) {
        const double start = args.value(QStringLiteral("debut")).toDouble(-1);
        if (start < 0) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("debut est un nombre de secondes, positif ou nul."));
        }
        attributes.insert(single ? QStringLiteral("data-koracut-start") : QStringLiteral("data-start"), KoraCutMotion::secondsText(start));
    }
    if (args.contains(QStringLiteral("duree"))) {
        const double duration = args.value(QStringLiteral("duree")).toDouble(0);
        if (duration <= 0) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("duree est un nombre de secondes, strictement positif."));
        }
        attributes.insert(QStringLiteral("data-duration"), KoraCutMotion::secondsText(duration));
    }
    const QJsonObject extra = args.value(QStringLiteral("attributs")).toObject();
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) {
        static const QRegularExpression attrName(QStringLiteral("^[a-zA-Z_:][-a-zA-Z0-9_:.]*$"));
        if (!attrName.match(it.key()).hasMatch()) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("« %1 » n'est pas un nom d'attribut.").arg(it.key()));
        }
        if (it.key().compare(QLatin1String("id"), Qt::CaseInsensitive) == 0) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("L'id d'un élément ne se change pas ici : réécris le HTML avec motion_ecrire."));
        }
        // Une valeur nulle retire l'attribut ; un nombre ou un booléen s'écrit tel quel.
        QString value;
        if (it.value().isString()) {
            value = it.value().toString();
        } else if (it.value().isDouble()) {
            value = KoraCutMotion::secondsText(it.value().toDouble());
        } else if (it.value().isBool()) {
            value = it.value().toBool() ? QStringLiteral("true") : QStringLiteral("false");
        }
        attributes.insert(it.key(), value);
    }
    QString text;
    const bool withText = args.contains(QStringLiteral("texte"));
    if (withText) {
        if (e->kind != KoraCutMotion::Kind::Block && e->kind != KoraCutMotion::Kind::Composition && !(single && e->textTagIndex >= 0)) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("« %1 » est un média, il n'a pas de texte.").arg(id), QStringLiteral("Change son fichier avec attributs: { \"src\": … }."));
        }
        text = args.value(QStringLiteral("texte")).toString();
    }
    if (attributes.isEmpty() && !withText) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Rien à changer : donne texte, debut, duree ou attributs."));
    }
    const QString hashBefore = e->hash;
    if (!mirror->setElement(project, id, attributes, withText ? &text : nullptr, i18nc("@action undo text", "Motion: change %1", id))) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("L'élément « %1 » n'a pas pu être modifié (une écriture extérieure attend peut-être d'être relue).").arg(id),
                     QStringLiteral("Relis motion_lister puis réessaie une fois."));
    }
    const KoraCutMotion::Element *after = sync->composition().element(id);
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("nom"), project}, {QStringLiteral("id"), id}};
    if (after) {
        for (const KoraCutMotionMirror::ElementState &s : mirror->elements(project)) {
            if (s.id == id) {
                // Le clip est dans la séquence de la composition, pas forcément dans celle qui est devant.
                result.insert(QStringLiteral("element"), elementJson(mirror->timelineFor(project), s, projectFps()));
            }
        }
        result.insert(QStringLiteral("rendu_relance"), after->hash != hashBefore && after->kind != KoraCutMotion::Kind::Audio);
    }
    return result;
}

QJsonObject motionRendre(const Timeline &timeline, const Args &args)
{
    QString project;
    QJsonObject failure;
    KoraCutMotionMirror *mirror = motionOrFail(args, project, failure);
    if (!mirror) {
        return failure;
    }
    if (KoraCutMotionHost::state() != KoraCutMotionHost::State::Ready && !KoraCutMotionRenderer::hasSimulator()) {
        const QString state = hostStateName();
        return error(QStringLiteral("non_disponible"),
                     state == QLatin1String("sans_node") ? QStringLiteral("Node.js n'est pas installé sur cette machine : pas de rendu Motion.")
                                                          : QStringLiteral("HyperFrames n'est pas encore installé (bouton « Installer HyperFrames » du panneau Motion)."),
                     QStringLiteral("Dis-le à la personne ; la composition est écrite, le rendu viendra quand l'outil sera là."));
    }
    QStringList ids;
    for (const QJsonValue &v : args.value(QStringLiteral("ids")).toArray()) {
        ids << v.toString();
    }
    const KoraCutMotionSync *sync = mirror->sync(project);
    for (const QString &id : std::as_const(ids)) {
        if (!sync->composition().element(id)) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun élément « %1 » dans « %2 ».").arg(id, project), QStringLiteral("Relis motion_lister."));
        }
    }
    const bool force = args.value(QStringLiteral("forcer")).toBool(false);
    const bool wait = args.value(QStringLiteral("attendre")).toBool(true);
    const int requested = mirror->renderElements(project, ids, force);
    bool finished = true;
    if (wait) {
        // Un rendu dure de quelques secondes à une minute : on attend ici (au plus 5 minutes) pour que
        // l'agent trouve les clips posés à l'appel suivant. La boucle d'événements continue de tourner.
        finished = mirror->renderer()->waitForIdle(300000);
    }
    QJsonObject result = projectJson(timeline, mirror, project);
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("demandes"), requested);
    result.insert(QStringLiteral("attendu"), wait);
    result.insert(QStringLiteral("termine"), finished);
    if (!mirror->renderer()->lastError().isEmpty() && mirror->projectState(project).failed > 0) {
        result.insert(QStringLiteral("derniere_erreur"), mirror->renderer()->lastError());
    }
    return result;
}

QJsonObject motionInstantane(const Timeline &, const Args &args)
{
    QString project;
    QJsonObject failure;
    KoraCutMotionMirror *mirror = motionOrFail(args, project, failure);
    if (!mirror) {
        return failure;
    }
    if (KoraCutMotionHost::state() != KoraCutMotionHost::State::Ready) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("HyperFrames n'est pas installé : pas d'instantané."));
    }
    const double seconds = args.value(QStringLiteral("temps")).toDouble(-1);
    if (seconds < 0) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("temps est un nombre de secondes dans la composition."));
    }
    const KoraCutMotionMirror::ProjectState state = mirror->projectState(project);
    const QString outDir = KoraCutMotionProject::cacheDir(mirror->document(), project) + QStringLiteral("/instantanes");
    QDir().mkpath(outDir);
    QString output;
    int code = 0;
    const QStringList cli = {QStringLiteral("snapshot"), state.dir, QStringLiteral("--at"), KoraCutMotion::secondsText(seconds), QStringLiteral("--no-end"),
                             QStringLiteral("--describe"), QStringLiteral("false"), QStringLiteral("--output"), outDir};
    if (!KoraCutMotionHost::runCli(cli, state.dir, 120000, &output, &code)) {
        return error(QStringLiteral("rendu_echoue"), QStringLiteral("hyperframes snapshot a échoué : %1").arg(output.section(QLatin1Char('\n'), -3).trimmed()),
                     QStringLiteral("Vérifie la composition avec motion_verifier."));
    }
    // La CLI nomme « frame-00-at-1.2s.png » : on prend le PNG le plus récent du dossier.
    const QFileInfoList pngs = QDir(outDir).entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Time);
    if (pngs.isEmpty()) {
        return error(QStringLiteral("rendu_echoue"), QStringLiteral("hyperframes snapshot n'a écrit aucune image."));
    }
    const QString stable = outDir + QStringLiteral("/%1s.png").arg(KoraCutMotion::secondsText(seconds).replace(QLatin1Char('.'), QLatin1Char('_')));
    QFile::remove(stable);
    QFile::rename(pngs.first().absoluteFilePath(), stable);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("nom"), project},
                       {QStringLiteral("temps"), seconds},
                       {QStringLiteral("chemin"), stable},
                       {QStringLiteral("conseil"), QStringLiteral("Regarde l'image (Read sur le chemin, ou voir_image) avant de corriger.")}};
}

QJsonObject motionVerifier(const Timeline &, const Args &args)
{
    QString project;
    QJsonObject failure;
    KoraCutMotionMirror *mirror = motionOrFail(args, project, failure);
    if (!mirror) {
        return failure;
    }
    const KoraCutMotionMirror::ProjectState state = mirror->projectState(project);
    QJsonArray messages;
    // Ce que le synchroniseur voit déjà sans Node : racine absente, débuts qui ne se résolvent pas.
    for (const QString &e : state.errors) {
        messages.append(QJsonObject{{QStringLiteral("niveau"), QStringLiteral("error")}, {QStringLiteral("code"), QStringLiteral("koracut_analyse")}, {QStringLiteral("message"), e}});
    }
    int errors = state.errors.size();
    int warnings = 0;
    bool linted = false;
    if (KoraCutMotionHost::state() == KoraCutMotionHost::State::Ready) {
        QString output;
        int code = 0;
        KoraCutMotionHost::runCli({QStringLiteral("lint"), state.dir, QStringLiteral("--json")}, state.dir, 60000, &output, &code);
        const int brace = output.indexOf(QLatin1Char('{'));
        const QJsonObject json = brace < 0 ? QJsonObject() : QJsonDocument::fromJson(output.mid(brace).toUtf8()).object();
        if (json.contains(QStringLiteral("findings"))) {
            linted = true;
            for (const QJsonValue &v : json.value(QStringLiteral("findings")).toArray()) {
                const QJsonObject f = v.toObject();
                // Pendant un rendu, l'enveloppe « .koracut-<id>.html » est à côté de index.html et lint la lit
                // aussi : ce qu'il y trouve ne concerne pas la composition.
                if (QFileInfo(f.value(QStringLiteral("file")).toString()).fileName().startsWith(QLatin1String(".koracut-"))) {
                    continue;
                }
                const QString severity = f.value(QStringLiteral("severity")).toString();
                if (severity == QLatin1String("error")) {
                    ++errors;
                } else if (severity == QLatin1String("warning")) {
                    ++warnings;
                }
                QJsonObject m{{QStringLiteral("niveau"), severity}, {QStringLiteral("code"), f.value(QStringLiteral("code")).toString()}, {QStringLiteral("message"), f.value(QStringLiteral("message")).toString()}};
                if (f.contains(QStringLiteral("elementId"))) {
                    m.insert(QStringLiteral("element"), f.value(QStringLiteral("elementId")).toString());
                }
                if (f.contains(QStringLiteral("line"))) {
                    m.insert(QStringLiteral("ligne"), f.value(QStringLiteral("line")).toInt());
                }
                if (f.contains(QStringLiteral("fixHint"))) {
                    m.insert(QStringLiteral("conseil"), f.value(QStringLiteral("fixHint")).toString());
                }
                messages.append(m);
            }
        } else {
            messages.append(QJsonObject{{QStringLiteral("niveau"), QStringLiteral("warning")}, {QStringLiteral("code"), QStringLiteral("lint_indisponible")},
                                        {QStringLiteral("message"), QStringLiteral("hyperframes lint n'a pas répondu : %1").arg(output.section(QLatin1Char('\n'), -2).trimmed())}});
        }
    } else {
        messages.append(QJsonObject{{QStringLiteral("niveau"), QStringLiteral("info")}, {QStringLiteral("code"), QStringLiteral("lint_indisponible")},
                                    {QStringLiteral("message"), QStringLiteral("HyperFrames n'est pas installé : seule l'analyse de KoraCut a été faite.")}});
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("nom"), project},
                       {QStringLiteral("lint"), linted},
                       {QStringLiteral("erreurs"), errors},
                       {QStringLiteral("avertissements"), warnings},
                       {QStringLiteral("messages"), messages}};
}

QJsonObject motionPoser(const Timeline &timeline, const Args &args)
{
    QString project;
    QJsonObject failure;
    KoraCutMotionMirror *mirror = motionOrFail(args, project, failure);
    if (!mirror) {
        return failure;
    }
    // Elle se pose dans SA séquence. Fermée : rien à poser tant qu'elle n'est pas rouverte. Une piste_id
    // est une piste de la séquence devant : elle ne vaut que si c'est celle de la composition.
    const Timeline own = mirror->timelineFor(project);
    if (!own) {
        return error(QStringLiteral("sequence_fermee"), QStringLiteral("La séquence de « %1 » n'est pas ouverte.").arg(project),
                     QStringLiteral("Active-la (projet.sequence faire=activer, ou activer_sequence) : ses clips sont posés à l'ouverture."));
    }
    int trackId = -1;
    if (args.contains(QStringLiteral("piste_id"))) {
        if (own != timeline) {
            return error(QStringLiteral("autre_sequence"), QStringLiteral("« %1 » vit dans une autre séquence que celle qui est devant : piste_id ne s'y applique pas.").arg(project),
                         QStringLiteral("Active sa séquence d'abord (motion_lister donne sequence et sequence_nom), ou laisse piste_id vide."));
        }
        trackId = args.value(QStringLiteral("piste_id")).toInt(-1);
        if (QJsonObject e = trackIdCheck(timeline, trackId); !e.isEmpty()) {
            return e;
        }
        if (timeline->isAudioTrack(trackId)) {
            return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("Une composition Motion se pose sur une piste vidéo."));
        }
    }
    double atSeconds = -1.;
    if (args.contains(QStringLiteral("position"))) {
        const int position = KoraCutAgentTools::parseTime(args.value(QStringLiteral("position")), projectFps());
        if (position < 0) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("position est un nombre d'images ou un timecode HH:MM:SS:II."));
        }
        atSeconds = position / projectFps();
    }
    QString why;
    const bool changed = mirror->place(project, trackId, atSeconds, &why);
    if (!changed && !why.isEmpty()) {
        if (why == QLatin1String("place_occupee")) {
            return error(why, QStringLiteral("La piste %1 appartient déjà à une autre composition Motion.").arg(trackLabel(timeline, trackId)), QStringLiteral("Laisse piste_id vide : le miroir crée sa piste."));
        }
        if (why == QLatin1String("mauvais_type_de_piste")) {
            return error(why, QStringLiteral("La piste %1 ne convient pas (audio ou verrouillée).").arg(trackLabel(timeline, trackId)));
        }
        return error(QStringLiteral("projet_inconnu"), QStringLiteral("La composition « %1 » n'a pas pu être posée.").arg(project));
    }
    QJsonObject result = projectJson(timeline, mirror, project);
    result.insert(QStringLiteral("ok"), true);
    result.insert(QStringLiteral("change"), changed);
    int placed = 0;
    int waiting = 0;
    for (const KoraCutMotionMirror::ElementState &e : mirror->elements(project)) {
        if (e.clipId >= 0) {
            ++placed;
        } else if (e.kind != QLatin1String("audio")) {
            ++waiting;
        }
    }
    result.insert(QStringLiteral("poses"), placed);
    result.insert(QStringLiteral("sans_clip"), waiting);
    if (waiting > 0) {
        result.insert(QStringLiteral("remarque"), QStringLiteral("%1 élément(s) n'ont pas de clip : leur rendu n'est pas prêt (motion_rendre) ou a échoué (motion_verifier).").arg(waiting));
    }
    return result;
}

QMap<QString, Tool> buildTools()
{
    QMap<QString, Tool> map = {
        {QStringLiteral("etat_timeline"), etatTimeline}, {QStringLiteral("couper_clip"), couperClip},
        {QStringLiteral("couper_toutes_pistes"), couperToutesPistes}, {QStringLiteral("deplacer_clip"), deplacerClip},
        {QStringLiteral("supprimer_clip"), supprimerClip}, {QStringLiteral("rogner_clip"), rognerClip},
        {QStringLiteral("ajouter_piste"), ajouterPiste}, {QStringLiteral("regler_piste"), reglerPiste}, {QStringLiteral("supprimer_piste"), supprimerPiste},
        {QStringLiteral("ajouter_clip"), ajouterClip}, {QStringLiteral("lister_medias"), listerMedias},
        {QStringLiteral("supprimer_espace"), supprimerEspace}, {QStringLiteral("changer_vitesse_clip"), changerVitesseClip},
        {QStringLiteral("ajouter_fondu"), ajouterFondu},
        {QStringLiteral("motion_lister"), motionLister}, {QStringLiteral("motion_creer"), motionCreer},
        {QStringLiteral("motion_lire"), motionLire}, {QStringLiteral("motion_ecrire"), motionEcrire},
        {QStringLiteral("motion_element"), motionElement}, {QStringLiteral("motion_rendre"), motionRendre},
        {QStringLiteral("motion_instantane"), motionInstantane}, {QStringLiteral("motion_verifier"), motionVerifier},
        {QStringLiteral("motion_poser"), motionPoser},
    };
    // Lots A et B (24 septembre 2026) : les autres fichiers d'outils.
    map.insert(montageTools());
    map.insert(mediaTools());
    map.insert(etalonnageTools());
    map.insert(gradeTools());
    map.insert(exportTools());
    map.insert(apercuTools());
    map.insert(scriptTools());
    map.insert(animationTools());
    map.insert(couleurTools());
    map.insert(organisationTools());
    map.insert(reglagesTools());
    map.insert(voixTools());
    map.insert(outilsTools());
    map.insert(detourageTools());
    map.insert(flouTools());
    return map;
}

const QMap<QString, Tool> &tools()
{
    static const QMap<QString, Tool> map = buildTools();
    return map;
}

} // namespace

namespace KoraCutAgentTools {

QString binFolderPath(const QString &binId)
{
    auto model = pCore ? pCore->projectItemModel() : nullptr;
    auto clip = model ? model->getClipByBinID(binId) : nullptr;
    if (!clip) {
        return QString();
    }
    QStringList parts;
    const auto root = model->getRootFolder();
    auto item = std::static_pointer_cast<AbstractProjectItem>(clip->parent());
    while (item && item != root && item->clipId() != QLatin1String("-1")) {
        parts.prepend(item->name());
        item = std::static_pointer_cast<AbstractProjectItem>(item->parent());
    }
    return parts.join(QLatin1Char('/'));
}

int parseTime(const QJsonValue &value, double fps)
{
    if (value.isDouble()) {
        const double d = value.toDouble();
        return (d >= 0 && qFuzzyCompare(d, std::floor(d))) ? int(d) : -1;
    }
    if (!value.isString()) {
        return -1;
    }
    static const QRegularExpression tc(QStringLiteral("^(\\d{2}):(\\d{2}):(\\d{2}):(\\d{2})$"));
    const QRegularExpressionMatch m = tc.match(value.toString());
    if (!m.hasMatch()) {
        return -1;
    }
    const int base = qRound(fps);
    const int frames = m.captured(4).toInt();
    if (frames >= base || m.captured(2).toInt() > 59 || m.captured(3).toInt() > 59) {
        return -1;
    }
    return ((m.captured(1).toInt() * 60 + m.captured(2).toInt()) * 60 + m.captured(3).toInt()) * base + frames;
}

QString timecode(int frames, double fps)
{
    const int base = std::max(1, int(qRound(fps)));
    const int f = frames % base;
    const int totalSeconds = frames / base;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(totalSeconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((totalSeconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg(f, 2, 10, QLatin1Char('0'));
}

QStringList implementedTools()
{
    return tools().keys();
}

void setMotionMirror(KoraCutMotionMirror *mirror)
{
    g_motionMirror = mirror;
}

KoraCutMotionMirror *motionMirror()
{
    return g_motionMirror;
}

QJsonObject call(const std::shared_ptr<TimelineItemModel> &timeline, const QString &name, const QJsonObject &args)
{
    // Le thème et les réglages de l'application ne demandent pas de séquence ouverte.
    if (!timeline && !reglagesToolNames().contains(name)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucune séquence ouverte."));
    }
    const auto it = tools().constFind(name);
    if (it == tools().constEnd()) {
        return error(QStringLiteral("outil_inconnu"), QStringLiteral("L'outil « %1 » n'existe pas dans l'application.").arg(name));
    }
    // Pendant un outil, aucune fenêtre à choix ne s'ouvre : le code qui en ouvrirait une prend sa
    // réponse par défaut (la même propriété que la porte de commande, document 18).
    const QVariant before = qApp ? qApp->property("koracut_sans_fenetre") : QVariant();
    if (qApp) {
        qApp->setProperty("koracut_sans_fenetre", true);
    }
    const QJsonObject result = it.value()(timeline, args);
    if (qApp) {
        qApp->setProperty("koracut_sans_fenetre", before);
    }
    return result;
}

} // namespace KoraCutAgentTools
