/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Lot 5 du document 19 (24 septembre 2026) : les animations élément par élément. Demande d'Amadou Ba :
// HyperFrames ne fait que ce qu'on lui demande et le pose exactement sur la timeline (un élément = une
// composition = un clip, une transition = la transition seule, à cheval sur la coupe), et les agents
// savent faire la même animation sans HyperFrames, avec l'application seule (effet Transformer, images
// clés, titres, fondus enchaînés). Contrat : outils/motion.json (motion_element_seul, motion_transition,
// motion_ouvrir) et outils/animation.json (animer_entree, animer_sortie, animer_texte, transition_app).
// Règle : un appel = une étape d'annulation.

#include "koracutagenttools.h"
#include "koracutagenttools_p.h"

#include "assets/keyframes/model/keyframemodel.hpp"
#include "assets/keyframes/model/keyframemodellist.hpp"
#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "koracut/motion/koracutmotioncatalog.h"
#include "koracut/motion/koracutmotionmirror.h"
#include "koracut/motion/koracutmotionproject.h"
#include "koracut/motion/koracutmotionrenderer.h"
#include "koracut/motion/koracutmotionsync.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

using namespace KoraCutAgentToolsPrivate;

namespace {

// ---------------------------------------------------------------------------------------------
// Les aides

// Le clip qui touche la fin de @p cid sur sa piste, -1 sinon.
int nextOnTrack(const Timeline &timeline, int cid)
{
    const int end = timeline->getClipPosition(cid) + timeline->getClipPlaytime(cid);
    const int next = timeline->getClipByPosition(timeline->getClipTrackId(cid), end);
    return (next != -1 && next != cid && timeline->getClipPosition(next) == end) ? next : -1;
}

// La coupe entre deux plans : clip_avant et clip_apres (ou le plan qui suit clip_avant), collés sur la même
// piste vidéo. @return la position de la coupe, -1 et une erreur sinon.
int cutBetween(const Timeline &timeline, const Args &args, int &before, int &after, QJsonObject &failure)
{
    before = args.value(QStringLiteral("clip_avant")).toInt(-1);
    if (QJsonObject e = clipIdCheck(timeline, before); !e.isEmpty()) {
        failure = e;
        return -1;
    }
    if (timeline->isAudioTrack(timeline->getClipTrackId(before))) {
        failure = error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("clip_avant est un clip audio : une transition se pose entre deux plans vidéo."),
                        QStringLiteral("Donne l'id du clip vidéo du plan."));
        return -1;
    }
    after = args.contains(QStringLiteral("clip_apres")) ? args.value(QStringLiteral("clip_apres")).toInt(-1) : nextOnTrack(timeline, before);
    if (after < 0 || !timeline->isClip(after)) {
        failure = error(QStringLiteral("pas_de_coupe"), QStringLiteral("Aucun plan ne suit le clip %1 sur sa piste.").arg(before),
                        QStringLiteral("Une transition se pose sur une coupe : deux plans collés sur la même piste. Referme le vide d'abord (supprimer_espace)."));
        return -1;
    }
    const int end = timeline->getClipPosition(before) + timeline->getClipPlaytime(before);
    if (timeline->getClipTrackId(after) != timeline->getClipTrackId(before) || timeline->getClipPosition(after) != end) {
        failure = error(QStringLiteral("pas_de_coupe"), QStringLiteral("Les clips %1 et %2 ne se touchent pas sur la même piste.").arg(before).arg(after),
                        QStringLiteral("Relis etat_timeline : clip_apres commence là où clip_avant finit (pos + dur)."));
        return -1;
    }
    return end;
}

QSize frameSize()
{
    const QSize size = pCore->getCurrentFrameSize();
    return size.isValid() ? size : QSize(1920, 1080);
}

KoraCutMotionMirror *mirrorOrFail(QJsonObject &failure)
{
    KoraCutMotionMirror *mirror = KoraCutAgentTools::motionMirror();
    if (!mirror || !mirror->document()) {
        failure = error(QStringLiteral("non_disponible"), QStringLiteral("Le module Motion n'est pas disponible (aucun projet ouvert)."));
        return nullptr;
    }
    mirror->rescan();
    mirror->flush();
    return mirror;
}

// Un nom de composition libre : « base », puis « base-2 », « base-3 »…
QString freeName(KoraCutMotionMirror *mirror, const QString &base)
{
    const QString clean = KoraCutMotionProject::sanitizeName(base);
    QString name = clean;
    int n = 2;
    // Une composition retirée garde son dossier (Rétablir la remet) : la suivante prend un numéro.
    while (mirror->projects().contains(name) || QFile::exists(mirror->motionRoot() + QLatin1Char('/') + name + QStringLiteral("/index.html"))) {
        name = QStringLiteral("%1-%2").arg(clean).arg(n++);
    }
    return name;
}

void readColors(const Args &args, KoraCutMotionCatalog::Spec &spec)
{
    const QJsonObject colors = args.value(QStringLiteral("couleurs")).toObject();
    if (colors.contains(QStringLiteral("accent"))) {
        spec.accent = colors.value(QStringLiteral("accent")).toString();
    }
    if (colors.contains(QStringLiteral("texte"))) {
        spec.textColor = colors.value(QStringLiteral("texte")).toString();
    }
    if (colors.contains(QStringLiteral("fond"))) {
        spec.background = colors.value(QStringLiteral("fond")).toString();
    }
}

QString renderStateName(const KoraCutMotionMirror::ElementState &e)
{
    if (e.pending) {
        return QStringLiteral("en_cours");
    }
    if (e.failed) {
        return QStringLiteral("echec");
    }
    return e.renderPath.isEmpty() ? QStringLiteral("aucun") : QStringLiteral("pret");
}

// Ce que les outils rendent d'une composition d'un seul élément : son nom, son clip, sa place.
QJsonObject singleJson(const Timeline &, KoraCutMotionMirror *mirror, const QString &name)
{
    const KoraCutMotionMirror::ProjectState state = mirror->projectState(name);
    QJsonObject o{{QStringLiteral("nom"), name},
                  {QStringLiteral("affichage"), state.display},
                  {QStringLiteral("sorte"), state.element},
                  {QStringLiteral("style"), state.style},
                  {QStringLiteral("dossier"), state.dir},
                  {QStringLiteral("fichier"), state.indexPath}};
    const auto elements = mirror->elements(name);
    if (!elements.isEmpty()) {
        const KoraCutMotionMirror::ElementState &e = elements.first();
        o.insert(QStringLiteral("element"), e.id);
        o.insert(QStringLiteral("rendu"), renderStateName(e));
        if (!e.text.isEmpty()) {
            o.insert(QStringLiteral("texte"), e.text);
        }
    }
    o.insert(QStringLiteral("sequence"), mirror->sequenceOf(name));
    // Le clip est dans la séquence de la composition (celle qui était devant à sa création).
    const Timeline timeline = mirror->timelineFor(name);
    if (state.clipId >= 0 && timeline && timeline->isClip(state.clipId)) {
        o.insert(QStringLiteral("clip_id"), state.clipId);
        o.insert(QStringLiteral("piste_id"), timeline->getClipTrackId(state.clipId));
        o.insert(QStringLiteral("piste"), trackLabel(timeline, timeline->getClipTrackId(state.clipId)));
        o.insert(QStringLiteral("position"), time(timeline->getClipPosition(state.clipId)));
        o.insert(QStringLiteral("duree"), time(timeline->getClipPlaytime(state.clipId)));
    }
    if (state.cut >= 0) {
        o.insert(QStringLiteral("coupe"), time(int(std::lround(state.cut * projectFps()))));
    }
    if (const QString followed = mirror->followedTransition(name); !followed.isEmpty()) {
        o.insert(QStringLiteral("suit_transition"), followed);
    }
    if (!state.errors.isEmpty()) {
        o.insert(QStringLiteral("erreurs"), QJsonArray::fromStringList(state.errors));
    }
    return o;
}

// Crée la composition, la rend (en attendant si demandé) et rend ce qu'il en est.
QJsonObject createSingle(const Timeline &timeline, KoraCutMotionMirror *mirror, const QString &name, const QString &html, int trackId, int before, int after,
                         bool wait)
{
    QString why;
    const QSize size = frameSize();
    const QString created = mirror->createProject(name, &why, size.width(), size.height(), projectFps(), html);
    if (created.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), why.isEmpty() ? QStringLiteral("La composition n'a pas pu être créée.") : why);
    }
    mirror->setElementPlacement(created, trackId, before, after);
    const bool canRender = KoraCutMotionHost::state() == KoraCutMotionHost::State::Ready || KoraCutMotionRenderer::hasSimulator();
    bool finished = false;
    if (canRender && wait) {
        // Le rendu d'un élément seul dure quelques secondes : on attend (5 minutes au plus), puis que son clip
        // soit posé, pour que la réponse donne clip_id.
        finished = mirror->renderer()->waitForIdle(300000);
        const auto elements = mirror->elements(created);
        if (!elements.isEmpty()) {
            mirror->waitForClip(created, elements.first().id, 20000);
        }
    }
    QJsonObject result = singleJson(timeline, mirror, created);
    result.insert(QStringLiteral("ok"), true);
    if (!canRender) {
        result.insert(QStringLiteral("remarque"), KoraCutMotionHost::state() == KoraCutMotionHost::State::NoNode
                                                      ? QStringLiteral("Node.js n'est pas installé : la composition est écrite, elle sera rendue quand il le sera.")
                                                      : QStringLiteral("HyperFrames n'est pas installé (bouton « Installer HyperFrames » du panneau Motion) : la composition est écrite."));
    } else if (wait && !result.contains(QStringLiteral("clip_id"))) {
        result.insert(QStringLiteral("remarque"), finished ? QStringLiteral("Le rendu est fini mais le clip n'est pas encore posé : relis motion_lister dans un instant.")
                                                           : QStringLiteral("Le rendu n'est pas fini : relis motion_lister dans un instant."));
        if (!mirror->renderer()->lastError().isEmpty() && mirror->projectState(created).failed > 0) {
            result.insert(QStringLiteral("derniere_erreur"), mirror->renderer()->lastError());
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// motion_element_seul

QJsonObject motionElementSeul(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    KoraCutMotionMirror *mirror = mirrorOrFail(failure);
    if (!mirror) {
        return failure;
    }
    const QString wanted = args.value(QStringLiteral("nom")).toString();
    const QString clean = KoraCutMotionProject::sanitizeName(wanted);
    if (clean.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le nom « %1 » ne donne aucun nom de dossier utilisable.").arg(wanted),
                     QStringLiteral("Des lettres, des chiffres et des tirets : « titre-ouverture »."));
    }
    if (mirror->projects().contains(clean) && !mirror->projectState(clean).retired) {
        return error(QStringLiteral("existe_deja"), QStringLiteral("Une composition « %1 » existe déjà.").arg(clean),
                     QStringLiteral("Change-la avec motion_element, ou choisis un autre nom."));
    }
    const int position = requiredTime(args, QStringLiteral("position"), failure);
    if (position < 0) {
        return failure;
    }
    const int duration = requiredTime(args, QStringLiteral("duree"), failure);
    if (duration < 2) {
        return failure.isEmpty() ? error(QStringLiteral("entree_invalide"), QStringLiteral("La durée doit faire au moins deux images.")) : failure;
    }
    int trackId = -1;
    if (args.contains(QStringLiteral("piste_id"))) {
        trackId = args.value(QStringLiteral("piste_id")).toInt(-1);
        if (QJsonObject e = trackIdCheck(timeline, trackId); !e.isEmpty()) {
            return e;
        }
        if (timeline->isAudioTrack(trackId)) {
            return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("%1 est une piste audio : un élément animé va sur une piste vidéo.").arg(trackLabel(timeline, trackId)));
        }
        for (int cid : clipsOnTrack(timeline, trackId)) {
            const int start = timeline->getClipPosition(cid);
            const int end = start + timeline->getClipPlaytime(cid);
            if (start < position + duration && end > position) {
                return error(QStringLiteral("place_occupee"), QStringLiteral("La piste %1 est occupée de %2 à %3.").arg(trackLabel(timeline, trackId)).arg(start).arg(end - 1),
                             QStringLiteral("Laisse piste_id vide : l'élément va sur la piste « Animations », créée au besoin."));
            }
        }
    }
    const QString kind = args.value(QStringLiteral("sorte")).toString(QStringLiteral("titre"));
    KoraCutMotionCatalog::Spec spec;
    spec.id = clean;
    spec.element = kind;
    spec.style = args.value(QStringLiteral("style")).toString();
    spec.text = args.value(QStringLiteral("texte")).toString().replace(QLatin1String("\\n"), QLatin1String("\n"));
    const QSize size = frameSize();
    spec.sequence = pCore->currentTimelineId().toString(QUuid::WithoutBraces);
    spec.width = size.width();
    spec.height = size.height();
    spec.fps = projectFps();
    spec.duration = duration / projectFps();
    spec.start = position / projectFps();
    readColors(args, spec);

    // Tout ce qui peut refuser est vérifié avant de toucher au disque : le logo, les fichiers en plus.
    const QString dir = mirror->motionRoot() + QLatin1Char('/') + clean;
    QString logo;
    if (args.contains(QStringLiteral("fichier"))) {
        logo = resolveUserPath(args.value(QStringLiteral("fichier")).toString(), failure);
        if (logo.isEmpty()) {
            return failure;
        }
        if (!QFileInfo::exists(logo)) {
            return error(QStringLiteral("fichier_introuvable"), QStringLiteral("Le fichier %1 n'existe pas.").arg(logo));
        }
    }
    QJsonArray copied;
    if (QJsonObject e = copyMotionAssets(args, dir, copied, true); !e.isEmpty()) {
        return e;
    }
    if (!logo.isEmpty()) {
        spec.file = QStringLiteral("assets/") + QFileInfo(logo).fileName();
    }

    QString html;
    QString why;
    const QString brief = args.value(QStringLiteral("brief_html")).toString();
    if (!brief.trimmed().isEmpty()) {
        spec.element = kind.isEmpty() ? QStringLiteral("autre") : kind;
        html = KoraCutMotionCatalog::wrapBrief(brief, spec, &why);
    } else if (kind == QLatin1String("autre")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Un élément « autre » s'écrit avec brief_html."),
                     QStringLiteral("Sinon choisis une sorte du catalogue : %1.").arg(KoraCutMotionCatalog::elementKinds().join(QLatin1String(", "))));
    } else {
        html = KoraCutMotionCatalog::elementHtml(spec, &why);
    }
    if (html.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), why, QStringLiteral("Lis la description de l'outil : sorte, style, texte, fichier, brief_html."));
    }
    // Le nom d'une composition retirée est libre : son dossier part dans la corbeille du projet.
    QString trashed;
    if (!releaseMotionName(mirror, clean, trashed)) {
        return error(QStringLiteral("existe_deja"), QStringLiteral("Une composition « %1 » existe déjà.").arg(clean));
    }
    // Les polices et les images sont copiées dans assets/ avant la création, pour que le rendu les trouve.
    if (QJsonObject e = copyMotionAssets(args, dir, copied); !e.isEmpty()) {
        return e;
    }
    if (!logo.isEmpty()) {
        const QString source = logo;
        QDir().mkpath(dir + QStringLiteral("/assets"));
        const QString target = dir + QStringLiteral("/assets/") + QFileInfo(source).fileName();
        if (QFileInfo(source).absoluteFilePath() != QFileInfo(target).absoluteFilePath()) {
            QFile::remove(target);
            QFile::copy(source, target);
        }
    }

    QJsonObject result = createSingle(timeline, mirror, clean, html, trackId, -1, -1, args.value(QStringLiteral("attendre")).toBool(true));
    if (result.value(QStringLiteral("ok")).toBool()) {
        if (!copied.isEmpty()) {
            result.insert(QStringLiteral("fichiers_copies"), copied);
        }
        if (!trashed.isEmpty()) {
            result.insert(QStringLiteral("ancienne_retiree"), trashed);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// motion_transition

QJsonObject motionTransition(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    KoraCutMotionMirror *mirror = mirrorOrFail(failure);
    if (!mirror) {
        return failure;
    }
    int before = -1;
    int after = -1;
    const int cut = cutBetween(timeline, args, before, after, failure);
    if (cut < 0) {
        return failure;
    }
    const QString style = args.value(QStringLiteral("style")).toString(KoraCutMotionCatalog::transitionStyles().first());
    if (!KoraCutMotionCatalog::transitionStyles().contains(style)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("style vaut %1.").arg(KoraCutMotionCatalog::transitionStyles().join(QLatin1String(", "))));
    }
    int duration = int(std::lround(KoraCutMotionCatalog::transitionDefaultDuration(style) * projectFps()));
    if (args.contains(QStringLiteral("duree"))) {
        duration = KoraCutAgentTools::parseTime(args.value(QStringLiteral("duree")), projectFps());
        if (duration < 4) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("duree fait au moins 4 images (en images ou en timecode)."));
        }
    }
    const int half = duration / 2;
    if (cut - half < 0) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("La transition commencerait avant le début de la timeline."), QStringLiteral("Raccourcis-la."));
    }
    KoraCutMotionCatalog::Spec spec;
    const QString name = freeName(mirror, args.contains(QStringLiteral("nom")) ? args.value(QStringLiteral("nom")).toString() : QStringLiteral("transition-%1").arg(style));
    if (name.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le nom ne donne aucun nom de dossier utilisable."));
    }
    spec.id = name;
    spec.element = QStringLiteral("transition");
    spec.style = style;
    const QSize size = frameSize();
    spec.sequence = pCore->currentTimelineId().toString(QUuid::WithoutBraces);
    spec.width = size.width();
    spec.height = size.height();
    spec.fps = projectFps();
    spec.duration = duration / projectFps();
    spec.start = (cut - half) / projectFps();
    spec.cut = cut / projectFps();
    // Les volets des cordes sont sombres par défaut (#1E1E1E, le fond de l'application).
    if (style == QLatin1String("cordes")) {
        spec.background = QStringLiteral("#1E1E1E");
    } else if (style == QLatin1String("voile")) {
        spec.textColor = QStringLiteral("#EDEDED");
    }
    readColors(args, spec);
    QString why;
    const QString html = KoraCutMotionCatalog::transitionHtml(spec, &why);
    if (html.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), why);
    }
    QJsonObject result = createSingle(timeline, mirror, name, html, -1, before, after, args.value(QStringLiteral("attendre")).toBool(true));
    if (!result.value(QStringLiteral("ok")).toBool()) {
        return result;
    }
    result.insert(QStringLiteral("clip_avant"), before);
    result.insert(QStringLiteral("clip_apres"), after);
    result.insert(QStringLiteral("piste_dessous_id"), mirror->animationsTrack());
    result.insert(QStringLiteral("conseil"), QStringLiteral("Elle suit la coupe si on la déplace. Un élément qui doit s'accorder à elle : motion_element_seul sans piste_id, il va sur « Animations », sous la transition, et la suit aussi quand la coupe bouge."));
    return result;
}

// ---------------------------------------------------------------------------------------------
// motion_ouvrir

QJsonObject motionOuvrir(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    KoraCutMotionMirror *mirror = mirrorOrFail(failure);
    if (!mirror) {
        return failure;
    }
    QString project;
    QString elementId;
    int cid = -1;
    if (args.contains(QStringLiteral("clip_id"))) {
        cid = args.value(QStringLiteral("clip_id")).toInt(-1);
        const auto info = mirror->clipInfo(cid);
        if (info.project.isEmpty()) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Le clip %1 n'est pas un élément Motion.").arg(cid), QStringLiteral("Relis motion_lister (champ clip_id)."));
        }
        project = info.project;
        elementId = info.elementId;
    } else {
        project = KoraCutMotionProject::sanitizeName(args.value(QStringLiteral("nom")).toString());
        if (project.isEmpty() || !mirror->projects().contains(project)) {
            return error(QStringLiteral("projet_inconnu"), QStringLiteral("Aucune composition Motion ne s'appelle « %1 ».").arg(args.value(QStringLiteral("nom")).toString()),
                         QStringLiteral("Relis motion_lister (champ nom)."));
        }
        const auto elements = mirror->elements(project);
        elementId = args.value(QStringLiteral("id")).toString();
        if (elementId.isEmpty() && !elements.isEmpty()) {
            elementId = elements.first().id;
        }
        cid = mirror->clipFor(project, elementId);
    }
    if (cid < 0) {
        return error(QStringLiteral("pas_pose"), QStringLiteral("L'élément « %1 » n'a pas de clip dans la timeline.").arg(elementId),
                     QStringLiteral("Rends-le (motion_rendre) ou pose-le (motion_poser) d'abord."));
    }
    // Comme un double-clic sur le clip : l'inspecteur s'ouvre sur lui, le panneau Motion montre sa
    // composition seule (Code, Studio sur son dossier). Rien d'autre ne change.
    if (pCore->window()) {
        // Le clip devient la sélection (la pile d'effets sous l'inspecteur est la sienne), puis l'inspecteur
        // prend son texte.
        pCore->selectTimelineItem(cid);
        pCore->window()->koraCutEditMotionClip(cid);
    } else {
        mirror->announceOpened(project, elementId);
    }
    const KoraCutMotionMirror::ProjectState state = mirror->projectState(project);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("nom"), project},
                       {QStringLiteral("element"), elementId},
                       {QStringLiteral("affichage"), state.display.isEmpty() ? project : state.display},
                       {QStringLiteral("clip_id"), cid},
                       {QStringLiteral("dossier_studio"), state.dir},
                       {QStringLiteral("fichier_code"), state.indexPath},
                       {QStringLiteral("ouvert"), mirror->lastOpenedProject()},
                       {QStringLiteral("panneau"), mirror->panelProject()}};
}

// ---------------------------------------------------------------------------------------------
// Les gestes d'animation de l'application : l'effet Transformer (qtblend, paramètre rect « x y l h opacité »)
// et ses images clés, avec les interpolations de Kdenlive.

struct Rect
{
    double x = 0, y = 0, w = 0, h = 0, o = 1;
    QString text() const
    {
        return QStringLiteral("%1 %2 %3 %4 %5").arg(qRound(x)).arg(qRound(y)).arg(qRound(w)).arg(qRound(h)).arg(QString::number(std::clamp(o, 0., 1.), 'f', 3));
    }
    Rect scaled(double s) const
    {
        Rect r = *this;
        r.w = w * s;
        r.h = h * s;
        r.x = x + (w - r.w) / 2;
        r.y = y + (h - r.h) / 2;
        return r;
    }
    Rect moved(double dx, double dy) const
    {
        Rect r = *this;
        r.x += dx;
        r.y += dy;
        return r;
    }
    Rect faded(double opacity) const
    {
        Rect r = *this;
        r.o = opacity;
        return r;
    }
};

Rect parseRect(const QString &text, const QSize &size)
{
    Rect r{0, 0, double(size.width()), double(size.height()), 1};
    const QStringList parts = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() >= 4) {
        r.x = parts.at(0).toDouble();
        r.y = parts.at(1).toDouble();
        r.w = parts.at(2).toDouble();
        r.h = parts.at(3).toDouble();
        r.o = parts.size() >= 5 ? parts.at(4).toDouble() : 1.;
    }
    return r;
}

struct Key
{
    int frame;
    Rect rect;
    KeyframeType::KeyframeEnum type;
};

QList<GenTime> keyTimes(const KeyframeModel &model)
{
    QList<GenTime> list;
    GenTime cursor(-1);
    bool ok = true;
    while (ok) {
        const Keyframe next = model.getNextKeyframe(cursor, &ok);
        if (!ok) {
            break;
        }
        list << next.first;
        cursor = next.first;
    }
    return list;
}

// L'effet Transformer du clip (le réveille s'il est intégré et éteint, le pose sinon) et son modèle
// d'images clés du paramètre rect.
std::shared_ptr<EffectItemModel> transformEffect(const Timeline &timeline, int cid, Fun &undo, Fun &redo)
{
    const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(cid);
    for (const auto &e : visibleEffects(stack)) {
        if (e->getAssetId() == QLatin1String("qtblend")) {
            return e;
        }
    }
    const QSize size = frameSize();
    const QString full = QStringLiteral("0 0 %1 %2 1").arg(size.width()).arg(size.height());
    std::shared_ptr<EffectItemModel> effect = appendEffect(stack, QStringLiteral("qtblend"), {{QStringLiteral("rect"), full}}, undo, redo);
    if (!effect) {
        return effect;
    }
    // Un Transformer tout juste posé garde une image clé de départ sans valeur (« 0= ») dans son modèle
    // d'images clés : la première image clé posée ensuite réécrit l'animation depuis ce modèle, et le plan
    // perd sa place jusqu'à elle (vu le 24 septembre 2026 sur un titre, à l'essai grandeur nature). On
    // donne une valeur à toute image clé vide.
    const QModelIndex index = effect->getParamIndexFromName(QStringLiteral("rect"));
    const std::shared_ptr<KeyframeModelList> models = effect->getKeyframeModel();
    if (KeyframeModel *model = models ? models->getKeyModel(QPersistentModelIndex(index)) : nullptr) {
        for (const GenTime &t : keyTimes(*model)) {
            if (model->getInterpolatedValue(t).toString().trimmed().isEmpty()) {
                model->updateKeyframe(t, QVariant(full), undo, redo);
            }
        }
    }
    return effect;
}

// Le rect à cette image du clip (images clés comprises).
Rect rectAt(const Timeline &timeline, int cid, const std::shared_ptr<EffectItemModel> &effect, int frame)
{
    const QSize size = frameSize();
    const QModelIndex index = effect->getParamIndexFromName(QStringLiteral("rect"));
    const std::shared_ptr<KeyframeModelList> models = effect->getKeyframeModel();
    KeyframeModel *keys = models ? models->getKeyModel(QPersistentModelIndex(index)) : nullptr;
    if (!keys) {
        return parseRect(effect->getParamFromName(QStringLiteral("rect")).toString(), size);
    }
    const GenTime pos(timeline->getClipIn(cid) + frame, projectFps());
    return parseRect(keys->getInterpolatedValue(pos).toString(), size);
}

// Pose des images clés de rect aux images données (depuis le début du clip), en retirant celles qui étaient
// entre @p clearFrom et @p clearTo (bornes exclues). Tout va dans undo/redo.
bool setRectKeys(const Timeline &timeline, int cid, const std::shared_ptr<EffectItemModel> &effect, const QList<Key> &keys, int clearFrom, int clearTo, Fun &undo,
                 Fun &redo)
{
    const QModelIndex index = effect->getParamIndexFromName(QStringLiteral("rect"));
    const std::shared_ptr<KeyframeModelList> models = effect->getKeyframeModel();
    KeyframeModel *model = models ? models->getKeyModel(QPersistentModelIndex(index)) : nullptr;
    if (!model) {
        return false;
    }
    const double fps = projectFps();
    const int in = timeline->getClipIn(cid);
    QSet<int> ours;
    for (const Key &k : keys) {
        ours.insert(k.frame);
    }
    for (const GenTime &t : keyTimes(*model)) {
        const int rel = t.frames(fps) - in;
        if (rel > clearFrom && rel < clearTo && !ours.contains(rel)) {
            models->removeKeyframeWithUndo(t, undo, redo);
        }
    }
    for (const Key &k : keys) {
        const GenTime pos(in + k.frame, fps);
        if (!model->hasKeyframe(pos)) {
            bool ok = false;
            const Keyframe closest = model->getClosestKeyframe(pos, &ok);
            if (!ok || !models->duplicateKeyframeWithUndo(closest.first, pos, undo, redo)) {
                return false;
            }
        }
        if (!model->updateKeyframe(pos, QVariant(k.rect.text()), undo, redo) || !model->updateKeyframeType(pos, int(k.type), undo, redo)) {
            return false;
        }
    }
    return true;
}

// Les styles des gestes, communs à l'entrée et à la sortie. @p base est la place du plan quand il est posé ;
// la fonction rend la place hors champ (ou réduite, ou transparente) d'où il arrive, ou où il part.
bool offRect(const QString &style, const Rect &base, bool entry, Rect &off, KeyframeType::KeyframeEnum &ease)
{
    const QSize size = frameSize();
    const double W = size.width();
    const double H = size.height();
    ease = entry ? KeyframeType::CubicOut : KeyframeType::CubicIn;
    if (style == QLatin1String("fondu")) {
        off = base.faded(0);
    } else if (style == QLatin1String("glisse_gauche")) {
        off = base.moved(-W, 0);
    } else if (style == QLatin1String("glisse_droite")) {
        off = base.moved(W, 0);
    } else if (style == QLatin1String("glisse_haut")) {
        off = base.moved(0, -H);
    } else if (style == QLatin1String("glisse_bas")) {
        off = base.moved(0, H);
    } else if (style == QLatin1String("glisse_court")) {
        // Un petit pas et un fondu : le geste des titres et des bandeaux.
        off = base.moved(entry ? -W * 0.04 : W * 0.03, 0).faded(0);
    } else if (style == QLatin1String("monte")) {
        off = base.moved(0, entry ? H * 0.05 : -H * 0.04).faded(0);
    } else if (style == QLatin1String("zoom_avant")) {
        off = base.scaled(entry ? 0.8 : 1.15).faded(0);
        ease = entry ? KeyframeType::ExponentialOut : KeyframeType::ExponentialIn;
    } else if (style == QLatin1String("zoom_arriere")) {
        off = base.scaled(entry ? 1.2 : 0.85).faded(0);
        ease = entry ? KeyframeType::ExponentialOut : KeyframeType::ExponentialIn;
    } else if (style == QLatin1String("rebond")) {
        off = base.moved(0, H);
        ease = entry ? KeyframeType::BounceOut : KeyframeType::CubicIn;
    } else if (style == QLatin1String("pop")) {
        off = base.scaled(0.2).faded(0);
        ease = entry ? KeyframeType::ElasticOut : KeyframeType::ExponentialIn;
    } else {
        return false;
    }
    return true;
}

const QStringList &gestureStyles()
{
    static const QStringList list = {QStringLiteral("fondu"),        QStringLiteral("glisse_gauche"), QStringLiteral("glisse_droite"), QStringLiteral("glisse_haut"),
                                     QStringLiteral("glisse_bas"),   QStringLiteral("glisse_court"),  QStringLiteral("monte"),         QStringLiteral("zoom_avant"),
                                     QStringLiteral("zoom_arriere"), QStringLiteral("rebond"),        QStringLiteral("pop")};
    return list;
}

int gestureDuration(const Args &args, int clipLength, QJsonObject &failure)
{
    int duration = int(std::lround(0.5 * projectFps()));
    if (args.contains(QStringLiteral("duree"))) {
        duration = KoraCutAgentTools::parseTime(args.value(QStringLiteral("duree")), projectFps());
        if (duration < 1) {
            failure = error(QStringLiteral("entree_invalide"), QStringLiteral("duree est un nombre d'images (au moins 1) ou un timecode."));
            return -1;
        }
    }
    if (duration >= clipLength) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("Le geste (%1 images) est plus long que le clip (%2 images).").arg(duration).arg(clipLength),
                        QStringLiteral("0,4 à 0,6 s suffisent : 10 à 15 images à 25 i/s."));
        return -1;
    }
    return duration;
}

// Un geste d'entrée ou de sortie sur un clip, dans undo/redo. @return une erreur, vide si tout va bien.
QJsonObject gesture(const Timeline &timeline, int cid, const QString &style, bool entry, int duration, Fun &undo, Fun &redo)
{
    const int length = timeline->getClipPlaytime(cid);
    const std::shared_ptr<EffectItemModel> effect = transformEffect(timeline, cid, undo, redo);
    if (!effect) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet Transformer (qtblend) n'a pas pu être posé sur ce clip."));
    }
    const int landing = entry ? duration : length - 1 - duration;
    const Rect base = rectAt(timeline, cid, effect, landing);
    Rect off;
    KeyframeType::KeyframeEnum ease;
    if (!offRect(style, base, entry, off, ease)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("style vaut %1.").arg(gestureStyles().join(QLatin1String(", "))));
    }
    QList<Key> keys;
    if (entry) {
        keys = {{0, off, ease}, {duration, base, KeyframeType::Linear}};
    } else {
        keys = {{landing, base, ease}, {length - 1, off, KeyframeType::Linear}};
    }
    // L'image clé d'arrivée garde l'interpolation qu'avait la suite du clip.
    if (!setRectKeys(timeline, cid, effect, keys, entry ? 0 : landing, entry ? duration : length - 1, undo, redo)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Les images clés n'ont pas pu être posées sur le clip %1.").arg(cid));
    }
    return {};
}

QJsonObject gestureTool(const Timeline &timeline, const Args &args, bool entry)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    if (timeline->isAudioTrack(timeline->getClipTrackId(cid))) {
        return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("Un clip audio n'a pas d'image à animer."), QStringLiteral("Pour le son : ajouter_fondu."));
    }
    const QString style = args.value(QStringLiteral("style")).toString(entry ? QStringLiteral("glisse_court") : QStringLiteral("fondu"));
    QJsonObject failure;
    const int duration = gestureDuration(args, timeline->getClipPlaytime(cid), failure);
    if (duration < 0) {
        return failure;
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (QJsonObject e = gesture(timeline, cid, style, entry, duration, undo, redo); !e.isEmpty()) {
        undo();
        return e;
    }
    pCore->pushUndo(undo, redo, entry ? i18nc("@action undo text", "Animate entrance (%1)", style) : i18nc("@action undo text", "Animate exit (%1)", style));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("clip_id"), cid},
                       {QStringLiteral("style"), style},
                       {QStringLiteral("duree"), time(duration)},
                       {QStringLiteral("effet"), QStringLiteral("qtblend")},
                       {QStringLiteral("conseil"), QStringLiteral("Rien à rendre : c'est l'effet Transformer du clip, visible tout de suite. etat_timeline niveau complet montre ses images clés.")}};
}

QJsonObject animerEntree(const Timeline &timeline, const Args &args)
{
    return gestureTool(timeline, args, true);
}

QJsonObject animerSortie(const Timeline &timeline, const Args &args)
{
    return gestureTool(timeline, args, false);
}

// ---------------------------------------------------------------------------------------------
// animer_texte : la machine à écrire du titreur de Kdenlive (lettre, mot, ligne), ou un geste d'entrée

QJsonObject animerTexte(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    const QString style = args.value(QStringLiteral("style")).toString(QStringLiteral("machine_a_ecrire"));
    int mode = 0;
    if (style == QLatin1String("machine_a_ecrire")) {
        mode = 1;
    } else if (style == QLatin1String("mot_a_mot")) {
        mode = 2;
    } else if (style == QLatin1String("ligne_a_ligne")) {
        mode = 3;
    }
    if (mode == 0) {
        // Les autres styles sont des gestes d'entrée sur le titre : monte, fondu, zoom_avant, glisse_court…
        if (!gestureStyles().contains(style)) {
            return error(QStringLiteral("entree_invalide"),
                         QStringLiteral("style vaut machine_a_ecrire, mot_a_mot, ligne_a_ligne, ou un geste : %1.").arg(gestureStyles().join(QLatin1String(", "))));
        }
        return gestureTool(timeline, args, true);
    }
    const auto bin = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(cid));
    if (!bin || bin->clipType() != ClipType::Text) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le clip %1 n'est pas un titre de KoraCut.").arg(cid),
                     QStringLiteral("La machine à écrire s'applique à un titre posé par ajouter_titre. Pour un autre plan : animer_entree."));
    }
    const QString before = bin->getProducerProperty(QStringLiteral("xmldata"));
    QDomDocument doc;
    if (!doc.setContent(before)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le titre n'a pas pu être lu."));
    }
    // Le pas : en combien d'images chaque lettre (mot, ligne) arrive, pour que tout le texte soit là à la fin
    // de la durée demandée (par défaut, la moitié du clip, 2 s au plus).
    const int length = timeline->getClipPlaytime(cid);
    int duration = std::min(length / 2, int(std::lround(2 * projectFps())));
    if (args.contains(QStringLiteral("duree"))) {
        duration = KoraCutAgentTools::parseTime(args.value(QStringLiteral("duree")), projectFps());
        if (duration < 1 || duration >= length) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("duree va de 1 image à la longueur du clip (%1 images).").arg(length));
        }
    }
    const QDomNodeList contents = doc.elementsByTagName(QStringLiteral("content"));
    if (contents.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le titre n'a pas de texte."));
    }
    int units = 0;
    for (int i = 0; i < contents.count(); ++i) {
        const QString text = contents.at(i).toElement().text();
        if (mode == 1) {
            units = std::max(units, int(text.size()));
        } else if (mode == 2) {
            units = std::max(units, int(text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts).size()));
        } else {
            units = std::max(units, int(text.split(QLatin1Char('\n')).size()));
        }
    }
    const int step = std::max(1, duration / std::max(1, units));
    for (int i = 0; i < contents.count(); ++i) {
        contents.at(i).toElement().setAttribute(QStringLiteral("typewriter"), QStringLiteral("1;%1;%2;0;0").arg(step).arg(mode));
    }
    const QString after = doc.toString();
    const QString binId = bin->clipId();
    Fun redo = [binId, after]() {
        if (auto clip = pCore->projectItemModel()->getClipByBinID(binId)) {
            clip->setProperties({{QStringLiteral("xmldata"), after}}, true);
        }
        return true;
    };
    Fun undo = [binId, before]() {
        if (auto clip = pCore->projectItemModel()->getClipByBinID(binId)) {
            clip->setProperties({{QStringLiteral("xmldata"), before}}, true);
        }
        return true;
    };
    redo();
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Animate title (%1)", style));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("clip_id"), cid},
                       {QStringLiteral("style"), style},
                       {QStringLiteral("pas_images"), step},
                       {QStringLiteral("duree"), time(std::min(length, step * units))},
                       {QStringLiteral("conseil"), QStringLiteral("Le titre du chutier change : les autres clips du même titre écrivent aussi. Rien à rendre.")}};
}

// ---------------------------------------------------------------------------------------------
// transition_app : les transitions de l'application, sans HyperFrames

QJsonObject transitionApp(const Timeline &timeline, const Args &args)
{
    int before = -1;
    int after = -1;
    QJsonObject failure;
    const int cut = cutBetween(timeline, args, before, after, failure);
    if (cut < 0) {
        return failure;
    }
    const QString style = args.value(QStringLiteral("style")).toString(QStringLiteral("fondu_enchaine"));
    int duration = int(std::lround(0.5 * projectFps()));
    if (args.contains(QStringLiteral("duree"))) {
        duration = KoraCutAgentTools::parseTime(args.value(QStringLiteral("duree")), projectFps());
        if (duration < 2) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("duree fait au moins 2 images (en images ou en timecode)."));
        }
    }
    // Les fondus enchaînés de Kdenlive (mixClip) : ils demandent de la matière en réserve des deux côtés.
    if (style == QLatin1String("fondu_enchaine") || style == QLatin1String("volet") || style == QLatin1String("glissement")) {
        const QString type = style == QLatin1String("fondu_enchaine") ? QStringLiteral("fondu") : style;
        QJsonObject result = KoraCutAgentTools::call(timeline, QStringLiteral("ajouter_transition"), {{QStringLiteral("clip_id"), before}, {QStringLiteral("duree"), duration}, {QStringLiteral("type"), type}});
        if (result.value(QStringLiteral("ok")).toBool()) {
            result.insert(QStringLiteral("style"), style);
            result.insert(QStringLiteral("coupe"), time(cut));
        } else if (result.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString() == QLatin1String("pas_de_matiere")) {
            QJsonObject e = result.value(QStringLiteral("erreur")).toObject();
            e.insert(QStringLiteral("conseil"), QString(e.value(QStringLiteral("conseil")).toString() + QStringLiteral(" Sans réserve : style zoom, pousse, secousse ou fondu_noir, qui n'en demandent pas.")));
            result.insert(QStringLiteral("erreur"), e);
        }
        return result;
    }
    // Les autres se font sur les bords des deux plans, avec l'effet Transformer : pas de réserve, rien à rendre.
    const int half = std::max(1, duration / 2);
    const int lengthBefore = timeline->getClipPlaytime(before);
    const int lengthAfter = timeline->getClipPlaytime(after);
    if (half >= lengthBefore || half >= lengthAfter) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("La transition est plus longue que l'un des deux plans."), QStringLiteral("Raccourcis-la : 10 à 16 images."));
    }
    const QSize size = frameSize();
    const double W = size.width();
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    const auto effectBefore = transformEffect(timeline, before, undo, redo);
    const auto effectAfter = transformEffect(timeline, after, undo, redo);
    if (!effectBefore || !effectAfter) {
        undo();
        return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet Transformer n'a pas pu être posé sur les deux plans."));
    }
    const int outFrom = lengthBefore - 1 - half;
    const Rect baseBefore = rectAt(timeline, before, effectBefore, outFrom);
    const Rect baseAfter = rectAt(timeline, after, effectAfter, half);
    QList<Key> keysBefore;
    QList<Key> keysAfter;
    if (style == QLatin1String("fondu_noir")) {
        keysBefore = {{outFrom, baseBefore, KeyframeType::CubicIn}, {lengthBefore - 1, baseBefore.faded(0), KeyframeType::Linear}};
        keysAfter = {{0, baseAfter.faded(0), KeyframeType::CubicOut}, {half, baseAfter, KeyframeType::Linear}};
    } else if (style == QLatin1String("zoom")) {
        keysBefore = {{outFrom, baseBefore, KeyframeType::ExponentialIn}, {lengthBefore - 1, baseBefore.scaled(1.4), KeyframeType::Linear}};
        keysAfter = {{0, baseAfter.scaled(1.4), KeyframeType::ExponentialOut}, {half, baseAfter, KeyframeType::Linear}};
    } else if (style == QLatin1String("pousse")) {
        keysBefore = {{outFrom, baseBefore, KeyframeType::ExponentialIn}, {lengthBefore - 1, baseBefore.moved(-W, 0), KeyframeType::Linear}};
        keysAfter = {{0, baseAfter.moved(W, 0), KeyframeType::ExponentialOut}, {half, baseAfter, KeyframeType::Linear}};
    } else if (style == QLatin1String("secousse")) {
        // Une image sur deux, un écart qui grandit vers la coupe puis décroît après elle.
        const double amp = W * 0.025;
        const int pattern[] = {1, -1, 1, -1};
        int n = 0;
        for (int f = outFrom; f < lengthBefore - 1; f += 2, ++n) {
            const double k = double(f - outFrom + 1) / half;
            keysBefore << Key{f, baseBefore.moved(pattern[n % 4] * amp * k, pattern[(n + 1) % 4] * amp * 0.5 * k).scaled(1 + 0.04 * k), KeyframeType::Linear};
        }
        keysBefore << Key{lengthBefore - 1, baseBefore.scaled(1.04).moved(amp, 0), KeyframeType::Linear};
        n = 0;
        for (int f = 0; f < half; f += 2, ++n) {
            const double k = 1. - double(f) / half;
            keysAfter << Key{f, baseAfter.moved(-pattern[n % 4] * amp * k, pattern[(n + 1) % 4] * amp * 0.5 * k).scaled(1 + 0.04 * k), KeyframeType::Linear};
        }
        keysAfter << Key{half, baseAfter, KeyframeType::Linear};
    } else {
        undo();
        return error(QStringLiteral("entree_invalide"), QStringLiteral("style vaut fondu_enchaine, volet, glissement, fondu_noir, zoom, pousse ou secousse."));
    }
    if (!setRectKeys(timeline, before, effectBefore, keysBefore, outFrom, lengthBefore - 1, undo, redo) ||
        !setRectKeys(timeline, after, effectAfter, keysAfter, 0, half, undo, redo)) {
        undo();
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Les images clés n'ont pas pu être posées."));
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Transition (%1)", style));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("style"), style},
                       {QStringLiteral("clip_avant"), before},
                       {QStringLiteral("clip_apres"), after},
                       {QStringLiteral("coupe"), time(cut)},
                       {QStringLiteral("duree"), time(half * 2)},
                       {QStringLiteral("conseil"), QStringLiteral("Rien à rendre : les images clés de l'effet Transformer des deux plans. Elles suivent les plans si on les déplace.")}};
}

} // namespace

namespace KoraCutAgentToolsPrivate {

QMap<QString, Tool> animationTools()
{
    return {
        {QStringLiteral("motion_element_seul"), motionElementSeul}, {QStringLiteral("motion_transition"), motionTransition},
        {QStringLiteral("motion_ouvrir"), motionOuvrir},            {QStringLiteral("animer_entree"), animerEntree},
        {QStringLiteral("animer_sortie"), animerSortie},            {QStringLiteral("animer_texte"), animerTexte},
        {QStringLiteral("transition_app"), transitionApp},
    };
}

} // namespace KoraCutAgentToolsPrivate
