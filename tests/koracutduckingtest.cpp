/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "catch.hpp"
#include "test_utils.hpp"

#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"

#include "koracut/koracutducking.h"

using namespace KoraCutDucking;

namespace {
// 25 i/s. Réglages par défaut : anticipation 150 ms = 4 images, maintien 400 ms = 10,
// fondu 250 ms = 6, fusion 600 ms = 15, voix minimale 120 ms = 3.
constexpr double Fps = 25.;

QVector<double> silence(int frames)
{
    return QVector<double>(frames, -144.);
}

void speak(QVector<double> &levels, int from, int to, double db = -12.)
{
    for (int i = from; i < to; ++i) {
        levels[i] = db;
    }
}
}

TEST_CASE("Baisse automatique : détection de la voix", "[KoraCutDucking]")
{
    const Settings settings;

    SECTION("Rien à baisser dans le silence ou sous le seuil")
    {
        QVector<double> levels = silence(300);
        CHECK(duckedIntervals(levels, settings, Fps).isEmpty());
        speak(levels, 50, 100, -55.);
        CHECK(duckedIntervals(levels, settings, Fps).isEmpty());
    }

    SECTION("Une phrase : anticipation avant, maintien après")
    {
        QVector<double> levels = silence(300);
        speak(levels, 50, 100);
        const auto intervals = duckedIntervals(levels, settings, Fps);
        REQUIRE(intervals.size() == 1);
        CHECK(intervals.first() == Interval(46, 110));
    }

    SECTION("Une respiration ne fait pas remonter la musique")
    {
        QVector<double> levels = silence(300);
        speak(levels, 50, 100);
        speak(levels, 110, 150); // 10 images de silence, moins que les 15 de la fusion
        const auto intervals = duckedIntervals(levels, settings, Fps);
        REQUIRE(intervals.size() == 1);
        CHECK(intervals.first() == Interval(46, 160));
    }

    SECTION("Deux phrases éloignées : deux baisses")
    {
        QVector<double> levels = silence(400);
        speak(levels, 50, 100);
        speak(levels, 200, 250);
        const auto intervals = duckedIntervals(levels, settings, Fps);
        REQUIRE(intervals.size() == 2);
        CHECK(intervals.at(1) == Interval(196, 260));
    }

    SECTION("Un clic trop court est ignoré")
    {
        QVector<double> levels = silence(300);
        speak(levels, 50, 52);
        CHECK(duckedIntervals(levels, settings, Fps).isEmpty());
    }

    SECTION("La baisse ne part jamais avant le début de la timeline")
    {
        QVector<double> levels = silence(100);
        speak(levels, 0, 40);
        const auto intervals = duckedIntervals(levels, settings, Fps);
        REQUIRE(intervals.size() == 1);
        CHECK(intervals.first().first == 0);
    }
}

TEST_CASE("Baisse automatique : images clés d'un clip de musique", "[KoraCutDucking]")
{
    Settings settings;
    const QList<Interval> ducked = {Interval(46, 110)};

    SECTION("Clip qui couvre toute la phrase")
    {
        const auto keys = clipKeyframes(ducked, 0, 200, settings, Fps);
        const QList<QPair<int, double>> expected = {{0, 0.}, {40, 0.}, {46, -12.}, {110, -12.}, {116, 0.}, {199, 0.}};
        CHECK(keys == expected);
    }

    SECTION("Clip qui commence pendant la voix : il part déjà bas")
    {
        const auto keys = clipKeyframes(ducked, 80, 100, settings, Fps);
        const QList<QPair<int, double>> expected = {{0, -12.}, {30, -12.}, {36, 0.}, {99, 0.}};
        CHECK(keys == expected);
    }

    SECTION("Clip loin de la voix : aucune image clé")
    {
        CHECK(clipKeyframes(ducked, 300, 100, settings, Fps).isEmpty());
    }

    SECTION("La baisse suit le réglage, en dB négatifs")
    {
        settings.duckDb = 18.;
        const auto keys = clipKeyframes(ducked, 0, 200, settings, Fps);
        CHECK(keys.at(2).second == -18.);
    }

    SECTION("Chaîne MLT décalée du point d'entrée du clip, séparateur décimal fixe")
    {
        const QList<QPair<int, double>> keys = {{0, 0.}, {6, -12.5}};
        CHECK(animationString(keys, 10) == QStringLiteral("10=0.00;16=-12.50"));
    }
}

TEST_CASE("Baisse automatique : effet Volume posé sur un clip de musique", "[KoraCutDucking]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    // Même mise en place que mixtest : un clip audio et vidéo posé sur une piste vidéo,
    // sa partie audio atterrit sur la piste audio ciblée. C'est elle, la « musique ».
    const int videoTrack = timeline->getTrackIndexFromPosition(2);
    const QString binId = KdenliveTests::createProducerWithSound(pCore->getProjectProfile(), binModel, 100);
    QMap<int, QString> audioInfo;
    audioInfo.insert(1, QStringLiteral("stream1"));
    KdenliveTests::setAudioTargets(timeline, audioInfo);
    int avClip;
    REQUIRE(timeline->requestClipInsertion(binId, videoTrack, 0, avClip));
    const int cid = timeline->getClipSplitPartner(avClip);
    REQUIRE(cid > -1);
    auto stack = timeline->getClipEffectStackModel(cid);
    REQUIRE(stack->rowCount() == 0);

    const int in = timeline->getClipIn(cid);
    const QList<QPair<int, double>> keys = {{0, 0.}, {10, -12.}, {20, -12.}, {30, 0.}};
    QMap<QString, QString> params;
    params.insert(QStringLiteral("level"), animationString(keys, in));
    params.insert(QStringLiteral("kdenlive:koracut_ducking"), QStringLiteral("1"));
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    REQUIRE(stack->appendEffectWithParams(QStringLiteral("volume"), params, undo, redo));
    pCore->pushUndo(undo, redo, QStringLiteral("Baisse automatique de la musique"));
    REQUIRE(stack->rowCount() == 1);

    auto effect = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(0));
    REQUIRE(effect);
    CHECK(effect->filter().get_int("kdenlive:koracut_ducking") == 1);
    // Le gain lu par MLT à l'image clé et entre deux images clés
    CHECK(effect->filter().anim_get_double("level", in + 15) == Approx(-12.));
    CHECK(effect->filter().anim_get_double("level", in + 5) == Approx(-6.));

    // Une seule étape d'annulation pour tout
    undoStack->undo();
    CHECK(stack->rowCount() == 0);
    undoStack->redo();
    CHECK(stack->rowCount() == 1);
    pCore->projectManager()->closeCurrentDocument(false, false);
}
