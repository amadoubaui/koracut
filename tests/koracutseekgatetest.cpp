/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Chantier 10 (document 6, journal du 4 octobre 2026) : le glisser de la tête de lecture.
// Une seule demande d'image en vol, la plus récente gagne, et l'image de la dernière position arrive toujours.

#include "catch.hpp"

#include "koracut/koracutseekgate.h"

using Step = KoraCutSeekGate::Step;

TEST_CASE("Glisser : une demande seule part tout de suite", "[KoraCut]")
{
    KoraCutSeekGate gate;
    CHECK_FALSE(gate.awaiting());
    REQUIRE(gate.request(100, false));
    gate.sent(100);
    CHECK(gate.awaiting());
    CHECK(gate.held() == 0);
    // L'image arrive : plus rien à faire, la suivante repartira tout de suite.
    CHECK(gate.frameShown(100).kind == Step::None);
    CHECK_FALSE(gate.awaiting());
    CHECK(gate.request(101, false));
}

TEST_CASE("Glisser : en rafale, une seule demande en vol et la plus récente gagne", "[KoraCut]")
{
    KoraCutSeekGate gate;
    REQUIRE(gate.request(100, false));
    gate.sent(100);
    // Soixante événements pendant que l'image 100 se calcule : aucun ne part.
    for (int p = 101; p <= 160; ++p) {
        CHECK_FALSE(gate.request(p, p % 2 == 0));
    }
    CHECK(gate.held() == 60);
    CHECK(gate.pending() == 160);
    // L'image 100 s'affiche : on part vers la dernière position, et vers elle seule.
    Step step = gate.frameShown(100);
    REQUIRE(step.kind == Step::Seek);
    CHECK(step.position == 160);
    CHECK(step.noAudioScrub);
    gate.sent(160);
    CHECK(gate.pending() == -1);
    // Au relâcher : l'image exacte de la dernière position arrive, puis le calme.
    CHECK(gate.frameShown(160).kind == Step::None);
    CHECK_FALSE(gate.awaiting());
    CHECK(gate.held() == 0);
}

TEST_CASE("Glisser : une image d'une autre position ne libère rien, le délai de garde si", "[KoraCut]")
{
    KoraCutSeekGate gate;
    REQUIRE(gate.request(50, false));
    gate.sent(50);
    CHECK_FALSE(gate.request(80, false));
    // Une image en retard (rafraîchissement d'avant) : on attend toujours la 50.
    CHECK(gate.frameShown(12).kind == Step::None);
    CHECK(gate.awaiting());
    CHECK(gate.pending() == 80);
    // L'image n'arrive jamais (image refusée par l'afficheur) : le délai de garde fait repartir.
    Step step = gate.guardExpired();
    REQUIRE(step.kind == Step::Seek);
    CHECK(step.position == 80);
    gate.sent(80);
    // Garde sans rien de retenu : on cesse d'attendre.
    CHECK(gate.guardExpired().kind == Step::None);
    CHECK_FALSE(gate.awaiting());
}

TEST_CASE("Glisser : revenir sur la position en vol efface la demande retenue", "[KoraCut]")
{
    KoraCutSeekGate gate;
    REQUIRE(gate.request(200, false));
    gate.sent(200);
    CHECK_FALSE(gate.request(230, false));
    CHECK_FALSE(gate.request(200, false));
    CHECK(gate.pending() == -1);
    CHECK(gate.frameShown(200).kind == Step::None);
    CHECK_FALSE(gate.awaiting());
}

TEST_CASE("Glisser : une édition pose la position retenue, son image reste due", "[KoraCut]")
{
    KoraCutSeekGate gate;
    CHECK(gate.flush() == -1);
    REQUIRE(gate.request(10, false));
    gate.sent(10);
    CHECK_FALSE(gate.request(40, false));
    // Avant de lire la position du producteur : la position retenue est rendue, une seule fois.
    CHECK(gate.flush() == 40);
    CHECK(gate.flush() == -1);
    CHECK(gate.owed());
    CHECK(gate.target() == 40);
    // L'image de l'ancienne position ne compte plus ; au délai de garde, l'image due est demandée.
    CHECK(gate.frameShown(10).kind == Step::None);
    Step step = gate.guardExpired();
    REQUIRE(step.kind == Step::Refresh);
    CHECK(step.position == 40);
    CHECK(gate.awaiting());
    CHECK(gate.frameShown(40).kind == Step::None);
    CHECK_FALSE(gate.awaiting());

    // Même chose quand un rafraîchissement part de lui-même après la pose : plus rien n'est dû.
    REQUIRE(gate.request(10, false));
    gate.sent(10);
    CHECK_FALSE(gate.request(40, false));
    CHECK(gate.flush() == 40);
    gate.refreshSent();
    CHECK_FALSE(gate.owed());
    CHECK(gate.frameShown(40).kind == Step::None);
    CHECK_FALSE(gate.awaiting());

    // Revenir sur la position posée mais pas encore montrée : elle est redemandée.
    REQUIRE(gate.request(10, false));
    gate.sent(10);
    CHECK_FALSE(gate.request(40, false));
    CHECK(gate.flush() == 40);
    CHECK_FALSE(gate.request(40, true));
    CHECK(gate.pending() == 40);
}

TEST_CASE("Glisser : la lecture ou un changement de producteur oublie tout", "[KoraCut]")
{
    KoraCutSeekGate gate;
    REQUIRE(gate.request(10, false));
    gate.sent(10);
    CHECK_FALSE(gate.request(40, false));
    gate.reset();
    CHECK_FALSE(gate.awaiting());
    CHECK(gate.pending() == -1);
    CHECK(gate.held() == 0);
    CHECK(gate.frameShown(10).kind == Step::None);
    CHECK(gate.request(11, false));
}
