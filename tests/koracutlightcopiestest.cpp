/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Chantier 10 (document 6, journal du 4 octobre 2026) : copies légères tout-intra pour les sources lourdes.
// La règle de décision (quel clip reçoit une copie, à quelle taille), le plafond de disque, les arguments
// de ffmpeg, et la preuve que l'export reprend l'original.

#include "catch.hpp"
#include "test_utils.hpp"

#include "doc/kdenlivedoc.h"
#include "koracut/koracutlightcopies.h"

#include <QDomDocument>
#include <QFile>
#include <QTemporaryDir>

using namespace KoraCutLightCopy;

static Source source(const QString &codec, const QString &pixFormat, int width, int height, bool alpha = false)
{
    Source s;
    s.codec = codec;
    s.pixFormat = pixFormat;
    s.width = width;
    s.height = height;
    s.hasAlpha = alpha;
    return s;
}

TEST_CASE("Copies légères : quelles sources sont lourdes", "[KoraCut]")
{
    // Les rushs d'iPhone du banc : HEVC 10 bits HLG, 1080p.
    CHECK(isHeavy(source(QStringLiteral("hevc"), QStringLiteral("yuv420p10le"), 1920, 1080)));
    CHECK(isHeavy(source(QStringLiteral("hevc"), QStringLiteral("yuv420p"), 1280, 720)));
    CHECK(isHeavy(source(QStringLiteral("av1"), QStringLiteral("yuv420p"), 1920, 1080)));
    CHECK(isHeavy(source(QStringLiteral("vp9"), QStringLiteral("yuv420p"), 1920, 1080)));
    // 10 bits, même en H.264 (rushs Sony XAVC).
    CHECK(isHeavy(source(QStringLiteral("h264"), QStringLiteral("yuv422p10le"), 1920, 1080)));
    // 4K, quel que soit le codec, couché ou debout.
    CHECK(isHeavy(source(QStringLiteral("h264"), QStringLiteral("yuv420p"), 3840, 2160)));
    CHECK(isHeavy(source(QStringLiteral("h264"), QStringLiteral("yuv420p"), 2160, 3840)));
    // Un H.264 8 bits 1080p se lit bien : pas de copie.
    CHECK_FALSE(isHeavy(source(QStringLiteral("h264"), QStringLiteral("yuv420p"), 1920, 1080)));
    // Un codec intra saute déjà pour presque rien, même en 10 bits ou en 4K.
    CHECK_FALSE(isHeavy(source(QStringLiteral("prores"), QStringLiteral("yuv422p10le"), 3840, 2160)));
    CHECK_FALSE(isHeavy(source(QStringLiteral("dnxhd"), QStringLiteral("yuv422p10le"), 1920, 1080)));
    CHECK_FALSE(isHeavy(source(QStringLiteral("mjpeg"), QStringLiteral("yuvj420p"), 3840, 2160)));
    // Couche alpha (sujets détourés, Motion) : jamais, la copie est en 4:2:0 sans alpha.
    CHECK_FALSE(isHeavy(source(QStringLiteral("hevc"), QStringLiteral("yuva420p10le"), 1920, 1080, true)));
    // Taille inconnue : le clip n'est pas encore chargé.
    CHECK_FALSE(isHeavy(source(QStringLiteral("hevc"), QStringLiteral("yuv420p10le"), 0, 0)));
}

TEST_CASE("Copies légères : la taille suit l'aperçu du moniteur", "[KoraCut]")
{
    // Projet vertical 1080 × 1920, aperçu à 1080 de haut : 608 × 1080 à l'écran, copie en 540.
    CHECK(targetShortSide(QSize(608, 1080)) == 540);
    // Projet horizontal 1920 × 1080, aperçu plein : 720.
    CHECK(targetShortSide(QSize(1920, 1080)) == 720);
    CHECK(targetShortSide(QSize(1280, 720)) == 720);
    // Aperçu à 540 ou moins : 540.
    CHECK(targetShortSide(QSize(960, 540)) == 540);
    CHECK(targetShortSide(QSize(640, 360)) == 540);
    // Jamais plus grand que la source, toujours pair.
    CHECK(shortSideFor(1920, 1080, 540) == 540);
    CHECK(shortSideFor(1080, 1920, 720) == 720);
    CHECK(shortSideFor(640, 480, 540) == 480);
    CHECK(shortSideFor(853, 481, 540) == 480);
}

TEST_CASE("Copies légères : les arguments de ffmpeg", "[KoraCut]")
{
    const QString src = QStringLiteral("/rushs/IMG 1.mov");
    const QString dst = QStringLiteral("/cache/proxy/abc-kc540-part.mp4");
    const QStringList vt = ffmpegArguments(src, dst, 540, Encoder::VideoToolbox, QStringLiteral("videotoolbox"), false, 6);
    // Tout-intra, sans image B, 8 bits, par la puce pour lire et pour écrire.
    CHECK(vt.indexOf(QStringLiteral("-g")) >= 0);
    CHECK(vt.at(vt.indexOf(QStringLiteral("-g")) + 1) == QStringLiteral("1"));
    CHECK(vt.at(vt.indexOf(QStringLiteral("-bf")) + 1) == QStringLiteral("0"));
    CHECK(vt.at(vt.indexOf(QStringLiteral("-c:v")) + 1) == QStringLiteral("h264_videotoolbox"));
    CHECK(vt.at(vt.indexOf(QStringLiteral("-hwaccel")) + 1) == QStringLiteral("videotoolbox"));
    CHECK(vt.indexOf(QStringLiteral("-hwaccel")) < vt.indexOf(QStringLiteral("-i")));
    CHECK(vt.at(vt.indexOf(QStringLiteral("-i")) + 1) == src);
    CHECK(vt.at(vt.indexOf(QStringLiteral("-vf")) + 1).contains(QStringLiteral("format=nv12")));
    CHECK(vt.at(vt.indexOf(QStringLiteral("-vf")) + 1).contains(QStringLiteral("540")));
    CHECK(vt.last() == dst);
    // La rotation est appliquée (copie droite) : pas de -noautorotate, et pas de limite de fils avec la puce.
    CHECK_FALSE(vt.contains(QStringLiteral("-noautorotate")));
    CHECK_FALSE(vt.contains(QStringLiteral("-threads")));
    // Le son est gardé, les flux de données non.
    CHECK(vt.at(vt.indexOf(QStringLiteral("-c:a")) + 1) == QStringLiteral("aac"));
    CHECK(vt.contains(QStringLiteral("-dn")));
    // Sans liste de flux : la première vidéo et le premier son, s'il y en a un.
    CHECK(vt.contains(QStringLiteral("0:V:0")));
    CHECK(vt.contains(QStringLiteral("0:a:0?")));
    // Avec la liste des flux que MLT sait lire (un iPhone récent : vidéo 0, son 1, son spatial 2 sans décodeur).
    const QStringList iphone = ffmpegArguments(src, dst, 540, Encoder::VideoToolbox, QString(), false, 6, {QStringLiteral("0"), QStringLiteral("1")});
    CHECK(iphone.count(QStringLiteral("-map")) == 2);
    CHECK(iphone.contains(QStringLiteral("0:0")));
    CHECK(iphone.contains(QStringLiteral("0:1")));
    CHECK_FALSE(iphone.contains(QStringLiteral("0:2")));

    const QStringList sw = ffmpegArguments(src, dst, 720, Encoder::Software, QString(), true, 4);
    CHECK(sw.at(sw.indexOf(QStringLiteral("-c:v")) + 1) == QStringLiteral("libx264"));
    CHECK(sw.at(sw.indexOf(QStringLiteral("-g")) + 1) == QStringLiteral("1"));
    CHECK(sw.at(sw.indexOf(QStringLiteral("-vf")) + 1).contains(QStringLiteral("format=yuv420p")));
    CHECK(sw.at(sw.indexOf(QStringLiteral("-tune")) + 1) == QStringLiteral("fastdecode"));
    CHECK_FALSE(sw.contains(QStringLiteral("-hwaccel")));
    // Rotation imposée sur le clip : l'image n'est pas tournée, avant la source.
    CHECK(sw.contains(QStringLiteral("-noautorotate")));
    CHECK(sw.indexOf(QStringLiteral("-noautorotate")) < sw.indexOf(QStringLiteral("-i")));
    CHECK(sw.at(sw.indexOf(QStringLiteral("-threads")) + 1) == QStringLiteral("4"));

    // Sans encodeur matériel essayé avec succès : logiciel, sur les trois systèmes.
    CHECK(encoderFor({}) == Encoder::Software);
    CHECK(encoderId(Encoder::Software) == QStringLiteral("libx264"));
    CHECK(hwDecoderFor(Encoder::Software).isEmpty());
#if defined(Q_OS_MACOS)
    CHECK(encoderFor({QStringLiteral("h264_videotoolbox"), QStringLiteral("hevc_videotoolbox")}) == Encoder::VideoToolbox);
#else
    CHECK(encoderFor({QStringLiteral("h264_nvenc")}) == Encoder::Nvenc);
    CHECK(encoderFor({QStringLiteral("h264_videotoolbox")}) == Encoder::Software);
#endif
}

TEST_CASE("Copies légères : les noms de fichiers", "[KoraCut]")
{
    const QString name = copyFileName(QStringLiteral("0123abcd"), 540);
    CHECK(name == QStringLiteral("0123abcd-kc540.mp4"));
    CHECK(isCopyFile(name));
    CHECK(isCopyFile(QStringLiteral("/Users/x/Library/Caches/koracut/proxy/0123abcd-kc720.mp4")));
    // Un proxy de Kdenlive et un fichier en cours d'écriture ne sont pas des copies légères.
    CHECK_FALSE(isCopyFile(QStringLiteral("0123abcd.mov")));
    CHECK_FALSE(isCopyFile(QStringLiteral("0123abcd.mp4")));
    CHECK_FALSE(isCopyFile(QStringLiteral("0123abcd-kc540-part.mp4")));
}

TEST_CASE("Copies légères : le plafond de disque", "[KoraCut]")
{
    const qint64 mega = 1024 * 1024;
    const qint64 giga = 1024 * mega;
    Budget budget;
    budget.capBytes = 6 * giga;
    budget.minFreeBytes = 8 * giga;
    auto file = [&](const QString &path, qint64 megas, qint64 lastUsed, bool inUse) {
        File f;
        f.path = path;
        f.size = megas * mega;
        f.lastUsedSecs = lastUsed;
        f.inUse = inUse;
        return f;
    };

    SECTION("De la place : on encode, rien ne part")
    {
        const Plan p = plan({file(QStringLiteral("a"), 500, 10, true)}, 15 * giga, 100 * mega, budget);
        CHECK(p.verdict == Verdict::Go);
        CHECK(p.evict.isEmpty());
    }
    SECTION("Moins de 8 Go libres après l'encodage : on n'encode pas")
    {
        const Plan p = plan({}, 8 * giga + 50 * mega, 100 * mega, budget);
        CHECK(p.verdict == Verdict::DiskFull);
        CHECK(p.evict.isEmpty());
    }
    SECTION("Disque juste, mais d'anciennes copies libres : elles partent pour faire la place")
    {
        const Plan p = plan({file(QStringLiteral("vieille"), 400, 10, false), file(QStringLiteral("tenue"), 400, 5, true)}, 8 * giga, 100 * mega, budget);
        CHECK(p.verdict == Verdict::Go);
        CHECK(p.evict == QStringList{QStringLiteral("vieille")});
    }
    SECTION("Plafond dépassé : les plus anciennes partent d'abord, jamais celles du projet ouvert")
    {
        const QList<File> files = {file(QStringLiteral("recente"), 2000, 300, false), file(QStringLiteral("ancienne"), 2000, 100, false),
                                   file(QStringLiteral("tenue"), 2000, 50, true), file(QStringLiteral("moyenne"), 100, 200, false)};
        const Plan p = plan(files, 50 * giga, 500 * mega, budget);
        CHECK(p.verdict == Verdict::Go);
        // 6 100 Mo pris, 500 à écrire, plafond 6 144 : l'ancienne (2 000 Mo) suffit.
        CHECK(p.evict == QStringList{QStringLiteral("ancienne")});
    }
    SECTION("Plafond atteint par le projet ouvert à lui seul : on s'arrête, sans rien effacer")
    {
        const QList<File> files = {file(QStringLiteral("tenue1"), 3000, 10, true), file(QStringLiteral("tenue2"), 3000, 20, true),
                                   file(QStringLiteral("libre"), 100, 5, false)};
        const Plan p = plan(files, 50 * giga, 500 * mega, budget);
        CHECK(p.verdict == Verdict::CapReached);
        CHECK(p.evict.isEmpty());
    }
    SECTION("L'estimation de place suit la durée, la taille et l'encodeur")
    {
        // Mesuré : environ 50 Mo par minute en 540p avec VideoToolbox, 80 avec x264.
        CHECK(estimateBytes(60, 540, Encoder::VideoToolbox) == 50 * mega);
        CHECK(estimateBytes(60, 540, Encoder::Software) == 80 * mega);
        CHECK(estimateBytes(60, 720, Encoder::VideoToolbox) > estimateBytes(60, 540, Encoder::VideoToolbox));
        CHECK(estimateBytes(120, 540, Encoder::VideoToolbox) == 2 * estimateBytes(60, 540, Encoder::VideoToolbox));
    }
}

TEST_CASE("Copies légères : suspendre sans processus ne fait rien", "[KoraCut]")
{
    Process process;
    process.setSuspended(true);
    CHECK_FALSE(process.suspended());
    process.setPid(0);
    process.setSuspended(false);
    CHECK_FALSE(process.suspended());
}
