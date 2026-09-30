/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "catch.hpp"
#include "test_utils.hpp"

#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "timeline2/model/timelinefunctions.hpp"

TEST_CASE("Lâcher un média sous la dernière piste crée une piste", "[KoraCutNewTrack]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    const int tracksBefore = timeline->getTracksCount();
    const int audioTracks = timeline->getTracksIds(true).count();
    REQUIRE(audioTracks > 0);

    // Un clip vidéo : la piste neuve est vidéo, en bas des pistes vidéo, juste au-dessus de l'audio.
    const QString binId = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel);
    int newTrack = -1;
    const int cid = TimelineFunctions::requestClipOnNewTrack(timeline, binId, 50, newTrack);
    REQUIRE(cid > -1);
    REQUIRE(newTrack > -1);
    CHECK(timeline->getTracksCount() == tracksBefore + 1);
    CHECK_FALSE(timeline->isAudioTrack(newTrack));
    CHECK(timeline->getTrackPosition(newTrack) == audioTracks);
    CHECK(timeline->getClipTrackId(cid) == newTrack);
    CHECK(timeline->getClipPosition(cid) == 50);

    // Une seule annulation retire le clip et la piste, sans piste vide laissée derrière.
    undoStack->undo();
    CHECK(timeline->getTracksCount() == tracksBefore);
    CHECK_FALSE(timeline->isClip(cid));
    undoStack->redo();
    CHECK(timeline->getTracksCount() == tracksBefore + 1);

    // Un identifiant inconnu : rien ne change.
    int none = -1;
    CHECK(TimelineFunctions::requestClipOnNewTrack(timeline, QStringLiteral("9999"), 0, none) == -1);
    CHECK(none == -1);
    CHECK(timeline->getTracksCount() == tracksBefore + 1);
    pCore->projectManager()->closeCurrentDocument(false, false);
}
