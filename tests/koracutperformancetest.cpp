/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Chantier 10 (document 6) : le classement des machines, le critère de proxy, l'aperçu adaptatif,
// le budget du cache et la liste des effets lourds.

#include "catch.hpp"

#include "koracut/koracutperformance.h"
#include "utils/machineprofile.h"

#include <framework/mlt.h>

using Level = MachineProfile::Level;

static MachineProfile::Hardware machine(int cores, int ramGb, bool appleSilicon = false, double diskMbs = 0.)
{
    MachineProfile::Hardware hw;
    hw.logicalCores = cores;
    hw.ramMb = qint64(ramGb) * 1024;
    hw.isAppleSilicon = appleSilicon;
    hw.diskWriteMbPerSec = diskMbs;
    return hw;
}

TEST_CASE("Profil : classement des trois machines du document 6", "[KoraCut]")
{
    CHECK(MachineProfile::classify(machine(4, 8)) == Level::Modest);
    CHECK(MachineProfile::classify(machine(8, 8)) == Level::Modest); // 8 Go : la mémoire compte autant que le processeur
    CHECK(MachineProfile::classify(machine(8, 16)) == Level::Medium);
    CHECK(MachineProfile::classify(machine(8, 16, false, 40.)) == Level::Modest); // disque lent
    CHECK(MachineProfile::classify(machine(8, 16, true)) == Level::Comfortable);
    CHECK(MachineProfile::classify(machine(12, 32)) == Level::Comfortable);
    MachineProfile::Hardware nvidia = machine(8, 16);
    nvidia.hasDedicatedHwEncoder = true;
    CHECK(MachineProfile::classify(nvidia) == Level::Comfortable);
    // Mémoire inconnue : on ne pénalise pas.
    MachineProfile::Hardware unknown = machine(8, 0);
    CHECK(MachineProfile::classify(unknown) == Level::Medium);
}

TEST_CASE("Profil : identifiants et réglages recommandés", "[KoraCut]")
{
    bool ok = false;
    CHECK(MachineProfile::levelFromId(QStringLiteral("modeste"), &ok) == Level::Modest);
    CHECK(ok);
    CHECK(MachineProfile::levelFromId(QStringLiteral("Comfortable"), &ok) == Level::Comfortable);
    MachineProfile::levelFromId(QStringLiteral("rapide"), &ok);
    CHECK_FALSE(ok);
    for (Level l : {Level::Modest, Level::Medium, Level::Comfortable}) {
        CHECK(MachineProfile::levelFromId(MachineProfile::levelId(l)) == l);
    }
    const auto modest = MachineProfile::recommend(Level::Modest, machine(4, 8));
    CHECK(modest.previewScaling == 4);
    CHECK(modest.adaptivePreviewFloorHeight == 360);
    CHECK(modest.dropDivisor == 2);
    CHECK(modest.proxyHeavyMinWidth == 0);
    CHECK(modest.simplifiedHeavyEffects);
    CHECK_FALSE(modest.videoThumbnails);
    CHECK(modest.producerCacheMax >= 4);
    const auto comfy = MachineProfile::recommend(Level::Comfortable, machine(12, 32, true));
    CHECK(comfy.adaptivePreviewFloorHeight == 0);
    CHECK_FALSE(comfy.generateProxy);
    CHECK(comfy.producerCacheMax == 0);
}

TEST_CASE("Proxy : critère du codec en plus de la largeur", "[KoraCut]")
{
    CHECK(MachineProfile::isHeavyCodec(QStringLiteral("hevc")));
    CHECK(MachineProfile::isHeavyCodec(QStringLiteral("av1")));
    CHECK(MachineProfile::isHeavyCodec(QStringLiteral("libdav1d")));
    CHECK(MachineProfile::isHeavyCodec(QStringLiteral("vp9")));
    CHECK(MachineProfile::isHeavyCodec(QStringLiteral("h264"), QStringLiteral("yuv420p10le")));
    CHECK_FALSE(MachineProfile::isHeavyCodec(QStringLiteral("h264"), QStringLiteral("yuv420p")));
    CHECK_FALSE(MachineProfile::isHeavyCodec(QStringLiteral("prores"), QStringLiteral("yuv422p")));

    MachineProfile::ProxyRule modest{true, 1280, 0};
    CHECK(MachineProfile::wantsProxy(modest, 1920, QStringLiteral("h264"), QStringLiteral("yuv420p")));
    CHECK_FALSE(MachineProfile::wantsProxy(modest, 1280, QStringLiteral("h264"), QStringLiteral("yuv420p")));
    CHECK(MachineProfile::wantsProxy(modest, 1280, QStringLiteral("hevc"), QStringLiteral("yuv420p"))); // HEVC 720p : toujours
    MachineProfile::ProxyRule medium{true, 1920, 1920};
    CHECK_FALSE(MachineProfile::wantsProxy(medium, 1920, QStringLiteral("h264"), QStringLiteral("yuv420p")));
    CHECK(MachineProfile::wantsProxy(medium, 1920, QStringLiteral("hevc"), QStringLiteral("yuv420p"))); // HEVC 1080p de téléphone
    CHECK_FALSE(MachineProfile::wantsProxy(medium, 1280, QStringLiteral("hevc"), QStringLiteral("yuv420p")));
    MachineProfile::ProxyRule off{false, 1280, 0};
    CHECK_FALSE(MachineProfile::wantsProxy(off, 3840, QStringLiteral("hevc"), QString()));
    MachineProfile::ProxyRule widthOnly{true, 1000, -1};
    CHECK_FALSE(MachineProfile::wantsProxy(widthOnly, 960, QStringLiteral("hevc"), QString()));
    CHECK(MachineProfile::wantsProxy(widthOnly, -1, QString(), QString()));
}

TEST_CASE("Proxy : profil d'encodage selon la machine", "[KoraCut]")
{
    const QStringList available{QStringLiteral("x264"), QStringLiteral("x264-videotoolbox"), QStringLiteral("x264-qsv"), QStringLiteral("MJPEG"),
                                QStringLiteral("KoraCut léger"), QStringLiteral("KoraCut ProRes Proxy")};
    const QStringList vt{QStringLiteral("h264_videotoolbox"), QStringLiteral("hevc_videotoolbox")};
    CHECK(MachineProfile::proxyProfileFor(Level::Modest, vt, available) == QStringLiteral("KoraCut léger"));
    CHECK(MachineProfile::proxyProfileFor(Level::Medium, vt, available) == QStringLiteral("x264-videotoolbox"));
    CHECK(MachineProfile::proxyProfileFor(Level::Medium, {}, available) == QStringLiteral("KoraCut léger"));
    CHECK(MachineProfile::proxyProfileFor(Level::Comfortable, {}, available) == QStringLiteral("KoraCut ProRes Proxy"));
    CHECK(MachineProfile::proxyProfileFor(Level::Comfortable, {QStringLiteral("hevc_nvenc")}, available) == QStringLiteral("KoraCut ProRes Proxy"));
    CHECK(MachineProfile::proxyProfileFor(Level::Modest, vt, {QStringLiteral("x264")}).isEmpty()); // vieux fichier de profils
}

TEST_CASE("Cache : 10 % du libre, entre 2 et 20 Go", "[KoraCut]")
{
    CHECK(MachineProfile::cacheBudgetMb(500000) == 20000);
    CHECK(MachineProfile::cacheBudgetMb(100000) == 10000);
    CHECK(MachineProfile::cacheBudgetMb(30000) == 3000);
    CHECK(MachineProfile::cacheBudgetMb(12000) == 2000);
    CHECK(MachineProfile::cacheBudgetMb(4000) == 1000); // disque presque plein : jamais plus du quart du libre
    CHECK(MachineProfile::cacheBudgetMb(0) == 2000);
}

TEST_CASE("Aperçu adaptatif : descend après deux secondes, revient à la pause", "[KoraCut]")
{
    MachineProfile::AdaptiveScaling a;
    a.configure(8); // plancher 360p (machine modeste)
    CHECK(a.enabled());
    CHECK(a.start(4) == -1); // 540p choisi, rien de retenu
    CHECK(a.tick(15, 30.) == -1); // première seconde : on ne juge pas
    CHECK(a.tick(15, 30.) == -1); // une mauvaise seconde ne suffit pas
    CHECK(a.tick(15, 30.) == 8); // deux de suite : 360p
    CHECK(a.degraded());
    CHECK(a.tick(15, 30.) == -1); // seconde de répit après un changement
    CHECK(a.tick(15, 30.) == -1);
    CHECK(a.tick(15, 30.) == -1); // plancher atteint : on n'ira pas plus bas
    CHECK(a.current() == 8);
    CHECK(a.stop() == 4); // à la pause, la résolution choisie revient
    CHECK_FALSE(a.degraded());

    // La lecture suivante part du cran retenu.
    CHECK(a.start(4) == 8);
    // Quelques images sautées isolées ne font rien.
    CHECK(a.tick(1, 30.) == -1);
    CHECK(a.tick(1, 30.) == -1);
    CHECK(a.tick(0, 30.) == -1);
    CHECK(a.tick(0, 30.) == -1);
    CHECK(a.tick(0, 30.) == -1);
    CHECK(a.stop() == 4);
    // Cinq secondes sans saut… mais il y a eu 2 images : pas de remontée.
    CHECK(a.learned() == 8);

    CHECK(a.start(4) == 8);
    for (int i = 0; i < 6; ++i) {
        CHECK(a.tick(0, 30.) == -1);
    }
    a.stop();
    // Lecture propre au cran retenu : on remonte d'un cran, ici jusqu'à la résolution choisie, donc plus rien à retenir.
    CHECK(a.learned() == -1);
    CHECK(a.start(4) == -1);
    a.stop();

    // Plancher 270p et choix 1080p : la remontée se fait un cran à la fois.
    MachineProfile::AdaptiveScaling b;
    b.configure(16);
    b.start(1);
    for (int i = 0; i < 12; ++i) {
        b.tick(20, 30.);
    }
    CHECK(b.current() == 16);
    b.stop();
    b.start(1);
    for (int i = 0; i < 6; ++i) {
        b.tick(0, 30.);
    }
    b.stop();
    CHECK(b.learned() == 8);
}

TEST_CASE("Aperçu adaptatif : coupé, et ordre des crans", "[KoraCut]")
{
    MachineProfile::AdaptiveScaling a;
    a.configure(0);
    CHECK_FALSE(a.enabled());
    CHECK(a.start(1) == -1);
    for (int i = 0; i < 5; ++i) {
        CHECK(a.tick(20, 30.) == -1);
    }
    CHECK(a.stop() == -1);
    CHECK(MachineProfile::AdaptiveScaling::lower(0) == 1);
    CHECK(MachineProfile::AdaptiveScaling::lower(4) == 8);
    CHECK(MachineProfile::AdaptiveScaling::lower(16) == 16);
    CHECK(MachineProfile::AdaptiveScaling::higher(1) == 0);
    CHECK(MachineProfile::AdaptiveScaling::higher(8) == 4);
    CHECK(MachineProfile::scalingForHeight(360) == 8);
    CHECK(MachineProfile::scalingHeight(2) == 720);
    // Plancher 540p (machine moyenne) : 720p descend une fois, pas deux.
    MachineProfile::AdaptiveScaling m;
    m.configure(4);
    m.start(2);
    m.tick(10, 25.);
    m.tick(10, 25.);
    CHECK(m.tick(10, 25.) == 4);
    m.tick(10, 25.);
    m.tick(10, 25.);
    CHECK(m.tick(10, 25.) == -1);
}

TEST_CASE("Effets lourds : la liste livrée", "[KoraCut]")
{
    const QStringList &l = KoraCutPerformance::HeavyEffects::list();
    CHECK(l.size() >= 10);
    CHECK(KoraCutPerformance::HeavyEffects::isHeavy(QStringLiteral("avfilter.gblur")));
    CHECK(KoraCutPerformance::HeavyEffects::isHeavy(QStringLiteral("vidstab")));
    // L'étalonnage de KoraCut n'y est pas : il cuit ses nœuds dans une LUT, une fois par changement.
    CHECK_FALSE(KoraCutPerformance::HeavyEffects::isHeavy(QStringLiteral("koracut_grade")));
    CHECK_FALSE(KoraCutPerformance::HeavyEffects::isHeavy(QStringLiteral("qtblend")));
    CHECK_FALSE(KoraCutPerformance::HeavyEffects::isHeavy(QString()));
}

// Plantages du 2 octobre 2026 (document 6) : une copie de la scène chargée par un outil (mesurer_image,
// voir_image) évinçait du cache de MLT les décodeurs que les images du moniteur tenaient encore.
namespace {
int s_closedDecoders = 0;
void closeDecoder(void *)
{
    ++s_closedDecoders;
}
// Le cache ne lit jamais le service : son adresse sert de clef.
mlt_service fakeService(int *slot)
{
    return reinterpret_cast<mlt_service>(slot);
}
} // namespace

TEST_CASE("Cache des décodeurs : une copie de la scène n'évince pas ceux du moniteur", "[KoraCut]")
{
    const char *cache = "producer_avformat";
    const int before = mlt_service_cache_get_size(nullptr, cache);
    mlt_service_cache_set_size(nullptr, cache, 4);
    int monitor = 0;
    int copy[4] = {0, 0, 0, 0};
    int data = 0;

    SECTION("Sans garde, le décodeur du moniteur sort du cache alors qu'une image le tient")
    {
        s_closedDecoders = 0;
        mlt_service_cache_put(fakeService(&monitor), cache, &data, 0, closeDecoder);
        mlt_cache_item shown = mlt_service_cache_get(fakeService(&monitor), cache);
        REQUIRE(shown != nullptr);
        for (int &c : copy) {
            mlt_service_cache_put(fakeService(&c), cache, &data, 0, closeDecoder);
        }
        CHECK(mlt_service_cache_get(fakeService(&monitor), cache) == nullptr); // évincé : c'est le départ du défaut
        CHECK(s_closedDecoders == 0);                                          // l'image le tient encore
        mlt_cache_item_close(shown);
        CHECK(s_closedDecoders == 1);
        for (int &c : copy) {
            mlt_service_cache_purge(fakeService(&c));
        }
    }

    SECTION("Avec la garde, il reste en place, et la taille revient")
    {
        s_closedDecoders = 0;
        mlt_service_cache_put(fakeService(&monitor), cache, &data, 0, closeDecoder);
        mlt_cache_item shown = mlt_service_cache_get(fakeService(&monitor), cache);
        REQUIRE(shown != nullptr);
        {
            const KoraCutPerformance::DecoderCacheRoom room(4);
            CHECK(mlt_service_cache_get_size(nullptr, cache) == 8);
            CHECK(KoraCutPerformance::DecoderCacheRoom::baseSize() == 4);
            {
                const KoraCutPerformance::DecoderCacheRoom nested(2);
                CHECK(mlt_service_cache_get_size(nullptr, cache) == 10);
            }
            CHECK(mlt_service_cache_get_size(nullptr, cache) == 8);
            for (int &c : copy) {
                mlt_service_cache_put(fakeService(&c), cache, &data, 0, closeDecoder);
            }
            mlt_cache_item again = mlt_service_cache_get(fakeService(&monitor), cache);
            CHECK(again == shown); // toujours dans le cache : pas de second décodeur, pas de compte faussé
            mlt_cache_item_close(again);
            // La copie se ferme avant la garde : ses entrées quittent le cache.
            for (int &c : copy) {
                mlt_service_cache_purge(fakeService(&c));
            }
            CHECK(s_closedDecoders == 4);
        }
        CHECK(mlt_service_cache_get_size(nullptr, cache) == 4);
        mlt_cache_item_close(shown);
        CHECK(s_closedDecoders == 4); // le cache garde le décodeur du moniteur ouvert
        mlt_service_cache_purge(fakeService(&monitor));
        CHECK(s_closedDecoders == 5);
    }

    SECTION("La place demandée ne dépasse jamais ce que MLT accepte")
    {
        const KoraCutPerformance::DecoderCacheRoom room(5000);
        CHECK(mlt_service_cache_get_size(nullptr, cache) == 200);
    }

    SECTION("Les producteurs d'un XML MLT sont comptés, avec une marge")
    {
        const QString xml = QStringLiteral("<mlt><producer id=\"a\"/><chain id=\"b\"/><producer id=\"c\"/><playlist id=\"d\"/></mlt>");
        CHECK(KoraCutPerformance::DecoderCacheRoom::producersIn(xml) == 3 + 16);
        CHECK(KoraCutPerformance::DecoderCacheRoom::producersIn(QString()) == 16);
    }

    mlt_service_cache_set_size(nullptr, cache, before);
}
