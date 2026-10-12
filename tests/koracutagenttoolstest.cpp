/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "catch.hpp"
#include "test_utils.hpp"

#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "koracut/koracutagenttools.h"

#include <QJsonArray>
#include <QJsonDocument>

using KoraCutAgentTools::call;

namespace {
QString errorCode(const QJsonObject &r)
{
    return r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString();
}

// Le clip de la piste @p trackId dans la réponse d'etat_timeline, par son id.
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
}

TEST_CASE("Outils d'agent : temps et timecodes", "[KoraCutAgentTools]")
{
    using namespace KoraCutAgentTools;
    CHECK(parseTime(QJsonValue(150), 25) == 150);
    CHECK(parseTime(QJsonValue(QStringLiteral("00:00:06:00")), 25) == 150);
    CHECK(parseTime(QJsonValue(QStringLiteral("00:01:00:12")), 25) == 1512);
    CHECK(parseTime(QJsonValue(QStringLiteral("00:00:06:25")), 25) == -1); // 25 images n'existe pas à 25 i/s
    CHECK(parseTime(QJsonValue(QStringLiteral("6 s")), 25) == -1);
    CHECK(parseTime(QJsonValue(-3), 25) == -1);
    CHECK(parseTime(QJsonValue(2.5), 25) == -1);
    CHECK(parseTime(QJsonValue(), 25) == -1);
    CHECK(timecode(150, 25) == QStringLiteral("00:00:06:00"));
    CHECK(timecode(1512, 25) == QStringLiteral("00:01:00:12"));
    CHECK(timecode(90000, 25) == QStringLiteral("01:00:00:00"));
}

TEST_CASE("Outils d'agent : monter une séquence", "[KoraCutAgentTools]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    const QString red = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 20, true);
    const int v1 = timeline->getTracksIds(false).first();

    SECTION("Outil inconnu et état de départ")
    {
        CHECK(errorCode(call(timeline, QStringLiteral("faire_le_cafe"), {})) == QStringLiteral("outil_inconnu"));
        CHECK(KoraCutAgentTools::implementedTools().contains(QStringLiteral("couper_clip")));
        const QJsonObject state = call(timeline, QStringLiteral("etat_timeline"), {});
        REQUIRE(state.value(QStringLiteral("ok")).toBool());
        CHECK(state.value(QStringLiteral("v")).toInt() == 1);
        const QJsonArray pistes = state.value(QStringLiteral("pistes")).toArray();
        CHECK(pistes.size() == timeline->getTracksCount());
        // De haut en bas : les pistes vidéo d'abord, rangs dans l'ordre.
        CHECK(pistes.first().toObject().value(QStringLiteral("t")).toString() == QStringLiteral("v"));
        CHECK(pistes.last().toObject().value(QStringLiteral("t")).toString() == QStringLiteral("a"));
        for (int i = 0; i < pistes.size(); ++i) {
            CHECK(pistes.at(i).toObject().value(QStringLiteral("rang")).toInt() == i);
        }
        // Le niveau complet existe depuis le lot A (24 septembre 2026) ; un niveau inconnu est refusé.
        CHECK(call(timeline, QStringLiteral("etat_timeline"), {{QStringLiteral("detail"), QStringLiteral("complet")}}).value(QStringLiteral("ok")).toBool());
        CHECK(errorCode(call(timeline, QStringLiteral("etat_timeline"), {{QStringLiteral("detail"), QStringLiteral("tout")}})) == QStringLiteral("entree_invalide"));
    }

    SECTION("Poser, couper, déplacer, rogner, retirer")
    {
        // Poser le média entier, puis refuser une place prise.
        QJsonObject r = call(timeline, QStringLiteral("ajouter_clip"),
                             {{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("video")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const int clip = r.value(QStringLiteral("clip_id")).toInt();
        CHECK(r.value(QStringLiteral("duree")).toObject().value(QStringLiteral("i")).toInt() == 20);
        CHECK(r.value(QStringLiteral("position")).toObject().value(QStringLiteral("tc")).toString() == QStringLiteral("00:00:00:00"));
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_clip"),
                             {{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 10}, {QStringLiteral("partie"), QStringLiteral("video")}})) ==
              QStringLiteral("place_occupee"));
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_clip"),
                             {{QStringLiteral("media_id"), QStringLiteral("9999")}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 100}})) ==
              QStringLiteral("id_inconnu"));

        // Une portion de la source : entree 5, sortie 15 (exclue), donc 10 images.
        r = call(timeline, QStringLiteral("ajouter_clip"),
                 {{QStringLiteral("media_id"), red},
                  {QStringLiteral("piste_id"), v1},
                  {QStringLiteral("position"), QStringLiteral("00:00:02:00")},
                  {QStringLiteral("entree"), 5},
                  {QStringLiteral("sortie"), 15},
                  {QStringLiteral("partie"), QStringLiteral("video")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const int portion = r.value(QStringLiteral("clip_id")).toInt();
        CHECK(timeline->getClipPosition(portion) == 50);
        QJsonObject state = call(timeline, QStringLiteral("etat_timeline"), {});
        QJsonObject c = clipInState(state, portion);
        CHECK(c.value(QStringLiteral("in")).toInt() == 5);
        CHECK(c.value(QStringLiteral("out")).toInt() == 15);
        CHECK(c.value(QStringLiteral("dur")).toInt() == 10);
        CHECK(c.value(QStringLiteral("pos")).toInt() == 50);

        // Couper au milieu ; refuser une coupe au bord.
        CHECK(errorCode(call(timeline, QStringLiteral("couper_clip"), {{QStringLiteral("clip_id"), clip}, {QStringLiteral("position"), 0}})) == QStringLiteral("hors_clip"));
        r = call(timeline, QStringLiteral("couper_clip"), {{QStringLiteral("clip_id"), clip}, {QStringLiteral("position"), 8}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const int right = r.value(QStringLiteral("clip_droit_id")).toInt();
        CHECK(right != clip);
        CHECK(timeline->getClipPosition(right) == 8);
        CHECK(timeline->getClipPlaytime(clip) == 8);
        CHECK(timeline->getClipPlaytime(right) == 12);

        // Déplacer le morceau de droite ; une place prise est refusée sans rien changer.
        CHECK(errorCode(call(timeline, QStringLiteral("deplacer_clip"),
                             {{QStringLiteral("clip_id"), right}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 52}})) == QStringLiteral("place_occupee"));
        CHECK(timeline->getClipPosition(right) == 8);
        r = call(timeline, QStringLiteral("deplacer_clip"), {{QStringLiteral("clip_id"), right}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 100}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(timeline->getClipPosition(right) == 100);

        // Rogner la fin à 5 images ; impossible d'aller au-delà du média source.
        r = call(timeline, QStringLiteral("rogner_clip"), {{QStringLiteral("clip_id"), right}, {QStringLiteral("cote"), QStringLiteral("fin")}, {QStringLiteral("duree"), 5}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(timeline->getClipPlaytime(right) == 5);
        CHECK(timeline->getClipPosition(right) == 100);
        CHECK(errorCode(call(timeline, QStringLiteral("rogner_clip"), {{QStringLiteral("clip_id"), right}, {QStringLiteral("cote"), QStringLiteral("fin")}, {QStringLiteral("duree"), 500}})) ==
              QStringLiteral("hors_media"));

        // Retirer sans refermer : le vide reste.
        r = call(timeline, QStringLiteral("supprimer_clip"), {{QStringLiteral("clip_ids"), QJsonArray{right}}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK_FALSE(timeline->isClip(right));
        CHECK(errorCode(call(timeline, QStringLiteral("supprimer_clip"), {{QStringLiteral("clip_ids"), QJsonArray{right}}})) == QStringLiteral("id_inconnu"));

        // Retirer en refermant : le clip de 50 à 59 part, le vide de 8 à 59 se referme... sauf que le
        // clip de 0 à 7 reste : seul le vide laissé à 50 se referme, jusqu'au clip suivant (aucun ici).
        r = call(timeline, QStringLiteral("supprimer_clip"), {{QStringLiteral("clip_ids"), QJsonArray{portion}}, {QStringLiteral("refermer"), true}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK_FALSE(timeline->isClip(portion));
        CHECK(timeline->getClipPosition(clip) == 0);

        // Chaque outil est une étape d'annulation : on revient en arrière d'un pas.
        undoStack->undo();
        CHECK(timeline->isClip(timeline->getClipByPosition(v1, 50)));
    }

    SECTION("Pistes : ajouter, nommer, verrouiller, masquer")
    {
        const int before = timeline->getTracksCount();
        QJsonObject r = call(timeline, QStringLiteral("ajouter_piste"), {{QStringLiteral("type"), QStringLiteral("video")}, {QStringLiteral("nom"), QStringLiteral("Titres")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const int titles = r.value(QStringLiteral("piste_id")).toInt();
        CHECK(timeline->getTracksCount() == before + 1);
        CHECK(r.value(QStringLiteral("rang")).toInt() == 0); // tout en haut
        CHECK(r.value(QStringLiteral("nom")).toString() == QStringLiteral("Titres"));
        CHECK_FALSE(timeline->isAudioTrack(titles));

        r = call(timeline, QStringLiteral("ajouter_piste"), {{QStringLiteral("type"), QStringLiteral("audio")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const int newAudio = r.value(QStringLiteral("piste_id")).toInt();
        CHECK(timeline->isAudioTrack(newAudio));
        CHECK(r.value(QStringLiteral("rang")).toInt() == timeline->getTracksCount() - 1); // tout en bas

        // supprimer_piste : une piste vide part en une étape, et revient à l'annulation.
        {
            const int count = timeline->getTracksCount();
            QJsonObject d = call(timeline, QStringLiteral("supprimer_piste"), {{QStringLiteral("piste_id"), newAudio}});
            REQUIRE(d.value(QStringLiteral("ok")).toBool());
            CHECK(d.value(QStringLiteral("type")).toString() == QStringLiteral("audio"));
            CHECK_FALSE(d.value(QStringLiteral("nom")).toString().isEmpty());
            CHECK(d.value(QStringLiteral("pistes_restantes")).toInt() == count - 1);
            CHECK(timeline->getTracksCount() == count - 1);
            CHECK_FALSE(timeline->isTrack(newAudio));
            CHECK(errorCode(call(timeline, QStringLiteral("supprimer_piste"), {{QStringLiteral("piste_id"), newAudio}})) == QStringLiteral("id_inconnu"));
            undoStack->undo();
            CHECK(timeline->getTracksCount() == count);
            undoStack->redo();
            CHECK(timeline->getTracksCount() == count - 1);
            // Verrouillée : refusée.
            call(timeline, QStringLiteral("regler_piste"), {{QStringLiteral("piste_id"), titles}, {QStringLiteral("verrouillee"), true}});
            CHECK(errorCode(call(timeline, QStringLiteral("supprimer_piste"), {{QStringLiteral("piste_id"), titles}})) == QStringLiteral("piste_verrouillee"));
            call(timeline, QStringLiteral("regler_piste"), {{QStringLiteral("piste_id"), titles}, {QStringLiteral("verrouillee"), false}});
        }

        r = call(timeline, QStringLiteral("regler_piste"), {{QStringLiteral("piste_id"), titles}, {QStringLiteral("masquee"), true}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("masquee")).toBool());
        r = call(timeline, QStringLiteral("regler_piste"), {{QStringLiteral("piste_id"), titles}, {QStringLiteral("masquee"), false}, {QStringLiteral("verrouillee"), true}});
        CHECK_FALSE(r.value(QStringLiteral("masquee")).toBool());
        CHECK(r.value(QStringLiteral("verrouillee")).toBool());
        // Une piste verrouillée refuse qu'on y pose quoi que ce soit.
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_clip"),
                             {{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), titles}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("video")}})) ==
              QStringLiteral("piste_verrouillee"));
        const QJsonObject state = call(timeline, QStringLiteral("etat_timeline"), {{QStringLiteral("detail"), QStringLiteral("resume")}});
        const QJsonObject top = state.value(QStringLiteral("pistes")).toArray().first().toObject();
        CHECK(top.value(QStringLiteral("id")).toInt() == titles);
        CHECK(top.value(QStringLiteral("nom")).toString() == QStringLiteral("Titres"));
        CHECK(top.value(QStringLiteral("verr")).toBool());
        CHECK(top.value(QStringLiteral("nb")).toInt() == 0);
        CHECK_FALSE(top.contains(QStringLiteral("clips")));
    }

    SECTION("Lot 2 : médias, vides, vitesse")
    {
        // Le chutier : le média rouge y est, de type couleur, et on le retrouve par son nom.
        QJsonObject r = call(timeline, QStringLiteral("lister_medias"), {});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("total")).toInt() >= 1);
        bool found = false;
        for (const QJsonValue &m : r.value(QStringLiteral("medias")).toArray()) {
            if (m.toObject().value(QStringLiteral("id")).toString() == red) {
                found = true;
                CHECK(m.toObject().value(QStringLiteral("type")).toString() == QStringLiteral("couleur"));
                CHECK(m.toObject().value(QStringLiteral("utilise")).toInt() == 0);
            }
        }
        CHECK(found);
        r = call(timeline, QStringLiteral("lister_medias"), {{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("recherche"), QStringLiteral("aucun-nom-pareil")}});
        CHECK(r.value(QStringLiteral("total")).toInt() == 0);

        // Refermer un vide : B, posé à 40, recule jusqu'à toucher A (0 à 19).
        const int a = call(timeline, QStringLiteral("ajouter_clip"),
                           {{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("video")}})
                          .value(QStringLiteral("clip_id"))
                          .toInt();
        const int b = call(timeline, QStringLiteral("ajouter_clip"),
                           {{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 40}, {QStringLiteral("partie"), QStringLiteral("video")}})
                          .value(QStringLiteral("clip_id"))
                          .toInt();
        REQUIRE(timeline->isClip(a));
        REQUIRE(timeline->isClip(b));
        CHECK(errorCode(call(timeline, QStringLiteral("supprimer_espace"), {{QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 5}})) == QStringLiteral("pas_de_vide"));
        r = call(timeline, QStringLiteral("supprimer_espace"), {{QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 25}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(timeline->getClipPosition(b) == 20);
        CHECK(r.value(QStringLiteral("duree_refermee")).toObject().value(QStringLiteral("i")).toInt() == 20);
        CHECK(errorCode(call(timeline, QStringLiteral("supprimer_espace"), {{QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 60}})) == QStringLiteral("pas_de_vide"));
        r = call(timeline, QStringLiteral("lister_medias"), {{QStringLiteral("recherche"), QStringLiteral("")}});
        for (const QJsonValue &m : r.value(QStringLiteral("medias")).toArray()) {
            if (m.toObject().value(QStringLiteral("id")).toString() == red) {
                CHECK(m.toObject().value(QStringLiteral("utilise")).toInt() == 2);
            }
        }

        // La vitesse : un son de 10 images ralenti à 50 % en dure 20.
        const QString sound = KdenliveTests::createProducerWithSound(pCore->getProjectProfile(), binModel, 10);
        const int a1 = timeline->getTracksIds(true).last();
        r = call(timeline, QStringLiteral("ajouter_clip"),
                 {{QStringLiteral("media_id"), sound}, {QStringLiteral("piste_id"), a1}, {QStringLiteral("position"), 100}, {QStringLiteral("partie"), QStringLiteral("audio")}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        const int s = r.value(QStringLiteral("clip_id")).toInt();
        // Une sortie au-delà de la fin du média : hors_media avec sa durée, pas place_occupee (1er octobre 2026).
        r = call(timeline, QStringLiteral("ajouter_clip"),
                 {{QStringLiteral("media_id"), sound}, {QStringLiteral("piste_id"), a1}, {QStringLiteral("position"), 300}, {QStringLiteral("partie"), QStringLiteral("audio")},
                  {QStringLiteral("entree"), 0}, {QStringLiteral("sortie"), 30}});
        CHECK(errorCode(r) == QStringLiteral("hors_media"));
        CHECK(r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("message")).toString().contains(QStringLiteral("10 images")));
        CHECK(r.value(QStringLiteral("duree_media")).toObject().value(QStringLiteral("i")).toInt() == 10);
        // Jusqu'à la dernière image, c'est bon (sortie est exclue) ; une couleur n'a pas de fin.
        r = call(timeline, QStringLiteral("ajouter_clip"),
                 {{QStringLiteral("media_id"), sound}, {QStringLiteral("piste_id"), a1}, {QStringLiteral("position"), 300}, {QStringLiteral("partie"), QStringLiteral("audio")},
                  {QStringLiteral("entree"), 0}, {QStringLiteral("sortie"), 10}});
        CHECK(r.value(QStringLiteral("ok")).toBool());
        // Mode piste_libre : un second son au même endroit part sur une autre piste audio, créée s'il le faut,
        // en une seule étape d'annulation ; avec image et son, le mode est refusé.
        {
            const int tracksBefore = timeline->getTracksCount();
            const int stepsBefore = undoStack->index();
            QJsonObject f = call(timeline, QStringLiteral("ajouter_clip"),
                                 {{QStringLiteral("media_id"), sound}, {QStringLiteral("piste_id"), a1}, {QStringLiteral("position"), 300}, {QStringLiteral("partie"), QStringLiteral("audio")},
                                  {QStringLiteral("mode"), QStringLiteral("piste_libre")}});
            REQUIRE(f.value(QStringLiteral("ok")).toBool());
            const int landed = f.value(QStringLiteral("piste_id")).toInt();
            CHECK(landed != a1);
            CHECK(timeline->isAudioTrack(landed));
            CHECK(timeline->getClipPosition(f.value(QStringLiteral("clip_id")).toInt()) == 300);
            CHECK(undoStack->index() == stepsBefore + 1);
            CHECK(f.value(QStringLiteral("piste_creee")).toBool() == (timeline->getTracksCount() == tracksBefore + 1));
            undoStack->undo();
            CHECK(timeline->getTracksCount() == tracksBefore);
            CHECK_FALSE(timeline->isClip(f.value(QStringLiteral("clip_id")).toInt()));
            // À une place libre, piste_libre reste sur la piste demandée.
            f = call(timeline, QStringLiteral("ajouter_clip"),
                     {{QStringLiteral("media_id"), sound}, {QStringLiteral("piste_id"), a1}, {QStringLiteral("position"), 700}, {QStringLiteral("partie"), QStringLiteral("audio")},
                      {QStringLiteral("mode"), QStringLiteral("piste_libre")}});
            REQUIRE(f.value(QStringLiteral("ok")).toBool());
            CHECK(f.value(QStringLiteral("piste_id")).toInt() == a1);
            CHECK_FALSE(f.value(QStringLiteral("piste_creee")).toBool());
            undoStack->undo();
            CHECK(errorCode(call(timeline, QStringLiteral("ajouter_clip"),
                                 {{QStringLiteral("media_id"), sound}, {QStringLiteral("piste_id"), a1}, {QStringLiteral("position"), 700}, {QStringLiteral("mode"), QStringLiteral("piste_libre")}})) ==
                  QStringLiteral("entree_invalide"));
        }
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_clip"),
                             {{QStringLiteral("media_id"), sound}, {QStringLiteral("piste_id"), a1}, {QStringLiteral("position"), 700}, {QStringLiteral("partie"), QStringLiteral("audio")},
                              {QStringLiteral("mode"), QStringLiteral("ailleurs")}})) == QStringLiteral("entree_invalide"));
        // sortie sans entree : du début du média jusqu'à sortie, pas le média entier (relevé le 6 octobre 2026).
        r = call(timeline, QStringLiteral("ajouter_clip"),
                 {{QStringLiteral("media_id"), sound}, {QStringLiteral("piste_id"), a1}, {QStringLiteral("position"), 400}, {QStringLiteral("partie"), QStringLiteral("audio")},
                  {QStringLiteral("sortie"), 4}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("duree")).toObject().value(QStringLiteral("i")).toInt() == 4);
        CHECK(timeline->getClipIn(r.value(QStringLiteral("clip_id")).toInt()) == 0);
        // entree sans sortie : de entree jusqu'à la fin du média.
        r = call(timeline, QStringLiteral("ajouter_clip"),
                 {{QStringLiteral("media_id"), sound}, {QStringLiteral("piste_id"), a1}, {QStringLiteral("position"), 500}, {QStringLiteral("partie"), QStringLiteral("audio")},
                  {QStringLiteral("entree"), 6}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("duree")).toObject().value(QStringLiteral("i")).toInt() == 4);
        CHECK(timeline->getClipIn(r.value(QStringLiteral("clip_id")).toInt()) == 6);
        CHECK(errorCode(call(timeline, QStringLiteral("changer_vitesse_clip"), {{QStringLiteral("clip_id"), s}, {QStringLiteral("vitesse_pourcent"), 0}})) == QStringLiteral("entree_invalide"));
        r = call(timeline, QStringLiteral("changer_vitesse_clip"), {{QStringLiteral("clip_id"), s}, {QStringLiteral("vitesse_pourcent"), 50}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("vitesse_pourcent")).toInt() == 50);
        CHECK(timeline->getClipPlaytime(s) == 20);
        const QJsonObject state = call(timeline, QStringLiteral("etat_timeline"), {});
        CHECK(clipInState(state, s).value(QStringLiteral("vit")).toInt() == 50);
    }

    SECTION("Lot 3 : fondus")
    {
        const int c = call(timeline, QStringLiteral("ajouter_clip"),
                           {{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("video")}})
                          .value(QStringLiteral("clip_id"))
                          .toInt();
        REQUIRE(timeline->isClip(c));
        auto fades = [&]() { return clipInState(call(timeline, QStringLiteral("etat_timeline"), {}), c).value(QStringLiteral("fondu")).toArray(); };
        CHECK(fades().isEmpty());

        QJsonObject r = call(timeline, QStringLiteral("ajouter_fondu"), {{QStringLiteral("clip_id"), c}, {QStringLiteral("cote"), QStringLiteral("entree")}, {QStringLiteral("duree"), 5}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(r.value(QStringLiteral("duree")).toObject().value(QStringLiteral("i")).toInt() == 5);
        r = call(timeline, QStringLiteral("ajouter_fondu"), {{QStringLiteral("clip_id"), c}, {QStringLiteral("cote"), QStringLiteral("sortie")}, {QStringLiteral("duree"), 8}});
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(fades() == QJsonArray{5, 8});

        // Remplacer la durée, puis retirer avec 0 ; chaque appel s'annule d'un pas.
        call(timeline, QStringLiteral("ajouter_fondu"), {{QStringLiteral("clip_id"), c}, {QStringLiteral("cote"), QStringLiteral("entree")}, {QStringLiteral("duree"), 3}});
        CHECK(fades() == QJsonArray{3, 8});
        call(timeline, QStringLiteral("ajouter_fondu"), {{QStringLiteral("clip_id"), c}, {QStringLiteral("cote"), QStringLiteral("sortie")}, {QStringLiteral("duree"), 0}});
        CHECK(fades() == QJsonArray{3, 0});
        undoStack->undo();
        CHECK(fades() == QJsonArray{3, 8});
        undoStack->undo();
        INFO("fondus : " << QJsonDocument(fades()).toJson(QJsonDocument::Compact).toStdString());
        CHECK(fades() == QJsonArray{5, 8});

        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_fondu"), {{QStringLiteral("clip_id"), c}, {QStringLiteral("cote"), QStringLiteral("entree")}, {QStringLiteral("duree"), 500}})) ==
              QStringLiteral("hors_clip"));
        CHECK(errorCode(call(timeline, QStringLiteral("ajouter_fondu"), {{QStringLiteral("clip_id"), c}, {QStringLiteral("cote"), QStringLiteral("milieu")}, {QStringLiteral("duree"), 5}})) ==
              QStringLiteral("entree_invalide"));
    }

    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}
