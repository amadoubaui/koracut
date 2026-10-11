/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Document 22, lot 3 : les outils d'étalonnage par nœuds (etal_*), qui pilotent le même moteur et
// le même graphe que le module Étalonnage. Contrat : Agents in-app/outils/etalonnage.json.
// Un appel = une étape d'annulation (KoraCutGradeController::setGraphsFor).

#include "koracutagenttools.h"
#include "koracutagenttools_p.h"

#include "color/koracutgradecontroller.h"
#include "color/koracutnodegraph.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include "kcgrade.h"

#include <KLocalizedString>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

#include <cmath>

namespace KoraCutAgentToolsPrivate {

namespace {

using namespace KoraCutNodeGraph;

// Le contrôleur travaille sur la séquence de l'outil le temps de l'appel.
struct Scope
{
    explicit Scope(const Timeline &t) { KoraCutGradeController::self()->setTimelineOverride(t); }
    ~Scope() { KoraCutGradeController::self()->setTimelineOverride(nullptr); }
};

// Le plan visé (clip_id), ou -1 pour la séquence ; une erreur si ni l'un ni l'autre ne va.
int target(const Timeline &timeline, const Args &args, QJsonObject &failure, bool modifies = true)
{
    if (args.value(QStringLiteral("sequence")).toBool(false)) {
        return -1;
    }
    if (!args.contains(QStringLiteral("clip_id"))) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("Donne clip_id (un plan) ou sequence = true."),
                        QStringLiteral("L'id d'un plan se lit dans etat_timeline."));
        return -2;
    }
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (!timeline->isClip(cid)) {
        failure = clipIdCheck(timeline, cid);
        return -2;
    }
    if (timeline->isAudioTrack(timeline->getClipTrackId(cid))) {
        failure = error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("Ce clip est un son : il n'a pas d'image à étalonner."),
                        QStringLiteral("Donne l'id du clip vidéo du plan."));
        return -2;
    }
    if (modifies) {
        if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
            failure = e;
            return -2;
        }
    }
    return cid;
}

QString compact(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// Le graphe tel que les agents le lisent : chaque nœud avec son numéro affiché et ses palettes.
QJsonArray nodesJson(const QJsonObject &g)
{
    QJsonArray out;
    for (int id : nodeIds(g)) {
        QJsonObject n = node(g, id);
        n.remove(QStringLiteral("x"));
        n.remove(QStringLiteral("y"));
        const int number = displayNumber(g, id);
        if (number > 0) {
            n.insert(QStringLiteral("numero"), QStringLiteral("%1").arg(number, 2, 10, QLatin1Char('0')));
        }
        n.insert(QStringLiteral("outils"), QJsonArray::fromStringList(usedTools(n)));
        out.append(n);
    }
    return out;
}

// Vérifie qu'un graphe se calcule, puis l'écrit en une étape.
QJsonObject write(int cid, const QJsonObject &g, const QString &gesture)
{
    std::string err;
    if (!kcgrade::Grade::compile(compact(g).toStdString(), &err)) {
        return error(QStringLiteral("entree_invalide"), QString::fromStdString(err), QStringLiteral("Relis le nœud avec etal_lire et corrige la valeur refusée."));
    }
    if (!KoraCutGradeController::self()->setGraphFor(cid, g, gesture)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le grade n'a pas pu être posé sur ce plan."));
    }
    return {};
}

// Le premier nœud de l'arbre : celui qui lit la source.
int firstNode(const QJsonObject &g)
{
    for (int id : nodeIds(g)) {
        const QJsonObject n = node(g, id);
        if (isCorrector(n) && n.value(QStringLiteral("entree")).toObject().value(QStringLiteral("rvb")).toInt(-1) == 0) {
            return id;
        }
    }
    return -1;
}

QJsonObject etalLire(const Timeline &timeline, const Args &args)
{
    Scope scope(timeline);
    QJsonObject failure;
    const int cid = target(timeline, args, failure, false);
    if (cid == -2) {
        return failure;
    }
    const QJsonObject g = KoraCutGradeController::self()->graphFor(cid);
    QJsonObject r{{QStringLiteral("ok"), true},
                  {QStringLiteral("sequence"), cid < 0},
                  {QStringLiteral("actif"), g.value(QStringLiteral("actif")).toBool(true)},
                  {QStringLiteral("sortie"), outputNode(g)},
                  {QStringLiteral("noeuds"), nodesJson(g)}};
    if (cid >= 0) {
        r.insert(QStringLiteral("clip_id"), cid);
    }
    return r;
}

QJsonObject etalNoeudAjouter(const Timeline &timeline, const Args &args)
{
    Scope scope(timeline);
    QJsonObject failure;
    const int cid = target(timeline, args, failure);
    if (cid == -2) {
        return failure;
    }
    QJsonObject g = KoraCutGradeController::self()->graphFor(cid);
    const QString type = args.value(QStringLiteral("type")).toString();
    const int from = args.contains(QStringLiteral("noeud_id")) ? args.value(QStringLiteral("noeud_id")).toInt() : outputNode(g);
    if (args.contains(QStringLiteral("noeud_id")) && !hasNode(g, from)) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun nœud %1 sur ce plan.").arg(from), QStringLiteral("Relis les nœuds avec etal_lire."));
    }
    int id = -1;
    if (type == QLatin1String("serie")) {
        id = addSerial(g, from);
    } else if (type == QLatin1String("serie_avant")) {
        id = addSerialBefore(g, from);
    } else if (type == QLatin1String("fin")) {
        id = appendNode(g);
    } else if (type == QLatin1String("parallele")) {
        id = addParallel(g, from);
    } else if (type == QLatin1String("calque")) {
        id = addLayer(g, from);
    } else if (type == QLatin1String("outside")) {
        id = addOutside(g, from);
    } else if (type == QLatin1String("separateur")) {
        id = addSplitterCombiner(g, from);
    }
    if (id < 0) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le geste « %1 » ne s'applique pas au nœud %2.").arg(type).arg(from),
                     QStringLiteral("Un nœud en parallèle, en calque ou outside part d'un correcteur, pas d'un mélangeur."));
    }
    const QString label = args.value(QStringLiteral("etiquette")).toString().trimmed();
    if (!label.isEmpty()) {
        setField(g, id, QStringLiteral("etiquette"), label);
    }
    if (QJsonObject e = write(cid, g, i18nc("@action undo text", "add node")); !e.isEmpty()) {
        return e;
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("noeud_id"), id},
                       {QStringLiteral("numero"), QStringLiteral("%1").arg(displayNumber(g, id), 2, 10, QLatin1Char('0'))},
                       {QStringLiteral("noeuds"), nodesJson(g)}};
}

// Fusionne une section : un objet complète celui du nœud (sous-objets compris), une liste le remplace.
QJsonValue merged(const QJsonValue &current, const QJsonValue &given)
{
    if (!given.isObject() || !current.isObject()) {
        return given;
    }
    QJsonObject out = current.toObject();
    const QJsonObject in = given.toObject();
    for (auto it = in.begin(); it != in.end(); ++it) {
        out.insert(it.key(), merged(out.value(it.key()), it.value()));
    }
    return out;
}

QJsonObject etalNoeudRegler(const Timeline &timeline, const Args &args)
{
    Scope scope(timeline);
    QJsonObject failure;
    const int cid = target(timeline, args, failure);
    if (cid == -2) {
        return failure;
    }
    QJsonObject g = KoraCutGradeController::self()->graphFor(cid);
    const int id = args.value(QStringLiteral("noeud_id")).toInt();
    QJsonObject n = node(g, id);
    if (n.isEmpty()) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun nœud %1 sur ce plan.").arg(id), QStringLiteral("Relis les nœuds avec etal_lire, ou crée-le avec etal_noeud_ajouter."));
    }
    const QStringList sections = {QStringLiteral("primaires"), QStringLiteral("log"),  QStringLiteral("courbes"), QStringLiteral("courbes_teinte"), QStringLiteral("qualificateur"),
                                  QStringLiteral("fenetres"),  QStringLiteral("flou"), QStringLiteral("cle"),     QStringLiteral("cst"),            QStringLiteral("lut")};
    bool changed = false;
    for (const QString &s : sections) {
        if (args.contains(s)) {
            if (!isCorrector(n)) {
                return error(QStringLiteral("entree_invalide"), QStringLiteral("Le nœud %1 est un mélangeur : il n'a pas de réglages de couleur.").arg(id),
                             QStringLiteral("Règle les nœuds qui entrent dans le mélangeur."));
            }
            n.insert(s, merged(n.value(s), args.value(s)));
            changed = true;
        }
    }
    for (const QString &k : {QStringLiteral("actif"), QStringLiteral("etiquette"), QStringLiteral("mode")}) {
        if (args.contains(k)) {
            n.insert(k, args.value(k));
            changed = true;
        }
    }
    if (!changed) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Aucun réglage donné."), QStringLiteral("Donne au moins une section (primaires, courbes…) ou actif, etiquette."));
    }
    putNode(g, n);
    if (QJsonObject e = write(cid, g, i18nc("@action undo text", "node settings")); !e.isEmpty()) {
        return e;
    }
    QJsonObject shown = n;
    shown.remove(QStringLiteral("x"));
    shown.remove(QStringLiteral("y"));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("noeud_id"), id},
                       {QStringLiteral("numero"), QStringLiteral("%1").arg(displayNumber(g, id), 2, 10, QLatin1Char('0'))},
                       {QStringLiteral("noeud"), shown}};
}

QJsonObject etalNoeudSupprimer(const Timeline &timeline, const Args &args)
{
    Scope scope(timeline);
    QJsonObject failure;
    const int cid = target(timeline, args, failure);
    if (cid == -2) {
        return failure;
    }
    QJsonObject g = KoraCutGradeController::self()->graphFor(cid);
    const int id = args.value(QStringLiteral("noeud_id")).toInt();
    if (!deleteNode(g, id)) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun nœud %1 sur ce plan.").arg(id), QStringLiteral("Relis les nœuds avec etal_lire."));
    }
    if (QJsonObject e = write(cid, g, i18nc("@action undo text", "delete node")); !e.isEmpty()) {
        return e;
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("noeuds"), nodesJson(g)}};
}

const QMap<QString, QPair<QString, QString>> &cameras()
{
    static const QMap<QString, QPair<QString, QString>> map{
        {QStringLiteral("sony_slog3_sgamut3cine"), {QStringLiteral("sgamut3cine"), QStringLiteral("slog3")}},
        {QStringLiteral("sony_slog3_sgamut3"), {QStringLiteral("sgamut3"), QStringLiteral("slog3")}},
        {QStringLiteral("sony_slog2"), {QStringLiteral("sgamut3"), QStringLiteral("slog2")}},
        {QStringLiteral("panasonic_vlog"), {QStringLiteral("vgamut"), QStringLiteral("vlog")}},
        {QStringLiteral("canon_log3"), {QStringLiteral("cinemagamut"), QStringLiteral("clog3")}},
        {QStringLiteral("canon_log2"), {QStringLiteral("cinemagamut"), QStringLiteral("clog2")}},
        {QStringLiteral("canon_log"), {QStringLiteral("cinemagamut"), QStringLiteral("clog")}},
        {QStringLiteral("arri_logc3"), {QStringLiteral("awg3"), QStringLiteral("logc3")}},
        {QStringLiteral("arri_logc4"), {QStringLiteral("awg4"), QStringLiteral("logc4")}},
        {QStringLiteral("blackmagic_film_gen5"), {QStringLiteral("bmdwg5"), QStringLiteral("bmdfilm5")}},
        {QStringLiteral("red_log3g10"), {QStringLiteral("rwg"), QStringLiteral("log3g10")}},
        {QStringLiteral("apple_log"), {QStringLiteral("rec2020"), QStringLiteral("applelog")}},
        {QStringLiteral("iphone_hdr_hlg"), {QStringLiteral("rec2020"), QStringLiteral("hlg")}},
        {QStringLiteral("dji_dlog"), {QStringLiteral("dgamut"), QStringLiteral("dlog")}},
        {QStringLiteral("nikon_nlog"), {QStringLiteral("rec2020"), QStringLiteral("nlog")}},
        {QStringLiteral("fujifilm_flog2"), {QStringLiteral("rec2020"), QStringLiteral("flog2")}},
        {QStringLiteral("fujifilm_flog"), {QStringLiteral("rec2020"), QStringLiteral("flog")}},
        {QStringLiteral("leica_llog"), {QStringLiteral("rec2020"), QStringLiteral("llog")}},
        {QStringLiteral("gopro_gplog2"), {QStringLiteral("rec2020"), QStringLiteral("gplog2")}},
        {QStringLiteral("davinci_intermediate"), {QStringLiteral("dwg"), QStringLiteral("davinci_intermediate")}},
    };
    return map;
}

// Pose dans @p g le CST demandé (camera, ou entree_gamut et entree_gamma) : dans noeud_id s'il est donné,
// sinon dans le nœud « CST » de tête, créé au besoin. Rend l'id du nœud, ou -1 et l'erreur dans @p failure.
int applyCst(QJsonObject &g, const Args &args, QJsonObject &failure, QJsonObject *written = nullptr)
{
    QString inGamut = args.value(QStringLiteral("entree_gamut")).toString();
    QString inGamma = args.value(QStringLiteral("entree_gamma")).toString();
    const QString camera = args.value(QStringLiteral("camera")).toString();
    if (!camera.isEmpty()) {
        inGamut = cameras().value(camera).first;
        inGamma = cameras().value(camera).second;
    }
    if (inGamut.isEmpty() || inGamma.isEmpty()) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("Donne camera, ou entree_gamut et entree_gamma."),
                        QStringLiteral("Ne devine pas le log : demande à la personne quelle caméra et quel profil ont servi."));
        return -1;
    }
    int id = args.value(QStringLiteral("noeud_id")).toInt(-1);
    if (id >= 0 && !hasNode(g, id)) {
        failure = error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun nœud %1 sur ce plan.").arg(id), QStringLiteral("Relis les nœuds avec etal_lire."));
        return -1;
    }
    if (id < 0) {
        // Le nœud « CST » de tête : réutilisé s'il existe, sinon le premier nœud s'il est vide, sinon un nouveau avant lui.
        for (int n : nodeIds(g)) {
            if (node(g, n).value(QStringLiteral("etiquette")).toString() == QLatin1String("CST")) {
                id = n;
            }
        }
        if (id < 0) {
            const int first = firstNode(g);
            if (first > 0 && usedTools(node(g, first)).isEmpty()) {
                id = first;
            } else if (first > 0) {
                id = addSerialBefore(g, first);
            } else {
                id = addSerial(g, 0);
            }
            setField(g, id, QStringLiteral("etiquette"), QStringLiteral("CST"));
        }
    }
    const QJsonObject cst{{QStringLiteral("actif"), true},
                          {QStringLiteral("entree_gamut"), inGamut},
                          {QStringLiteral("entree_gamma"), inGamma},
                          {QStringLiteral("sortie_gamut"), args.value(QStringLiteral("sortie_gamut")).toString(QStringLiteral("rec709"))},
                          {QStringLiteral("sortie_gamma"), args.value(QStringLiteral("sortie_gamma")).toString(QStringLiteral("gamma24"))},
                          {QStringLiteral("tone_mapping"), args.value(QStringLiteral("tone_mapping")).toString(QStringLiteral("davinci"))},
                          {QStringLiteral("gamut_mapping"), args.value(QStringLiteral("gamut_mapping")).toString(QStringLiteral("compression"))}};
    setSection(g, id, QStringLiteral("cst"), cst);
    if (written) {
        *written = cst;
    }
    return id;
}

QJsonObject etalCst(const Timeline &timeline, const Args &args)
{
    Scope scope(timeline);
    QJsonObject failure;
    const int cid = target(timeline, args, failure);
    if (cid == -2) {
        return failure;
    }
    QJsonObject g = KoraCutGradeController::self()->graphFor(cid);
    QJsonObject cst;
    const int id = applyCst(g, args, failure, &cst);
    if (id < 0) {
        return failure;
    }
    if (QJsonObject e = write(cid, g, i18nc("@action undo text", "color space transform")); !e.isEmpty()) {
        return e;
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("noeud_id"), id}, {QStringLiteral("cst"), cst}};
}

QJsonObject etalLut(const Timeline &timeline, const Args &args)
{
    Scope scope(timeline);
    QJsonObject failure;
    const int cid = target(timeline, args, failure);
    if (cid == -2) {
        return failure;
    }
    QString file = args.value(QStringLiteral("fichier")).toString().trimmed();
    if (file.startsWith(QLatin1String("~/"))) {
        file = QDir::homePath() + file.mid(1);
    }
    if (!QDir::isAbsolutePath(file)) {
        QString found;
        for (const QString &folder : KoraCutGradeController::lutFolders()) {
            for (const QString &cand : QStringList{file, file + QStringLiteral(".cube")}) {
                const QFileInfo fi(QDir(folder).filePath(cand));
                if (fi.exists()) {
                    found = fi.absoluteFilePath();
                    break;
                }
            }
            if (!found.isEmpty()) {
                break;
            }
        }
        if (found.isEmpty()) {
            return error(QStringLiteral("fichier_introuvable"), QStringLiteral("Aucune LUT « %1 » dans les dossiers de LUT (%2).").arg(file, KoraCutGradeController::lutFolders().join(QLatin1String(", "))),
                         QStringLiteral("Donne le chemin absolu du fichier. L'outil ne télécharge rien."));
        }
        file = found;
    }
    if (!QFileInfo::exists(file)) {
        return error(QStringLiteral("fichier_introuvable"), QStringLiteral("Le fichier %1 n'existe pas sur le disque.").arg(file),
                     QStringLiteral("N'invente pas de chemin : demande à la personne où est sa LUT."));
    }
    QJsonObject g = KoraCutGradeController::self()->graphFor(cid);
    int id = args.value(QStringLiteral("noeud_id")).toInt(-1);
    if (id >= 0 && !hasNode(g, id)) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun nœud %1 sur ce plan.").arg(id), QStringLiteral("Relis les nœuds avec etal_lire."));
    }
    if (id < 0) {
        id = appendNode(g);
        setField(g, id, QStringLiteral("etiquette"), QFileInfo(file).completeBaseName().left(24));
    }
    setSection(g, id, QStringLiteral("lut"), QJsonObject{{QStringLiteral("fichier"), file}, {QStringLiteral("melange"), args.value(QStringLiteral("melange")).toDouble(100.)}});
    if (QJsonObject e = write(cid, g, i18nc("@action undo text", "apply LUT")); !e.isEmpty()) {
        return e;
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("noeud_id"), id}, {QStringLiteral("fichier"), file}};
}

QJsonObject etalCopier(const Timeline &timeline, const Args &args)
{
    Scope scope(timeline);
    const int source = args.value(QStringLiteral("source_clip_id")).toInt(-1);
    QJsonObject failure;
    if (target(timeline, QJsonObject{{QStringLiteral("clip_id"), source}}, failure, false) == -2) {
        return failure;
    }
    QList<int> targets;
    for (const QJsonValue &v : args.value(QStringLiteral("clip_ids")).toArray()) {
        const int cid = v.toInt(-1);
        if (target(timeline, QJsonObject{{QStringLiteral("clip_id"), cid}}, failure) == -2) {
            return failure;
        }
        if (cid != source && !targets.contains(cid)) {
            targets << cid;
        }
    }
    const bool append = args.value(QStringLiteral("mode")).toString() == QLatin1String("ajouter");
    const int done = KoraCutGradeController::self()->copyGrade(source, targets, append);
    QJsonArray ids;
    for (int t : std::as_const(targets)) {
        ids.append(t);
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("source"), source}, {QStringLiteral("cibles"), ids}, {QStringLiteral("copies"), done}};
}

QJsonObject etalReinitialiser(const Timeline &timeline, const Args &args)
{
    Scope scope(timeline);
    QJsonObject failure;
    const int cid = target(timeline, args, failure);
    if (cid == -2) {
        return failure;
    }
    QJsonObject g = KoraCutGradeController::self()->graphFor(cid);
    const bool keep = args.value(QStringLiteral("garder_noeuds")).toBool(false);
    resetAll(g, keep);
    if (QJsonObject e = write(cid, g, keep ? i18nc("@action undo text", "reset grades, keep nodes") : i18nc("@action undo text", "reset grade")); !e.isEmpty()) {
        return e;
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("noeuds"), nodesJson(g)}};
}

// ---- etal_signature : l'étalonnage par défaut de KoraCut (demande d'Amadou Ba, 8 octobre 2026) ----
// Un nœud par intention : CST (seulement s'il existe ou s'il est demandé), HDR (les ombres remontent, les
// hautes lumières redescendent, le gamma est réglé à la mesure), Vif, Peau (qualificateur, fenêtre sur le
// visage s'il est donné), Sujet (une fenêtre qui éclaire, jamais rien qui assombrisse le reste).
// Valeurs revues deux fois le même jour après les retours d'Amadou Ba : la fiche etalonnage-signature fait foi.

constexpr double kSignatureLuma = 116.;         // luma moyenne visée, plan avec une personne
constexpr double kSignatureLumaNoPerson = 136.; // produit, table, écran : personne à l'image
constexpr double kSignatureTolerance = 1.5; // écart accepté à la cible
constexpr double kSignatureGammaLow = -0.45;
constexpr double kSignatureGammaHigh = 0.25;
constexpr int kSignatureMeasures = 7;

// Une fenêtre donnée par l'agent : x, y et taille entre 0 et 1, aspect facultatif.
bool readSpot(const Args &args, const QString &key, bool withAspect, QJsonObject &spot, QJsonObject &failure)
{
    if (!args.contains(key)) {
        return true;
    }
    const QJsonObject o = args.value(key).toObject();
    const double x = o.value(QStringLiteral("x")).toDouble(-1.);
    const double y = o.value(QStringLiteral("y")).toDouble(-1.);
    const double size = o.value(QStringLiteral("taille")).toDouble(-1.);
    const double aspect = o.value(QStringLiteral("aspect")).toDouble(1.);
    if (x < 0. || x > 1. || y < 0. || y > 1. || size < 0.02 || size > 1. || (withAspect && (aspect < 0.2 || aspect > 5.))) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("%1 se donne en { x, y, taille } entre 0 et 1 dans l'image (taille au moins 0,02).").arg(key),
                        QStringLiteral("x et y sont le centre, origine en haut à gauche. Un visage en plan taille : taille 0,30 à 0,36."));
        return false;
    }
    spot = QJsonObject{{QStringLiteral("x"), x}, {QStringLiteral("y"), y}, {QStringLiteral("taille"), size}};
    if (withAspect && o.contains(QStringLiteral("aspect"))) {
        spot.insert(QStringLiteral("aspect"), aspect);
    }
    return true;
}

QJsonObject circle(double x, double y, double size, double aspect, double soft)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("cercle")}, {QStringLiteral("x"), x},        {QStringLiteral("y"), y},      {QStringLiteral("taille"), size},
                       {QStringLiteral("aspect"), aspect},                 {QStringLiteral("rotation"), 0}, {QStringLiteral("doux"), soft}};
}

struct Signature
{
    bool replace = true;
    bool withCst = false;
    QJsonObject face;    // vide : pas de fenêtre sur la peau
    QJsonObject subject; // vide : la fenêtre du sujet reprend le visage, ou le nœud Sujet est omis
};

// Écrit l'arbre de la signature dans @p g. Rend l'id du nœud HDR (son gamma reste à régler), ou -1.
int buildSignature(QJsonObject &g, const Signature &s, const Args &args, QJsonObject &failure, bool *strong = nullptr)
{
    if (s.replace) {
        // L'arbre repart de zéro, sauf le nœud « CST » de tête : on ne perd pas la conversion d'un plan log.
        QJsonObject kept;
        for (int id : nodeIds(g)) {
            const QJsonObject n = node(g, id);
            if (isCorrector(n) && n.value(QStringLiteral("etiquette")).toString() == QLatin1String("CST")) {
                kept = n;
                break;
            }
        }
        g = defaultGraph();
        if (!kept.isEmpty()) {
            QJsonObject first = node(g, 1);
            static const QStringList structure{QStringLiteral("id"), QStringLiteral("type"), QStringLiteral("entree"), QStringLiteral("entrees"),
                                               QStringLiteral("x"),  QStringLiteral("y"),    QStringLiteral("cle")};
            for (auto it = kept.begin(); it != kept.end(); ++it) {
                if (!structure.contains(it.key())) {
                    first.insert(it.key(), it.value());
                }
            }
            putNode(g, first);
        }
    }
    if (s.withCst) {
        Args cst = args;
        cst.remove(QStringLiteral("noeud_id"));
        if (applyCst(g, cst, failure) < 0) {
            return -1;
        }
    }
    // Le nœud HDR : le dernier nœud s'il est encore vide et sans nom, sinon un nœud de plus à la fin.
    const QJsonObject last = node(g, outputNode(g));
    int hdr = -1;
    if (isCorrector(last) && usedTools(last).isEmpty() && last.value(QStringLiteral("etiquette")).toString().isEmpty()) {
        hdr = outputNode(g);
    } else {
        hdr = appendNode(g);
    }
    const int vif = hdr > 0 ? appendNode(g) : -1;
    const int skin = vif > 0 ? appendNode(g) : -1;
    if (skin < 0) {
        failure = error(QStringLiteral("non_disponible"), QStringLiteral("Les nœuds n'ont pas pu être ajoutés à cet arbre."),
                        QStringLiteral("Relance avec remplacer = true, ou range l'arbre avec etal_reinitialiser."));
        return -1;
    }
    setField(g, hdr, QStringLiteral("etiquette"), QStringLiteral("HDR"));
    // HDR fort pour un plan qui passe par un CST (log, HDR de téléphone) ; plus doux pour un plan SDR, qui a
    // moins de matière dans les ombres et les hautes lumières (mesuré sur un plan Rec.709, 8 octobre 2026).
    bool converted = false;
    for (int id : nodeIds(g)) {
        const QJsonObject cst = node(g, id).value(QStringLiteral("cst")).toObject();
        converted = converted || (!cst.value(QStringLiteral("entree_gamma")).toString().isEmpty() && cst.value(QStringLiteral("actif")).toBool(true));
    }
    if (strong) {
        *strong = converted;
    }
    setSection(g, hdr, QStringLiteral("primaires"),
               QJsonObject{{QStringLiteral("ombres"), converted ? 85 : 45},
                           {QStringLiteral("hautes_lumieres"), converted ? -90 : -40},
                           {QStringLiteral("mid_detail"), converted ? 6 : 4},
                           {QStringLiteral("contraste"), 1.0},
                           {QStringLiteral("gamma"), QJsonArray{0, 0, 0, 0}}});
    setField(g, vif, QStringLiteral("etiquette"), QStringLiteral("Vif"));
    setSection(g, vif, QStringLiteral("primaires"), QJsonObject{{QStringLiteral("saturation"), 54}, {QStringLiteral("boost"), 6}});
    setField(g, skin, QStringLiteral("etiquette"), QStringLiteral("Peau"));
    setSection(g, skin, QStringLiteral("primaires"), QJsonObject{{QStringLiteral("mid_detail"), -28}, {QStringLiteral("gamma"), QJsonArray{0.008, 0, 0, 0}}});
    setSection(g, skin, QStringLiteral("qualificateur"),
               QJsonObject{{QStringLiteral("actif"), true},
                           {QStringLiteral("mode"), QStringLiteral("hsl")},
                           {QStringLiteral("teinte"), QJsonObject{{QStringLiteral("centre"), 24}, {QStringLiteral("largeur"), 16}, {QStringLiteral("doux"), 8}, {QStringLiteral("sym"), 50}}},
                           {QStringLiteral("sat"), QJsonObject{{QStringLiteral("bas"), 8}, {QStringLiteral("haut"), 80}, {QStringLiteral("doux_bas"), 6}, {QStringLiteral("doux_haut"), 10}}},
                           {QStringLiteral("lum"), QJsonObject{{QStringLiteral("bas"), 6}, {QStringLiteral("haut"), 88}, {QStringLiteral("doux_bas"), 5}, {QStringLiteral("doux_haut"), 8}}},
                           {QStringLiteral("flou"), 14}});
    const double fx = s.face.value(QStringLiteral("x")).toDouble();
    const double fy = s.face.value(QStringLiteral("y")).toDouble();
    const double fs = s.face.value(QStringLiteral("taille")).toDouble();
    if (!s.face.isEmpty()) {
        setSection(g, skin, QStringLiteral("fenetres"), QJsonArray{circle(fx, fy, fs, 0.8, fs * 0.8)});
    }
    if (!s.subject.isEmpty() || !s.face.isEmpty()) {
        // Pas de nœud outside, pas de vignette : le reste ne s'assombrit jamais.
        const int subject = appendNode(g);
        setField(g, subject, QStringLiteral("etiquette"), QStringLiteral("Sujet"));
        if (!s.subject.isEmpty()) {
            // Un objet, un écran : contraste, couleur et netteté. Ni gain ni gamma, qui dessinaient un halo.
            setSection(g, subject, QStringLiteral("primaires"), QJsonObject{{QStringLiteral("contraste"), 1.16}, {QStringLiteral("saturation"), 60}});
            setSection(g, subject, QStringLiteral("flou"), QJsonObject{{QStringLiteral("rayon"), 0.45}});
        } else {
            // Un visage : la fenêtre l'éclaire.
            setSection(g, subject, QStringLiteral("primaires"),
                       QJsonObject{{QStringLiteral("gamma"), QJsonArray{0.04, 0, 0, 0}}, {QStringLiteral("gain"), QJsonArray{1.03, 1, 1, 1}}});
        }
        if (!s.subject.isEmpty()) {
            const double size = s.subject.value(QStringLiteral("taille")).toDouble();
            setSection(g, subject, QStringLiteral("fenetres"),
                       QJsonArray{circle(s.subject.value(QStringLiteral("x")).toDouble(), s.subject.value(QStringLiteral("y")).toDouble(), size,
                                         s.subject.value(QStringLiteral("aspect")).toDouble(1.), size * 1.2)});
        } else {
            setSection(g, subject, QStringLiteral("fenetres"), QJsonArray{circle(fx, fy, fs * 1.1, 0.8, fs * 1.4)});
        }
    }
    return hdr;
}

// Un plan que « tous » étalonne : un fichier vidéo ou une image, pas un clip Motion, un titre ni une couleur.
bool signatureSource(const Timeline &timeline, int cid)
{
    const auto bin = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(cid));
    if (!bin) {
        return false;
    }
    const ClipType::ProducerType type = bin->clipType();
    if (type != ClipType::AV && type != ClipType::Video && type != ClipType::Image && type != ClipType::SlideShow) {
        return false;
    }
    return bin->getProducerProperty(QStringLiteral("kdenlive:koracut_motion_project")).isEmpty();
}

// Les nœuds d'un plan, en court : le détail se lit avec etal_lire.
QJsonArray nodesBrief(const QJsonObject &g)
{
    QJsonArray out;
    for (int id : nodeIds(g)) {
        const QJsonObject n = node(g, id);
        QJsonObject o{{QStringLiteral("id"), id}, {QStringLiteral("etiquette"), n.value(QStringLiteral("etiquette")).toString()}};
        const int number = displayNumber(g, id);
        if (number > 0) {
            o.insert(QStringLiteral("numero"), QStringLiteral("%1").arg(number, 2, 10, QLatin1Char('0')));
        }
        out.append(o);
    }
    return out;
}

QJsonObject etalSignature(const Timeline &timeline, const Args &args)
{
    Scope scope(timeline);
    const bool all = args.value(QStringLiteral("tous")).toBool(false);
    if (all == args.contains(QStringLiteral("clip_id"))) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Donne clip_id (un plan) ou tous = true, l'un ou l'autre."),
                     QStringLiteral("tous = true étalonne tous les plans vidéo de la séquence ; l'id d'un plan se lit dans etat_timeline."));
    }
    const bool noPerson = args.value(QStringLiteral("sans_personne")).toBool(false);
    const double wanted = args.value(QStringLiteral("luma_cible")).toDouble(noPerson ? kSignatureLumaNoPerson : kSignatureLuma);
    if (wanted < 95. || wanted > 160.) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("luma_cible se donne entre 95 et 160 (défaut 116, ou 136 avec sans_personne)."),
                     QStringLiteral("116 pour un plan avec une personne, 136 pour un produit, une table ou un écran qui remplit l'image. Clair ne veut pas dire surexposé."));
    }
    Signature s;
    s.replace = args.value(QStringLiteral("remplacer")).toBool(true);
    s.withCst = args.contains(QStringLiteral("camera")) || args.contains(QStringLiteral("entree_gamut")) || args.contains(QStringLiteral("entree_gamma"));
    QJsonObject failure;
    if (!readSpot(args, QStringLiteral("visage"), false, s.face, failure) || !readSpot(args, QStringLiteral("sujet"), true, s.subject, failure)) {
        return failure;
    }
    if (all && (!s.face.isEmpty() || !s.subject.isEmpty())) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("visage et sujet désignent un endroit de l'image d'un seul plan : ils ne vont pas avec tous = true."),
                     QStringLiteral("Pose d'abord la signature sur tous les plans, puis rappelle l'outil avec clip_id et visage sur les plans qui le demandent."));
    }

    // Les plans : celui qu'on donne, ou tous les fichiers vidéo et images de la séquence. Un sujet détouré
    // (piste « Sujets détourés ») reprend ensuite le grade du plan qu'il recouvre, pour rester raccord.
    QList<int> clips;
    QList<int> cutouts;
    int skipped = 0;
    if (all) {
        const QString cutoutMark = KoraCutAgentTools::subjectTrackProperty();
        const QList<int> candidates = KoraCutGradeController::self()->videoClips();
        for (int cid : candidates) {
            QJsonObject ignored;
            if (!signatureSource(timeline, cid) || target(timeline, QJsonObject{{QStringLiteral("clip_id"), cid}}, ignored) == -2) {
                ++skipped;
            } else if (!timeline->getTrackProperty(timeline->getClipTrackId(cid), cutoutMark).toString().isEmpty()) {
                cutouts << cid;
            } else {
                clips << cid;
            }
        }
        if (clips.isEmpty()) {
            return error(QStringLiteral("pas_de_matiere"), QStringLiteral("Aucun plan vidéo ni image à étalonner dans cette séquence."),
                         QStringLiteral("Les clips Motion, les titres et les couleurs ne reçoivent pas cet étalonnage. Pour un plan précis, donne clip_id."));
        }
    } else {
        const int cid = target(timeline, args, failure);
        if (cid == -2) {
            return failure;
        }
        clips << cid;
    }

    auto *controller = KoraCutGradeController::self();
    GradeProbe probe(timeline);
    QList<QPair<int, QJsonObject>> graphs;
    QJsonArray plans;
    for (int cid : std::as_const(clips)) {
        QJsonObject g = controller->graphFor(cid);
        bool strong = false;
        const int hdr = buildSignature(g, s, args, failure, &strong);
        if (hdr < 0) {
            return failure;
        }
        // Le gamma du nœud HDR, par moitiés successives, jusqu'à la luma moyenne visée.
        const double before = probe.meanLuma(cid);
        double low = kSignatureGammaLow;
        double high = kSignatureGammaHigh;
        double best = 0.;
        double bestLuma = -1.;
        for (int i = 0; i < kSignatureMeasures && before >= 0.; ++i) {
            const double gamma = std::round((low + high) * 500.) / 1000.;
            setValue(g, hdr, QStringLiteral("primaires"), QStringLiteral("gamma"), QJsonArray{gamma, 0, 0, 0});
            const double luma = probe.meanLuma(cid, compact(g));
            if (luma < 0.) {
                break;
            }
            if (bestLuma < 0. || std::abs(luma - wanted) < std::abs(bestLuma - wanted)) {
                best = gamma;
                bestLuma = luma;
            }
            if (std::abs(luma - wanted) < kSignatureTolerance) {
                break;
            }
            if (luma > wanted) {
                high = gamma;
            } else {
                low = gamma;
            }
        }
        setValue(g, hdr, QStringLiteral("primaires"), QStringLiteral("gamma"), QJsonArray{best, 0, 0, 0});
        std::string err;
        if (!kcgrade::Grade::compile(compact(g).toStdString(), &err)) {
            return error(QStringLiteral("entree_invalide"), QString::fromStdString(err), QStringLiteral("Relis le plan avec etal_lire."));
        }
        graphs.append({cid, g});
        QJsonObject plan{{QStringLiteral("clip_id"), cid},
                         {QStringLiteral("gamma"), best},
                         {QStringLiteral("hdr"), strong ? QStringLiteral("fort") : QStringLiteral("sdr")},
                         {QStringLiteral("noeuds"), nodesBrief(g)}};
        if (before >= 0.) {
            plan.insert(QStringLiteral("luma_avant"), std::round(before * 10.) / 10.);
        }
        if (bestLuma >= 0.) {
            plan.insert(QStringLiteral("luma_apres"), std::round(bestLuma * 10.) / 10.);
            if (std::abs(bestLuma - wanted) >= kSignatureTolerance) {
                plan.insert(QStringLiteral("avertissement"), QStringLiteral("La cible n'est pas atteinte dans la plage du gamma (-0,45 à +0,25) : regarde le plan, ou change luma_cible."));
            }
        } else {
            plan.insert(QStringLiteral("avertissement"), QStringLiteral("Aucune image n'a pu être mesurée : le gamma du nœud HDR est resté à 0. Règle-le avec mesurer_image et etal_noeud_regler."));
        }
        plans.append(plan);
    }
    QList<QPair<int, QJsonObject>> matches;
    for (int cid : std::as_const(cutouts)) {
        const int middle = timeline->getClipPosition(cid) + timeline->getClipPlaytime(cid) / 2;
        for (const auto &[under, g] : std::as_const(graphs)) {
            if (middle >= timeline->getClipPosition(under) && middle < timeline->getClipPosition(under) + timeline->getClipPlaytime(under)) {
                matches.append({cid, g});
                break;
            }
        }
    }
    const int matched = int(matches.size());
    graphs.append(matches);
    if (!controller->setGraphsFor(graphs, i18nc("@action undo text", "KoraCut grade (HDR, vivid, skin)"))) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("L'étalonnage n'a pas pu être posé sur tous les plans."), QStringLiteral("Relis les plans avec etal_lire."));
    }
    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("luma_cible"), wanted},
                       {QStringLiteral("plans"), plans},
                       {QStringLiteral("conseil"), QStringLiteral("Exporte quelques images et regarde-les zone par zone (zone sombre, sujet, zone claire) avant de livrer.")}};
    if (all) {
        result.insert(QStringLiteral("ignores"), skipped + int(cutouts.size()) - matched);
        result.insert(QStringLiteral("sujets_detoures_raccordes"), matched);
    }
    return result;
}

QJsonObject etalExporterLut(const Timeline &timeline, const Args &args)
{
    Scope scope(timeline);
    QJsonObject failure;
    const int cid = target(timeline, args, failure, false);
    if (cid == -2) {
        return failure;
    }
    QString file = args.value(QStringLiteral("fichier")).toString().trimmed();
    if (file.startsWith(QLatin1String("~/"))) {
        file = QDir::homePath() + file.mid(1);
    }
    if (!QDir::isAbsolutePath(file) || !file.endsWith(QLatin1String(".cube"), Qt::CaseInsensitive)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Donne un chemin absolu qui finit par .cube."));
    }
    if (QFileInfo::exists(file)) {
        return error(QStringLiteral("fichier_existe"), QStringLiteral("Le fichier %1 existe déjà.").arg(file), QStringLiteral("Choisis un autre nom : l'outil n'écrase jamais un fichier."));
    }
    if (!QFileInfo(file).dir().exists()) {
        return error(QStringLiteral("fichier_introuvable"), QStringLiteral("Le dossier %1 n'existe pas.").arg(QFileInfo(file).absolutePath()));
    }
    const int size = args.value(QStringLiteral("taille")).toInt(33);
    std::string err;
    auto grade = kcgrade::Grade::compile(compact(KoraCutGradeController::self()->graphFor(cid)).toStdString(), &err);
    if (!grade) {
        return error(QStringLiteral("entree_invalide"), QString::fromStdString(err));
    }
    if (!grade->exportCube(file.toStdString(), size, &err)) {
        return error(QStringLiteral("non_disponible"), QString::fromStdString(err), QStringLiteral("Exporte un grade sans fenêtre ni flou, ou retire-les d'une copie du plan."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("fichier"), file}, {QStringLiteral("taille"), size}};
}

} // namespace

QMap<QString, Tool> gradeTools()
{
    return {
        {QStringLiteral("etal_lire"), etalLire},
        {QStringLiteral("etal_noeud_ajouter"), etalNoeudAjouter},
        {QStringLiteral("etal_noeud_regler"), etalNoeudRegler},
        {QStringLiteral("etal_noeud_supprimer"), etalNoeudSupprimer},
        {QStringLiteral("etal_cst"), etalCst},
        {QStringLiteral("etal_lut"), etalLut},
        {QStringLiteral("etal_copier"), etalCopier},
        {QStringLiteral("etal_reinitialiser"), etalReinitialiser},
        {QStringLiteral("etal_exporter_lut"), etalExporterLut},
        {QStringLiteral("etal_signature"), etalSignature},
    };
}

} // namespace KoraCutAgentToolsPrivate
