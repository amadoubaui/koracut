/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Document 13 (24 septembre 2026) : la couleur des pistes et des clips. Poser, lire dans l'état,
// annuler, couper, enregistrer, rouvrir.

#include "catch.hpp"
#include "test_utils.hpp"

#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "koracut/koracutagenttools.h"
#include "koracut/koracuttimelinecolors.h"
#include "timeline2/model/timelinefunctions.hpp"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUndoGroup>

using KoraCutAgentTools::call;

namespace {
QString errorCode(const QJsonObject &r)
{
    return r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString();
}

QString tagOf(const std::shared_ptr<TimelineItemModel> &timeline, int cid)
{
    return timeline->data(timeline->makeClipIndexFromID(cid), TimelineModel::TagRole).toString();
}

QJsonObject trackInState(const QJsonObject &state, int tid)
{
    for (const QJsonValue &p : state.value(QStringLiteral("pistes")).toArray()) {
        if (p.toObject().value(QStringLiteral("id")).toInt() == tid) {
            return p.toObject();
        }
    }
    return {};
}

QJsonObject clipInState(const QJsonObject &state, int cid)
{
    for (const QJsonValue &p : state.value(QStringLiteral("pistes")).toArray()) {
        for (const QJsonValue &c : p.toObject().value(QStringLiteral("clips")).toArray()) {
            if (c.toObject().value(QStringLiteral("id")).toInt() == cid) {
                return c.toObject();
            }
        }
    }
    return {};
}
} // namespace

TEST_CASE("Couleur des pistes et des clips", "[KoraCutColors]")
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
    int cid1 = -1;
    int cid2 = -1;
    REQUIRE(timeline->requestClipInsertion(binId, tid, 0, cid1, true, true, false));
    REQUIRE(timeline->requestClipInsertion(binId, tid, 40, cid2, true, true, false));
    REQUIRE(tagOf(timeline, cid1).isEmpty());

    // La palette : douze couleurs, les quatre de la charte d'abord.
    REQUIRE(KoraCutTimelineColors::palette().size() == 12);
    bool ok = false;
    REQUIRE(KoraCutTimelineColors::normalize(QStringLiteral("musique"), &ok).isEmpty());
    REQUIRE_FALSE(ok);
    REQUIRE(KoraCutTimelineColors::normalize(QStringLiteral("#e07b39"), &ok) == QStringLiteral("#E07B39"));
    REQUIRE(KoraCutTimelineColors::nameOf(QStringLiteral("#E07B39")) == QStringLiteral("orange"));

    const int start = undoStack->index();

    // La piste : filet dans la tête (rôle), et tous ses clips la prennent.
    QJsonObject r = call(timeline, QStringLiteral("couleur_piste"), {{QStringLiteral("piste_id"), tid}, {QStringLiteral("couleur"), QStringLiteral("orange")}});
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    REQUIRE(r.value(QStringLiteral("couleur")).toString() == QStringLiteral("orange"));
    REQUIRE(timeline->getTrackProperty(tid, QStringLiteral("kdenlive:koracut_color")).toString() == QStringLiteral("#E07B39"));
    REQUIRE(timeline->data(timeline->makeTrackIndexFromID(tid), TimelineModel::KoraCutColorRole).toString() == QStringLiteral("#E07B39"));
    REQUIRE(tagOf(timeline, cid1) == QStringLiteral("#E07B39"));
    REQUIRE(undoStack->index() == start + 1);

    // Le clip : sa couleur passe avant celle de la piste, sur toute la sélection en une étape.
    r = call(timeline, QStringLiteral("couleur_clip"), {{QStringLiteral("clip_ids"), QJsonArray{cid1, cid2}}, {QStringLiteral("couleur"), QStringLiteral("violet")}});
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    REQUIRE(TimelineFunctions::koraCutClipColor(timeline, cid1) == QStringLiteral("#7C5CFF"));
    REQUIRE(tagOf(timeline, cid2) == QStringLiteral("#7C5CFF"));
    REQUIRE(undoStack->index() == start + 2);

    // L'état lu par les agents et la porte.
    QJsonObject state = call(timeline, QStringLiteral("etat_timeline"), {});
    REQUIRE(trackInState(state, tid).value(QStringLiteral("coul")).toString() == QStringLiteral("orange"));
    REQUIRE(clipInState(state, cid1).value(QStringLiteral("coul")).toString() == QStringLiteral("violet"));

    // Les refus.
    REQUIRE(errorCode(call(timeline, QStringLiteral("couleur_clip"), {{QStringLiteral("clip_ids"), QJsonArray{cid1}}, {QStringLiteral("couleur"), QStringLiteral("fuchsia")}})) ==
            QStringLiteral("entree_invalide"));
    REQUIRE(errorCode(call(timeline, QStringLiteral("couleur_clip"), {{QStringLiteral("clip_ids"), QJsonArray{9999}}, {QStringLiteral("couleur"), QStringLiteral("rouge")}})) ==
            QStringLiteral("id_inconnu"));
    REQUIRE(errorCode(call(timeline, QStringLiteral("couleur_piste"), {{QStringLiteral("piste_id"), 9999}, {QStringLiteral("couleur"), QStringLiteral("rouge")}})) ==
            QStringLiteral("id_inconnu"));
    REQUIRE(undoStack->index() == start + 2);

    // Annuler, refaire.
    undoStack->undo();
    REQUIRE(TimelineFunctions::koraCutClipColor(timeline, cid1).isEmpty());
    REQUIRE(tagOf(timeline, cid1) == QStringLiteral("#E07B39"));
    undoStack->undo();
    REQUIRE(tagOf(timeline, cid1).isEmpty());
    REQUIRE(timeline->getTrackProperty(tid, QStringLiteral("kdenlive:koracut_color")).toString().isEmpty());
    undoStack->redo();
    undoStack->redo();
    REQUIRE(tagOf(timeline, cid2) == QStringLiteral("#7C5CFF"));

    // « defaut » rend au clip la couleur de sa piste.
    r = call(timeline, QStringLiteral("couleur_clip"), {{QStringLiteral("clip_ids"), QJsonArray{cid2}}, {QStringLiteral("couleur"), QStringLiteral("defaut")}});
    REQUIRE(r.value(QStringLiteral("couleur")).toString() == QStringLiteral("defaut"));
    REQUIRE(tagOf(timeline, cid2) == QStringLiteral("#E07B39"));

    // Une coupe garde la couleur des deux côtés.
    REQUIRE(TimelineFunctions::requestClipCut(timeline, cid1, 20));
    const int right = timeline->getClipByPosition(tid, 25);
    REQUIRE(right != cid1);
    REQUIRE(TimelineFunctions::koraCutClipColor(timeline, right) == QStringLiteral("#7C5CFF"));

    // Enregistrer, rouvrir.
    QDir dir = QDir::temp();
    const QString saveFile = dir.absoluteFilePath(QStringLiteral("koracut-couleurs.kdenlive"));
    pCore->projectManager()->testSaveFileAs(saveFile);
    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);

    QUndoGroup *undoGroup = new QUndoGroup(&document);
    undoGroup->addStack(undoStack.get());
    DocOpenResult openResults = KdenliveDoc::Open(QUrl::fromLocalFile(saveFile), QDir::temp().path(), undoGroup, false, nullptr);
    REQUIRE(openResults.isSuccessful());
    std::unique_ptr<KdenliveDoc> openedDoc = openResults.getDocument();
    pCore->projectManager()->testSetDocument(openedDoc.get());
    const QUuid uuid = openedDoc->uuid();
    KdenliveTests::updateTimeline(false, QString(), QString(), QFileInfo(saveFile).lastModified(), 0);
    const QString seqId = binModel->getAllSequenceClips().value(uuid);
    pCore->projectManager()->openTimeline(seqId, -1, uuid);
    timeline = openedDoc->getTimeline(uuid);
    pCore->projectManager()->testSetActiveTimeline(timeline);
    const int reopenedTrack = timeline->getTrackIndexFromPosition(2);
    REQUIRE(timeline->getTrackProperty(reopenedTrack, QStringLiteral("kdenlive:koracut_color")).toString() == QStringLiteral("#E07B39"));
    const int first = timeline->getClipByPosition(reopenedTrack, 5);
    const int second = timeline->getClipByPosition(reopenedTrack, 45);
    REQUIRE(first > -1);
    REQUIRE(second > -1);
    REQUIRE(TimelineFunctions::koraCutClipColor(timeline, first) == QStringLiteral("#7C5CFF"));
    REQUIRE(TimelineFunctions::koraCutClipColor(timeline, second).isEmpty());
    REQUIRE(tagOf(timeline, second) == QStringLiteral("#E07B39"));
    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
    QFile::remove(saveFile);
}
