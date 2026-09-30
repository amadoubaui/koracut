/*
    SPDX-FileCopyrightText: 2025 Kdenlive contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QColor>
#include <QFont>
#include <QPalette>

class AudioLevelStyleProvider
{
public:
    struct LevelColors
    {
        QColor darkGreen;
        QColor green;
        QColor yellow;
        QColor orange;
        QColor red;
        QColor darkRed;

        // KoraCut, 21 septembre 2026 : deux seuils et trois zones (« 12 - Passe montage », § 1.6).
        // Vert jusqu'à -12 dB, jaune jusqu'à -6, rouge au-delà. Les deux seuils intermédiaires
        // hérités (-4 et -2) sont conservés pour ne pas casser le code qui les lit, mais ils
        // pointent sur la même couleur que le rouge.
        static constexpr double greenThreshold = -12.0; // Au-dessus, on passe au jaune
        static constexpr double yellowThreshold = -6.0; // Au-dessus, on passe au rouge
        static constexpr double orangeThreshold = -4.0;
        static constexpr double redThreshold = -2.0;
    };

    static AudioLevelStyleProvider &instance();

    LevelColors getLevelsFillColors() const;
    QColor getBorderColor(const QPalette &palette, bool isEnabled) const;
    QColor getClippingColor() const;
    QColor getPeakColor(const QPalette &palette, double peakValue) const;
    QColor getChannelBackgroundColor(const QPalette &palette) const;
    QLinearGradient getLevelsFillGradient(Qt::Orientation orientation, double maxDb) const;

private:
    AudioLevelStyleProvider() = default; // Private constructor for singleton
};