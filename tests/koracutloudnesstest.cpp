/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Le niveau sonore à l'export (document 16, 24 septembre 2026) : la mesure du mixage, le gain fixe posé sur
// le tractor du rendu, puis un vrai rendu de deux secondes contrôlé par ffmpeg ebur128 (entre -15 et -13
// LUFS pour une cible de -14).

#include "catch.hpp"
#include "test_utils.hpp"

#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "kdenlivesettings.h"
#include "koracut/koracutagenttools.h"
#include "koracut/koracutloudness.h"
#include "renderpresets/renderpresetrepository.hpp"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>

using KoraCutAgentTools::call;

namespace {
std::string dump(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)).toStdString();
}

QString ffmpegPath()
{
    for (const QString &candidate : {KdenliveSettings::ffmpegpath(), QStandardPaths::findExecutable(QStringLiteral("ffmpeg")),
                                     QStringLiteral("/opt/homebrew/bin/ffmpeg"), QStringLiteral("/usr/local/bin/ffmpeg")}) {
        if (!candidate.isEmpty() && QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

// Le LUFS intégré et la crête vraie d'un fichier, mesurés par ffmpeg (ebur128). NaN si illisible.
std::pair<double, double> loudnessAndPeak(const QString &ffmpeg, const QString &file)
{
    QProcess p;
    p.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-nostats"), QStringLiteral("-i"), file, QStringLiteral("-af"), QStringLiteral("ebur128=peak=true"),
                     QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")});
    if (!p.waitForFinished(60000)) {
        return {std::nan(""), std::nan("")};
    }
    const QString out = QString::fromUtf8(p.readAllStandardError());
    const int summary = out.lastIndexOf(QLatin1String("Integrated loudness"));
    static const QRegularExpression lufsRx(QStringLiteral("I:\\s*(-?[0-9.]+) LUFS"));
    static const QRegularExpression peakRx(QStringLiteral("Peak:\\s*(-?[0-9.]+|-inf) dBFS"));
    const QRegularExpressionMatch l = lufsRx.match(out, summary);
    const QRegularExpressionMatch k = peakRx.match(out, out.lastIndexOf(QLatin1String("True peak")));
    return {l.hasMatch() ? l.captured(1).toDouble() : std::nan(""), k.hasMatch() ? k.captured(1).toDouble() : std::nan("")};
}

// Le LUFS intégré d'un fichier, mesuré par ffmpeg (ebur128). NaN si la mesure n'a pas pu être lue.
double integratedLufs(const QString &ffmpeg, const QString &file)
{
    QProcess p;
    p.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-nostats"), QStringLiteral("-i"), file, QStringLiteral("-af"), QStringLiteral("ebur128"),
                     QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")});
    if (!p.waitForFinished(60000)) {
        return std::nan("");
    }
    const QString out = QString::fromUtf8(p.readAllStandardError());
    static const QRegularExpression rx(QStringLiteral("I:\\s*(-?[0-9.]+) LUFS"));
    const QRegularExpressionMatch m = rx.match(out, out.lastIndexOf(QLatin1String("Integrated loudness")));
    return m.hasMatch() ? m.captured(1).toDouble() : std::nan("");
}
} // namespace

TEST_CASE("Le niveau sonore à l'export : mesure, gain fixe, rendu", "[KoraCutLoudness]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    if (!RenderPresetRepository::get()->presetExists(QStringLiteral("MP4-H264/AAC"))) {
        KdenliveTests::loadRenderPresets(QFileInfo(sourcesPath + QStringLiteral("/../data/profiles.xml")).absoluteFilePath());
    }
    REQUIRE(RenderPresetRepository::get()->presetExists(QStringLiteral("MP4-H264/AAC")));

    // Le dialogue d'Amélie sur A1, deux secondes de bleu sur V1.
    const QString wav = sourcesPath + QStringLiteral("/dataset/koracut-dialogue-amelie.wav");
    const QString voiceId = KdenliveTests::createFileProducer(pCore->getProjectProfile(), binModel, wav);
    REQUIRE_FALSE(voiceId.isEmpty());
    const int fps = qRound(pCore->getCurrentFps());
    const QString blue = KdenliveTests::createProducer(pCore->getProjectProfile(), "blue", binModel, 3 * fps, true);
    const int v1 = timeline->getTracksIds(false).first();
    const int a1 = timeline->getTracksIds(true).first();
    QJsonObject r = call(timeline, QStringLiteral("ajouter_clip"), {{QStringLiteral("media_id"), voiceId}, {QStringLiteral("piste_id"), a1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("audio")}});
    INFO(dump(r));
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    r = call(timeline, QStringLiteral("ajouter_clip"), {{QStringLiteral("media_id"), blue}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("video")}});
    REQUIRE(r.value(QStringLiteral("ok")).toBool());
    const int last = 2 * fps - 1;

    SECTION("La mesure du mixage, la même que mesurer_audio")
    {
        const KoraCutLoudness::Measure m = KoraCutLoudness::measure(timeline->sceneList(QString()), fps, 0, last);
        REQUIRE(m.ok);
        CHECK_FALSE(m.silent);
        CHECK(m.frames == 2 * fps);
        CHECK(m.lufs > -40.);
        CHECK(m.lufs < 0.);
        CHECK(m.truePeak > m.lufs);
        // L'outil d'agent donne le même chiffre.
        r = call(timeline, QStringLiteral("mesurer_audio"), {{QStringLiteral("cible"), QStringLiteral("mixage")}, {QStringLiteral("debut"), 0}, {QStringLiteral("fin"), last}});
        INFO(dump(r));
        REQUIRE(r.value(QStringLiteral("ok")).toBool());
        CHECK(std::abs(r.value(QStringLiteral("lufs_integre")).toDouble() - m.lufs) < 0.15);
        // Une zone vide ou un XML vide : pas de mesure.
        CHECK_FALSE(KoraCutLoudness::measure(QString(), fps, 0, last).ok);
        CHECK_FALSE(KoraCutLoudness::measure(timeline->sceneList(QString()), fps, 10, 5).ok);
    }

    SECTION("Le gain fixe posé sur le tractor : cible moins mesure")
    {
        QDomDocument doc;
        doc.setContent(QStringLiteral("<mlt producer=\"main\"><tractor id=\"main\"/></mlt>"));
        const KoraCutLoudness::Plan plan = KoraCutLoudness::prepare(doc, 0, last, -14);
        INFO(KoraCutLoudness::planText(plan).toStdString());
        REQUIRE(plan.measured);
        CHECK_FALSE(plan.fallback);
        // Sans limiteur, le gain est exactement la cible moins la mesure ; avec, il peut monter un peu
        // (correction mesurée : le limiteur retient une partie du niveau).
        if (!plan.limiter) {
            CHECK(std::abs(plan.gainDb - (-14. - plan.lufs)) < 0.01);
        } else {
            CHECK(plan.gainDb >= -14. - plan.lufs - 0.01);
        }
        const QDomNodeList filters = doc.elementsByTagName(QStringLiteral("filter"));
        REQUIRE(filters.count() >= 1);
        const QDomElement gain = filters.at(0).toElement();
        CHECK(gain.attribute(QStringLiteral("id")) == QStringLiteral("koracut_loudness"));
        QString service;
        QString level;
        for (QDomElement p = gain.firstChildElement(QStringLiteral("property")); !p.isNull(); p = p.nextSiblingElement(QStringLiteral("property"))) {
            if (p.attribute(QStringLiteral("name")) == QLatin1String("mlt_service")) {
                service = p.text();
            } else if (p.attribute(QStringLiteral("name")) == QLatin1String("level")) {
                level = p.text();
            }
        }
        CHECK(service == QStringLiteral("volume"));
        CHECK(std::abs(level.toDouble() - plan.gainDb) < 0.01);
        // Le limiteur n'est là que si la crête après gain dépasse -1,5 dBTP (marge sous le -1 des plateformes).
        CHECK((filters.count() == 2) == plan.limiter);
        CHECK((plan.truePeak + plan.gainDb > -1.5) == plan.limiter);
        const QJsonObject json = KoraCutLoudness::planJson(plan);
        CHECK(json.value(QStringLiteral("methode")).toString() == QStringLiteral("gain_mesure"));
        CHECK(json.value(QStringLiteral("cible_lufs")).toInt() == -14);
        // Une zone au-delà de la fin (rien à mesurer) : repli sur le filtre adaptatif, et on le dit.
        QDomDocument silent;
        silent.setContent(QStringLiteral("<mlt producer=\"main\"><tractor id=\"main\"/></mlt>"));
        const KoraCutLoudness::Plan fallback = KoraCutLoudness::prepare(silent, timeline->duration() + 10 * fps, timeline->duration() + 12 * fps, -14);
        CHECK_FALSE(fallback.measured);
        CHECK(fallback.fallback);
        CHECK_FALSE(fallback.note.isEmpty());
        REQUIRE(silent.elementsByTagName(QStringLiteral("filter")).count() == 1);
        CHECK(silent.elementsByTagName(QStringLiteral("filter")).at(0).toElement().firstChildElement(QStringLiteral("property")).text() == QStringLiteral("dynamic_loudness"));
    }

    SECTION("Un rendu de deux secondes à -14 LUFS, mesuré par ffmpeg")
    {
        const bool canRender = QFileInfo::exists(KdenliveSettings::meltpath());
        const QString ffmpeg = ffmpegPath();
        INFO("melt : " << KdenliveSettings::meltpath().toStdString() << ", ffmpeg : " << ffmpeg.toStdString());
        if (canRender && !ffmpeg.isEmpty()) {
            QTemporaryDir folder;
            REQUIRE(folder.isValid());
            const QString out = folder.filePath(QStringLiteral("niveau.mp4"));
            QJsonObject args{{QStringLiteral("preset"), QStringLiteral("personnalise")},
                             {QStringLiteral("nom_preset_kdenlive"), QStringLiteral("MP4-H264/AAC")},
                             {QStringLiteral("chemin"), out},
                             {QStringLiteral("niveau_lufs"), -14},
                             {QStringLiteral("plage"), QJsonObject{{QStringLiteral("type"), QStringLiteral("zone")}, {QStringLiteral("debut"), 0}, {QStringLiteral("fin"), last}}}};
            // sous_titres : incruster ou sans, rien d'autre.
            QJsonObject wrong = args;
            wrong.insert(QStringLiteral("sous_titres"), QStringLiteral("parfois"));
            CHECK(call(timeline, QStringLiteral("exporter"), wrong).value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("entree_invalide"));
            r = call(timeline, QStringLiteral("exporter"), args);
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK(r.value(QStringLiteral("sous_titres")).toString() == QStringLiteral("incruster"));
            CHECK(r.value(QStringLiteral("niveau_lufs")).toInt() == -14);
            const QJsonObject levelling = r.value(QStringLiteral("nivellement")).toObject();
            CHECK(levelling.value(QStringLiteral("methode")).toString() == QStringLiteral("gain_mesure"));
            const QString id = r.value(QStringLiteral("export_id")).toString();
            QElapsedTimer timer;
            timer.start();
            QString etat;
            while (timer.elapsed() < 180000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                const QJsonObject s = call(timeline, QStringLiteral("etat_exports"), {{QStringLiteral("export_id"), id}});
                etat = s.value(QStringLiteral("exports")).toArray().first().toObject().value(QStringLiteral("etat")).toString();
                if (etat == QLatin1String("termine") || etat == QLatin1String("echec") || etat == QLatin1String("annule")) {
                    INFO(dump(s));
                    break;
                }
            }
            REQUIRE(etat == QStringLiteral("termine"));
            REQUIRE(QFileInfo(out).size() > 1000);
            const double lufs = integratedLufs(ffmpeg, out);
            INFO("LUFS du rendu : " << lufs << ", mesure avant gain : " << levelling.value(QStringLiteral("lufs_mesure")).toDouble() << ", gain : "
                                    << levelling.value(QStringLiteral("gain_db")).toDouble());
            REQUIRE(std::isfinite(lufs));
            CHECK(lufs > -15.);
            CHECK(lufs < -13.);
        }
    }

    SECTION("Sans niveau demandé, le fichier contient ce que mesurer_audio annonce (le master est rendu)")
    {
        // 1er octobre 2026 : un export sans niveau sortait à -23,0 LUFS alors que mesurer_audio annonçait -14.
        // La cause était la cible du type de vidéo, appliquée d'office par export.lancer ; on prouve ici que
        // l'effet de normalisation posé sur le master est bien dans le fichier quand aucun niveau n'est demandé.
        const bool canRender = QFileInfo::exists(KdenliveSettings::meltpath());
        const QString ffmpeg = ffmpegPath();
        if (canRender && !ffmpeg.isEmpty()) {
            r = call(timeline, QStringLiteral("normaliser_loudness"), {{QStringLiteral("cible"), QStringLiteral("mixage")}, {QStringLiteral("lufs_cible"), -18}});
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            const QJsonObject announced = call(timeline, QStringLiteral("mesurer_audio"), {{QStringLiteral("cible"), QStringLiteral("mixage")}});
            INFO(dump(announced));
            REQUIRE(announced.value(QStringLiteral("ok")).toBool());
            const double expected = announced.value(QStringLiteral("lufs_integre")).toDouble();
            CHECK(std::abs(expected + 18.) < 1.);
            QTemporaryDir folder;
            REQUIRE(folder.isValid());
            const QString out = folder.filePath(QStringLiteral("tel-quel.mp4"));
            r = call(timeline, QStringLiteral("exporter"),
                     {{QStringLiteral("preset"), QStringLiteral("personnalise")}, {QStringLiteral("nom_preset_kdenlive"), QStringLiteral("MP4-H264/AAC")}, {QStringLiteral("chemin"), out}});
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            CHECK_FALSE(r.contains(QStringLiteral("niveau_lufs")));
            const QString id = r.value(QStringLiteral("export_id")).toString();
            QElapsedTimer timer;
            timer.start();
            QString etat;
            while (timer.elapsed() < 180000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                const QJsonObject st = call(timeline, QStringLiteral("etat_exports"), {{QStringLiteral("export_id"), id}});
                etat = st.value(QStringLiteral("exports")).toArray().first().toObject().value(QStringLiteral("etat")).toString();
                if (etat == QLatin1String("termine") || etat == QLatin1String("echec") || etat == QLatin1String("annule")) {
                    INFO(dump(st));
                    break;
                }
            }
            REQUIRE(etat == QStringLiteral("termine"));
            const double lufs = integratedLufs(ffmpeg, out);
            INFO("LUFS du fichier : " << lufs << ", annoncé par mesurer_audio : " << expected);
            REQUIRE(std::isfinite(lufs));
            CHECK(std::abs(lufs - expected) < 0.5);
        }
    }

    SECTION("Une voix muette et des sons courts très forts : la crête reste sous -1 dBTP, le niveau suit")
    {
        // Le défaut du 27 septembre 2026 (vidéos AFEBABA sans voix) : la voix muette, il reste des sons
        // courts ; +10 dB de gain et un limiteur qui laissait passer les crêtes (+8 dBFS dans le fichier).
        const bool canRender = QFileInfo::exists(KdenliveSettings::meltpath());
        const QString ffmpeg = ffmpegPath();
        if (canRender && !ffmpeg.isEmpty()) {
            QTemporaryDir folder;
            REQUIRE(folder.isValid());
            // Un fond bas et un claquement presque plein toutes les demi-secondes : -26 LUFS, crête -3 dBTP ;
            // les claquements portent 2 LU du niveau, que le limiteur retient.
            const QString pops = folder.filePath(QStringLiteral("pops.wav"));
            QProcess gen;
            gen.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                               QStringLiteral("aevalsrc=0.06*sin(2*PI*220*t)+if(lt(mod(t\\,0.5)\\,0.0008)\\,0.95*sin(2*PI*1500*t)\\,0):s=48000:d=3"),
                               QStringLiteral("-t"), QStringLiteral("3"), QStringLiteral("-ac"), QStringLiteral("2"), pops});
            REQUIRE(gen.waitForFinished(30000));
            REQUIRE(QFileInfo(pops).size() > 1000);
            const QString popsId = KdenliveTests::createFileProducer(pCore->getProjectProfile(), binModel, pops);
            REQUIRE_FALSE(popsId.isEmpty());
            QList<int> audioTracks = timeline->getTracksIds(true);
            if (audioTracks.size() < 2) {
                int added = -1;
                REQUIRE(timeline->requestTrackInsertion(-1, added, QString(), true));
                audioTracks = timeline->getTracksIds(true);
            }
            const int voiceTrack = a1;
            const int popsTrack = audioTracks.at(audioTracks.first() == a1 ? 1 : 0);
            r = call(timeline, QStringLiteral("ajouter_clip"), {{QStringLiteral("media_id"), popsId}, {QStringLiteral("piste_id"), popsTrack}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("audio")}});
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            r = call(timeline, QStringLiteral("regler_piste_audio"), {{QStringLiteral("piste_id"), voiceTrack}, {QStringLiteral("muet"), true}});
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            // mesurer_audio voit le mixage sans la voix.
            r = call(timeline, QStringLiteral("mesurer_audio"), {{QStringLiteral("cible"), QStringLiteral("mixage")}, {QStringLiteral("debut"), 0}, {QStringLiteral("fin"), last}});
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            const double before = r.value(QStringLiteral("lufs_integre")).toDouble();
            CHECK(before < -24.);

            const QString out = folder.filePath(QStringLiteral("sans-voix.mp4"));
            r = call(timeline, QStringLiteral("exporter"), {{QStringLiteral("preset"), QStringLiteral("personnalise")},
                                                            {QStringLiteral("nom_preset_kdenlive"), QStringLiteral("MP4-H264/AAC")},
                                                            {QStringLiteral("chemin"), out},
                                                            {QStringLiteral("niveau_lufs"), -14},
                                                            {QStringLiteral("plage"), QJsonObject{{QStringLiteral("type"), QStringLiteral("zone")}, {QStringLiteral("debut"), 0}, {QStringLiteral("fin"), last}}}});
            INFO(dump(r));
            REQUIRE(r.value(QStringLiteral("ok")).toBool());
            const QJsonObject levelling = r.value(QStringLiteral("nivellement")).toObject();
            CHECK(levelling.value(QStringLiteral("limiteur")).toString() != QStringLiteral("aucun"));
            CHECK(levelling.value(QStringLiteral("prevision_mesuree")).toBool());
            CHECK(std::abs(levelling.value(QStringLiteral("lufs_mesure")).toDouble() - before) < 0.3);
            const QString id = r.value(QStringLiteral("export_id")).toString();
            QElapsedTimer timer;
            timer.start();
            QString etat;
            while (timer.elapsed() < 180000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                const QJsonObject st = call(timeline, QStringLiteral("etat_exports"), {{QStringLiteral("export_id"), id}});
                etat = st.value(QStringLiteral("exports")).toArray().first().toObject().value(QStringLiteral("etat")).toString();
                if (etat == QLatin1String("termine") || etat == QLatin1String("echec") || etat == QLatin1String("annule")) {
                    break;
                }
            }
            REQUIRE(etat == QStringLiteral("termine"));
            const auto [lufs, peak] = loudnessAndPeak(ffmpeg, out);
            INFO("rendu : " << lufs << " LUFS, crête vraie " << peak << " dBTP ; prévu : " << levelling.value(QStringLiteral("lufs_prevu")).toDouble() << " LUFS");
            REQUIRE(std::isfinite(lufs));
            REQUIRE(std::isfinite(peak));
            // La crête : sous -1 dBTP (elle montait à +8 dBFS).
            CHECK(peak < -1.);
            // Le niveau : ce que le plan annonce, et proche de la cible.
            CHECK(std::abs(lufs - levelling.value(QStringLiteral("lufs_prevu")).toDouble()) < 1.);
            CHECK(lufs > -15.5);
            CHECK(lufs < -12.5);
        }
    }

    // Comme les autres tests d'outils : la timeline et le document se ferment avant la fin du cas,
    // sinon ils sont détruits après Core à la sortie du programme (plantage à la sortie).
    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}
