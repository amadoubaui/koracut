/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "catch.hpp"
#include "test_utils.hpp"

#include "monitor/koracutmonitorguides.h"

using namespace KoraCutMonitorGuides;

TEST_CASE("Les repères de plateforme se lisent et restent dans le cadre", "[KoraCutMonitor]")
{
    const QList<Platform> platforms = loadPlatforms();
    REQUIRE(platforms.size() == 8);
    QStringList ids;
    for (const Platform &p : platforms) {
        ids << p.id;
        CHECK_FALSE(p.name.isEmpty());
        CHECK_FALSE(p.keywords.isEmpty());
        CHECK(p.ratio.width() > 0);
        CHECK(p.ratio.height() > 0);
        CHECK_FALSE(p.zones.isEmpty());
        for (const Zone &z : p.zones) {
            CHECK(z.rect.x() >= 0);
            CHECK(z.rect.y() >= 0);
            CHECK(z.rect.right() <= 1.0001);
            CHECK(z.rect.bottom() <= 1.0001);
        }
        CHECK(p.safe.right() <= 1.0001);
        CHECK(p.safe.bottom() <= 1.0001);
    }
    for (const char *id : {"tiktok", "reels", "shorts", "stories", "whatsapp", "youtube", "facebook", "linkedin"}) {
        CHECK(ids.contains(QString::fromLatin1(id)));
    }
    CHECK_FALSE(platform(QStringLiteral("inconnue"), platforms).isValid());
    // L'écran de fin de YouTube n'existe que sur les 20 dernières secondes.
    const Platform yt = platform(QStringLiteral("youtube"), platforms);
    bool endScreen = false;
    for (const Zone &z : yt.zones) {
        if (z.lastSeconds == 20) {
            endScreen = true;
        }
    }
    CHECK(endScreen);
}

TEST_CASE("Le cadre de la plateforme se centre dans un projet d'un autre ratio", "[KoraCutMonitor]")
{
    const QList<Platform> platforms = loadPlatforms();
    const Platform tiktok = platform(QStringLiteral("tiktok"), platforms);
    REQUIRE(tiktok.isValid());
    // Même ratio : tout le cadre.
    CHECK(platformFrame(tiktok, QSize(1080, 1920)) == QRectF(0, 0, 1080, 1920));
    // Projet horizontal : une colonne 9:16 centrée.
    const QRectF inWide = platformFrame(tiktok, QSize(1920, 1080));
    CHECK(inWide.height() == 1080);
    CHECK(inWide.width() == 608);
    CHECK(inWide.x() == 656);
    CHECK(inWide.y() == 0);

    // Les zones en pixels : la colonne de droite de TikTok sur un 1080×1920.
    const QVariantList zones = platformZones(tiktok, QSize(1080, 1920), 25.);
    bool right = false, safe = false, outside = false;
    for (const QVariant &v : zones) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("id")) == QLatin1String("droite")) {
            right = true;
            CHECK(qRound(m.value(QStringLiteral("x")).toDouble()) == 918);
            CHECK(qRound(m.value(QStringLiteral("w")).toDouble()) == 162);
            CHECK(qRound(m.value(QStringLiteral("y")).toDouble()) == 768);
            CHECK(m.value(QStringLiteral("kind")) == QLatin1String("zone"));
        }
        safe = safe || m.value(QStringLiteral("kind")) == QLatin1String("safe");
        outside = outside || m.value(QStringLiteral("kind")) == QLatin1String("outside");
    }
    CHECK(right);
    CHECK(safe);
    CHECK_FALSE(outside);
    // Dans un projet horizontal, ce qui déborde du cadre 9:16 est marqué.
    const QVariantList wideZones = platformZones(tiktok, QSize(1920, 1080), 25.);
    int outsideCount = 0;
    for (const QVariant &v : wideZones) {
        if (v.toMap().value(QStringLiteral("kind")) == QLatin1String("outside")) {
            ++outsideCount;
        }
    }
    CHECK(outsideCount == 2);
    // L'écran de fin de YouTube en images : 20 s à 25 i/s.
    const QVariantList ytZones = platformZones(platform(QStringLiteral("youtube"), platforms), QSize(1920, 1080), 25.);
    bool found = false;
    for (const QVariant &v : ytZones) {
        if (v.toMap().value(QStringLiteral("id")) == QLatin1String("fin")) {
            found = true;
            CHECK(v.toMap().value(QStringLiteral("lastFrames")).toInt() == 500);
        }
    }
    CHECK(found);
}

TEST_CASE("Le type de vidéo propose sa première plateforme", "[KoraCutMonitor]")
{
    const QList<Platform> platforms = loadPlatforms();
    CHECK(platformForVideoType(QStringLiteral("TikTok, Instagram Reels, YouTube Shorts"), platforms) == QLatin1String("tiktok"));
    CHECK(platformForVideoType(QStringLiteral("Instagram Reels, TikTok"), platforms) == QLatin1String("reels"));
    CHECK(platformForVideoType(QStringLiteral("YouTube Shorts, TikTok"), platforms) == QLatin1String("shorts"));
    CHECK(platformForVideoType(QStringLiteral("Statut WhatsApp, stories"), platforms) == QLatin1String("whatsapp"));
    CHECK(platformForVideoType(QStringLiteral("Salles de cinéma, festivals"), platforms).isEmpty());
    CHECK(platformForVideoType(QString(), platforms).isEmpty());
}

TEST_CASE("L'aimantation du cadre : centre, bords, tiers et repères", "[KoraCutMonitor]")
{
    const QList<double> targetsX = axisTargets(1920, {300.});
    const QList<double> targetsY = axisTargets(1080, {});
    REQUIRE(targetsX.size() == 6);
    CHECK(targetsX.at(2) == 960.);
    CHECK(targetsX.last() == 300.);

    // Le centre du cadre à 5 px du centre de l'image : il s'y colle.
    SnapResult r = snapRect(QRectF(855, 400, 200, 100), targetsX, targetsY, 8);
    CHECK(r.rect.x() == 860.);
    CHECK(r.lineX == 960.);
    CHECK(r.rect.width() == 200.);
    // Le bord gauche près d'un repère vertical.
    r = snapRect(QRectF(296, 400, 200, 100), targetsX, targetsY, 8);
    CHECK(r.rect.x() == 300.);
    CHECK(r.lineX == 300.);
    // Le bord droit près du bord de l'image, le haut près du haut de l'image.
    r = snapRect(QRectF(1715, 3, 200, 100), targetsX, targetsY, 8);
    CHECK(r.rect.right() == 1920.);
    CHECK(r.rect.y() == 0.);
    CHECK(r.lineY == 0.);
    // Loin de tout : rien ne bouge, pas de trait.
    r = snapRect(QRectF(500, 400, 200, 100), targetsX, targetsY, 8);
    CHECK(r.rect == QRectF(500, 400, 200, 100));
    CHECK(r.lineX == -1.);
    CHECK(r.lineY == -1.);
}

TEST_CASE("Les règles et le pourcentage de zoom", "[KoraCutMonitor]")
{
    // Au moins 60 px d'écran entre deux chiffres.
    CHECK(rulerStep(1.) == 100.);
    CHECK(rulerStep(0.3) == 200.);
    CHECK(rulerStep(4.) == 20.);
    CHECK(rulerStep(0.02) == 5000.);
    CHECK(rulerStep(0.) == 100.);
    // 100 % = un pixel du projet pour un pixel d'écran ; « Ajuster » (zoom 1) montre l'image entière.
    CHECK(zoomToPercent(1., 960, 1920) == Approx(50.));
    CHECK(zoomToPercent(2., 960, 1920) == Approx(100.));
    CHECK(percentToZoom(100., 960, 1920) == Approx(2.));
    CHECK(percentToZoom(50., 960, 1920) == Approx(1.));
    CHECK(zoomToPercent(1., 0, 1920) == Approx(100.));
    CHECK(zoomSteps().contains(100));
}
