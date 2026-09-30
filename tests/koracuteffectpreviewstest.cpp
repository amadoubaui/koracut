/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "catch.hpp"
#include "test_utils.hpp"

#include "core.h"
#include "effects/effectsrepository.hpp"
#include "koracut/koracuteffectpreviews.h"

#include <QDir>
#include <QFile>
#include <QElapsedTimer>

namespace {
// Écart moyen entre deux images de même taille, de 0 (identiques) à 255.
double difference(const QImage &a, const QImage &b)
{
    const QImage x = a.convertToFormat(QImage::Format_RGB32);
    const QImage y = b.convertToFormat(QImage::Format_RGB32).scaled(x.size());
    double total = 0;
    for (int j = 0; j < x.height(); ++j) {
        for (int i = 0; i < x.width(); ++i) {
            const QRgb p = x.pixel(i, j);
            const QRgb q = y.pixel(i, j);
            total += std::abs(qRed(p) - qRed(q)) + std::abs(qGreen(p) - qGreen(q)) + std::abs(qBlue(p) - qBlue(q));
        }
    }
    return total / (x.width() * x.height() * 3);
}
}

TEST_CASE("Vignettes des effets", "[KoraCutEffectPreviews]")
{
    auto *previews = KoraCutEffectPreviews::instance();
    // L'image d'exemple : la photo de la famille quand generer.sh en donne une, sinon le paysage dessiné.
    const QString photo = qEnvironmentVariable("KORACUT_APERCUS_EXEMPLE");
    const QImage source = photo.isEmpty() ? KoraCutEffectPreviews::sampleImage() : QImage(photo);
    const QImage sample = source.scaled(KoraCutEffectPreviews::size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const bool customSize = qEnvironmentVariableIsSet("KORACUT_APERCUS_SORTIE");

    SECTION("Des effets connus changent l'image")
    {
        for (const QString &id : {QStringLiteral("sepia"), QStringLiteral("brightness"), QStringLiteral("box_blur"), QStringLiteral("charcoal")}) {
            if (!EffectsRepository::get()->exists(id)) {
                WARN("effet absent de ce MLT : " << id.toStdString());
                continue;
            }
            const QImage image = previews->renderNow(id);
            INFO("effet " << id.toStdString());
            REQUIRE_FALSE(image.isNull());
            if (!customSize) {
                CHECK(image.size() == KoraCutEffectPreviews::size());
            }
            CHECK(difference(image, sample) > 1.5);
        }
        CHECK(previews->renderNow(QStringLiteral("effet-qui-n-existe-pas")).isNull());
    }

    // KORACUT_APERCUS_DOSSIER=/chemin : rend tous les effets vidéo dans ce dossier, pour les relire.
    const QString folder = qEnvironmentVariable("KORACUT_APERCUS_DOSSIER");
    if (!folder.isEmpty()) {
        QDir().mkpath(folder);
        QElapsedTimer timer;
        timer.start();
        int done = 0;
        int failed = 0;
        int unchanged = 0;
        // KORACUT_APERCUS_EXCLURE=/fichier : un identifiant par ligne, jamais rendu (ceux qui ont fait
        // planter MLT lors d'un passage précédent). Une vignette déjà dans le dossier n'est pas refaite :
        // on peut relancer après un plantage.
        QStringList excluded;
        QFile exclusions(qEnvironmentVariable("KORACUT_APERCUS_EXCLURE"));
        if (exclusions.open(QIODevice::ReadOnly | QIODevice::Text)) {
            excluded = QString::fromUtf8(exclusions.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        }
        // KORACUT_APERCUS_SEULEMENT=/fichier : un identifiant par ligne, seuls ces effets sont rendus
        // (les effets d'une famille, sur la photo de cette famille ; 27 septembre 2026).
        QStringList only;
        QFile onlyFile(qEnvironmentVariable("KORACUT_APERCUS_SEULEMENT"));
        const bool restricted = onlyFile.open(QIODevice::ReadOnly | QIODevice::Text);
        if (restricted) {
            only = QString::fromUtf8(onlyFile.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (QString &id : only) {
                id = id.trimmed();
            }
        }
        for (const auto &asset : EffectsRepository::get()->getNames()) {
            const QString id = asset.first;
            if (EffectsRepository::get()->isAudioEffect(id) || excluded.contains(id) || (restricted && !only.contains(id)) ||
                QFile::exists(folder + QLatin1Char('/') + id + QStringLiteral(".png"))) {
                continue;
            }
            fprintf(stderr, "KORACUT-APERCU %s\n", qPrintable(id));
            const QImage image = previews->renderNow(id);
            if (image.isNull()) {
                ++failed;
                continue;
            }
            if (difference(image, sample) < 1.5) {
                ++unchanged;
            }
            image.save(folder + QLatin1Char('/') + id + QStringLiteral(".png"));
            ++done;
        }
        WARN("vignettes : " << done << " rendues, " << failed << " impossibles, " << unchanged << " sans changement visible, en " << timer.elapsed() << " ms");
    }
}
