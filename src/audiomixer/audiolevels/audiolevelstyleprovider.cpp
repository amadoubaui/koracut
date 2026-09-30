/*
    SPDX-FileCopyrightText: 2025 Kdenlive contributors
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "audiolevelstyleprovider.h"
#include "audiolevelconfig.h"
#include "audiomixer/iecscale.h"
#include <KColorScheme>
#include <QFontDatabase>

AudioLevelStyleProvider &AudioLevelStyleProvider::instance()
{
    static AudioLevelStyleProvider instance;
    return instance;
}

AudioLevelStyleProvider::LevelColors AudioLevelStyleProvider::getLevelsFillColors() const
{
    // KoraCut, 21 septembre 2026 : trois couleurs, celles de la charte, et rien entre les deux.
    // Le vumètre était une barre à dégradé continu vert-jaune-orange-rouge en six teintes, dont
    // aucune n'appartenait à la charte : dessin des années 2000. Trois zones franches se lisent
    // d'un coup d'oeil, ce qui est tout ce qu'on demande à un vumètre pendant un montage.
    LevelColors colors;
    const QColor vert(74, 222, 128);   // #4ADE80
    const QColor jaune(255, 209, 102); // #FFD166
    const QColor rouge(255, 114, 98);  // #FF7262
    colors.darkGreen = vert;
    colors.green = vert;
    colors.yellow = jaune;
    colors.orange = rouge;
    colors.red = rouge;
    colors.darkRed = rouge;

    return colors;
}

QColor AudioLevelStyleProvider::getBorderColor(const QPalette &palette, bool isEnabled) const
{
    KColorScheme scheme(palette.currentColorGroup(), KColorScheme::Window);

    // Get appropriate border color with good contrast
    // Use shadow color for subtle borders that work well with level colors
    QColor shadowColor = scheme.shade(KColorScheme::ShadowShade);
    QColor windowColor = palette.color(QPalette::Window);

    bool isDarkTheme = palette.color(QPalette::Window).lightness() < palette.color(QPalette::WindowText).lightness();

    // Blend shadow color with window background color
    if (isDarkTheme) {
        if (isEnabled) {
            return QColor::fromRgbF(shadowColor.redF() * 0.6 + windowColor.redF() * 0.4, shadowColor.greenF() * 0.6 + windowColor.greenF() * 0.4,
                                    shadowColor.blueF() * 0.6 + windowColor.blueF() * 0.4);
        } else {
            return QColor::fromRgbF(shadowColor.redF() * 0.3 + windowColor.redF() * 0.7, shadowColor.greenF() * 0.3 + windowColor.greenF() * 0.7,
                                    shadowColor.blueF() * 0.3 + windowColor.blueF() * 0.7);
        }
    } else {
        // Light theme: Use stronger borders for better definition
        if (isEnabled) {
            return QColor::fromRgbF(shadowColor.redF() * 0.9 + windowColor.redF() * 0.1, shadowColor.greenF() * 0.9 + windowColor.greenF() * 0.1,
                                    shadowColor.blueF() * 0.9 + windowColor.blueF() * 0.1);
        } else {
            return QColor::fromRgbF(shadowColor.redF() * 0.6 + windowColor.redF() * 0.4, shadowColor.greenF() * 0.6 + windowColor.greenF() * 0.4,
                                    shadowColor.blueF() * 0.6 + windowColor.blueF() * 0.4);
        }
    }
}

QColor AudioLevelStyleProvider::getClippingColor() const
{
    return QColor(255, 114, 98); // KoraCut : #FF7262, le rouge de la charte
}

QColor AudioLevelStyleProvider::getPeakColor(const QPalette &palette, double peakValue) const
{
    if (AudioLevelConfig::instance().peakIndicatorStyle() == AudioLevel::PeakIndicatorStyle::Colorful) {
        LevelColors levelColors = getLevelsFillColors();
        if (peakValue > LevelColors::yellowThreshold) {
            return levelColors.red;
        } else if (peakValue > LevelColors::greenThreshold) {
            return levelColors.yellow;
        } else {
            return levelColors.green;
        }
    } else {
        // Monochrome style - use WindowText color for best contrast
        return palette.color(QPalette::WindowText);
    }
}

QColor AudioLevelStyleProvider::getChannelBackgroundColor(const QPalette &palette) const
{
    bool isDarkTheme = palette.color(QPalette::Window).lightness() < palette.color(QPalette::WindowText).lightness();
    QColor windowColor = palette.color(QPalette::Window);

    if (isDarkTheme) {
        // Make it slightly brighter for dark themes
        return QColor::fromHslF(windowColor.hueF(), windowColor.saturationF(), qMin(windowColor.lightnessF() + 0.04, 1.0));
    } else {
        // Make it slightly darker for light themes
        return QColor::fromHslF(windowColor.hueF(), windowColor.saturationF(), qMax(windowColor.lightnessF() - 0.04, 0.0));
    }
}

QLinearGradient AudioLevelStyleProvider::getLevelsFillGradient(Qt::Orientation orientation, double maxDb) const
{
    auto levelColors = getLevelsFillColors();
    QLinearGradient gradient;

    // KoraCut : trois zones franches, pas un dégradé. Deux arrêts de couleur posés au même
    // endroit (à un millième près) donnent une coupure nette là où Qt ferait un fondu.
    const double versJaune = IEC_ScaleMax(LevelColors::greenThreshold, maxDb);
    const double versRouge = IEC_ScaleMax(LevelColors::yellowThreshold, maxDb);
    constexpr double cheveu = 0.001;
    if (orientation == Qt::Horizontal) {
        gradient = QLinearGradient(0, 0, 1, 0);
        gradient.setColorAt(0., levelColors.green);
        gradient.setColorAt(qMax(0., versJaune - cheveu), levelColors.green);
        gradient.setColorAt(versJaune, levelColors.yellow);
        gradient.setColorAt(qMax(versJaune, versRouge - cheveu), levelColors.yellow);
        gradient.setColorAt(versRouge, levelColors.red);
        gradient.setColorAt(1.0, levelColors.red);
    } else {
        gradient = QLinearGradient(0, 1, 0, 0);
        gradient.setColorAt(0., levelColors.green);
        gradient.setColorAt(qMax(0., versJaune - cheveu), levelColors.green);
        gradient.setColorAt(versJaune, levelColors.yellow);
        gradient.setColorAt(qMax(versJaune, versRouge - cheveu), levelColors.yellow);
        gradient.setColorAt(versRouge, levelColors.red);
        gradient.setColorAt(1.0, levelColors.red);
    }

    return gradient;
}