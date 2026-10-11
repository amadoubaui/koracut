/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Document 22, lot 2 : les gestes de l'éditeur de nœuds sur le graphe, et le contrôleur du module
// Étalonnage (une étape d'annulation par geste, le filtre koracut_grade en tête de pile, la copie de grade).

#include "catch.hpp"
#include "test_utils.hpp"

#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "koracut/color/koracutgradecontroller.h"
#include "koracut/color/koracutnodegraph.h"

#include "kcgrade.h"
#include "koracut/koracutagenttools.h"

#include <QFileInfo>
#include <QTemporaryDir>

#include <QJsonArray>
#include <QJsonDocument>

using namespace KoraCutNodeGraph;

namespace {
bool compiles(const QJsonObject &g)
{
    std::string error;
    auto grade = kcgrade::Grade::compile(QJsonDocument(g).toJson(QJsonDocument::Compact).toStdString(), &error);
    INFO(error);
    return bool(grade);
}

std::shared_ptr<EffectItemModel> gradeOf(const std::shared_ptr<EffectStackModel> &stack)
{
    for (int row = 0; row < stack->rowCount(); ++row) {
        auto e = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (e && e->getAssetId() == QLatin1String("koracut_grade")) {
            return e;
        }
    }
    return nullptr;
}
} // namespace

TEST_CASE("Étalonnage : les gestes de l'éditeur de nœuds", "[KoraCutGradeUi]")
{
    QJsonObject g = defaultGraph();
    REQUIRE(nodeIds(g) == QList<int>{1});
    REQUIRE(outputNode(g) == 1);

    // Option-S : un nœud en série après le 1, la sortie suit.
    const int n2 = addSerial(g, 1);
    REQUIRE(n2 == 2);
    REQUIRE(outputNode(g) == 2);
    REQUIRE(rgbInputs(node(g, 2)) == QList<int>{1});
    REQUIRE(node(g, 2).value(QStringLiteral("x")).toInt() == 2);
    REQUIRE(compiles(g));

    // Option-P sur le 2 : un correcteur qui lit la même entrée, et un mélangeur parallèle en sortie.
    const int n3 = addParallel(g, 2);
    REQUIRE(n3 == 3);
    const int mixer = outputNode(g);
    REQUIRE(node(g, mixer).value(QStringLiteral("type")).toString() == QStringLiteral("parallele"));
    REQUIRE(rgbInputs(node(g, mixer)) == QList<int>{2, 3});
    REQUIRE(rgbInputs(node(g, 3)) == QList<int>{1});
    // Une deuxième branche parallèle rejoint le même mélangeur.
    const int n5 = addParallel(g, 3);
    REQUIRE(rgbInputs(node(g, mixer)) == QList<int>{2, 3, n5});
    REQUIRE(compiles(g));

    // Les numéros affichés suivent l'ordre de création des correcteurs, sans compter le mélangeur.
    REQUIRE(displayNumber(g, 1) == 1);
    REQUIRE(displayNumber(g, 3) == 3);
    REQUIRE(displayNumber(g, n5) == 4);

    // Supprimer une branche : le mélangeur garde les autres ; à une seule entrée, il disparaît.
    REQUIRE(deleteNode(g, n5));
    REQUIRE(rgbInputs(node(g, mixer)) == QList<int>{2, 3});
    REQUIRE(deleteNode(g, 3));
    REQUIRE_FALSE(hasNode(g, mixer));
    REQUIRE(outputNode(g) == 2);
    REQUIRE(displayNumber(g, 2) == 2);
    REQUIRE(compiles(g));

    // Option-O : le nœud outside lit la clé du précédent, inversée.
    const int out = addOutside(g, 2);
    const QJsonObject o = node(g, out);
    REQUIRE(o.value(QStringLiteral("entree")).toObject().value(QStringLiteral("cle")).toInt() == 2);
    REQUIRE(o.value(QStringLiteral("cle")).toObject().value(QStringLiteral("entree_inverser")).toBool());
    REQUIRE(outputNode(g) == out);
    // Réinitialiser le nœud garde sa structure (la clé inversée).
    setValue(g, out, QStringLiteral("primaires"), QStringLiteral("saturation"), 20);
    resetNode(g, out);
    REQUIRE_FALSE(node(g, out).contains(QStringLiteral("primaires")));
    REQUIRE(node(g, out).value(QStringLiteral("cle")).toObject().value(QStringLiteral("entree_inverser")).toBool());

    // Option-L : un mélangeur de calques.
    const int layer = addLayer(g, 1);
    REQUIRE(layer > 0);
    bool hasLayer = false;
    for (int id : nodeIds(g)) {
        hasLayer = hasLayer || node(g, id).value(QStringLiteral("type")).toString() == QStringLiteral("calque");
    }
    REQUIRE(hasLayer);
    REQUIRE(compiles(g));

    // Option-Y : séparateur, trois correcteurs (R, V, B) et combineur.
    QJsonObject s = defaultGraph();
    const int c0 = addSplitterCombiner(s, 1);
    REQUIRE(node(s, c0).value(QStringLiteral("entree")).toObject().value(QStringLiteral("canal")).toInt() == 0);
    REQUIRE(node(s, outputNode(s)).value(QStringLiteral("type")).toString() == QStringLiteral("combineur"));
    REQUIRE(compiles(s));

    // Une boucle est refusée.
    QJsonObject loop = defaultGraph();
    addSerial(loop, 1);
    REQUIRE_FALSE(connectNodes(loop, 2, 1, false));

    // Ajouter le graphe d'un still à la suite (Append Node Graph).
    QJsonObject a = defaultGraph();
    QJsonObject b = defaultGraph();
    addSerial(b, 1);
    appendGraph(a, b);
    REQUIRE(nodeIds(a).size() == 3);
    REQUIRE(rgbInputs(node(a, 2)) == QList<int>{1});
    REQUIRE(outputNode(a) == 3);
    REQUIRE(compiles(a));

    // Les palettes utilisées (badges sous la vignette).
    QJsonObject t = defaultGraph();
    setValue(t, 1, QStringLiteral("primaires"), QStringLiteral("lift"), QJsonArray{0.1, 0, 0, 0});
    setValue(t, 1, QStringLiteral("lut"), QStringLiteral("fichier"), QStringLiteral("x.cube"));
    REQUIRE(usedTools(node(t, 1)) == QStringList{QStringLiteral("primaires"), QStringLiteral("lut")});
}

TEST_CASE("Étalonnage : le contrôleur, l'annulation et la copie", "[KoraCutGradeUi]")
{
    auto binModel = pCore->projectItemModel();
    binModel->clean();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    pCore->setCurrentProfile(QStringLiteral("dv_pal"));
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);
    KdenliveTests::resetNextId();

    const QString binId = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 40, false);
    const int tid = timeline->getTrackIndexFromPosition(2);
    int cid1 = -1, cid2 = -1;
    REQUIRE(timeline->requestClipInsertion(binId, tid, 0, cid1, true, true, false));
    REQUIRE(timeline->requestClipInsertion(binId, tid, 40, cid2, true, true, false));

    auto *c = KoraCutGradeController::self();
    c->setScope(KoraCutGradeController::Scope::Clip);
    c->setCurrentClip(cid1);
    REQUIRE(c->currentClip() == cid1);
    REQUIRE(c->videoClips() == QList<int>{cid1, cid2});
    REQUIRE_FALSE(c->hasGrade(cid1));
    auto stack = timeline->getClipEffectStackModel(cid1);
    REQUIRE_FALSE(gradeOf(stack));

    const int start = undoStack->index();
    // Un glisser : trois mouvements, puis le relâcher. Une seule étape, qui pose aussi l'effet.
    for (double lift : {0.02, 0.05, 0.08}) {
        QJsonObject g = c->graph();
        setValue(g, 1, QStringLiteral("primaires"), QStringLiteral("lift"), QJsonArray{lift, 0, 0, 0});
        c->edit(g, QStringLiteral("Lift"), false);
    }
    REQUIRE(undoStack->index() == start);
    QJsonObject g = c->graph();
    setValue(g, 1, QStringLiteral("primaires"), QStringLiteral("lift"), QJsonArray{0.1, 0, 0, 0});
    c->edit(g, QStringLiteral("Lift"), true);
    REQUIRE(undoStack->index() == start + 1);
    auto effect = gradeOf(stack);
    REQUIRE(effect);
    REQUIRE(effect->row() == 0); // en tête de pile
    REQUIRE(c->hasGrade(cid1));
    REQUIRE(value(c->graph(), 1, QStringLiteral("primaires"), QStringLiteral("lift")).toArray().at(0).toDouble() == Approx(0.1));
    // Ce qui est enregistré dans le projet : la propriété du filtre MLT.
    const QJsonObject saved = QJsonDocument::fromJson(QByteArray(effect->filter().get("graphe"))).object();
    REQUIRE(value(saved, 1, QStringLiteral("primaires"), QStringLiteral("lift")).toArray().at(0).toDouble() == Approx(0.1));

    // Un deuxième geste : une étape de plus.
    g = c->graph();
    addSerial(g, 1);
    c->edit(g, QStringLiteral("nœud en série"), true);
    REQUIRE(undoStack->index() == start + 2);
    REQUIRE(nodeIds(c->graph()).size() == 2);

    // Annuler deux fois : le nœud, puis le premier geste et l'effet avec lui.
    undoStack->undo();
    REQUIRE(nodeIds(c->graph()).size() == 1);
    undoStack->undo();
    REQUIRE_FALSE(gradeOf(stack));
    REQUIRE_FALSE(c->hasGrade(cid1));
    undoStack->redo();
    REQUIRE(gradeOf(stack));
    REQUIRE(value(c->graph(), 1, QStringLiteral("primaires"), QStringLiteral("lift")).toArray().at(0).toDouble() == Approx(0.1));
    undoStack->redo();
    REQUIRE(nodeIds(c->graph()).size() == 2);

    // Copier le grade (clic milieu de Resolve), puis l'ajouter à la suite.
    REQUIRE(c->copyGrade(cid1, {cid2}) == 1);
    REQUIRE(nodeIds(c->graphFor(cid2)).size() == 2);
    REQUIRE(c->copyGrade(cid1, {cid2}, true) == 1);
    REQUIRE(nodeIds(c->graphFor(cid2)).size() == 4);

    // Couper l'étalonnage (Shift-D), puis le rétablir.
    c->toggleBypass();
    REQUIRE(c->bypassed());
    REQUIRE_FALSE(c->hasGrade(cid1));
    c->toggleBypass();
    REQUIRE(c->hasGrade(cid1));

    // Réinitialiser en gardant les nœuds, puis tout.
    c->resetGrade(true);
    REQUIRE(nodeIds(c->graph()).size() == 2);
    REQUIRE_FALSE(c->hasGrade(cid1));
    c->resetGrade(false);
    REQUIRE(nodeIds(c->graph()).size() == 1);

    // Le grade de la séquence (Timeline de Resolve) vit dans la pile de la séquence.
    c->setScope(KoraCutGradeController::Scope::Timeline);
    g = c->graph();
    setValue(g, 1, QStringLiteral("primaires"), QStringLiteral("saturation"), 60);
    c->edit(g, QStringLiteral("Saturation"), true);
    REQUIRE(gradeOf(timeline->getMasterEffectStackModel()));
    c->setScope(KoraCutGradeController::Scope::Clip);

    stack.reset();
    effect.reset();
    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}

TEST_CASE("Étalonnage : les outils etal_* des agents", "[KoraCutGradeUi]")
{
    using KoraCutAgentTools::call;
    auto binModel = pCore->projectItemModel();
    binModel->clean();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    pCore->setCurrentProfile(QStringLiteral("dv_pal"));
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);
    KdenliveTests::resetNextId();
    const QString binId = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 40, false);
    const int tid = timeline->getTrackIndexFromPosition(2);
    int cid1 = -1, cid2 = -1;
    REQUIRE(timeline->requestClipInsertion(binId, tid, 0, cid1, true, true, false));
    REQUIRE(timeline->requestClipInsertion(binId, tid, 40, cid2, true, true, false));
    auto err = [](const QJsonObject &r) { return r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString(); };

    const int start = undoStack->index();
    QJsonObject r = call(timeline, QStringLiteral("etal_lire"), {{QStringLiteral("clip_id"), cid1}});
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    REQUIRE(r.value(QStringLiteral("noeuds")).toArray().size() == 1);
    REQUIRE(undoStack->index() == start);

    // Le CST d'une caméra : un nœud « CST » en tête, qui convertit le gris S-Log3 en gris Rec.709.
    r = call(timeline, QStringLiteral("etal_cst"), {{QStringLiteral("clip_id"), cid1}, {QStringLiteral("camera"), QStringLiteral("sony_slog3_sgamut3cine")}});
    INFO(QJsonDocument(r).toJson().toStdString());
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    REQUIRE(undoStack->index() == start + 1);
    const int cstNode = r.value(QStringLiteral("noeud_id")).toInt();
    auto *c = KoraCutGradeController::self();
    REQUIRE(node(c->graphFor(cid1), cstNode).value(QStringLiteral("etiquette")).toString() == QStringLiteral("CST"));
    {
        auto g = kcgrade::Grade::compile(QJsonDocument(c->graphFor(cid1)).toJson(QJsonDocument::Compact).toStdString());
        REQUIRE(g);
        const double gray = kcgrade::grayCode("slog3");
        const kcgrade::Vec3 o = g->evalColor({gray, gray, gray});
        REQUIRE(o[0] > 0.35);
        REQUIRE(o[0] < 0.6);
    }

    // Un nœud de balance en série, étiqueté, puis réglé : une étape chacun.
    r = call(timeline, QStringLiteral("etal_noeud_ajouter"), {{QStringLiteral("clip_id"), cid1}, {QStringLiteral("type"), QStringLiteral("serie")}, {QStringLiteral("etiquette"), QStringLiteral("Balance")}});
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    const int balance = r.value(QStringLiteral("noeud_id")).toInt();
    REQUIRE(r.value(QStringLiteral("numero")).toString() == QStringLiteral("02"));
    REQUIRE(undoStack->index() == start + 2);
    r = call(timeline, QStringLiteral("etal_noeud_regler"),
             {{QStringLiteral("clip_id"), cid1}, {QStringLiteral("noeud_id"), balance}, {QStringLiteral("primaires"), QJsonObject{{QStringLiteral("temp"), -400}, {QStringLiteral("gain"), QJsonArray{1.05, 1, 1, 1}}}}});
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    REQUIRE(undoStack->index() == start + 3);
    // Un deuxième réglage complète le premier sans l'effacer.
    r = call(timeline, QStringLiteral("etal_noeud_regler"), {{QStringLiteral("clip_id"), cid1}, {QStringLiteral("noeud_id"), balance}, {QStringLiteral("primaires"), QJsonObject{{QStringLiteral("saturation"), 55}}}});
    const QJsonObject prim = r.value(QStringLiteral("noeud")).toObject().value(QStringLiteral("primaires")).toObject();
    REQUIRE(prim.value(QStringLiteral("temp")).toDouble() == -400);
    REQUIRE(prim.value(QStringLiteral("saturation")).toDouble() == 55);

    // Parallèle, outside, suppression.
    r = call(timeline, QStringLiteral("etal_noeud_ajouter"), {{QStringLiteral("clip_id"), cid1}, {QStringLiteral("type"), QStringLiteral("parallele")}, {QStringLiteral("noeud_id"), balance}});
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    const int par = r.value(QStringLiteral("noeud_id")).toInt();
    REQUIRE(call(timeline, QStringLiteral("etal_noeud_supprimer"), {{QStringLiteral("clip_id"), cid1}, {QStringLiteral("noeud_id"), par}}).value(QStringLiteral("ok")).toBool());
    REQUIRE(nodeIds(c->graphFor(cid1)).size() == 2);

    // Une LUT : un fichier écrit par le moteur, posé dans un nœud en fin d'arbre.
    QTemporaryDir dir;
    const QString cube = dir.filePath(QStringLiteral("essai.cube"));
    REQUIRE(call(timeline, QStringLiteral("etal_exporter_lut"), {{QStringLiteral("clip_id"), cid1}, {QStringLiteral("fichier"), cube}}).value(QStringLiteral("ok")).toBool());
    REQUIRE(QFileInfo::exists(cube));
    REQUIRE(err(call(timeline, QStringLiteral("etal_exporter_lut"), {{QStringLiteral("clip_id"), cid1}, {QStringLiteral("fichier"), cube}})) == QStringLiteral("fichier_existe"));
    r = call(timeline, QStringLiteral("etal_lut"), {{QStringLiteral("clip_id"), cid2}, {QStringLiteral("fichier"), cube}, {QStringLiteral("melange"), 50}});
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    REQUIRE(err(call(timeline, QStringLiteral("etal_lut"), {{QStringLiteral("clip_id"), cid2}, {QStringLiteral("fichier"), QStringLiteral("/nulle/part.cube")}})) == QStringLiteral("fichier_introuvable"));

    // Copier le grade du plan 1 sur le plan 2 : une seule étape.
    const int beforeCopy = undoStack->index();
    r = call(timeline, QStringLiteral("etal_copier"), {{QStringLiteral("source_clip_id"), cid1}, {QStringLiteral("clip_ids"), QJsonArray{cid2}}});
    REQUIRE(r.value(QStringLiteral("copies")).toInt() == 1);
    REQUIRE(undoStack->index() == beforeCopy + 1);
    REQUIRE(nodeIds(c->graphFor(cid2)).size() == 2);

    // Les erreurs.
    REQUIRE(err(call(timeline, QStringLiteral("etal_noeud_regler"), {{QStringLiteral("clip_id"), cid1}, {QStringLiteral("noeud_id"), 99}, {QStringLiteral("primaires"), QJsonObject{}}})) == QStringLiteral("id_inconnu"));
    REQUIRE(err(call(timeline, QStringLiteral("etal_lire"), {{QStringLiteral("clip_id"), 9999}})) == QStringLiteral("id_inconnu"));
    REQUIRE(err(call(timeline, QStringLiteral("etal_cst"), {{QStringLiteral("clip_id"), cid1}})) == QStringLiteral("entree_invalide"));
    REQUIRE(err(call(timeline, QStringLiteral("etal_lire"), {})) == QStringLiteral("entree_invalide"));

    // Réinitialiser en gardant les nœuds, puis annuler : le grade revient.
    REQUIRE(call(timeline, QStringLiteral("etal_reinitialiser"), {{QStringLiteral("clip_id"), cid1}, {QStringLiteral("garder_noeuds"), true}}).value(QStringLiteral("ok")).toBool());
    REQUIRE_FALSE(c->hasGrade(cid1));
    undoStack->undo();
    REQUIRE(c->hasGrade(cid1));

    // Le grade de la séquence.
    REQUIRE(call(timeline, QStringLiteral("etal_noeud_regler"), {{QStringLiteral("sequence"), true}, {QStringLiteral("noeud_id"), 1}, {QStringLiteral("primaires"), QJsonObject{{QStringLiteral("contraste"), 1.1}}}})
                .value(QStringLiteral("ok"))
                .toBool());
    REQUIRE(c->hasGrade(-1));

    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}

// 8 octobre 2026 : etal_signature, l'étalonnage par défaut de KoraCut (HDR, Vif, Peau, Sujet).
TEST_CASE("Étalonnage : etal_signature pose l'arbre par défaut en une étape", "[KoraCutGradeUi]")
{
    using KoraCutAgentTools::call;
    auto binModel = pCore->projectItemModel();
    binModel->clean();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    pCore->setCurrentProfile(QStringLiteral("dv_pal"));
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);
    KdenliveTests::resetNextId();
    const QString colourId = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 40, false);
    const QString imageId = KdenliveTests::createFileProducer(pCore->getProjectProfile(), binModel, sourcesPath + QStringLiteral("/dataset/red.png"));
    const int tid = timeline->getTrackIndexFromPosition(2);
    int colour = -1, image1 = -1, image2 = -1;
    REQUIRE(timeline->requestClipInsertion(colourId, tid, 0, colour, true, true, false));
    REQUIRE(timeline->requestClipInsertion(imageId, tid, 40, image1, true, true, false));
    REQUIRE(timeline->requestClipInsertion(imageId, tid, 40 + timeline->getClipPlaytime(image1), image2, true, true, false));
    auto err = [](const QJsonObject &r) { return r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString(); };
    auto *c = KoraCutGradeController::self();
    auto labels = [c](int cid) {
        QStringList out;
        const QJsonObject g = c->graphFor(cid);
        for (int id : nodeIds(g)) {
            out << node(g, id).value(QStringLiteral("etiquette")).toString();
        }
        return out;
    };
    auto byLabel = [c](int cid, const QString &label) {
        const QJsonObject g = c->graphFor(cid);
        for (int id : nodeIds(g)) {
            if (node(g, id).value(QStringLiteral("etiquette")).toString() == label) {
                return node(g, id);
            }
        }
        return QJsonObject();
    };

    // Un plan, avec un visage : HDR, Vif, Peau (fenêtre sur le visage), Sujet. Une seule étape.
    const int start = undoStack->index();
    QJsonObject r = call(timeline, QStringLiteral("etal_signature"),
                         {{QStringLiteral("clip_id"), image1}, {QStringLiteral("visage"), QJsonObject{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.3}, {QStringLiteral("taille"), 0.34}}}});
    INFO(QJsonDocument(r).toJson().toStdString());
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    REQUIRE(undoStack->index() == start + 1);
    REQUIRE(labels(image1) == QStringList{QStringLiteral("HDR"), QStringLiteral("Vif"), QStringLiteral("Peau"), QStringLiteral("Sujet")});
    REQUIRE(compiles(c->graphFor(image1)));
    const QJsonObject plan = r.value(QStringLiteral("plans")).toArray().at(0).toObject();
    REQUIRE(plan.value(QStringLiteral("clip_id")).toInt() == image1);
    REQUIRE(plan.value(QStringLiteral("noeuds")).toArray().size() == 4);
    REQUIRE(plan.contains(QStringLiteral("luma_avant")));
    REQUIRE(plan.contains(QStringLiteral("luma_apres")));
    const QJsonObject hdr = byLabel(image1, QStringLiteral("HDR")).value(QStringLiteral("primaires")).toObject();
    // Un plan SDR (sans nœud CST) : le HDR doux.
    REQUIRE(hdr.value(QStringLiteral("ombres")).toDouble() == 45);
    REQUIRE(hdr.value(QStringLiteral("hautes_lumieres")).toDouble() == -40);
    REQUIRE(hdr.value(QStringLiteral("mid_detail")).toDouble() == 4);
    REQUIRE(hdr.value(QStringLiteral("contraste")).toDouble() == Approx(1.0));
    REQUIRE_FALSE(hdr.contains(QStringLiteral("lift")));
    REQUIRE(plan.value(QStringLiteral("hdr")).toString() == QStringLiteral("sdr"));
    REQUIRE(r.value(QStringLiteral("luma_cible")).toDouble() == 116);
    REQUIRE(r.value(QStringLiteral("conseil")).toString().contains(QStringLiteral("zone par zone")));
    const double gamma = hdr.value(QStringLiteral("gamma")).toArray().at(0).toDouble();
    REQUIRE(gamma >= -0.45);
    REQUIRE(gamma <= 0.25);
    REQUIRE(plan.value(QStringLiteral("gamma")).toDouble() == Approx(gamma));
    // Un rouge plein (luma 54) est sous la cible 116 : le gamma monte, l'image s'éclaircit.
    REQUIRE(gamma > 0.);
    REQUIRE(plan.value(QStringLiteral("luma_apres")).toDouble() > plan.value(QStringLiteral("luma_avant")).toDouble());
    const QJsonObject vif = byLabel(image1, QStringLiteral("Vif")).value(QStringLiteral("primaires")).toObject();
    REQUIRE(vif.value(QStringLiteral("saturation")).toDouble() == 54);
    REQUIRE(vif.value(QStringLiteral("boost")).toDouble() == 6);
    const QJsonObject skin = byLabel(image1, QStringLiteral("Peau"));
    REQUIRE(skin.value(QStringLiteral("primaires")).toObject().value(QStringLiteral("mid_detail")).toDouble() == -28);
    REQUIRE(skin.value(QStringLiteral("primaires")).toObject().value(QStringLiteral("gamma")).toArray().at(0).toDouble() == Approx(0.008));
    REQUIRE(skin.value(QStringLiteral("qualificateur")).toObject().value(QStringLiteral("teinte")).toObject().value(QStringLiteral("centre")).toDouble() == 24);
    REQUIRE(skin.value(QStringLiteral("qualificateur")).toObject().value(QStringLiteral("flou")).toDouble() == 14);
    const QJsonObject skinWindow = skin.value(QStringLiteral("fenetres")).toArray().at(0).toObject();
    REQUIRE(skinWindow.value(QStringLiteral("aspect")).toDouble() == Approx(0.8));
    REQUIRE(skinWindow.value(QStringLiteral("doux")).toDouble() == Approx(0.8 * 0.34));
    const QJsonObject subject = byLabel(image1, QStringLiteral("Sujet"));
    REQUIRE(subject.value(QStringLiteral("primaires")).toObject().value(QStringLiteral("gain")).toArray().at(0).toDouble() == Approx(1.03));
    REQUIRE(subject.value(QStringLiteral("primaires")).toObject().value(QStringLiteral("gamma")).toArray().at(0).toDouble() == Approx(0.04));
    REQUIRE_FALSE(subject.value(QStringLiteral("primaires")).toObject().contains(QStringLiteral("saturation")));
    REQUIRE(subject.value(QStringLiteral("fenetres")).toArray().size() == 1);
    // Rien n'assombrit le reste : aucun nœud ne lit une clé inversée (outside).
    for (int id : nodeIds(c->graphFor(image1))) {
        REQUIRE_FALSE(node(c->graphFor(image1), id).value(QStringLiteral("cle")).toObject().value(QStringLiteral("entree_inverser")).toBool());
    }

    // Un seul Annuler retire tout l'arbre, et l'effet avec lui.
    undoStack->undo();
    REQUIRE(undoStack->index() == start);
    REQUIRE_FALSE(c->hasGrade(image1));
    REQUIRE_FALSE(gradeOf(timeline->getClipEffectStackModel(image1)));
    undoStack->redo();
    REQUIRE(labels(image1).size() == 4);
    undoStack->undo();

    // Un plan log : le nœud CST de tête est posé par les mêmes paramètres qu'etal_cst, puis gardé quand on recommence.
    r = call(timeline, QStringLiteral("etal_signature"), {{QStringLiteral("clip_id"), image1}, {QStringLiteral("camera"), QStringLiteral("iphone_hdr_hlg")}, {QStringLiteral("sans_personne"), true}});
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    REQUIRE(labels(image1) == QStringList{QStringLiteral("CST"), QStringLiteral("HDR"), QStringLiteral("Vif"), QStringLiteral("Peau")});
    REQUIRE(byLabel(image1, QStringLiteral("CST")).value(QStringLiteral("cst")).toObject().value(QStringLiteral("entree_gamma")).toString() == QStringLiteral("hlg"));
    REQUIRE(r.value(QStringLiteral("luma_cible")).toDouble() == 136); // sans_personne : la cible par défaut monte
    {
        // Un plan qui passe par un CST : le HDR fort.
        const QJsonObject strong = byLabel(image1, QStringLiteral("HDR")).value(QStringLiteral("primaires")).toObject();
        REQUIRE(strong.value(QStringLiteral("ombres")).toDouble() == 85);
        REQUIRE(strong.value(QStringLiteral("hautes_lumieres")).toDouble() == -90);
        REQUIRE(strong.value(QStringLiteral("mid_detail")).toDouble() == 6);
        REQUIRE(r.value(QStringLiteral("plans")).toArray().at(0).toObject().value(QStringLiteral("hdr")).toString() == QStringLiteral("fort"));
    }
    REQUIRE(byLabel(image1, QStringLiteral("Vif")).value(QStringLiteral("primaires")).toObject().value(QStringLiteral("boost")).toDouble() == 6);
    REQUIRE_FALSE(byLabel(image1, QStringLiteral("Peau")).contains(QStringLiteral("fenetres")));
    r = call(timeline, QStringLiteral("etal_signature"), {{QStringLiteral("clip_id"), image1}, {QStringLiteral("luma_cible"), 110}});
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    REQUIRE(labels(image1) == QStringList{QStringLiteral("CST"), QStringLiteral("HDR"), QStringLiteral("Vif"), QStringLiteral("Peau")});
    REQUIRE(byLabel(image1, QStringLiteral("CST")).value(QStringLiteral("cst")).toObject().value(QStringLiteral("entree_gamut")).toString() == QStringLiteral("rec2020"));
    // Un objet à regarder : contraste, couleur et netteté dans la fenêtre, ni gain ni gamma (pas de halo).
    r = call(timeline, QStringLiteral("etal_signature"),
             {{QStringLiteral("clip_id"), image2}, {QStringLiteral("sujet"), QJsonObject{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.6}, {QStringLiteral("taille"), 0.3}, {QStringLiteral("aspect"), 1.5}}}});
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    {
        const QJsonObject object = byLabel(image2, QStringLiteral("Sujet"));
        const QJsonObject prim = object.value(QStringLiteral("primaires")).toObject();
        REQUIRE(prim.value(QStringLiteral("contraste")).toDouble() == Approx(1.16));
        REQUIRE(prim.value(QStringLiteral("saturation")).toDouble() == 60);
        REQUIRE_FALSE(prim.contains(QStringLiteral("gain")));
        REQUIRE_FALSE(prim.contains(QStringLiteral("gamma")));
        REQUIRE(object.value(QStringLiteral("flou")).toObject().value(QStringLiteral("rayon")).toDouble() == Approx(0.45));
        REQUIRE(object.value(QStringLiteral("fenetres")).toArray().at(0).toObject().value(QStringLiteral("aspect")).toDouble() == Approx(1.5));
        REQUIRE_FALSE(byLabel(image2, QStringLiteral("Peau")).contains(QStringLiteral("fenetres")));
        REQUIRE(compiles(c->graphFor(image2)));
    }
    undoStack->undo();
    REQUIRE_FALSE(c->hasGrade(image2));
    // remplacer = false : les nœuds s'ajoutent à la suite.
    r = call(timeline, QStringLiteral("etal_signature"), {{QStringLiteral("clip_id"), image1}, {QStringLiteral("remplacer"), false}});
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    REQUIRE(labels(image1).size() == 7);
    REQUIRE(compiles(c->graphFor(image1)));

    // tous : les deux images en une étape, la couleur est laissée, pas de fenêtre ni de nœud Sujet.
    const int beforeAll = undoStack->index();
    r = call(timeline, QStringLiteral("etal_signature"), {{QStringLiteral("tous"), true}});
    INFO(QJsonDocument(r).toJson().toStdString());
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    REQUIRE(undoStack->index() == beforeAll + 1);
    REQUIRE(r.value(QStringLiteral("plans")).toArray().size() == 2);
    REQUIRE(r.value(QStringLiteral("ignores")).toInt() == 1);
    REQUIRE_FALSE(c->hasGrade(colour));
    REQUIRE(labels(image2) == QStringList{QStringLiteral("HDR"), QStringLiteral("Vif"), QStringLiteral("Peau")});
    REQUIRE(labels(image1) == QStringList{QStringLiteral("CST"), QStringLiteral("HDR"), QStringLiteral("Vif"), QStringLiteral("Peau")});
    undoStack->undo();
    REQUIRE_FALSE(c->hasGrade(image2));
    REQUIRE(labels(image1).size() == 7);

    // Les erreurs.
    REQUIRE(err(call(timeline, QStringLiteral("etal_signature"), {})) == QStringLiteral("entree_invalide"));
    REQUIRE(err(call(timeline, QStringLiteral("etal_signature"), {{QStringLiteral("tous"), true}, {QStringLiteral("clip_id"), image1}})) == QStringLiteral("entree_invalide"));
    REQUIRE(err(call(timeline, QStringLiteral("etal_signature"), {{QStringLiteral("clip_id"), image1}, {QStringLiteral("luma_cible"), 220}})) == QStringLiteral("entree_invalide"));
    REQUIRE(err(call(timeline, QStringLiteral("etal_signature"),
                     {{QStringLiteral("tous"), true}, {QStringLiteral("visage"), QJsonObject{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.3}, {QStringLiteral("taille"), 0.3}}}})) ==
            QStringLiteral("entree_invalide"));
    REQUIRE(err(call(timeline, QStringLiteral("etal_signature"), {{QStringLiteral("clip_id"), image1}, {QStringLiteral("visage"), QJsonObject{{QStringLiteral("x"), 1.5}}}})) ==
            QStringLiteral("entree_invalide"));
    REQUIRE(err(call(timeline, QStringLiteral("etal_signature"), {{QStringLiteral("clip_id"), 9999}})) == QStringLiteral("id_inconnu"));

    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}
