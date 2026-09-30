/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Chantier 3, lot B (24 septembre 2026) : les outils d'étalonnage et d'export. Chaque section vérifie
// le cas normal, un cas d'erreur, et qu'un Annuler par appel remet l'état de départ. L'export fait un
// vrai rendu de deux secondes avec koracut_render, dans un dossier temporaire.

#include "catch.hpp"
#include "test_utils.hpp"

#include "bin/projectclip.h"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "effects/effectsrepository.hpp"
#include "kdenlivesettings.h"
#include "koracut/koracutagenttools.h"
#include "renderpresets/renderpresetrepository.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

using KoraCutAgentTools::call;

namespace {
QString errorCode(const QJsonObject &r)
{
    return r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString();
}

std::string dump(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)).toStdString();
}

QJsonObject clipInState(const QJsonObject &state, int clipId)
{
    for (const QJsonValue &p : state.value(QStringLiteral("pistes")).toArray()) {
        for (const QJsonValue &c : p.toObject().value(QStringLiteral("clips")).toArray()) {
            if (c.toObject().value(QStringLiteral("id")).toInt() == clipId) {
                return c.toObject();
            }
        }
    }
    return {};
}

QStringList effectIds(const QJsonObject &clip)
{
    QStringList ids;
    for (const QJsonValue &fx : clip.value(QStringLiteral("fx")).toArray()) {
        ids << fx.toObject().value(QStringLiteral("id")).toString();
    }
    return ids;
}

double luma(const QJsonObject &measure)
{
    return measure.value(QStringLiteral("luma")).toObject().value(QStringLiteral("moyenne")).toDouble();
}

double dominante(const QJsonObject &measure, const QString &channel)
{
    return measure.value(QStringLiteral("dominante")).toObject().value(channel).toDouble();
}
} // namespace

TEST_CASE("Outils d'agent, lot B : l'étalonnage", "[KoraCutAgentTools]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    // Un gris moyen, un gris clair, un gris sombre, un rouge : de quoi mesurer dans les deux sens.
    const QString gray = KdenliveTests::createProducer(pCore->getProjectProfile(), "#808080", binModel, 40, true);
    const QString light = KdenliveTests::createProducer(pCore->getProjectProfile(), "#c0c0c0", binModel, 40, true);
    const QString dark = KdenliveTests::createProducer(pCore->getProjectProfile(), "#404040", binModel, 40, true);
    const QString red = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 40, true);
    const QString sound = KdenliveTests::createProducerWithSound(pCore->getProjectProfile(), binModel, 10);
    const int v1 = timeline->getTracksIds(false).first();
    const int a1 = timeline->getTracksIds(true).first();
    auto place = [&](const QString &media, int position, const QString &part = QStringLiteral("video"), int track = -1) {
        const QJsonObject r = call(timeline, QStringLiteral("ajouter_clip"),
                                   {{QStringLiteral("media_id"), media}, {QStringLiteral("piste_id"), track < 0 ? v1 : track}, {QStringLiteral("position"), position}, {QStringLiteral("partie"), part}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        return r.value(QStringLiteral("clip_id")).toInt();
    };
    auto state = [&](const QString &detail = QStringLiteral("normal")) { return call(timeline, QStringLiteral("etat_timeline"), {{QStringLiteral("detail"), detail}}); };
    auto measure = [&](int cid, bool raw = false) {
        const QJsonObject r = call(timeline, QStringLiteral("mesurer_image"), {{QStringLiteral("clip_id"), cid}, {QStringLiteral("sans_effets"), raw}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        return r;
    };

    SECTION("Mesurer un gris, un rouge, hors du clip, un son")
    {
        const int g = place(gray, 0);
        const int r = place(red, 40);
        const int s = place(sound, 100, QStringLiteral("audio"), a1);
        QJsonObject m = measure(g);
        CHECK(luma(m) > 110.);
        CHECK(luma(m) < 145.);
        CHECK(m.value(QStringLiteral("saturation_moyenne")).toDouble() < 0.05);
        CHECK(std::abs(dominante(m, QStringLiteral("r"))) < 1.);
        CHECK(m.value(QStringLiteral("luma")).toObject().value(QStringLiteral("noirs_ecrases_pourcent")).toDouble() == 0.);
        CHECK_FALSE(m.value(QStringLiteral("peau")).toObject().value(QStringLiteral("detectee")).toBool());
        CHECK(m.value(QStringLiteral("position")).toObject().value(QStringLiteral("i")).toInt() == 20);
        m = measure(r);
        CHECK(m.value(QStringLiteral("saturation_moyenne")).toDouble() > 0.9);
        CHECK(dominante(m, QStringLiteral("r")) > 100.);
        CHECK(dominante(m, QStringLiteral("b")) < -50.);
        CHECK_FALSE(m.value(QStringLiteral("peau")).toObject().value(QStringLiteral("detectee")).toBool());
        CHECK(errorCode(call(timeline, QStringLiteral("mesurer_image"), {{QStringLiteral("clip_id"), g}, {QStringLiteral("position_dans_clip"), 40}})) == QStringLiteral("hors_clip"));
        CHECK(errorCode(call(timeline, QStringLiteral("mesurer_image"), {{QStringLiteral("clip_id"), s}})) == QStringLiteral("mauvais_type_de_piste"));
        CHECK(errorCode(call(timeline, QStringLiteral("mesurer_image"), {{QStringLiteral("clip_id"), 9999}})) == QStringLiteral("id_inconnu"));
    }

    SECTION("Exposition : moins un diaphragme assombrit, la mesure le voit, un pas en arrière")
    {
        const int g = place(gray, 0);
        const double before = luma(measure(g));
        const int steps = undoStack->count();
        QJsonObject r = call(timeline, QStringLiteral("regler_exposition"), {{QStringLiteral("clip_id"), g}, {QStringLiteral("exposition_ev"), -1}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("exposition_ev")).toDouble() == -1.);
        CHECK(r.value(QStringLiteral("effets")).toArray().size() == 1);
        CHECK(undoStack->count() == steps + 1);
        const double after = luma(measure(g));
        CHECK(after < before * 0.7);
        CHECK(std::abs(luma(measure(g, true)) - before) < 3.);
        // Contraste et saturation vont dans un second effet ; relancer règle sans doubler.
        r = call(timeline, QStringLiteral("regler_exposition"), {{QStringLiteral("clip_id"), g}, {QStringLiteral("contraste"), 1.2}, {QStringLiteral("exposition_ev"), -0.5}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("effets")).toArray().size() == 2);
        CHECK(r.value(QStringLiteral("exposition_ev")).toDouble() == -0.5);
        CHECK(r.value(QStringLiteral("contraste")).toDouble() == 1.2);
        CHECK(effectIds(clipInState(state(), g)) == QStringList{QStringLiteral("avfilter.exposure"), QStringLiteral("avfilter.eq")});
        CHECK(errorCode(call(timeline, QStringLiteral("regler_exposition"), {{QStringLiteral("clip_id"), g}})) == QStringLiteral("entree_invalide"));
        undoStack->undo();
        CHECK(effectIds(clipInState(state(), g)) == QStringList{QStringLiteral("avfilter.exposure")});
        CHECK(std::abs(luma(measure(g)) - after) < 3.);
        undoStack->undo();
        CHECK_FALSE(clipInState(state(), g).contains(QStringLiteral("fx")));
        CHECK(std::abs(luma(measure(g)) - before) < 3.);
    }

    SECTION("Balance des blancs : au-dessus de 6500 K l'image refroidit, en dessous elle réchauffe")
    {
        const int g = place(gray, 0);
        const int steps = undoStack->count();
        QJsonObject r = call(timeline, QStringLiteral("balance_blancs"), {{QStringLiteral("clip_id"), g}, {QStringLiteral("temperature_k"), 8000}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("temperature_k")).toDouble() == 8000.);
        CHECK(r.value(QStringLiteral("teinte")).toDouble() == 0.);
        CHECK(undoStack->count() == steps + 1);
        QJsonObject m = measure(g);
        INFO(dump(m));
        CHECK(dominante(m, QStringLiteral("b")) > dominante(m, QStringLiteral("r")) + 2.);
        r = call(timeline, QStringLiteral("balance_blancs"), {{QStringLiteral("clip_id"), g}, {QStringLiteral("temperature_k"), 4500}, {QStringLiteral("teinte"), 0.3}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("teinte")).toDouble() == 0.3);
        m = measure(g);
        INFO(dump(m));
        CHECK(dominante(m, QStringLiteral("r")) > dominante(m, QStringLiteral("b")) + 2.);
        // Un seul effet de température, un seul de balance.
        CHECK(effectIds(clipInState(state(), g)) == QStringList{QStringLiteral("avfilter.colortemperature"), QStringLiteral("avfilter.colorbalance")});
        CHECK(errorCode(call(timeline, QStringLiteral("balance_blancs"), {{QStringLiteral("clip_id"), g}, {QStringLiteral("temperature_k"), 500}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("balance_blancs"), {{QStringLiteral("clip_id"), g}})) == QStringLiteral("entree_invalide"));
        undoStack->undo();
        CHECK(effectIds(clipInState(state(), g)) == QStringList{QStringLiteral("avfilter.colortemperature")});
        undoStack->undo();
        CHECK_FALSE(clipInState(state(), g).contains(QStringLiteral("fx")));
        CHECK(std::abs(dominante(measure(g), QStringLiteral("b"))) < 1.);
    }

    SECTION("Roues, courbes, LUT : posés, lisibles, annulés")
    {
        const int g = place(gray, 0);
        const int steps = undoStack->count();
        QJsonObject r = call(timeline, QStringLiteral("lift_gamma_gain"),
                             {{QStringLiteral("clip_id"), g}, {QStringLiteral("hautes_lumieres"), QJsonObject{{QStringLiteral("r"), 1.5}, {QStringLiteral("v"), 1.5}, {QStringLiteral("b"), 1.5}}}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("hautes_lumieres")).toObject().value(QStringLiteral("r")).toDouble() == 1.5);
        CHECK(r.value(QStringLiteral("ombres")).toObject().value(QStringLiteral("r")).toDouble() == 0.);
        CHECK(r.value(QStringLiteral("tons_moyens")).toObject().value(QStringLiteral("b")).toDouble() == 1.);
        CHECK(luma(measure(g)) > 150.);
        r = call(timeline, QStringLiteral("lift_gamma_gain"), {{QStringLiteral("clip_id"), g}, {QStringLiteral("ombres"), QJsonObject{{QStringLiteral("r"), 0.1}, {QStringLiteral("v"), 0.1}, {QStringLiteral("b"), 0.2}}}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("hautes_lumieres")).toObject().value(QStringLiteral("r")).toDouble() == 1.5);
        CHECK(r.value(QStringLiteral("ombres")).toObject().value(QStringLiteral("b")).toDouble() == 0.2);
        CHECK(effectIds(clipInState(state(), g)) == QStringList{QStringLiteral("lift_gamma_gain")});
        CHECK(errorCode(call(timeline, QStringLiteral("lift_gamma_gain"), {{QStringLiteral("clip_id"), g}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("lift_gamma_gain"), {{QStringLiteral("clip_id"), g}, {QStringLiteral("ombres"), QJsonObject{{QStringLiteral("r"), 9}, {QStringLiteral("v"), 0}, {QStringLiteral("b"), 0}}}})) ==
              QStringLiteral("entree_invalide"));
        // Les courbes : une courbe en S sur la luma.
        r = call(timeline, QStringLiteral("regler_courbes"),
                 {{QStringLiteral("clip_id"), g},
                  {QStringLiteral("canal"), QStringLiteral("luma")},
                  {QStringLiteral("points"), QJsonArray{QJsonObject{{QStringLiteral("entree"), 0}, {QStringLiteral("sortie"), 0}}, QJsonObject{{QStringLiteral("entree"), 0.25}, {QStringLiteral("sortie"), 0.2}},
                                                        QJsonObject{{QStringLiteral("entree"), 0.75}, {QStringLiteral("sortie"), 0.8}}, QJsonObject{{QStringLiteral("entree"), 1}, {QStringLiteral("sortie"), 1}}}}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("effet_index")).toInt() == 1);
        CHECK(r.value(QStringLiteral("points")).toArray().size() == 4);
        QJsonArray fx = clipInState(state(QStringLiteral("complet")), g).value(QStringLiteral("fx")).toArray();
        REQUIRE(fx.size() == 2);
        CHECK(fx.at(1).toObject().value(QStringLiteral("p")).toObject().value(QStringLiteral("av.master")).toString() == QStringLiteral("0/0;0.25/0.2;0.75/0.8;1/1"));
        CHECK(errorCode(call(timeline, QStringLiteral("regler_courbes"),
                             {{QStringLiteral("clip_id"), g},
                              {QStringLiteral("canal"), QStringLiteral("luma")},
                              {QStringLiteral("points"), QJsonArray{QJsonObject{{QStringLiteral("entree"), 0.5}, {QStringLiteral("sortie"), 0}}, QJsonObject{{QStringLiteral("entree"), 0.25}, {QStringLiteral("sortie"), 0.2}}}}})) ==
              QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("regler_courbes"), {{QStringLiteral("clip_id"), g}, {QStringLiteral("canal"), QStringLiteral("alpha")}, {QStringLiteral("points"), QJsonArray{}}})) ==
              QStringLiteral("entree_invalide"));
        // La LUT livrée avec KoraCut, en début de pile ; les erreurs.
        const QString lut = QFileInfo(sourcesPath + QStringLiteral("/../data/luts/TEAL_ORANGE.cube")).absoluteFilePath();
        REQUIRE(QFileInfo::exists(lut));
        r = call(timeline, QStringLiteral("appliquer_lut"), {{QStringLiteral("clip_id"), g}, {QStringLiteral("fichier"), lut}, {QStringLiteral("place"), QStringLiteral("debut_de_pile")}, {QStringLiteral("interpolation"), QStringLiteral("trilineaire")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("effet_index")).toInt() == 0);
        CHECK(r.value(QStringLiteral("interpolation")).toString() == QStringLiteral("trilineaire"));
        fx = clipInState(state(QStringLiteral("complet")), g).value(QStringLiteral("fx")).toArray();
        REQUIRE(fx.size() == 3);
        CHECK(fx.first().toObject().value(QStringLiteral("id")).toString() == QStringLiteral("avfilter.lut3d"));
        CHECK(fx.first().toObject().value(QStringLiteral("p")).toObject().value(QStringLiteral("av.interp")).toString() == QStringLiteral("trilinear"));
        CHECK(errorCode(call(timeline, QStringLiteral("appliquer_lut"), {{QStringLiteral("clip_id"), g}, {QStringLiteral("fichier"), QStringLiteral("/nulle/part/look.cube")}})) == QStringLiteral("fichier_introuvable"));
        CHECK(errorCode(call(timeline, QStringLiteral("appliquer_lut"), {{QStringLiteral("clip_id"), g}, {QStringLiteral("fichier"), QString(lut + QStringLiteral(".txt"))}})) == QStringLiteral("entree_invalide"));
        CHECK(undoStack->count() == steps + 4);
        // Quatre appels, quatre pas en arrière.
        undoStack->undo();
        CHECK(effectIds(clipInState(state(), g)) == QStringList{QStringLiteral("lift_gamma_gain"), QStringLiteral("avfilter.curves")});
        undoStack->undo();
        CHECK(effectIds(clipInState(state(), g)) == QStringList{QStringLiteral("lift_gamma_gain")});
        undoStack->undo();
        fx = clipInState(state(QStringLiteral("complet")), g).value(QStringLiteral("fx")).toArray();
        REQUIRE(fx.size() == 1);
        CHECK(fx.first().toObject().value(QStringLiteral("p")).toObject().value(QStringLiteral("lift_b")).toString().toDouble() == 0.);
        undoStack->undo();
        CHECK_FALSE(clipInState(state(), g).contains(QStringLiteral("fx")));
    }

    SECTION("Copier, activer, réinitialiser")
    {
        const int src = place(gray, 0);
        const int a = place(light, 40);
        const int b = place(dark, 80);
        REQUIRE(call(timeline, QStringLiteral("balance_blancs"), {{QStringLiteral("clip_id"), src}, {QStringLiteral("temperature_k"), 7500}}).value(QStringLiteral("ok")).toBool());
        REQUIRE(call(timeline, QStringLiteral("regler_exposition"), {{QStringLiteral("clip_id"), src}, {QStringLiteral("exposition_ev"), 0.5}}).value(QStringLiteral("ok")).toBool());
        REQUIRE(call(timeline, QStringLiteral("balance_blancs"), {{QStringLiteral("clip_id"), a}, {QStringLiteral("temperature_k"), 5000}}).value(QStringLiteral("ok")).toBool());
        REQUIRE(call(timeline, QStringLiteral("ajouter_effet"), {{QStringLiteral("clip_id"), a}, {QStringLiteral("effet_id"), QStringLiteral("qtblend")}}).value(QStringLiteral("ok")).toBool());
        const int steps = undoStack->count();
        REQUIRE(effectIds(clipInState(state(), src)) == QStringList{QStringLiteral("avfilter.colortemperature"), QStringLiteral("avfilter.exposure")});
        QJsonObject r = call(timeline, QStringLiteral("copier_etalonnage"), {{QStringLiteral("clip_source_id"), src}, {QStringLiteral("clip_cible_ids"), QJsonArray{a, b}}});
        INFO(dump(clipInState(state(), a)));
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("cibles")).toArray().size() == 2);
        CHECK(r.value(QStringLiteral("cibles")).toArray().first().toObject().value(QStringLiteral("effets_copies")).toInt() == 2);
        CHECK(undoStack->count() == steps + 1);
        // La cible garde son Transformer, perd sa balance à 5000 K, reçoit les deux effets de la source.
        QJsonArray fx = clipInState(state(QStringLiteral("complet")), a).value(QStringLiteral("fx")).toArray();
        CHECK(effectIds(clipInState(state(), a)) == QStringList{QStringLiteral("qtblend"), QStringLiteral("avfilter.colortemperature"), QStringLiteral("avfilter.exposure")});
        REQUIRE(fx.size() == 3);
        CHECK(fx.at(1).toObject().value(QStringLiteral("p")).toObject().value(QStringLiteral("av.temperature")).toString().toDouble() == 7500.);
        CHECK(effectIds(clipInState(state(), b)) == QStringList{QStringLiteral("avfilter.colortemperature"), QStringLiteral("avfilter.exposure")});
        CHECK(errorCode(call(timeline, QStringLiteral("copier_etalonnage"), {{QStringLiteral("clip_source_id"), b}, {QStringLiteral("clip_cible_ids"), QJsonArray{b}}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("copier_etalonnage"), {{QStringLiteral("clip_source_id"), src}, {QStringLiteral("clip_cible_ids"), QJsonArray{9999}}})) == QStringLiteral("id_inconnu"));
        // Activer / désactiver toute la pile, puis réinitialiser (Transformer reste).
        r = call(timeline, QStringLiteral("activer_etalonnage"), {{QStringLiteral("clip_id"), a}, {QStringLiteral("actif"), false}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK_FALSE(r.value(QStringLiteral("actif")).toBool());
        CHECK(clipInState(state(), a).value(QStringLiteral("fx_off")).toBool());
        r = call(timeline, QStringLiteral("activer_etalonnage"), {{QStringLiteral("clip_id"), a}, {QStringLiteral("actif"), true}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK_FALSE(clipInState(state(), a).contains(QStringLiteral("fx_off")));
        CHECK(errorCode(call(timeline, QStringLiteral("activer_etalonnage"), {{QStringLiteral("clip_id"), a}})) == QStringLiteral("entree_invalide"));
        r = call(timeline, QStringLiteral("reinitialiser_etalonnage"), {{QStringLiteral("clip_id"), a}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("effets_retires")).toArray() == QJsonArray{QStringLiteral("avfilter.colortemperature"), QStringLiteral("avfilter.exposure")});
        CHECK(effectIds(clipInState(state(), a)) == QStringList{QStringLiteral("qtblend")});
        CHECK(undoStack->count() == steps + 4);
        undoStack->undo();
        CHECK(effectIds(clipInState(state(), a)) == QStringList{QStringLiteral("qtblend"), QStringLiteral("avfilter.colortemperature"), QStringLiteral("avfilter.exposure")});
        undoStack->undo();
        CHECK(clipInState(state(), a).value(QStringLiteral("fx_off")).toBool());
        undoStack->undo();
        CHECK_FALSE(clipInState(state(), a).contains(QStringLiteral("fx_off")));
        undoStack->undo();
        fx = clipInState(state(QStringLiteral("complet")), a).value(QStringLiteral("fx")).toArray();
        CHECK(effectIds(clipInState(state(), a)) == QStringList{QStringLiteral("avfilter.colortemperature"), QStringLiteral("qtblend")});
        CHECK(fx.first().toObject().value(QStringLiteral("p")).toObject().value(QStringLiteral("av.temperature")).toString().toDouble() == 5000.);
        CHECK_FALSE(clipInState(state(), b).contains(QStringLiteral("fx")));
    }

    SECTION("Raccorder un plan sombre sur un plan clair")
    {
        const int ref = place(light, 0);
        const int target = place(dark, 40);
        const int steps = undoStack->count();
        QJsonObject r = call(timeline, QStringLiteral("matcher_plans"), {{QStringLiteral("clip_reference_id"), ref}, {QStringLiteral("clip_cible_id"), target}, {QStringLiteral("force"), 1}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const double before = luma(r.value(QStringLiteral("avant")).toObject());
        const double after = luma(r.value(QStringLiteral("apres")).toObject());
        const double reference = luma(r.value(QStringLiteral("reference")).toObject());
        CHECK(before < reference);
        CHECK(std::abs(after - reference) < std::abs(before - reference) / 2.);
        CHECK(r.value(QStringLiteral("reglages_poses")).toArray().size() >= 1);
        CHECK(undoStack->count() == steps + 1);
        CHECK(errorCode(call(timeline, QStringLiteral("matcher_plans"), {{QStringLiteral("clip_reference_id"), ref}, {QStringLiteral("clip_cible_id"), ref}})) == QStringLiteral("entree_invalide"));
        undoStack->undo();
        CHECK_FALSE(clipInState(state(), target).contains(QStringLiteral("fx")));
        CHECK(std::abs(luma(measure(target)) - before) < 3.);
    }

    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}

TEST_CASE("Outils d'agent, lot B : l'export", "[KoraCutAgentTools]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    // Les tests ne voient pas les données installées : les préréglages viennent de la source.
    if (!RenderPresetRepository::get()->presetExists(QStringLiteral("MP4-H264/AAC"))) {
        KdenliveTests::loadRenderPresets(QFileInfo(sourcesPath + QStringLiteral("/../data/profiles.xml")).absoluteFilePath());
    }
    REQUIRE(RenderPresetRepository::get()->presetExists(QStringLiteral("MP4-H264/AAC")));
    const QString mp4 = sourcesPath + QStringLiteral("/dataset/red.mp4");
    const QString redFile = KdenliveTests::createFileProducer(pCore->getProjectProfile(), binModel, mp4);
    REQUIRE_FALSE(redFile.isEmpty());
    const QString blue = KdenliveTests::createProducer(pCore->getProjectProfile(), "blue", binModel, 60, true);
    const int v1 = timeline->getTracksIds(false).first();
    const int fps = qRound(pCore->getCurrentFps());
    QTemporaryDir folder;
    REQUIRE(folder.isValid());
    auto preset = [](const QString &name) {
        return QJsonObject{{QStringLiteral("preset"), QStringLiteral("personnalise")}, {QStringLiteral("nom_preset_kdenlive"), name}};
    };

    SECTION("Les préréglages, la vérification d'une timeline vide")
    {
        QJsonObject r = call(timeline, QStringLiteral("lister_prereglages"), {});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        QStringList names;
        int koracut = 0;
        for (const QJsonValue &v : r.value(QStringLiteral("prereglages")).toArray()) {
            names << v.toObject().value(QStringLiteral("nom")).toString();
            if (v.toObject().value(QStringLiteral("koracut")).toBool()) {
                ++koracut;
                CHECK(v.toObject().contains(QStringLiteral("preset")));
            }
        }
        CHECK(names.contains(QStringLiteral("MP4-H264/AAC")));
        CHECK(koracut == 5);
        r = call(timeline, QStringLiteral("lister_prereglages"), {{QStringLiteral("categorie"), QStringLiteral("KoraCut")}});
        CHECK(r.value(QStringLiteral("prereglages")).toArray().size() == 5);
        r = call(timeline, QStringLiteral("verifier_avant_export"), preset(QStringLiteral("MP4-H264/AAC")));
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK_FALSE(r.value(QStringLiteral("pret")).toBool());
        CHECK(r.value(QStringLiteral("problemes")).toArray().first().toObject().value(QStringLiteral("code")).toString() == QStringLiteral("timeline_vide"));
        CHECK(errorCode(call(timeline, QStringLiteral("verifier_avant_export"), preset(QStringLiteral("Codec Magique")))) == QStringLiteral("id_inconnu"));
        CHECK(errorCode(call(timeline, QStringLiteral("verifier_avant_export"), {{QStringLiteral("preset"), QStringLiteral("personnalise")}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("verifier_avant_export"), {{QStringLiteral("preset"), QStringLiteral("dvd")}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("estimer_export"), {{QStringLiteral("preset"), QStringLiteral("youtube_1080p")}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("exporter"), {{QStringLiteral("preset"), QStringLiteral("youtube_1080p")}, {QStringLiteral("chemin"), folder.filePath(QStringLiteral("a.mp4"))}})) ==
              QStringLiteral("entree_invalide"));
    }

    SECTION("Un vrai rendu de deux secondes, le fichier qui existe, l'image fixe")
    {
        // Le rouge du jeu d'essai (cinq images), puis deux secondes de bleu.
        QJsonObject r = call(timeline, QStringLiteral("ajouter_clip"), {{QStringLiteral("media_id"), redFile}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("video")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const int redLength = r.value(QStringLiteral("duree")).toObject().value(QStringLiteral("i")).toInt();
        r = call(timeline, QStringLiteral("ajouter_clip"), {{QStringLiteral("media_id"), blue}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), redLength}, {QStringLiteral("partie"), QStringLiteral("video")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const int total = timeline->duration();
        REQUIRE(total > 2 * fps);

        // Vérifier : prêt, avec l'avertissement d'un mixage muet.
        r = call(timeline, QStringLiteral("verifier_avant_export"), preset(QStringLiteral("MP4-H264/AAC")));
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("pret")).toBool());
        CHECK(r.value(QStringLiteral("duree")).toObject().value(QStringLiteral("i")).toInt() == total);
        CHECK(r.value(QStringLiteral("taille_estimee_mo")).toDouble() > 0.);
        // Un chemin de sortie qui existe déjà : signalé, sans bloquer.
        const QString out = folder.filePath(QStringLiteral("essai.mp4"));
        {
            QFile f(out);
            REQUIRE(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
        r = call(timeline, QStringLiteral("verifier_avant_export"), {{QStringLiteral("preset"), QStringLiteral("personnalise")}, {QStringLiteral("nom_preset_kdenlive"), QStringLiteral("MP4-H264/AAC")}, {QStringLiteral("chemin"), out}});
        bool flagged = false;
        for (const QJsonValue &v : r.value(QStringLiteral("problemes")).toArray()) {
            if (v.toObject().value(QStringLiteral("code")).toString() == QStringLiteral("fichier_existe")) {
                flagged = true;
            }
        }
        CHECK(flagged);
        CHECK(r.value(QStringLiteral("pret")).toBool());
        QFile::remove(out);

        // Estimer sans mesurer le temps : taille et espace, temps non disponible.
        QJsonObject args = preset(QStringLiteral("MP4-H264/AAC"));
        args.insert(QStringLiteral("mesurer_temps"), false);
        args.insert(QStringLiteral("plage"), QJsonObject{{QStringLiteral("type"), QStringLiteral("zone")}, {QStringLiteral("debut"), 0}, {QStringLiteral("fin"), 2 * fps - 1}});
        r = call(timeline, QStringLiteral("estimer_export"), args);
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("duree_exportee")).toObject().value(QStringLiteral("i")).toInt() == 2 * fps);
        CHECK(r.value(QStringLiteral("taille_estimee_mo")).toDouble() > 0.);
        CHECK(r.value(QStringLiteral("temps")).toString() == QStringLiteral("non_disponible"));
        CHECK(r.value(QStringLiteral("espace_libre_mo")).toDouble() > 0.);

        // Les refus avant tout rendu.
        args = preset(QStringLiteral("MP4-H264/AAC"));
        args.insert(QStringLiteral("chemin"), QStringLiteral("Exports/essai.mp4"));
        CHECK(errorCode(call(timeline, QStringLiteral("exporter"), args)) == QStringLiteral("entree_invalide"));
        args.insert(QStringLiteral("chemin"), folder.filePath(QStringLiteral("essai.mov")));
        CHECK(errorCode(call(timeline, QStringLiteral("exporter"), args)) == QStringLiteral("entree_invalide"));
        args.insert(QStringLiteral("chemin"), folder.filePath(QStringLiteral("nulle/part/essai.mp4")));
        CHECK(errorCode(call(timeline, QStringLiteral("exporter"), args)) == QStringLiteral("fichier_introuvable"));
        args.insert(QStringLiteral("chemin"), mp4);
        args.insert(QStringLiteral("ecraser"), true);
        CHECK(errorCode(call(timeline, QStringLiteral("exporter"), args)) == QStringLiteral("fichier_protege"));
        args.remove(QStringLiteral("ecraser"));
        args.insert(QStringLiteral("chemin"), out);
        args.insert(QStringLiteral("niveau_lufs"), 3);
        CHECK(errorCode(call(timeline, QStringLiteral("exporter"), args)) == QStringLiteral("entree_invalide"));
        args.remove(QStringLiteral("niveau_lufs"));
        CHECK(errorCode(call(timeline, QStringLiteral("etat_exports"), {{QStringLiteral("export_id"), QStringLiteral("exp-999")}})) == QStringLiteral("id_inconnu"));
        CHECK(errorCode(call(timeline, QStringLiteral("annuler_export"), {{QStringLiteral("export_id"), QStringLiteral("exp-999")}})) == QStringLiteral("id_inconnu"));

        const bool canRender = QFileInfo::exists(KdenliveSettings::meltpath());
        INFO("melt : " << KdenliveSettings::meltpath().toStdString());
        if (canRender) {
            // Le rendu : deux secondes de la timeline, en MP4.
            args.insert(QStringLiteral("plage"), QJsonObject{{QStringLiteral("type"), QStringLiteral("zone")}, {QStringLiteral("debut"), 0}, {QStringLiteral("fin"), 2 * fps - 1}});
            r = call(timeline, QStringLiteral("exporter"), args);
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            const QString id = r.value(QStringLiteral("export_id")).toString();
            CHECK(id.startsWith(QStringLiteral("exp-")));
            CHECK(r.value(QStringLiteral("chemin")).toString() == out);
            CHECK(r.value(QStringLiteral("tache_id")).toString() == out);
            CHECK(r.value(QStringLiteral("etat")).toString() == QStringLiteral("en_cours"));
            CHECK(r.value(QStringLiteral("duree_exportee")).toObject().value(QStringLiteral("i")).toInt() == 2 * fps);
            CHECK_FALSE(r.value(QStringLiteral("remplace_un_fichier")).toBool());
            // Le même chemin pendant le rendu : refusé.
            CHECK(errorCode(call(timeline, QStringLiteral("exporter"), args)) == QStringLiteral("fichier_existe"));
            // Suivre jusqu'à la fin (au plus trois minutes).
            QElapsedTimer timer;
            timer.start();
            QString etat;
            while (timer.elapsed() < 180000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                const QJsonObject s = call(timeline, QStringLiteral("etat_exports"), {{QStringLiteral("export_id"), id}});
                REQUIRE(s.value(QStringLiteral("ok")).toBool());
                REQUIRE(s.value(QStringLiteral("exports")).toArray().size() == 1);
                const QJsonObject e = s.value(QStringLiteral("exports")).toArray().first().toObject();
                etat = e.value(QStringLiteral("etat")).toString();
                if (etat == QLatin1String("termine") || etat == QLatin1String("echec") || etat == QLatin1String("annule")) {
                    INFO(dump(e));
                    CHECK(etat == QStringLiteral("termine"));
                    CHECK(e.value(QStringLiteral("progression_pourcent")).toInt() == 100);
                    CHECK(e.value(QStringLiteral("taille_ko")).toInt() > 0);
                    break;
                }
            }
            REQUIRE(etat == QStringLiteral("termine"));
            CHECK(QFileInfo(out).size() > 1000);
            // Le fichier existe : refusé sans ecraser, accepté avec, puis annulé.
            CHECK(errorCode(call(timeline, QStringLiteral("exporter"), args)) == QStringLiteral("fichier_existe"));
            args.insert(QStringLiteral("ecraser"), true);
            r = call(timeline, QStringLiteral("exporter"), args);
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(r.value(QStringLiteral("remplace_un_fichier")).toBool());
            // Le temps est maintenant estimé d'après le premier rendu.
            CHECK(r.value(QStringLiteral("temps_estime_secondes")).isDouble());
            const QString second = r.value(QStringLiteral("export_id")).toString();
            r = call(timeline, QStringLiteral("annuler_export"), {{QStringLiteral("export_id"), second}});
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(r.value(QStringLiteral("etat")).toString() == QStringLiteral("annule"));
            timer.restart();
            while (timer.elapsed() < 30000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                const QJsonObject s = call(timeline, QStringLiteral("etat_exports"), {{QStringLiteral("export_id"), second}});
                etat = s.value(QStringLiteral("exports")).toArray().first().toObject().value(QStringLiteral("etat")).toString();
                if (etat != QLatin1String("en_cours")) {
                    break;
                }
            }
            CHECK(etat == QStringLiteral("annule"));
            CHECK(errorCode(call(timeline, QStringLiteral("annuler_export"), {{QStringLiteral("export_id"), second}})) == QStringLiteral("entree_invalide"));
            // La file entière : deux entrées.
            r = call(timeline, QStringLiteral("etat_exports"), {});
            CHECK(r.value(QStringLiteral("exports")).toArray().size() == 2);
            // Une série : deux fichiers de suite, tout est contrôlé avant.
            QFile::remove(out);
            QJsonObject one = preset(QStringLiteral("MP4-H264/AAC"));
            one.insert(QStringLiteral("chemin"), folder.filePath(QStringLiteral("serie-1.mp4")));
            one.insert(QStringLiteral("plage"), QJsonObject{{QStringLiteral("type"), QStringLiteral("zone")}, {QStringLiteral("debut"), 0}, {QStringLiteral("fin"), fps - 1}});
            QJsonObject two = one;
            two.insert(QStringLiteral("chemin"), folder.filePath(QStringLiteral("serie-1.mp4")));
            CHECK(errorCode(call(timeline, QStringLiteral("exporter_serie"), {{QStringLiteral("exports"), QJsonArray{one, two}}})) == QStringLiteral("fichier_existe"));
            two.insert(QStringLiteral("chemin"), folder.filePath(QStringLiteral("serie-2.mp4")));
            r = call(timeline, QStringLiteral("exporter_serie"), {{QStringLiteral("exports"), QJsonArray{one, two}}});
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            REQUIRE(r.value(QStringLiteral("exports")).toArray().size() == 2);
            CHECK(r.value(QStringLiteral("exports")).toArray().first().toObject().value(QStringLiteral("etat")).toString() == QStringLiteral("en_cours"));
            CHECK(r.value(QStringLiteral("exports")).toArray().last().toObject().value(QStringLiteral("etat")).toString() == QStringLiteral("en_attente"));
            const QString last = r.value(QStringLiteral("exports")).toArray().last().toObject().value(QStringLiteral("export_id")).toString();
            timer.restart();
            while (timer.elapsed() < 180000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                const QJsonObject s = call(timeline, QStringLiteral("etat_exports"), {{QStringLiteral("export_id"), last}});
                etat = s.value(QStringLiteral("exports")).toArray().first().toObject().value(QStringLiteral("etat")).toString();
                if (etat == QLatin1String("termine") || etat == QLatin1String("echec")) {
                    break;
                }
            }
            CHECK(etat == QStringLiteral("termine"));
            CHECK(QFileInfo(folder.filePath(QStringLiteral("serie-1.mp4"))).size() > 1000);
            CHECK(QFileInfo(folder.filePath(QStringLiteral("serie-2.mp4"))).size() > 1000);
        } else {
            WARN("melt introuvable : le rendu réel n'a pas été joué");
        }

        // L'image fixe : au format du projet, refusée si elle existe, ajoutée au chutier sur demande.
        const QString png = folder.filePath(QStringLiteral("image.png"));
        const size_t medias = binModel->getAllClipIds().size();
        r = call(timeline, QStringLiteral("exporter_image"), {{QStringLiteral("position"), redLength + 5}, {QStringLiteral("chemin"), png}, {QStringLiteral("ajouter_au_chutier"), true}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("largeur_px")).toInt() == pCore->getCurrentFrameSize().width());
        CHECK(r.value(QStringLiteral("hauteur_px")).toInt() == pCore->getCurrentFrameSize().height());
        CHECK(r.contains(QStringLiteral("media_id")));
        CHECK(binModel->getAllClipIds().size() == medias + 1);
        const QImage image(png);
        REQUIRE_FALSE(image.isNull());
        CHECK(qBlue(image.pixel(image.width() / 2, image.height() / 2)) > 200);
        CHECK(qRed(image.pixel(image.width() / 2, image.height() / 2)) < 50);
        // Entrée au chutier, l'image est devenue un média du projet : protégée. Un autre fichier qui
        // existe déjà : refusé, l'outil n'écrase jamais.
        CHECK(errorCode(call(timeline, QStringLiteral("exporter_image"), {{QStringLiteral("position"), 0}, {QStringLiteral("chemin"), png}})) == QStringLiteral("fichier_protege"));
        const QString existing = folder.filePath(QStringLiteral("deja-la.png"));
        {
            QFile f(existing);
            REQUIRE(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
        CHECK(errorCode(call(timeline, QStringLiteral("exporter_image"), {{QStringLiteral("position"), 0}, {QStringLiteral("chemin"), existing}})) == QStringLiteral("fichier_existe"));
        CHECK(errorCode(call(timeline, QStringLiteral("exporter_image"), {{QStringLiteral("position"), total + 10}, {QStringLiteral("chemin"), folder.filePath(QStringLiteral("b.png"))}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("exporter_image"), {{QStringLiteral("position"), 0}, {QStringLiteral("chemin"), folder.filePath(QStringLiteral("b.jpg"))}})) == QStringLiteral("entree_invalide"));
    }

    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}
