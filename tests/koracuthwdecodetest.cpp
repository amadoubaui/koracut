/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Chantier 10 (document 6, journal du 1er octobre 2026) : le décodage matériel n'est demandé que si
// tous les clips du projet passent par la puce. Un clip refusé ou pas encore essayé fait retomber
// le projet en logiciel, pour l'application comme pour les rendus, et l'image ne sort jamais blanche.

#include "catch.hpp"
#include "test_utils.hpp"

#include "core.h"
#include "kdenlivesettings.h"
#include "koracut/koracuthwdecode.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <atomic>
#include <memory>
#include <mlt++/MltFrame.h>
#include <mlt++/MltProducer.h>

namespace {

/** Les verdicts du test vont dans un dossier à lui, jamais dans le cache de la personne. */
QString testVerdicts()
{
    static QTemporaryDir dir;
    return dir.filePath(QStringLiteral("koracut-decodage.json"));
}

/** Remet le réglage et l'environnement comme avant le test. */
struct SettingGuard
{
    QString before{KdenliveSettings::hwDecoding()};
    ~SettingGuard()
    {
        KoraCutHwDecode::resetForTests(QString());
        KdenliveSettings::setHwDecoding(before);
        KoraCutHwDecode::apply();
        qunsetenv("MLT_AVFORMAT_HWACCEL");
    }
};

QString findFfmpeg()
{
    // Le ffmpeg de Craft ne se lance pas hors de l'application : celui de Homebrew d'abord.
    for (const QString &candidate : {QStringLiteral("/opt/homebrew/bin/ffmpeg"), QStringLiteral("/usr/local/bin/ffmpeg")}) {
        if (QFileInfo(candidate).isExecutable()) {
            return candidate;
        }
    }
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

/** Part des pixels qui ne sont pas blancs, sur l'image @p position du fichier. -1 : pas d'image. */
double nonWhiteShare(const QString &path, int position)
{
    Mlt::Producer producer(pCore->getProjectProfile(), "avformat", path.toUtf8().constData());
    if (!producer.is_valid()) {
        return -1.;
    }
    producer.seek(position);
    std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
    if (!frame || !frame->is_valid()) {
        return -1.;
    }
    mlt_image_format format = mlt_image_rgb;
    int width = 0;
    int height = 0;
    const uint8_t *image = frame->get_image(format, width, height);
    if (image == nullptr || width <= 0 || height <= 0) {
        return -1.;
    }
    qint64 dark = 0;
    const qint64 pixels = qint64(width) * height;
    for (qint64 i = 0; i < pixels; ++i) {
        const uint8_t *p = image + 3 * i;
        if (p[0] < 225 || p[1] < 225 || p[2] < 225) {
            ++dark;
        }
    }
    return double(dark) / double(pixels);
}

} // namespace

TEST_CASE("Décodage matériel : règles et lecture de la sortie de ffmpeg", "[KoraCut]")
{
    // Seul VideoToolbox accepte un flux puis échoue image après image : lui seul demande un essai par fichier.
    CHECK(KoraCutHwDecode::needsTrial(QStringLiteral("videotoolbox"), QStringLiteral("h264")));
    CHECK(KoraCutHwDecode::needsTrial(QStringLiteral("videotoolbox"), QStringLiteral("hevc")));
    CHECK(KoraCutHwDecode::needsTrial(QStringLiteral("videotoolbox"), QString())); // proxy : format d'origine inconnu
    CHECK_FALSE(KoraCutHwDecode::needsTrial(QStringLiteral("videotoolbox"), QStringLiteral("mjpeg")));
    CHECK_FALSE(KoraCutHwDecode::needsTrial(QStringLiteral("d3d11va"), QStringLiteral("h264")));
    CHECK_FALSE(KoraCutHwDecode::needsTrial(QStringLiteral("vaapi"), QStringLiteral("hevc")));
    CHECK_FALSE(KoraCutHwDecode::needsTrial(QString(), QStringLiteral("h264")));

    // La sortie relevée sur un rush Sony XAVC (H.264 4:2:2 10 bits, trois PPS) le 1er octobre 2026.
    const QString sony = QStringLiteral("[h264 @ 0x72cc1df80] vt decoder cb: output image buffer is null: -12909, reconfig 1\n"
                                        "[h264 @ 0x72cc1df80] hardware accelerator failed to decode picture\n"
                                        "[vist#0:0/h264 @ 0xa68c14300] Error submitting packet to decoder: Unknown error occurred\n");
    CHECK(KoraCutHwDecode::outputSaysRefused(sony));
    // Un refus à l'ouverture n'est pas un échec : FFmpeg et MLT retombent en logiciel tout seuls.
    CHECK_FALSE(KoraCutHwDecode::outputSaysRefused(QStringLiteral("[mpeg2video @ 0xa22804e00] Failed setup for format videotoolbox_vld: hwaccel initialisation returned error.")));
    CHECK_FALSE(KoraCutHwDecode::outputSaysRefused(QString()));

    CHECK(KoraCutHwDecode::describeFormat(QStringLiteral("h264"), QStringLiteral("yuv422p10le")) == QLatin1String("H.264 4:2:2 10 bits"));
    CHECK(KoraCutHwDecode::describeFormat(QStringLiteral("hevc"), QStringLiteral("yuv420p")) == QLatin1String("HEVC 4:2:0 8 bits"));
    CHECK(KoraCutHwDecode::describeFormat(QStringLiteral("h264"), QStringLiteral("yuvj420p")) == QLatin1String("H.264 4:2:0 8 bits"));
    CHECK(KoraCutHwDecode::describeFormat(QStringLiteral("prores"), QStringLiteral("yuva444p12le")) == QLatin1String("ProRes 4:4:4 12 bits"));
    CHECK(KoraCutHwDecode::describeFormat(QString(), QString()).isEmpty());
}

TEST_CASE("Décodage matériel : un clip refusé fait retomber tout le projet en logiciel", "[KoraCut]")
{
    SettingGuard guard;
    KoraCutHwDecode::resetForTests(testVerdicts());
    KdenliveSettings::setHwDecoding(QStringLiteral("videotoolbox"));
    auto calls = std::make_shared<std::atomic<int>>(0);
    KoraCutHwDecode::setTrial([calls](const QString &, const QString &path) {
        ++(*calls);
        return !path.contains(QLatin1String("sony"));
    });
    QStringList refusedSeen;
    QList<bool> legacySeen;
    const auto connection = QObject::connect(KoraCutHwDecode::instance(), &KoraCutHwDecode::clipRefused, qApp, [&](const QString &path, bool legacy) {
        refusedSeen << path;
        legacySeen << legacy;
    });
    KoraCutHwDecode::apply();
    CHECK(KoraCutHwDecode::effective() == QLatin1String("videotoolbox"));
    CHECK(qgetenv("MLT_AVFORMAT_HWACCEL") == "videotoolbox");

    int phone = 0;
    int camera = 0;
    int cameraAgain = 0;
    // Un clip pas encore essayé : logiciel tout de suite, avant même le verdict.
    KoraCutHwDecode::noteClip(&phone, QStringLiteral("telephone.mov"), QStringLiteral("/essai/telephone.mov"), QStringLiteral("hevc"), QStringLiteral("yuv420p10le"),
                              false);
    REQUIRE(KoraCutHwDecode::waitForTrials(10000));
    CHECK(KoraCutHwDecode::effective() == QLatin1String("videotoolbox"));
    CHECK(KoraCutHwDecode::softwareClips().isEmpty());

    KoraCutHwDecode::noteClip(&camera, QStringLiteral("sony.MP4"), QStringLiteral("/essai/sony.MP4"), QStringLiteral("h264"), QStringLiteral("yuv422p10le"), true);
    REQUIRE(KoraCutHwDecode::waitForTrials(10000));
    CHECK(KoraCutHwDecode::effective().isEmpty());
    CHECK(qgetenv("MLT_AVFORMAT_HWACCEL").isEmpty());
    const QList<KoraCutHwDecode::Clip> clips = KoraCutHwDecode::softwareClips();
    REQUIRE(clips.size() == 1);
    CHECK(clips.first().name == QLatin1String("sony.MP4"));
    CHECK(clips.first().format == QLatin1String("H.264 4:2:2 10 bits"));
    CHECK(clips.first().state == KoraCutHwDecode::State::Refused);

    // Le Diagnostic le dit.
    const QJsonObject state = KoraCutHwDecode::stateJson();
    CHECK(state.value(QStringLiteral("demande")).toString() == QLatin1String("videotoolbox"));
    CHECK(state.value(QStringLiteral("effectif")).toString() == QLatin1String("logiciel"));
    CHECK(state.value(QStringLiteral("clips_logiciel")).toArray().size() == 1);

    // Les rendus (aperçu, export) n'héritent pas de la puce.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("MLT_AVFORMAT_HWACCEL"), QStringLiteral("videotoolbox"));
    KoraCutHwDecode::applyTo(env);
    CHECK_FALSE(env.contains(QStringLiteral("MLT_AVFORMAT_HWACCEL")));

    // Le signal part une fois, avec la marque « déjà dans le projet » qui fait soigner les caches.
    // Le signal est posté par le fil de l'essai juste après qu'il s'est retiré de la liste des essais en cours :
    // waitForTrials peut donc rendre la main un instant avant lui. Un seul processEvents() le manquait parfois
    // (une fois sur le Mac sous charge, une fois sous Windows, 12 octobre 2026) : on l'attend, deux secondes au plus.
    {
        QElapsedTimer waited;
        waited.start();
        while (refusedSeen.isEmpty() && waited.elapsed() < 2000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
    }
    REQUIRE(refusedSeen.size() == 1);
    CHECK(refusedSeen.first() == QLatin1String("/essai/sony.MP4"));
    CHECK(legacySeen.first());

    // Le verdict est gardé : le même fichier ne relance pas d'essai.
    const int before = calls->load();
    KoraCutHwDecode::noteClip(&cameraAgain, QStringLiteral("sony.MP4"), QStringLiteral("/essai/sony.MP4"), QStringLiteral("h264"), QStringLiteral("yuv422p10le"),
                              false);
    REQUIRE(KoraCutHwDecode::waitForTrials(10000));
    CHECK(calls->load() == before);
    CHECK(KoraCutHwDecode::effective().isEmpty());
    // Et il est écrit sur le disque, pour la prochaine ouverture du projet.
    CHECK(QFileInfo::exists(testVerdicts()));

    // Les clips partis, la puce revient.
    KoraCutHwDecode::forgetClip(&camera);
    CHECK(KoraCutHwDecode::effective().isEmpty());
    KoraCutHwDecode::forgetClip(&cameraAgain);
    CHECK(KoraCutHwDecode::effective() == QLatin1String("videotoolbox"));
    CHECK(qgetenv("MLT_AVFORMAT_HWACCEL") == "videotoolbox");
    env.remove(QStringLiteral("MLT_AVFORMAT_HWACCEL"));
    KoraCutHwDecode::applyTo(env);
    CHECK(env.value(QStringLiteral("MLT_AVFORMAT_HWACCEL")) == QLatin1String("videotoolbox"));

    // Un autre décodeur (Windows, Linux) refuse à l'ouverture et retombe seul : pas d'essai, rien ne change.
    KdenliveSettings::setHwDecoding(QStringLiteral("d3d11va"));
    KoraCutHwDecode::apply();
    KoraCutHwDecode::noteClip(&camera, QStringLiteral("sony.MP4"), QStringLiteral("/essai/sony.MP4"), QStringLiteral("h264"), QStringLiteral("yuv422p10le"), false);
    CHECK(KoraCutHwDecode::effective() == QLatin1String("d3d11va"));

    // Réglage en logiciel : rien n'est demandé, mais un clip déjà dans le projet est quand même essayé
    // sur le décodeur du système, pour refaire les caches faits pendant que la puce échouait.
    KdenliveSettings::setHwDecoding(QString());
    KoraCutHwDecode::apply();
    CHECK(KoraCutHwDecode::effective().isEmpty());
    QObject::disconnect(connection);
    KoraCutHwDecode::forgetClip(&phone);
    KoraCutHwDecode::forgetClip(&camera);
}

TEST_CASE("Décodage matériel : un H.264 4:2:2 10 bits ne sort jamais blanc", "[KoraCut]")
{
    const QString ffmpeg = findFfmpeg();
    if (ffmpeg.isEmpty()) {
        WARN("ffmpeg introuvable : essai de bout en bout sauté");
        return;
    }
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString clip = dir.filePath(QStringLiteral("camera-422-10.mp4"));
    QProcess generate;
    generate.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"),
                            QStringLiteral("lavfi"), QStringLiteral("-i"), QStringLiteral("testsrc2=s=640x360:r=30"), QStringLiteral("-t"), QStringLiteral("2"),
                            QStringLiteral("-pix_fmt"), QStringLiteral("yuv422p10le"), QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"),
                            QStringLiteral("ultrafast"), QStringLiteral("-bf"), QStringLiteral("2"), clip});
    REQUIRE(generate.waitForFinished(60000));
    if (generate.exitCode() != 0 || !QFileInfo::exists(clip)) {
        WARN("ce ffmpeg n'écrit pas de H.264 4:2:2 10 bits : essai de bout en bout sauté");
        return;
    }

    SettingGuard guard;
    KoraCutHwDecode::resetForTests(testVerdicts());
    int owner = 0;

    // 1. En logiciel (réglage vide) : l'image de référence.
    KdenliveSettings::setHwDecoding(QString());
    KoraCutHwDecode::apply();
    const double software = nonWhiteShare(clip, 20);
    INFO("part de pixels non blancs en logiciel : " << software);
    CHECK(software > 0.5);

    // 2. Avec le décodeur matériel de ce système et le vrai essai : quel que soit le verdict, l'image est là.
    const QString hw = KoraCutHwDecode::platformDecoder();
    if (!hw.isEmpty()) {
        KdenliveSettings::setHwDecoding(hw);
        KoraCutHwDecode::setTrial([ffmpeg](const QString &decoder, const QString &path) { return KoraCutHwDecode::ffmpegTrial(ffmpeg, decoder, path); });
        KoraCutHwDecode::noteClip(&owner, QFileInfo(clip).fileName(), clip, QStringLiteral("h264"), QStringLiteral("yuv422p10le"), false);
        // Avant le verdict, le projet est en logiciel.
        CHECK((KoraCutHwDecode::effective().isEmpty() || KoraCutHwDecode::waitForTrials(0)));
        REQUIRE(KoraCutHwDecode::waitForTrials(30000));
        const QString effective = KoraCutHwDecode::effective();
        INFO("verdict de l'essai : " << (effective.isEmpty() ? "logiciel" : effective.toStdString()));
        CHECK(qgetenv("MLT_AVFORMAT_HWACCEL") == effective.toUtf8());
        for (int position : {0, 20, 45}) {
            const double share = nonWhiteShare(clip, position);
            INFO("image " << position << " : part de pixels non blancs " << share);
            CHECK(share > 0.5);
        }
        KoraCutHwDecode::forgetClip(&owner);
    }

    // 3. La puce refuse ce fichier (comme un rush Sony XAVC) : le projet repasse en logiciel, l'image est là.
    KoraCutHwDecode::resetForTests(testVerdicts());
    KdenliveSettings::setHwDecoding(QStringLiteral("videotoolbox"));
    KoraCutHwDecode::setTrial([](const QString &, const QString &) { return false; });
    KoraCutHwDecode::noteClip(&owner, QFileInfo(clip).fileName(), clip, QStringLiteral("h264"), QStringLiteral("yuv422p10le"), false);
    REQUIRE(KoraCutHwDecode::waitForTrials(10000));
    CHECK(KoraCutHwDecode::effective().isEmpty());
    CHECK(qgetenv("MLT_AVFORMAT_HWACCEL").isEmpty());
    for (int position : {0, 20, 45}) {
        CHECK(nonWhiteShare(clip, position) > 0.5);
    }
    KoraCutHwDecode::forgetClip(&owner);
    QCoreApplication::processEvents();
}
