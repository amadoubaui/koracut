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

#include "core.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include "kcgrade.h"

#include <KLocalizedString>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>

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

QJsonObject etalCst(const Timeline &timeline, const Args &args)
{
    Scope scope(timeline);
    QJsonObject failure;
    const int cid = target(timeline, args, failure);
    if (cid == -2) {
        return failure;
    }
    QString inGamut = args.value(QStringLiteral("entree_gamut")).toString();
    QString inGamma = args.value(QStringLiteral("entree_gamma")).toString();
    const QString camera = args.value(QStringLiteral("camera")).toString();
    if (!camera.isEmpty()) {
        inGamut = cameras().value(camera).first;
        inGamma = cameras().value(camera).second;
    }
    if (inGamut.isEmpty() || inGamma.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Donne camera, ou entree_gamut et entree_gamma."),
                     QStringLiteral("Ne devine pas le log : demande à la personne quelle caméra et quel profil ont servi."));
    }
    QJsonObject g = KoraCutGradeController::self()->graphFor(cid);
    int id = args.value(QStringLiteral("noeud_id")).toInt(-1);
    if (id >= 0 && !hasNode(g, id)) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun nœud %1 sur ce plan.").arg(id), QStringLiteral("Relis les nœuds avec etal_lire."));
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
    };
}

} // namespace KoraCutAgentToolsPrivate
