/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "catch.hpp"
#include "test_utils.hpp"

#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "bin/model/markerlistmodel.hpp"
#include "timeline2/model/timelinefunctions.hpp"

#include "koracut/koracutautocut.h"
#include "koracut/koracuttimelineaudio.h"
#include "koracut/koracutsync.h"

#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "definitions.h"
#include <cmath>

#include <QFile>
#include <QtEndian>

using namespace KoraCutAutoCut;

namespace {
constexpr double Fps = 25.;

/** Lit un WAV PCM 16 bits mono et rend, par image de 1/25 s, la crête en dBFS : exactement ce que
 *  la timeline tire des formes d'onde (ProjectClip::audioFrameCache garde des crêtes). */
QVector<double> levelsFromWav(const QString &path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    REQUIRE(data.startsWith("RIFF"));
    // On parcourt les morceaux jusqu'à « data », sans supposer un en-tête de 44 octets.
    int pos = 12;
    int rate = 0;
    int sampleStart = -1;
    int sampleBytes = 0;
    while (pos + 8 <= data.size()) {
        const QByteArray id = data.mid(pos, 4);
        const int size = int(qFromLittleEndian<quint32>(data.constData() + pos + 4));
        if (id == "fmt ") {
            rate = int(qFromLittleEndian<quint32>(data.constData() + pos + 12));
        } else if (id == "data") {
            sampleStart = pos + 8;
            sampleBytes = size;
            break;
        }
        pos += 8 + size + (size % 2);
    }
    REQUIRE(rate > 0);
    REQUIRE(sampleStart > 0);
    const int samples = sampleBytes / 2;
    const int perFrame = int(rate / Fps);
    QVector<double> levels;
    for (int f = 0; (f + 1) * perFrame <= samples; ++f) {
        int peak = 0;
        for (int i = 0; i < perFrame; ++i) {
            const qint16 v = qFromLittleEndian<qint16>(data.constData() + sampleStart + 2 * (f * perFrame + i));
            peak = qMax(peak, qAbs(int(v)));
        }
        levels << KoraCutDucking::peakToDb(peak);
    }
    return levels;
}

int frames(double seconds)
{
    return int(std::lround(seconds * Fps));
}
}

TEST_CASE("Coupe automatique : silences d'une vraie voix", "[KoraCutAutoCut]")
{
    // Voix de synthèse du jeu d'essai (Recherche/AutoCut - jeu d'essai). Référence mesurée par
    // ffmpeg silencedetect à -40 dB et 0,5 s : 3,07-5,09 ; 8,09-9,18 ; 12,57-14,99 ; 20,32-22,01.
    const QVector<double> levels = levelsFromWav(sourcesPath + QStringLiteral("/dataset/koracut-voix-silences.wav"));
    REQUIRE(levels.size() > frames(24.));
    const QList<QPair<double, double>> reference = {{3.073, 5.089}, {8.089, 9.180}, {12.575, 14.987}, {20.323, 22.006}};

    SECTION("Rythme normal, seuil automatique : les quatre silences, rognés de leurs marges")
    {
        const SilenceSettings settings = SilenceSettings::forRythme(Rythme::Normal);
        double threshold = 0.;
        const QList<Interval> silences = silenceIntervals(levels, settings, Fps, &threshold);
        INFO("seuil " << threshold);
        REQUIRE(silences.size() == 4);
        for (int i = 0; i < 4; ++i) {
            INFO("silence " << i << " : " << silences.at(i).first << " à " << silences.at(i).second);
            // Marges : 150 ms gardées après la parole (4 images), 100 ms avant (3 images).
            // Tolérance de 3 images : le seuil automatique n'est pas celui de ffmpeg.
            CHECK(qAbs(silences.at(i).first - (frames(reference.at(i).first) + 4)) <= 3);
            CHECK(qAbs(silences.at(i).second - (frames(reference.at(i).second) - 3)) <= 3);
        }
    }

    SECTION("Rythme posé : les mêmes silences, plus courts")
    {
        const QList<Interval> normal = silenceIntervals(levels, SilenceSettings::forRythme(Rythme::Normal), Fps);
        const QList<Interval> pose = silenceIntervals(levels, SilenceSettings::forRythme(Rythme::Pose), Fps);
        REQUIRE(pose.size() == 4);
        CHECK(totalFrames(pose) < totalFrames(normal));
    }

    SECTION("Silence minimum plus long que tous les silences : rien à couper")
    {
        SilenceSettings settings;
        settings.minSilenceMs = 3000;
        CHECK(silenceIntervals(levels, settings, Fps).isEmpty());
    }

    SECTION("Seuil manuel très haut : la voix elle-même passe pour du silence, tout devient une seule zone")
    {
        SilenceSettings settings;
        settings.autoThreshold = false;
        settings.thresholdDb = 0.;
        const QList<Interval> silences = silenceIntervals(levels, settings, Fps);
        REQUIRE(silences.size() == 1);
        CHECK(silences.first() == Interval(0, int(levels.size())));
    }
}

TEST_CASE("Coupe automatique : cas limites", "[KoraCutAutoCut]")
{
    SECTION("Silence en début et en fin : marge seulement du côté de la parole")
    {
        QVector<double> levels(100, -144.);
        for (int i = 30; i < 70; ++i) {
            levels[i] = -12.;
        }
        SilenceSettings settings;
        settings.autoThreshold = false;
        settings.thresholdDb = -40.;
        const QList<Interval> silences = silenceIntervals(levels, settings, Fps);
        REQUIRE(silences.size() == 2);
        CHECK(silences.at(0) == Interval(0, 30 - 3));
        CHECK(silences.at(1) == Interval(70 + 4, 100));
    }

    SECTION("Un clic au milieu d'un silence ne le coupe pas en deux")
    {
        QVector<double> levels(100, -144.);
        for (int i = 0; i < 20; ++i) {
            levels[i] = -12.;
        }
        levels[50] = -10.; // une image : 40 ms, sous les 200 ms de parole minimum
        for (int i = 80; i < 100; ++i) {
            levels[i] = -12.;
        }
        SilenceSettings settings;
        settings.autoThreshold = false;
        const QList<Interval> silences = silenceIntervals(levels, settings, Fps);
        REQUIRE(silences.size() == 1);
        CHECK(silences.first() == Interval(20 + 4, 80 - 3));
    }

    SECTION("Seuil automatique borné")
    {
        CHECK(estimateThreshold(QVector<double>(50, -144.)) == -40.);
        QVector<double> bruit(100, -35.);
        for (int i = 0; i < 50; ++i) {
            bruit[i] = -8.;
        }
        const double t = estimateThreshold(bruit);
        CHECK(t > -35.);
        CHECK(t <= -25.);
    }
}

TEST_CASE("Coupe automatique : retirer des zones sur toute la timeline", "[KoraCutAutoCut]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    // Un clip audio et vidéo de 100 images, comme un plan de podcast avec son micro.
    const int videoTrack = timeline->getTrackIndexFromPosition(2);
    const QString binId = KdenliveTests::createProducerWithSound(pCore->getProjectProfile(), binModel, 100);
    QMap<int, QString> audioInfo;
    audioInfo.insert(1, QStringLiteral("stream1"));
    KdenliveTests::setAudioTargets(timeline, audioInfo);
    int videoClip;
    REQUIRE(timeline->requestClipInsertion(binId, videoTrack, 0, videoClip));
    const int audioClip = timeline->getClipSplitPartner(videoClip);
    REQUIRE(audioClip > -1);
    const int audioTrack = timeline->getClipTrackId(audioClip);

    auto trackLength = [&](int trackId) {
        int total = 0;
        for (int id : timeline->getItemsInRange(trackId, 0, -1, false)) {
            if (timeline->isClip(id)) {
                total += timeline->getClipPlaytime(id);
            }
        }
        return total;
    };
    auto trackEnd = [&](int trackId) {
        int end = 0;
        for (int id : timeline->getItemsInRange(trackId, 0, -1, false)) {
            if (timeline->isClip(id)) {
                end = qMax(end, timeline->getClipPosition(id) + timeline->getClipPlaytime(id));
            }
        }
        return end;
    };
    const QList<QPair<int, int>> zones = {{10, 20}, {50, 60}};

    SECTION("Couper et refermer : 80 images, vidéo et son toujours calés, une seule annulation")
    {
        const double fps = pCore->getCurrentFps();
        auto guides = timeline->getGuideModel();
        REQUIRE(guides->addMarker(GenTime(70, fps), QStringLiteral("après les zones")));
        REQUIRE(guides->addMarker(GenTime(15, fps), QStringLiteral("dans une zone")));
        Fun undo = []() { return true; };
        Fun redo = []() { return true; };
        REQUIRE(TimelineFunctions::removeZonesOnAllTracks(timeline, zones, false, true, undo, redo));
        pCore->pushUndo(undo, redo, QStringLiteral("Coupe automatique"));
        CHECK(trackLength(videoTrack) == 80);
        CHECK(trackLength(audioTrack) == 80);
        CHECK(trackEnd(videoTrack) == 80);
        CHECK(trackEnd(audioTrack) == 80);
        // Trois morceaux par piste : 0-10, 20-50, 60-100 de la source.
        CHECK(timeline->getItemsInRange(videoTrack, 0, -1, false).size() == 3);
        // Le guide d'après recule de 20 images, celui de la zone disparaît.
        const QList<CommentedTime> apres = guides->getAllMarkers();
        REQUIRE(apres.size() == 1);
        CHECK(apres.first().time().frames(fps) == 50);
        undoStack->undo();
        CHECK(guides->getAllMarkers().size() == 2);
        CHECK(trackLength(videoTrack) == 100);
        CHECK(trackLength(audioTrack) == 100);
        CHECK(timeline->getItemsInRange(videoTrack, 0, -1, false).size() == 1);
        undoStack->redo();
        CHECK(trackEnd(videoTrack) == 80);
    }

    SECTION("Laisser les trous : la durée ne change pas")
    {
        Fun undo = []() { return true; };
        Fun redo = []() { return true; };
        REQUIRE(TimelineFunctions::removeZonesOnAllTracks(timeline, zones, true, true, undo, redo));
        CHECK(trackLength(videoTrack) == 80);
        CHECK(trackEnd(videoTrack) == 100);
        CHECK(trackEnd(audioTrack) == 100);
    }
    pCore->projectManager()->closeCurrentDocument(false, false);
}

TEST_CASE("Coupe automatique : de la timeline à la coupe, avec la vraie voix", "[KoraCutAutoCut]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);
    const double fps = pCore->getCurrentFps();

    // Le vrai fichier de la voix, dans le chutier.
    const QString path = sourcesPath + QStringLiteral("/dataset/koracut-voix-silences.wav");
    const QString binId = KdenliveTests::createFileProducer(pCore->getProjectProfile(), binModel, path);
    auto binClip = binModel->getClipByBinID(binId);
    REQUIRE(binClip);
    const QMap<int, QString> streams = binClip->audioStreams();
    REQUIRE(!streams.isEmpty());

    // Les niveaux, posés comme AudioLevelsTask les pose : 5 crêtes par image, un canal. On reprend
    // la crête de chaque image calculée depuis le fichier, pour ne pas dépendre de la tâche de fond.
    const QVector<double> frameDb = levelsFromWav(path);
    QVector<int16_t> cache;
    for (double db : frameDb) {
        const int16_t peak = int16_t(qBound(0., std::pow(10., db / 20.) * 32768., 32767.));
        for (int k = 0; k < AUDIOLEVELS_POINTS_PER_FRAME; ++k) {
            cache << peak;
        }
    }
    const int stream = streams.firstKey();
    binClip->originalProducer()->set(QStringLiteral("_kdenlive:audio%1").arg(stream).toUtf8().constData(), new QVector<int16_t>(cache), 0,
                                     [](void *ptr) { delete static_cast<QVector<int16_t> *>(ptr); });

    // Sur une piste audio, à 2 s du début : les positions de la timeline ne sont pas celles du fichier.
    int audioTrack;
    REQUIRE(timeline->requestTrackInsertion(-1, audioTrack, QString(), true));
    int cid;
    REQUIRE(timeline->requestClipInsertion(binId, audioTrack, 50, cid));
    const int length = timeline->getClipPlaytime(cid);
    REQUIRE(length > frames(24.));

    int missing = -1;
    const QVector<double> levels = KoraCutTimelineAudio::voiceLevels(timeline, {audioTrack}, &missing);
    CHECK(missing == 0);
    const QList<Interval> silences = silenceIntervals(levels, SilenceSettings::forRythme(Rythme::Normal), fps);
    // Les deux premières secondes vides de la timeline, puis les quatre silences de la voix, décalés de 50 images.
    REQUIRE(silences.size() == 5);
    CHECK(silences.first().first == 0);
    CHECK(qAbs(silences.at(1).first - (50 + frames(3.073) + 4)) <= 3);
    CHECK(qAbs(silences.at(4).second - (50 + frames(22.006) - 3)) <= 3);

    // Et la coupe : la voix raccourcit d'autant, en une seule annulation.
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    REQUIRE(TimelineFunctions::removeZonesOnAllTracks(timeline, silences, false, true, undo, redo));
    pCore->pushUndo(undo, redo, QStringLiteral("Coupe automatique des silences"));
    int total = 0;
    for (int id : KoraCutTimelineAudio::clipsOnTrack(timeline, audioTrack)) {
        total += timeline->getClipPlaytime(id);
    }
    // La première zone couvre le vide avant le clip et s'arrête 3 images avant la parole : seule la
    // partie qui mord sur le clip lui est retirée.
    CHECK(total == length - (totalFrames(silences) - silences.first().second));
    undoStack->undo();
    CHECK(KoraCutTimelineAudio::clipsOnTrack(timeline, audioTrack).size() == 1);
    CHECK(timeline->getClipPlaytime(cid) == length);
    // Comme les autres tests : on lâche nos pointeurs avant de fermer le document, sinon le clip est
    // détruit après lui (plantage dans ~ProjectClip).
    binClip.reset();
    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}

TEST_CASE("Coupe automatique : mots parasites", "[KoraCutAutoCut]")
{
    SECTION("La vraie transcription du jeu d'essai : les deux « euh » écrits « eux », rien d'autre")
    {
        // Sortie réelle de koracut_mots.py (Whisper base, français) sur la voix du jeu d'essai.
        QFile file(sourcesPath + QStringLiteral("/dataset/koracut-voix-mots.json"));
        REQUIRE(file.open(QIODevice::ReadOnly));
        const QList<Word> words = parseWords(file.readAll());
        REQUIRE(words.size() == 54);
        const QList<Filler> fillers = findFillers(words, {}, -40., FillerSettings(), Fps);
        REQUIRE(fillers.size() == 2);
        for (const Filler &f : fillers) {
            CHECK(f.kind == FillerKind::Isolated);
            CHECK(f.text == QStringLiteral("eux"));
        }
        // 5,98-6,14 s et 10,86-11,00 s, avec 40 ms d'air.
        CHECK(fillers.at(0).frames == Interval(frames(5.94), frames(6.18)));
        CHECK(fillers.at(1).frames == Interval(frames(10.82), frames(11.04)));
    }

    SECTION("Mots sûrs, suspect pas isolé, expressions ambiguës")
    {
        const QList<Word> words = {{QStringLiteral("Euh,"), 0.0, 0.3},   {QStringLiteral("je"), 0.5, 0.6},       {QStringLiteral("pense"), 0.6, 0.9},
                                   {QStringLiteral("à"), 0.9, 1.0},      {QStringLiteral("eux"), 1.0, 1.2},      {QStringLiteral("du"), 2.0, 2.1},
                                   {QStringLiteral("coup,"), 2.1, 2.4}, {QStringLiteral("hum"), 3.0, 3.3}};
        const QList<Filler> fillers = findFillers(words, {}, -40., FillerSettings(), Fps);
        REQUIRE(fillers.size() == 3);
        CHECK(fillers.at(0).kind == FillerKind::Sure);
        CHECK(fillers.at(0).text == QStringLiteral("euh"));
        // « eux » collé à « à » : un vrai mot, pas marqué.
        CHECK(fillers.at(1).kind == FillerKind::Ambiguous);
        CHECK(fillers.at(1).text == QStringLiteral("du coup"));
        CHECK(fillers.at(2).kind == FillerKind::Sure);
        // La marge ne mord jamais sur le mot d'avant : « Euh » s'arrête avant « je ».
        CHECK(fillers.at(0).frames.second <= frames(0.5));
    }

    SECTION("Voix sans mot : une voix que la transcription n'a pas écrite")
    {
        QVector<double> levels(100, -144.);
        for (int i = 10; i < 30; ++i) {
            levels[i] = -12.; // un mot transcrit
        }
        for (int i = 50; i < 58; ++i) {
            levels[i] = -15.; // 320 ms de voix sans aucun mot : un « euh » omis
        }
        for (int i = 70; i < 72; ++i) {
            levels[i] = -15.; // 80 ms : trop court, un souffle
        }
        const QList<Word> words = {{QStringLiteral("Bonjour"), 10 / Fps, 30 / Fps}};
        const QList<Filler> fillers = findFillers(words, levels, -40., FillerSettings(), Fps);
        REQUIRE(fillers.size() == 1);
        CHECK(fillers.first().kind == FillerKind::VoiceWithoutWord);
        CHECK(fillers.first().frames == Interval(50, 58));
    }

    SECTION("Fusion d'intervalles")
    {
        CHECK(mergeIntervals({{10, 20}, {5, 12}, {30, 40}, {40, 45}}) == QList<Interval>({{5, 20}, {30, 45}}));
    }
}

TEST_CASE("Coupe automatique : répétitions", "[KoraCutAutoCut]")
{
    SECTION("La vraie transcription : « il faut couper ce qui ne sert pas » dite deux fois")
    {
        QFile file(sourcesPath + QStringLiteral("/dataset/koracut-voix-mots.json"));
        REQUIRE(file.open(QIODevice::ReadOnly));
        const QList<KoraCutAutoCut::Repeat> repeats = findRepeats(parseWords(file.readAll()), RepeatSettings(), Fps);
        REQUIRE(repeats.size() == 1);
        // La première prise (14,86 s) jusqu'au début de la seconde (16,92 s) : la dernière est gardée.
        CHECK(repeats.first().frames == Interval(frames(14.86), frames(16.92)));
        CHECK(repeats.first().words == 8);
        CHECK(repeats.first().text.startsWith(QStringLiteral("il faut couper")));
    }

    SECTION("Un faux départ, puis la phrase entière : le faux départ part")
    {
        const QList<Word> words = {{QStringLiteral("Je"), 0.0, 0.2},  {QStringLiteral("voulais"), 0.2, 0.5}, {QStringLiteral("dire"), 0.5, 0.8},
                                   {QStringLiteral("je"), 1.5, 1.7},  {QStringLiteral("voulais"), 1.7, 2.0}, {QStringLiteral("dire"), 2.0, 2.3},
                                   {QStringLiteral("merci."), 2.3, 2.8}};
        const QList<KoraCutAutoCut::Repeat> repeats = findRepeats(words, RepeatSettings(), Fps);
        REQUIRE(repeats.size() == 1);
        CHECK(repeats.first().frames == Interval(0, frames(1.5)));
    }

    SECTION("Deux mots répétés, ou une reprise trop lointaine : rien")
    {
        const QList<Word> courts = {{QStringLiteral("c'est,"), 0.0, 0.2}, {QStringLiteral("c'est"), 0.5, 0.7}, {QStringLiteral("l'art"), 0.7, 1.0}};
        CHECK(findRepeats(courts, RepeatSettings(), Fps).isEmpty());
        const QList<Word> loin = {{QStringLiteral("il"), 0.0, 0.1},   {QStringLiteral("faut"), 0.1, 0.3},  {QStringLiteral("couper"), 0.3, 0.6},
                                  {QStringLiteral("il"), 30.0, 30.1}, {QStringLiteral("faut"), 30.1, 30.3}, {QStringLiteral("couper"), 30.3, 30.6}};
        CHECK(findRepeats(loin, RepeatSettings(), Fps).isEmpty());
    }

    SECTION("Tolérance : un mot différent accepté si on le demande")
    {
        const QList<Word> words = {{QStringLiteral("on"), 0.0, 0.2},  {QStringLiteral("va"), 0.2, 0.4},     {QStringLiteral("parler"), 0.4, 0.8},
                                   {QStringLiteral("de"), 0.8, 0.9},  {QStringLiteral("on"), 2.0, 2.2},     {QStringLiteral("va"), 2.2, 2.4},
                                   {QStringLiteral("causer"), 2.4, 2.8}, {QStringLiteral("de"), 2.8, 2.9}};
        CHECK(findRepeats(words, RepeatSettings(), Fps).isEmpty());
        RepeatSettings tolerant;
        tolerant.maxMismatches = 1;
        CHECK(findRepeats(words, tolerant, Fps).size() == 1);
    }
}

namespace {
/** Enveloppe d'un WAV comme la timeline la voit : 5 crêtes par image de 1/25 s. */
QVector<float> envelopeFromWav(const QString &path)
{
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    int pos = 12;
    int rate = 0;
    int start = -1;
    int bytes = 0;
    while (pos + 8 <= data.size()) {
        const QByteArray id = data.mid(pos, 4);
        const int size = int(qFromLittleEndian<quint32>(data.constData() + pos + 4));
        if (id == "fmt ") {
            rate = int(qFromLittleEndian<quint32>(data.constData() + pos + 12));
        } else if (id == "data") {
            start = pos + 8;
            bytes = size;
            break;
        }
        pos += 8 + size + (size % 2);
    }
    REQUIRE(start > 0);
    const int perPoint = int(rate / (Fps * AUDIOLEVELS_POINTS_PER_FRAME));
    QVector<int16_t> levels;
    for (int p = 0; (p + 1) * perPoint <= bytes / 2; ++p) {
        int peak = 0;
        for (int i = 0; i < perPoint; ++i) {
            peak = qMax(peak, qAbs(int(qFromLittleEndian<qint16>(data.constData() + start + 2 * (p * perPoint + i)))));
        }
        levels << int16_t(qMin(peak, 32767));
    }
    return KoraCutSync::envelopeFromLevels(levels, 1);
}
}

TEST_CASE("Coupe automatique : synchroniser les micros et la caméra", "[KoraCutAutoCut]")
{
    const double pointsPerSecond = Fps * AUDIOLEVELS_POINTS_PER_FRAME;
    const QVector<float> reference = envelopeFromWav(sourcesPath + QStringLiteral("/dataset/koracut-voix-silences.wav"));
    const int maxLag = int(10 * pointsPerSecond);

    SECTION("Second micro, décalé de 1,37 s, deux fois moins fort, avec du bruit")
    {
        const KoraCutSync::Result r = KoraCutSync::align(reference, envelopeFromWav(sourcesPath + QStringLiteral("/dataset/koracut-micro-b.wav")), maxLag);
        INFO("décalage " << r.lag << " points, confiance " << r.confidence);
        CHECK(qAbs(r.lag - int(std::lround(-1.37 * pointsPerSecond))) <= 2);
        CHECK(r.confidence > 0.6);
    }

    SECTION("Son témoin de la caméra, décalé de 0,8 s")
    {
        const KoraCutSync::Result r = KoraCutSync::align(reference, envelopeFromWav(sourcesPath + QStringLiteral("/dataset/koracut-camera-son.wav")), maxLag);
        INFO("décalage " << r.lag << " points, confiance " << r.confidence);
        CHECK(qAbs(r.lag - int(std::lround(-0.8 * pointsPerSecond))) <= 2);
        CHECK(r.confidence > 0.6);
    }

    SECTION("Deux sons sans rapport : confiance basse")
    {
        QVector<float> bruit;
        uint32_t seed = 12345;
        for (int i = 0; i < reference.size(); ++i) {
            seed = seed * 1664525u + 1013904223u;
            bruit << float(seed >> 20);
        }
        const KoraCutSync::Result r = KoraCutSync::align(reference, bruit, maxLag);
        INFO("confiance " << r.confidence);
        CHECK(r.confidence < 0.3);
    }
}

namespace {
/** Les crêtes d'un WAV au format du cache des formes d'onde (5 par image, un canal). */
QVector<int16_t> levelsCacheFromWav(const QString &path)
{
    const QVector<float> env = envelopeFromWav(path);
    QVector<int16_t> levels;
    for (float v : env) {
        levels << int16_t(v);
    }
    return levels;
}

void injectLevels(const std::shared_ptr<ProjectClip> &binClip, const QVector<int16_t> &levels)
{
    const int stream = binClip->audioStreams().firstKey();
    binClip->originalProducer()->set(QStringLiteral("_kdenlive:audio%1").arg(stream).toUtf8().constData(), new QVector<int16_t>(levels), 0,
                                     [](void *ptr) { delete static_cast<QVector<int16_t> *>(ptr); });
}
}

TEST_CASE("Coupe automatique : synchroniser dans la timeline", "[KoraCutAutoCut]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    const QString pathA = sourcesPath + QStringLiteral("/dataset/koracut-voix-silences.wav");
    const QString pathB = sourcesPath + QStringLiteral("/dataset/koracut-micro-b.wav");
    const QString binA = KdenliveTests::createFileProducer(pCore->getProjectProfile(), binModel, pathA);
    const QString binB = KdenliveTests::createFileProducer(pCore->getProjectProfile(), binModel, pathB);
    auto clipA = binModel->getClipByBinID(binA);
    auto clipB = binModel->getClipByBinID(binB);
    injectLevels(clipA, levelsCacheFromWav(pathA));
    injectLevels(clipB, levelsCacheFromWav(pathB));
    int trackA;
    int trackB;
    REQUIRE(timeline->requestTrackInsertion(-1, trackA, QString(), true));
    REQUIRE(timeline->requestTrackInsertion(-1, trackB, QString(), true));

    SECTION("Micro B au même endroit que A : il recule de 1,37 s")
    {
        int cidA;
        int cidB;
        REQUIRE(timeline->requestClipInsertion(binA, trackA, 100, cidA));
        REQUIRE(timeline->requestClipInsertion(binB, trackB, 100, cidB));
        Fun undo = []() { return true; };
        Fun redo = []() { return true; };
        bool ok = false;
        const auto outcomes = KoraCutTimelineAudio::syncToReference(timeline, cidA, {cidB}, 0.5, undo, redo, &ok);
        REQUIRE(ok);
        REQUIRE(outcomes.size() == 1);
        CHECK(outcomes.first().moved);
        CHECK(outcomes.first().confidence > 0.6);
        pCore->pushUndo(undo, redo, QStringLiteral("Synchroniser"));
        CHECK(timeline->getClipPosition(cidA) == 100);
        CHECK(qAbs(timeline->getClipPosition(cidB) - (100 - 34)) <= 1);
        undoStack->undo();
        CHECK(timeline->getClipPosition(cidB) == 100);
    }

    SECTION("Les deux à zéro : c'est A qui avance, rien ne passe avant le début")
    {
        int cidA;
        int cidB;
        REQUIRE(timeline->requestClipInsertion(binA, trackA, 0, cidA));
        REQUIRE(timeline->requestClipInsertion(binB, trackB, 0, cidB));
        Fun undo = []() { return true; };
        Fun redo = []() { return true; };
        bool ok = false;
        KoraCutTimelineAudio::syncToReference(timeline, cidA, {cidB}, 0.5, undo, redo, &ok);
        REQUIRE(ok);
        CHECK(timeline->getClipPosition(cidB) == 0);
        CHECK(qAbs(timeline->getClipPosition(cidA) - 34) <= 1);
    }

    SECTION("Confiance exigée trop haute : personne ne bouge")
    {
        int cidA;
        int cidB;
        REQUIRE(timeline->requestClipInsertion(binA, trackA, 100, cidA));
        REQUIRE(timeline->requestClipInsertion(binB, trackB, 100, cidB));
        Fun undo = []() { return true; };
        Fun redo = []() { return true; };
        const auto outcomes = KoraCutTimelineAudio::syncToReference(timeline, cidA, {cidB}, 0.99, undo, redo);
        CHECK_FALSE(outcomes.first().moved);
        CHECK(timeline->getClipPosition(cidB) == 100);
    }
    clipA.reset();
    clipB.reset();
    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}

TEST_CASE("Coupe automatique : fusion des transcriptions de plusieurs micros", "[KoraCutAutoCut]")
{
    SECTION("La même voix sur trois micros : un seul exemplaire, et le « euh » redevient isolé")
    {
        QFile file(sourcesPath + QStringLiteral("/dataset/koracut-voix-mots.json"));
        REQUIRE(file.open(QIODevice::ReadOnly));
        const QList<Word> one = parseWords(file.readAll());
        QList<Word> three = one;
        // Deux autres micros, à quelques centièmes près (après synchronisation).
        for (Word w : one) {
            w.start += 0.02;
            w.end += 0.02;
            three << w;
        }
        for (Word w : one) {
            w.start -= 0.03;
            w.end -= 0.03;
            three << w;
        }
        const QList<Word> merged = mergeTranscriptions(three);
        CHECK(merged.size() == one.size());
        CHECK(findFillers(merged, {}, -40., FillerSettings(), Fps).size() == 2);
        CHECK(findRepeats(merged, RepeatSettings(), Fps).size() == 1);
    }

    SECTION("Deux personnes qui se répondent : rien n'est fusionné")
    {
        const QList<Word> words = {{QStringLiteral("oui"), 0.0, 0.3}, {QStringLiteral("non"), 0.1, 0.4}, {QStringLiteral("oui"), 1.0, 1.3}};
        CHECK(mergeTranscriptions(words).size() == 3);
    }
}

TEST_CASE("Coupe automatique : choisir la caméra selon qui parle", "[KoraCutAutoCut]")
{
    // Dialogue du jeu d'essai : Thomas parle à 0,5 s et à 8,5 s, Amélie à 4,5 s et à 13 s.
    // Chaque micro entend l'autre à -15 dB.
    const QVector<double> thomas = levelsFromWav(sourcesPath + QStringLiteral("/dataset/koracut-dialogue-thomas.wav"));
    const QVector<double> amelie = levelsFromWav(sourcesPath + QStringLiteral("/dataset/koracut-dialogue-amelie.wav"));

    SECTION("Sans plan large : quatre plans, chacun sur celui qui parle, malgré la diaphonie")
    {
        const QList<Shot> shots = cameraPlan({thomas, amelie}, CameraSettings(), Fps);
        REQUIRE(shots.size() == 4);
        CHECK(shots.at(0).speaker == 0);
        CHECK(shots.at(1).speaker == 1);
        CHECK(shots.at(2).speaker == 0);
        CHECK(shots.at(3).speaker == 1);
        CHECK(shots.at(0).frames.first == 0);
        CHECK(qAbs(shots.at(1).frames.first - frames(4.5)) <= 2);
        CHECK(qAbs(shots.at(2).frames.first - frames(8.5)) <= 2);
        CHECK(qAbs(shots.at(3).frames.first - frames(13.0)) <= 2);
        CHECK(shots.last().frames.second == int(thomas.size()));
    }

    SECTION("Plan minimum de 5 s : la réponse de 3,4 s d'Amélie est absorbée")
    {
        CameraSettings settings;
        settings.minShotMs = 5000;
        const QList<Shot> shots = cameraPlan({thomas, amelie}, settings, Fps);
        for (const Shot &s : shots) {
            CHECK(s.frames.second - s.frames.first >= frames(3.4));
        }
        CHECK(shots.size() < 4);
    }

    SECTION("Avec plan large et 3 s maximum : la longue réponse d'Amélie passe par le large")
    {
        CameraSettings settings;
        settings.hasWide = true;
        settings.maxShotMs = 3000;
        settings.wideCutawayMs = 1000;
        settings.minShotMs = 1000;
        const QList<Shot> shots = cameraPlan({thomas, amelie}, settings, Fps);
        bool wideInLastAnswer = false;
        for (const Shot &s : shots) {
            if (s.speaker == -1 && s.frames.first > frames(13.0)) {
                wideInLastAnswer = true;
            }
        }
        CHECK(wideInLastAnswer);
    }

    SECTION("Deux voix aussi fortes en même temps : plan large")
    {
        QVector<double> a(100, -144.);
        QVector<double> b(100, -144.);
        for (int i = 0; i < 100; ++i) {
            a[i] = -10.;
            b[i] = i >= 40 && i < 80 ? -11. : -30.;
        }
        CameraSettings settings;
        settings.hasWide = true;
        settings.minShotMs = 400;
        const QList<Shot> shots = cameraPlan({a, b}, settings, Fps);
        REQUIRE(shots.size() == 3);
        CHECK(shots.at(1).speaker == -1);
        CHECK(shots.at(1).frames == Interval(40, 80));
    }
}

TEST_CASE("Coupe automatique : monter les caméras sans rien supprimer", "[KoraCutAutoCut]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    int trackA;
    int trackB;
    REQUIRE(timeline->requestTrackInsertion(-1, trackA));
    REQUIRE(timeline->requestTrackInsertion(-1, trackB));
    const QString blue = KdenliveTests::createProducer(pCore->getProjectProfile(), "blue", binModel, 100);
    const QString red = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 100);
    int camA;
    int camB;
    REQUIRE(timeline->requestClipInsertion(blue, trackA, 0, camA));
    REQUIRE(timeline->requestClipInsertion(red, trackB, 0, camB));

    auto states = [&](int track) {
        QList<QPair<int, bool>> pieces; // position, désactivé
        for (int cid : timeline->getItemsInRange(track, 0, -1, false)) {
            pieces << qMakePair(timeline->getClipPosition(cid), timeline->getClipState(cid).first == PlaylistState::Disabled);
        }
        std::sort(pieces.begin(), pieces.end());
        return pieces;
    };
    const QList<QPair<QPair<int, int>, int>> shots = {{{0, 40}, 0}, {{40, 70}, 1}, {{70, 100}, 0}};
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    REQUIRE(TimelineFunctions::applyCameraPlan(timeline, shots, {trackA, trackB}, -1, undo, redo));
    pCore->pushUndo(undo, redo, QStringLiteral("Monter les caméras"));
    CHECK(states(trackA) == QList<QPair<int, bool>>({{0, false}, {40, true}, {70, false}}));
    CHECK(states(trackB) == QList<QPair<int, bool>>({{0, true}, {40, false}, {70, true}}));
    // Rien n'a disparu : les deux pistes font toujours 100 images.
    int total = 0;
    for (int cid : timeline->getItemsInRange(trackB, 0, -1, false)) {
        total += timeline->getClipPlaytime(cid);
    }
    CHECK(total == 100);
    undoStack->undo();
    CHECK(states(trackA) == QList<QPair<int, bool>>({{0, false}}));
    CHECK(states(trackB) == QList<QPair<int, bool>>({{0, false}}));
    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}
