/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Chantier 3, lot A (24 septembre 2026) : les outils qui manquaient. Chaque section vérifie le cas
// normal, un cas d'erreur, et qu'un Annuler par appel remet l'état de départ.

#include "catch.hpp"
#include "test_utils.hpp"

#include "bin/projectclip.h"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "effects/effectsrepository.hpp"
#include "koracut/koracutagenttools.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

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

QJsonObject trackInState(const QJsonObject &state, int trackId)
{
    for (const QJsonValue &p : state.value(QStringLiteral("pistes")).toArray()) {
        if (p.toObject().value(QStringLiteral("id")).toInt() == trackId) {
            return p.toObject();
        }
    }
    return {};
}

int frames(const QJsonObject &r, const QString &key)
{
    return r.value(key).toObject().value(QStringLiteral("i")).toInt();
}
}

TEST_CASE("Outils d'agent, lot A : titre, transition, effets, images clés, transformation", "[KoraCutAgentTools]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    const QString red = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 40, true);
    const int v1 = timeline->getTracksIds(false).first();
    const int a1 = timeline->getTracksIds(true).first();
    auto place = [&](int track, int position, int in = -1, int out = -1) {
        QJsonObject args{{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), track}, {QStringLiteral("position"), position}, {QStringLiteral("partie"), QStringLiteral("video")}};
        if (in >= 0) {
            args.insert(QStringLiteral("entree"), in);
            args.insert(QStringLiteral("sortie"), out);
        }
        const QJsonObject r = call(timeline, QStringLiteral("ajouter_clip"), args);
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        return r.value(QStringLiteral("clip_id")).toInt();
    };
    auto state = [&](const QString &detail = QStringLiteral("normal")) { return call(timeline, QStringLiteral("etat_timeline"), {{QStringLiteral("detail"), detail}}); };

    SECTION("Un titre posé, lisible dans l'état, annulé d'un pas")
    {
        const int before = undoStack->count();
        const size_t medias = binModel->getAllClipIds().size();
        QJsonObject r = call(timeline, QStringLiteral("ajouter_titre"),
                             {{QStringLiteral("texte"), QStringLiteral("Saly, vue du ciel")}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 12}, {QStringLiteral("duree"), 63}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const int title = r.value(QStringLiteral("clip_id")).toInt();
        CHECK(frames(r, QStringLiteral("position")) == 12);
        CHECK(frames(r, QStringLiteral("duree")) == 63);
        CHECK(r.value(QStringLiteral("style")).toString() == QStringLiteral("ouverture"));
        CHECK(undoStack->count() == before + 1);
        CHECK(binModel->getAllClipIds().size() == medias + 1);
        const QJsonObject c = clipInState(state(), title);
        CHECK(c.value(QStringLiteral("k")).toString() == QStringLiteral("t"));
        CHECK(c.value(QStringLiteral("txt")).toString() == QStringLiteral("Saly, vue du ciel"));
        CHECK(c.value(QStringLiteral("dur")).toInt() == 63);
        // Une ligne trop longue prévient ; une piste audio refuse ; une place prise refuse.
        r = call(timeline, QStringLiteral("ajouter_titre"),
                 {{QStringLiteral("texte"), QStringLiteral("Une ligne vraiment beaucoup trop longue pour un titre lisible")}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 200}, {QStringLiteral("duree"), 25}, {QStringLiteral("style"), QStringLiteral("bas_ecran")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.contains(QStringLiteral("avertissement")));
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_titre"), {{QStringLiteral("texte"), QStringLiteral("x")}, {QStringLiteral("piste_id"), a1}, {QStringLiteral("position"), 0}, {QStringLiteral("duree"), 25}})) ==
              QStringLiteral("mauvais_type_de_piste"));
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_titre"), {{QStringLiteral("texte"), QStringLiteral("x")}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 20}, {QStringLiteral("duree"), 25}})) ==
              QStringLiteral("place_occupee"));
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_titre"), {{QStringLiteral("texte"), QStringLiteral("x")}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 500}, {QStringLiteral("duree"), 25}, {QStringLiteral("style"), QStringLiteral("neon")}})) ==
              QStringLiteral("entree_invalide"));
        undoStack->undo();
        undoStack->undo();
        CHECK_FALSE(timeline->isClip(title));
        CHECK(binModel->getAllClipIds().size() == medias);
        CHECK(undoStack->count() == before + 2);
    }

    SECTION("Un fondu enchaîné entre deux clips collés, la réserve qui manque")
    {
        // A prend les images 0 à 9 de la source, B les images 10 à 19 : chacun a 10 images en réserve.
        const int a = place(v1, 0, 0, 10);
        const int b = place(v1, 10, 10, 20);
        const int before = undoStack->count();
        QJsonObject r = call(timeline, QStringLiteral("ajouter_transition"), {{QStringLiteral("clip_id"), a}, {QStringLiteral("duree"), 6}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("clip_droit_id")).toInt() == b);
        CHECK(frames(r, QStringLiteral("duree")) == 6);
        CHECK(r.value(QStringLiteral("type")).toString() == QStringLiteral("fondu"));
        CHECK(undoStack->count() == before + 1);
        CHECK(timeline->getMixDuration(b) == 6);
        CHECK(clipInState(state(), b).value(QStringLiteral("mix")).toInt() == 6);
        // Déjà mélangés : refus. Sans voisin : refus.
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_transition"), {{QStringLiteral("clip_id"), a}, {QStringLiteral("duree"), 6}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_transition"), {{QStringLiteral("clip_id"), b}, {QStringLiteral("duree"), 6}})) == QStringLiteral("entree_invalide"));
        undoStack->undo();
        CHECK(timeline->getMixDuration(b) == 0);
        CHECK_FALSE(clipInState(state(), b).contains(QStringLiteral("mix")));
        // Deux clips qui utilisent toute leur source : pas de matière.
        const int c = place(v1, 100);
        const int d = place(v1, 140);
        r = call(timeline, QStringLiteral("ajouter_transition"), {{QStringLiteral("clip_id"), c}, {QStringLiteral("duree"), 6}});
        CHECK(errorCode(r) == QStringLiteral("pas_de_matiere"));
        CHECK(timeline->getMixDuration(d) == 0);
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_transition"), {{QStringLiteral("clip_id"), a}, {QStringLiteral("duree"), 6}, {QStringLiteral("type"), QStringLiteral("iris")}})) ==
              QStringLiteral("entree_invalide"));
    }

    SECTION("Effets : poser, régler, retirer, sur un clip, une piste et le master")
    {
        const int c = place(v1, 0);
        const int before = undoStack->count();
        QJsonObject r = call(timeline, QStringLiteral("ajouter_effet"), {{QStringLiteral("clip_id"), c}, {QStringLiteral("effet_id"), QStringLiteral("qtblend")}, {QStringLiteral("parametres"), QJsonObject{{QStringLiteral("rotation"), QStringLiteral("15")}}}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("effet_index")).toInt() == 0);
        CHECK(r.value(QStringLiteral("parametres")).toObject().value(QStringLiteral("rotation")).toString() == QStringLiteral("15"));
        CHECK(r.value(QStringLiteral("parametres")).toObject().contains(QStringLiteral("rect")));
        CHECK(undoStack->count() == before + 1);
        QJsonArray fx = clipInState(state(), c).value(QStringLiteral("fx")).toArray();
        REQUIRE(fx.size() == 1);
        CHECK(fx.first().toObject().value(QStringLiteral("id")).toString() == QStringLiteral("qtblend"));
        // Réglage : un paramètre, puis désactivation.
        r = call(timeline, QStringLiteral("regler_effet"), {{QStringLiteral("clip_id"), c}, {QStringLiteral("effet_index"), 0}, {QStringLiteral("parametres"), QJsonObject{{QStringLiteral("rotation"), QStringLiteral("30")}}}, {QStringLiteral("actif"), false}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("parametres")).toObject().value(QStringLiteral("rotation")).toString() == QStringLiteral("30"));
        CHECK_FALSE(r.value(QStringLiteral("actif")).toBool());
        CHECK(undoStack->count() == before + 2);
        fx = clipInState(state(QStringLiteral("complet")), c).value(QStringLiteral("fx")).toArray();
        CHECK(fx.first().toObject().value(QStringLiteral("off")).toBool());
        CHECK(fx.first().toObject().value(QStringLiteral("p")).toObject().value(QStringLiteral("rotation")).toString() == QStringLiteral("30"));
        // Les erreurs : paramètre inconnu, effet inconnu, rang inconnu, clip et piste ensemble.
        CHECK(errorCode(call(timeline, QStringLiteral("regler_effet"), {{QStringLiteral("clip_id"), c}, {QStringLiteral("effet_index"), 0}, {QStringLiteral("parametres"), QJsonObject{{QStringLiteral("zoom"), QStringLiteral("2")}}}})) ==
              QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_effet"), {{QStringLiteral("clip_id"), c}, {QStringLiteral("effet_id"), QStringLiteral("koracut.magie")}})) == QStringLiteral("id_inconnu"));
        CHECK(errorCode(call(timeline, QStringLiteral("supprimer_effet"), {{QStringLiteral("clip_id"), c}, {QStringLiteral("effet_index"), 4}})) == QStringLiteral("id_inconnu"));
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_effet"), {{QStringLiteral("clip_id"), c}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("effet_id"), QStringLiteral("qtblend")}})) ==
              QStringLiteral("entree_invalide"));
        // Une piste, puis le master (-1).
        r = call(timeline, QStringLiteral("ajouter_effet"), {{QStringLiteral("piste_id"), v1}, {QStringLiteral("effet_id"), QStringLiteral("qtblend")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(trackInState(state(), v1).value(QStringLiteral("fx")).toArray().size() == 1);
        r = call(timeline, QStringLiteral("ajouter_effet"), {{QStringLiteral("piste_id"), -1}, {QStringLiteral("effet_id"), QStringLiteral("qtblend")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(state().value(QStringLiteral("master_fx")).toArray().size() == 1);
        CHECK(undoStack->count() == before + 4);
        // Retirer celui du clip : la liste restante est vide.
        r = call(timeline, QStringLiteral("supprimer_effet"), {{QStringLiteral("clip_id"), c}, {QStringLiteral("effet_index"), 0}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("effet_id_retire")).toString() == QStringLiteral("qtblend"));
        CHECK(r.value(QStringLiteral("effets_restants")).toArray().isEmpty());
        CHECK_FALSE(clipInState(state(), c).contains(QStringLiteral("fx")));
        // Cinq appels, cinq pas en arrière : tout revient, puis plus rien.
        undoStack->undo();
        CHECK(clipInState(state(), c).value(QStringLiteral("fx")).toArray().size() == 1);
        undoStack->undo();
        CHECK_FALSE(state().contains(QStringLiteral("master_fx")));
        undoStack->undo();
        CHECK_FALSE(trackInState(state(), v1).contains(QStringLiteral("fx")));
        undoStack->undo();
        fx = clipInState(state(QStringLiteral("complet")), c).value(QStringLiteral("fx")).toArray();
        CHECK(fx.first().toObject().value(QStringLiteral("p")).toObject().value(QStringLiteral("rotation")).toString() == QStringLiteral("15"));
        CHECK_FALSE(fx.first().toObject().contains(QStringLiteral("off")));
        undoStack->undo();
        CHECK_FALSE(clipInState(state(), c).contains(QStringLiteral("fx")));
    }

    SECTION("Transformer, puis deux images clés sur rect")
    {
        const int c = place(v1, 0);
        const int audio = [&]() {
            const QString sound = KdenliveTests::createProducerWithSound(pCore->getProjectProfile(), binModel, 10);
            return call(timeline, QStringLiteral("ajouter_clip"), {{QStringLiteral("media_id"), sound}, {QStringLiteral("piste_id"), a1}, {QStringLiteral("position"), 100}, {QStringLiteral("partie"), QStringLiteral("audio")}})
                .value(QStringLiteral("clip_id"))
                .toInt();
        }();
        REQUIRE(timeline->isClip(audio));
        const int before = undoStack->count();
        QJsonObject r = call(timeline, QStringLiteral("transformer_clip"),
                             {{QStringLiteral("clip_id"), c}, {QStringLiteral("x_px"), 100}, {QStringLiteral("y_px"), 50}, {QStringLiteral("largeur_px"), 960}, {QStringLiteral("hauteur_px"), 540}, {QStringLiteral("opacite"), 0.5}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("effet_index")).toInt() == 0);
        CHECK(r.value(QStringLiteral("rect")).toObject().value(QStringLiteral("largeur_px")).toInt() == 960);
        CHECK(undoStack->count() == before + 1);
        QJsonArray fx = clipInState(state(QStringLiteral("complet")), c).value(QStringLiteral("fx")).toArray();
        REQUIRE(fx.size() == 1);
        // Le rect s'écrit en valeur simple ou, depuis que l'image clé de départ vide reçoit une valeur, en image clé « 0=… ».
        QString rect = fx.first().toObject().value(QStringLiteral("p")).toObject().value(QStringLiteral("rect")).toString();
        INFO(rect.toStdString());
        if (rect.contains(QLatin1Char('='))) {
            rect = rect.section(QLatin1Char('='), 1);
        }
        CHECK(rect.startsWith(QStringLiteral("100 50 960 540")));
        CHECK_FALSE(fx.first().toObject().value(QStringLiteral("p")).toObject().value(QStringLiteral("rect")).toString().contains(QStringLiteral("=;")));
        // Relancer met à jour le même effet, sans en poser un deuxième.
        r = call(timeline, QStringLiteral("transformer_clip"), {{QStringLiteral("clip_id"), c}, {QStringLiteral("x_px"), 0}, {QStringLiteral("y_px"), 0}, {QStringLiteral("largeur_px"), 1920}, {QStringLiteral("hauteur_px"), 1080}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(clipInState(state(), c).value(QStringLiteral("fx")).toArray().size() == 1);
        CHECK(errorCode(call(timeline, QStringLiteral("transformer_clip"), {{QStringLiteral("clip_id"), audio}, {QStringLiteral("x_px"), 0}, {QStringLiteral("y_px"), 0}, {QStringLiteral("largeur_px"), 10}, {QStringLiteral("hauteur_px"), 10}})) ==
              QStringLiteral("mauvais_type_de_piste"));
        // Deux images clés : un zoom lent. Puis une hors du clip, et une retirée.
        r = call(timeline, QStringLiteral("poser_keyframe"),
                 {{QStringLiteral("clip_id"), c}, {QStringLiteral("effet_index"), 0}, {QStringLiteral("parametre"), QStringLiteral("rect")}, {QStringLiteral("position_dans_clip"), 0}, {QStringLiteral("valeur"), QStringLiteral("0 0 1920 1080 1")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        r = call(timeline, QStringLiteral("poser_keyframe"),
                 {{QStringLiteral("clip_id"), c}, {QStringLiteral("effet_index"), 0}, {QStringLiteral("parametre"), QStringLiteral("rect")}, {QStringLiteral("position_dans_clip"), 39}, {QStringLiteral("valeur"), QStringLiteral("-96 -54 2112 1188 1")}, {QStringLiteral("interpolation"), QStringLiteral("courbe_douce")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        QJsonArray keys = r.value(QStringLiteral("keyframes")).toArray();
        REQUIRE(keys.size() == 2);
        CHECK(keys.last().toObject().value(QStringLiteral("i")).toInt() == 39);
        CHECK(keys.last().toObject().value(QStringLiteral("valeur")).toString().startsWith(QStringLiteral("-96 -54 2112 1188")));
        CHECK(keys.last().toObject().value(QStringLiteral("interpolation")).toString() == QStringLiteral("courbe_douce"));
        CHECK(undoStack->count() == before + 4);
        fx = clipInState(state(QStringLiteral("complet")), c).value(QStringLiteral("fx")).toArray();
        const QJsonArray kf = fx.first().toObject().value(QStringLiteral("kf")).toObject().value(QStringLiteral("rect")).toArray();
        REQUIRE(kf.size() == 2);
        CHECK(kf.last().toArray().at(0).toInt() == 39);
        CHECK(errorCode(call(timeline, QStringLiteral("poser_keyframe"),
                             {{QStringLiteral("clip_id"), c}, {QStringLiteral("effet_index"), 0}, {QStringLiteral("parametre"), QStringLiteral("rect")}, {QStringLiteral("position_dans_clip"), 40}, {QStringLiteral("valeur"), QStringLiteral("0 0 1 1 1")}})) ==
              QStringLiteral("hors_clip"));
        CHECK(errorCode(call(timeline, QStringLiteral("poser_keyframe"),
                             {{QStringLiteral("clip_id"), c}, {QStringLiteral("effet_index"), 0}, {QStringLiteral("parametre"), QStringLiteral("zoom")}, {QStringLiteral("position_dans_clip"), 5}, {QStringLiteral("valeur"), QStringLiteral("1")}})) ==
              QStringLiteral("entree_invalide"));
        r = call(timeline, QStringLiteral("poser_keyframe"),
                 {{QStringLiteral("clip_id"), c}, {QStringLiteral("effet_index"), 0}, {QStringLiteral("parametre"), QStringLiteral("rect")}, {QStringLiteral("position_dans_clip"), 39}, {QStringLiteral("supprimer"), true}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("keyframes")).toArray().size() == 1);
        CHECK(errorCode(call(timeline, QStringLiteral("poser_keyframe"),
                             {{QStringLiteral("clip_id"), c}, {QStringLiteral("effet_index"), 0}, {QStringLiteral("parametre"), QStringLiteral("rect")}, {QStringLiteral("position_dans_clip"), 39}, {QStringLiteral("valeur"), QStringLiteral("x")}, {QStringLiteral("supprimer"), true}})) ==
              QStringLiteral("id_inconnu"));
        // Cinq pas en arrière : plus d'effet.
        for (int i = 0; i < 5; ++i) {
            undoStack->undo();
        }
        CHECK_FALSE(clipInState(state(), c).contains(QStringLiteral("fx")));
        CHECK(undoStack->count() == before + 5);
    }

    SECTION("Marqueurs de timeline et de média")
    {
        const int before = undoStack->count();
        QJsonObject r = call(timeline, QStringLiteral("ajouter_marqueur"), {{QStringLiteral("position"), 125}, {QStringLiteral("commentaire"), QStringLiteral("Début interview")}, {QStringLiteral("categorie"), 1}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(undoStack->count() == before + 1);
        QJsonArray markers = state().value(QStringLiteral("marqueurs")).toArray();
        REQUIRE(markers.size() == 1);
        CHECK(markers.first().toObject().value(QStringLiteral("i")).toInt() == 125);
        CHECK(markers.first().toObject().value(QStringLiteral("txt")).toString() == QStringLiteral("Début interview"));
        CHECK(markers.first().toObject().value(QStringLiteral("cat")).toInt() == 1);
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_marqueur"), {{QStringLiteral("position"), 10}, {QStringLiteral("categorie"), 99}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_marqueur"), {{QStringLiteral("position"), 10}, {QStringLiteral("supprimer"), true}})) == QStringLiteral("id_inconnu"));
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_marqueur"), {{QStringLiteral("position"), 10}, {QStringLiteral("media_id"), QStringLiteral("9999")}})) == QStringLiteral("id_inconnu"));
        r = call(timeline, QStringLiteral("ajouter_marqueur"), {{QStringLiteral("position"), 125}, {QStringLiteral("supprimer"), true}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("commentaire")).toString() == QStringLiteral("Début interview"));
        CHECK_FALSE(state().contains(QStringLiteral("marqueurs")));
        // Sur le média rouge (40 images) : au-delà, hors_media.
        r = call(timeline, QStringLiteral("ajouter_marqueur"), {{QStringLiteral("position"), 5}, {QStringLiteral("media_id"), red}, {QStringLiteral("commentaire"), QStringLiteral("point")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(binModel->getClipByBinID(red)->markerModel()->hasMarker(5));
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_marqueur"), {{QStringLiteral("position"), 500}, {QStringLiteral("media_id"), red}})) == QStringLiteral("hors_media"));
        undoStack->undo();
        CHECK_FALSE(binModel->getClipByBinID(red)->markerModel()->hasMarker(5));
        undoStack->undo();
        CHECK(state().value(QStringLiteral("marqueurs")).toArray().size() == 1);
        undoStack->undo();
        CHECK_FALSE(state().contains(QStringLiteral("marqueurs")));
    }

    SECTION("Voir une image de la timeline et d'un média")
    {
        place(v1, 0);
        QJsonObject r = call(timeline, QStringLiteral("voir_image"), {{QStringLiteral("position"), 5}, {QStringLiteral("largeur_px"), 256}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("l")).toInt() == 256);
        CHECK(QFileInfo::exists(r.value(QStringLiteral("chemin")).toString()));
        r = call(timeline, QStringLiteral("voir_image"), {{QStringLiteral("position"), 3}, {QStringLiteral("media_id"), red}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("l")).toInt() == 384);
        CHECK(errorCode(call(timeline, QStringLiteral("voir_image"), {{QStringLiteral("position"), 3}, {QStringLiteral("media_id"), QStringLiteral("9999")}})) == QStringLiteral("id_inconnu"));
        CHECK(errorCode(call(timeline, QStringLiteral("voir_image"), {{QStringLiteral("position"), 9999}})) == QStringLiteral("hors_clip"));
    }

    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}

TEST_CASE("Outils d'agent, lot A : le son et les médias", "[KoraCutAgentTools]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    // Le dialogue d'Amélie (jeu d'essai de la coupe automatique) : elle parle à 4,5 s et à 13 s.
    const QString wav = sourcesPath + QStringLiteral("/dataset/koracut-dialogue-amelie.wav");
    const QString voiceId = KdenliveTests::createFileProducer(pCore->getProjectProfile(), binModel, wav);
    REQUIRE_FALSE(voiceId.isEmpty());
    const QString red = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 40, true);
    const int v1 = timeline->getTracksIds(false).first();
    const int a1 = timeline->getTracksIds(true).first();
    const int a2 = timeline->getTracksIds(true).last();
    REQUIRE(a1 != a2);
    auto state = [&](const QString &detail = QStringLiteral("normal")) { return call(timeline, QStringLiteral("etat_timeline"), {{QStringLiteral("detail"), detail}}); };
    QJsonObject r = call(timeline, QStringLiteral("ajouter_clip"), {{QStringLiteral("media_id"), voiceId}, {QStringLiteral("piste_id"), a1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("audio")}});
    INFO(dump(r));
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    const int voice = r.value(QStringLiteral("clip_id")).toInt();
    const int voiceLength = frames(r, QStringLiteral("duree"));
    REQUIRE(voiceLength > 25 * 14);
    r = call(timeline, QStringLiteral("ajouter_clip"), {{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("video")}});
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    const int video = r.value(QStringLiteral("clip_id")).toInt();

    SECTION("Le gain d'un clip, absolu puis relatif, images clés comprises")
    {
        const int before = undoStack->count();
        r = call(timeline, QStringLiteral("regler_gain_clip"), {{QStringLiteral("clip_id"), voice}, {QStringLiteral("gain_db"), -6}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("gain_db")).toDouble() == -6.);
        CHECK(r.value(QStringLiteral("effet_index")).toInt() == 0);
        QJsonArray fx = clipInState(state(QStringLiteral("complet")), voice).value(QStringLiteral("fx")).toArray();
        REQUIRE(fx.size() == 1);
        CHECK(fx.first().toObject().value(QStringLiteral("id")).toString() == QStringLiteral("volume"));
        CHECK(fx.first().toObject().value(QStringLiteral("p")).toObject().value(QStringLiteral("level")).toString() == QStringLiteral("-6"));
        r = call(timeline, QStringLiteral("regler_gain_clip"), {{QStringLiteral("clip_id"), voice}, {QStringLiteral("gain_db"), -3}, {QStringLiteral("relatif"), true}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("gain_db")).toDouble() == -9.);
        CHECK(clipInState(state(), voice).value(QStringLiteral("fx")).toArray().size() == 1);
        CHECK(undoStack->count() == before + 2);
        CHECK(errorCode(call(timeline, QStringLiteral("regler_gain_clip"), {{QStringLiteral("clip_id"), video}, {QStringLiteral("gain_db"), -6}})) == QStringLiteral("mauvais_type_de_piste"));
        CHECK(errorCode(call(timeline, QStringLiteral("regler_gain_clip"), {{QStringLiteral("clip_id"), voice}, {QStringLiteral("gain_db"), -60}, {QStringLiteral("relatif"), true}})) == QStringLiteral("entree_invalide"));
        undoStack->undo();
        fx = clipInState(state(QStringLiteral("complet")), voice).value(QStringLiteral("fx")).toArray();
        CHECK(fx.first().toObject().value(QStringLiteral("p")).toObject().value(QStringLiteral("level")).toString() == QStringLiteral("-6"));
        undoStack->undo();
        CHECK_FALSE(clipInState(state(), voice).contains(QStringLiteral("fx")));
    }

    SECTION("La tranche d'une piste : gain, panoramique, muet, master")
    {
        const int before = undoStack->count();
        r = call(timeline, QStringLiteral("regler_piste_audio"), {{QStringLiteral("piste_id"), a1}, {QStringLiteral("gain_db"), -3}, {QStringLiteral("panoramique"), 0.5}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("gain_db")).toDouble() == -3.);
        CHECK(r.value(QStringLiteral("panoramique")).toDouble() == 0.5);
        CHECK_FALSE(r.value(QStringLiteral("muet")).toBool());
        CHECK(undoStack->count() == before + 1);
        r = call(timeline, QStringLiteral("regler_piste_audio"), {{QStringLiteral("piste_id"), a1}, {QStringLiteral("muet"), true}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("muet")).toBool());
        CHECK(trackInState(state(), a1).value(QStringLiteral("masq")).toBool());
        r = call(timeline, QStringLiteral("regler_piste_audio"), {{QStringLiteral("piste_id"), -1}, {QStringLiteral("gain_db"), 2}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("gain_db")).toDouble() == 2.);
        CHECK(errorCode(call(timeline, QStringLiteral("regler_piste_audio"), {{QStringLiteral("piste_id"), -1}, {QStringLiteral("muet"), true}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("regler_piste_audio"), {{QStringLiteral("piste_id"), a1}, {QStringLiteral("solo"), true}})) == QStringLiteral("non_disponible"));
        CHECK(errorCode(call(timeline, QStringLiteral("regler_piste_audio"), {{QStringLiteral("piste_id"), v1}, {QStringLiteral("gain_db"), 1}})) == QStringLiteral("mauvais_type_de_piste"));
        undoStack->undo();
        undoStack->undo();
        undoStack->undo();
        r = call(timeline, QStringLiteral("regler_piste_audio"), {{QStringLiteral("piste_id"), a1}});
        CHECK(r.value(QStringLiteral("gain_db")).toDouble() == 0.);
        CHECK(r.value(QStringLiteral("panoramique")).toDouble() == 0.);
        CHECK_FALSE(r.value(QStringLiteral("muet")).toBool());
        CHECK(call(timeline, QStringLiteral("regler_piste_audio"), {{QStringLiteral("piste_id"), -1}}).value(QStringLiteral("gain_db")).toDouble() == 0.);
    }

    SECTION("Les silences d'un clip, lus du fichier")
    {
        // Le micro d'Amélie entend Thomas à -15 dB : avec un seuil à -10 dBFS, seule sa voix compte.
        r = call(timeline, QStringLiteral("detecter_silences"), {{QStringLiteral("clip_id"), voice}, {QStringLiteral("seuil_db"), -10}, {QStringLiteral("duree_mini"), 25}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("seuil_db")).toDouble() == -10.);
        const QJsonArray silences = r.value(QStringLiteral("silences")).toArray();
        REQUIRE(!silences.isEmpty());
        bool between = false;
        int last = -1;
        for (const QJsonValue &v : silences) {
            const int start = frames(v.toObject(), QStringLiteral("debut"));
            const int end = frames(v.toObject(), QStringLiteral("fin"));
            CHECK(start > last); // triés
            last = start;
            CHECK(end > start);
            CHECK(end <= voiceLength);
            CHECK(frames(v.toObject(), QStringLiteral("duree")) == end - start);
            if (start <= 25 * 9 && end >= 25 * 12) {
                between = true;
            }
        }
        CHECK(between);
        CHECK(frames(r, QStringLiteral("total_silence")) > 25 * 3);
        // Par la piste, même résultat ; les erreurs.
        const QJsonObject byTrack = call(timeline, QStringLiteral("detecter_silences"), {{QStringLiteral("piste_id"), a1}, {QStringLiteral("seuil_db"), -10}, {QStringLiteral("duree_mini"), 25}});
        CHECK(frames(byTrack, QStringLiteral("total_silence")) == frames(r, QStringLiteral("total_silence")));
        CHECK(errorCode(call(timeline, QStringLiteral("detecter_silences"), {})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("detecter_silences"), {{QStringLiteral("clip_id"), video}})) == QStringLiteral("mauvais_type_de_piste"));
        CHECK(errorCode(call(timeline, QStringLiteral("detecter_silences"), {{QStringLiteral("piste_id"), v1}})) == QStringLiteral("mauvais_type_de_piste"));
    }

    SECTION("Mesurer, normaliser, baisser sous la voix, traiter")
    {
        // La musique : le même dialogue posé sur A2, pour avoir deux pistes qui jouent.
        r = call(timeline, QStringLiteral("ajouter_clip"), {{QStringLiteral("media_id"), voiceId}, {QStringLiteral("piste_id"), a2}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("audio")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const int music = r.value(QStringLiteral("clip_id")).toInt();

        r = call(timeline, QStringLiteral("mesurer_audio"), {{QStringLiteral("cible"), QStringLiteral("clip")}, {QStringLiteral("clip_id"), voice}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const double clipLufs = r.value(QStringLiteral("lufs_integre")).toDouble();
        CHECK(clipLufs < -5.);
        CHECK(clipLufs > -60.);
        CHECK(r.value(QStringLiteral("crete_vraie_dbtp")).toDouble() > clipLufs);
        CHECK(r.value(QStringLiteral("crete_vraie_dbtp")).toDouble() <= 3.);
        CHECK(frames(r, QStringLiteral("duree")) == voiceLength);
        CHECK_FALSE(r.contains(QStringLiteral("silence")));
        // La piste, puis le mixage (deux fois la même voix : plus fort que la voix seule).
        const QJsonObject track = call(timeline, QStringLiteral("mesurer_audio"), {{QStringLiteral("cible"), QStringLiteral("piste")}, {QStringLiteral("piste_id"), a1}, {QStringLiteral("debut"), 100}, {QStringLiteral("fin"), 149}});
        INFO(dump(track));
        REQUIRE(track.value(QStringLiteral("ok")).toBool());
        CHECK(frames(track, QStringLiteral("duree")) == 50);
        const QJsonObject mix = call(timeline, QStringLiteral("mesurer_audio"), {{QStringLiteral("cible"), QStringLiteral("mixage")}});
        INFO(dump(mix));
        REQUIRE(mix.value(QStringLiteral("ok")).toBool());
        CHECK(mix.value(QStringLiteral("lufs_integre")).toDouble() > clipLufs + 3.);
        // Une plage sans son : silence.
        const QJsonObject quiet = call(timeline, QStringLiteral("mesurer_audio"), {{QStringLiteral("cible"), QStringLiteral("mixage")}, {QStringLiteral("debut"), voiceLength + 100}, {QStringLiteral("fin"), voiceLength + 200}});
        INFO(dump(quiet));
        CHECK(quiet.value(QStringLiteral("silence")).toBool());
        CHECK(errorCode(call(timeline, QStringLiteral("mesurer_audio"), {{QStringLiteral("cible"), QStringLiteral("clip")}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("mesurer_audio"), {{QStringLiteral("cible"), QStringLiteral("piste")}, {QStringLiteral("piste_id"), v1}})) == QStringLiteral("mauvais_type_de_piste"));
        CHECK(errorCode(call(timeline, QStringLiteral("mesurer_audio"), {{QStringLiteral("cible"), QStringLiteral("mixage")}, {QStringLiteral("debut"), 50}, {QStringLiteral("fin"), 10}})) == QStringLiteral("entree_invalide"));

        // Normaliser le clip à -20 LUFS : la mesure d'après doit s'en approcher, une étape.
        const int before = undoStack->count();
        r = call(timeline, QStringLiteral("normaliser_loudness"), {{QStringLiteral("cible"), QStringLiteral("clip")}, {QStringLiteral("clip_id"), voice}, {QStringLiteral("lufs_cible"), -20}, {QStringLiteral("crete_max_dbtp"), 0}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(std::abs(r.value(QStringLiteral("lufs_avant")).toDouble() - clipLufs) < 0.6);
        CHECK(std::abs(r.value(QStringLiteral("lufs_apres")).toDouble() + 20.) < 2.);
        CHECK(std::abs(r.value(QStringLiteral("gain_applique_db")).toDouble() - (-20. - clipLufs)) < 0.6);
        CHECK(undoStack->count() == before + 1);
        QJsonArray fx = clipInState(state(), voice).value(QStringLiteral("fx")).toArray();
        REQUIRE(fx.size() == 1);
        CHECK(fx.first().toObject().value(QStringLiteral("id")).toString() == QStringLiteral("loudness"));
        // Relancer remplace : toujours un seul effet de normalisation.
        r = call(timeline, QStringLiteral("normaliser_loudness"), {{QStringLiteral("cible"), QStringLiteral("clip")}, {QStringLiteral("clip_id"), voice}, {QStringLiteral("lufs_cible"), -23}, {QStringLiteral("crete_max_dbtp"), 0}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(clipInState(state(), voice).value(QStringLiteral("fx")).toArray().size() == 1);
        CHECK(std::abs(r.value(QStringLiteral("lufs_apres")).toDouble() + 23.) < 2.);
        // Le mixage à -16 : l'effet va sur le master.
        r = call(timeline, QStringLiteral("normaliser_loudness"), {{QStringLiteral("cible"), QStringLiteral("mixage")}, {QStringLiteral("lufs_cible"), -16}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(std::abs(r.value(QStringLiteral("lufs_apres")).toDouble() + 16.) < 2.);
        const QJsonArray master = state().value(QStringLiteral("master_fx")).toArray();
        REQUIRE(master.size() == 1);
        CHECK(master.first().toObject().value(QStringLiteral("id")).toString() == QStringLiteral("loudness"));
        CHECK(errorCode(call(timeline, QStringLiteral("normaliser_loudness"), {{QStringLiteral("cible"), QStringLiteral("clip")}, {QStringLiteral("lufs_cible"), -14}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("normaliser_loudness"), {{QStringLiteral("cible"), QStringLiteral("clip")}, {QStringLiteral("clip_id"), video}, {QStringLiteral("lufs_cible"), -14}})) ==
              QStringLiteral("mauvais_type_de_piste"));
        CHECK(undoStack->count() == before + 3);
        undoStack->undo();
        CHECK_FALSE(state().contains(QStringLiteral("master_fx")));
        undoStack->undo();
        undoStack->undo();
        CHECK_FALSE(clipInState(state(), voice).contains(QStringLiteral("fx")));

        // La baisse automatique : la musique (A2) descend de 6 dB là où la voix (A1) parle.
        r = call(timeline, QStringLiteral("appliquer_ducking"), {{QStringLiteral("piste_musique_id"), a2}, {QStringLiteral("piste_voix_id"), a1}, {QStringLiteral("reduction_db"), 6}, {QStringLiteral("seuil_db"), -10}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("clips_modifies")).toArray() == QJsonArray{music});
        CHECK(r.value(QStringLiteral("keyframes_poses")).toInt() >= 4);
        CHECK(r.value(QStringLiteral("zones")).toArray().size() >= 1);
        // Trois pas en arrière puis une nouvelle étape : la pile ne garde que celle-ci au-dessus.
        CHECK(undoStack->count() == before + 1);
        fx = clipInState(state(QStringLiteral("complet")), music).value(QStringLiteral("fx")).toArray();
        REQUIRE(fx.size() == 1);
        CHECK(fx.first().toObject().value(QStringLiteral("id")).toString() == QStringLiteral("volume"));
        const QJsonArray levelKeys = fx.first().toObject().value(QStringLiteral("kf")).toObject().value(QStringLiteral("level")).toArray();
        CHECK(levelKeys.size() >= 4);
        bool lowered = false;
        for (const QJsonValue &k : levelKeys) {
            if (k.toArray().at(1).toString().toDouble() < -5.5) {
                lowered = true;
            }
        }
        CHECK(lowered);
        // Relancer remplace : toujours un seul volume.
        r = call(timeline, QStringLiteral("appliquer_ducking"), {{QStringLiteral("piste_musique_id"), a2}, {QStringLiteral("piste_voix_id"), a1}, {QStringLiteral("reduction_db"), 9}, {QStringLiteral("seuil_db"), -10}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(clipInState(state(), music).value(QStringLiteral("fx")).toArray().size() == 1);
        CHECK(errorCode(call(timeline, QStringLiteral("appliquer_ducking"), {{QStringLiteral("piste_musique_id"), a1}, {QStringLiteral("piste_voix_id"), a1}})) == QStringLiteral("entree_invalide"));
        CHECK(errorCode(call(timeline, QStringLiteral("appliquer_ducking"), {{QStringLiteral("piste_musique_id"), v1}, {QStringLiteral("piste_voix_id"), a1}})) == QStringLiteral("mauvais_type_de_piste"));
        undoStack->undo();
        undoStack->undo();
        CHECK_FALSE(clipInState(state(), music).contains(QStringLiteral("fx")));

        // Les traitements : le seuil du compresseur passe des dB au linéaire ; un limiteur sur le master.
        if (EffectsRepository::get()->exists(QStringLiteral("avfilter.acompressor"))) {
            r = call(timeline, QStringLiteral("ajouter_effet_audio"), {{QStringLiteral("clip_id"), voice}, {QStringLiteral("traitement"), QStringLiteral("compresseur")}, {QStringLiteral("seuil_db"), -18}, {QStringLiteral("ratio"), 3}, {QStringLiteral("frequence_hz"), 100}});
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(r.value(QStringLiteral("effet_id")).toString() == QStringLiteral("avfilter.acompressor"));
            CHECK(std::abs(r.value(QStringLiteral("parametres")).toObject().value(QStringLiteral("av.threshold")).toString().toDouble() - 0.126) < 0.002);
            CHECK(r.value(QStringLiteral("parametres")).toObject().value(QStringLiteral("av.ratio")).toString().toDouble() == 3.);
            CHECK(r.value(QStringLiteral("champs_ignores")).toArray() == QJsonArray{QStringLiteral("frequence_hz")});
            r = call(timeline, QStringLiteral("ajouter_effet_audio"), {{QStringLiteral("piste_id"), -1}, {QStringLiteral("traitement"), QStringLiteral("limiteur")}, {QStringLiteral("seuil_db"), -1}});
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(std::abs(r.value(QStringLiteral("parametres")).toObject().value(QStringLiteral("av.limit")).toString().toDouble() - 0.891) < 0.002);
            CHECK(state().value(QStringLiteral("master_fx")).toArray().size() == 1);
            r = call(timeline, QStringLiteral("ajouter_effet_audio"), {{QStringLiteral("piste_id"), a1}, {QStringLiteral("traitement"), QStringLiteral("passe_haut")}, {QStringLiteral("frequence_hz"), 100}});
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(r.value(QStringLiteral("parametres")).toObject().value(QStringLiteral("av.f")).toString().toDouble() == 100.);
            CHECK(undoStack->count() == before + 3);
            // Le dé-esseur arrive réglé : posé nu, son intensité était nulle et il ne faisait rien.
            if (EffectsRepository::get()->exists(QStringLiteral("avfilter.deesser"))) {
                r = call(timeline, QStringLiteral("ajouter_effet_audio"), {{QStringLiteral("clip_id"), voice}, {QStringLiteral("traitement"), QStringLiteral("de_esseur")}});
                INFO(dump(r));
                REQUIRE(r.value(QStringLiteral("ok")).toBool());
                CHECK(r.value(QStringLiteral("parametres")).toObject().value(QStringLiteral("av.i")).toString().toDouble() == 0.4);
                CHECK(r.value(QStringLiteral("parametres")).toObject().value(QStringLiteral("av.f")).toString().toDouble() == 0.5);
                undoStack->undo();
            }
            undoStack->undo();
            undoStack->undo();
            undoStack->undo();
            CHECK_FALSE(state().contains(QStringLiteral("master_fx")));
            CHECK_FALSE(clipInState(state(), voice).contains(QStringLiteral("fx")));
            CHECK_FALSE(trackInState(state(), a1).contains(QStringLiteral("fx")));
        } else {
            CHECK(errorCode(call(timeline, QStringLiteral("ajouter_effet_audio"), {{QStringLiteral("clip_id"), voice}, {QStringLiteral("traitement"), QStringLiteral("compresseur")}})) == QStringLiteral("non_disponible"));
        }
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_effet_audio"), {{QStringLiteral("clip_id"), video}, {QStringLiteral("traitement"), QStringLiteral("compresseur")}})) == QStringLiteral("mauvais_type_de_piste"));
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_effet_audio"), {{QStringLiteral("clip_id"), voice}, {QStringLiteral("traitement"), QStringLiteral("reverb")}})) == QStringLiteral("entree_invalide"));
    }

    SECTION("Importer, retirer, transcrire")
    {
        const size_t medias = binModel->getAllClipIds().size();
        const int before = undoStack->count();
        const QString file = sourcesPath + QStringLiteral("/dataset/koracut-voix-silences.wav");
        REQUIRE(QFileInfo::exists(file));
        r = call(timeline, QStringLiteral("importer_media"), {{QStringLiteral("chemins"), QJsonArray{file, QStringLiteral("/nulle/part.mp4"), QStringLiteral("https://exemple.org/a.mp4")}}, {QStringLiteral("dossier_chutier"), QStringLiteral("Voix")}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const QJsonArray imported = r.value(QStringLiteral("medias")).toArray();
        REQUIRE(imported.size() == 1);
        const QString newId = imported.first().toObject().value(QStringLiteral("id")).toString();
        CHECK(imported.first().toObject().value(QStringLiteral("type")).toString() == QStringLiteral("audio"));
        CHECK_FALSE(imported.first().toObject().value(QStringLiteral("deja_present")).toBool());
        CHECK(frames(imported.first().toObject(), QStringLiteral("duree")) > 0);
        CHECK(r.value(QStringLiteral("refuses")).toArray().size() == 2);
        CHECK(binModel->getAllClipIds().size() == medias + 1);
        CHECK(undoStack->count() == before + 1);
        // Le même fichier : déjà présent, rien d'ajouté, pas d'étape.
        r = call(timeline, QStringLiteral("importer_media"), {{QStringLiteral("chemins"), QJsonArray{file}}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("medias")).toArray().first().toObject().value(QStringLiteral("deja_present")).toBool());
        CHECK(r.value(QStringLiteral("medias")).toArray().first().toObject().value(QStringLiteral("id")).toString() == newId);
        CHECK(binModel->getAllClipIds().size() == medias + 1);
        CHECK(undoStack->count() == before + 1);
        CHECK(errorCode(call(timeline, QStringLiteral("importer_media"), {{QStringLiteral("chemins"), QJsonArray{QStringLiteral("/nulle/part.mp4")}}})) == QStringLiteral("fichier_introuvable"));
        // Retirer le média : il quitte le chutier, son clip de timeline avec lui ; Annuler le remet.
        r = call(timeline, QStringLiteral("ajouter_clip"), {{QStringLiteral("media_id"), newId}, {QStringLiteral("piste_id"), a2}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("audio")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const int posed = r.value(QStringLiteral("clip_id")).toInt();
        r = call(timeline, QStringLiteral("retirer_media"), {{QStringLiteral("media_ids"), QJsonArray{newId}}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("clips_timeline_retires")).toInt() == 1);
        CHECK_FALSE(timeline->isClip(posed));
        CHECK_FALSE(binModel->hasClip(newId));
        CHECK(undoStack->count() == before + 3);
        CHECK(errorCode(call(timeline, QStringLiteral("retirer_media"), {{QStringLiteral("media_ids"), QJsonArray{newId}}})) == QStringLiteral("id_inconnu"));
        undoStack->undo();
        CHECK(binModel->hasClip(newId));
        CHECK(timeline->isClip(posed));
        undoStack->undo();
        undoStack->undo();
        CHECK(binModel->getAllClipIds().size() == medias);
        // La transcription : Whisper s'il est installé, sinon l'outil le dit.
        r = call(timeline, QStringLiteral("transcrire_media"), {{QStringLiteral("media_id"), voiceId}, {QStringLiteral("langue"), QStringLiteral("fr")}, {QStringLiteral("niveau"), QStringLiteral("segments")}});
        INFO(dump(r));
        if (r.value(QStringLiteral("ok")).toBool()) {
            CHECK(r.contains(QStringLiteral("segments")));
            CHECK(r.contains(QStringLiteral("depuis_cache")));
        } else {
            CHECK(errorCode(r) == QStringLiteral("non_disponible"));
        }
        CHECK(errorCode(call(timeline, QStringLiteral("transcrire_media"), {{QStringLiteral("media_id"), QStringLiteral("9999")}})) == QStringLiteral("id_inconnu"));
        CHECK(errorCode(call(timeline, QStringLiteral("transcrire_media"), {{QStringLiteral("media_id"), red}})) == QStringLiteral("entree_invalide"));
    }

    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}
