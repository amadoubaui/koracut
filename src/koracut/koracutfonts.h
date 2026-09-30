/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "koracutmetrics.h"

#include <QFont>
#include <QFontDatabase>
#include <QString>
#include <QStringList>

/**
 * @brief Les polices embarquées : le même texte, aux mêmes largeurs, sur macOS, Windows et Linux.
 *
 * Décidé le 30 septembre 2026 (« 2 - Charte graphique KoraCut.md », Police) : la police du système
 * changeait d'un ordinateur à l'autre (SF Pro, Segoe UI, Noto ou Cantarell), et avec elle la largeur
 * de chaque libellé, donc les alignements. La chasse fixe changeait aussi de taille (Menlo 10 px sur
 * Mac, Consolas 12 px sous Windows, 13 px sous Linux), et la timeline QML calcule des hauteurs
 * à partir d'elle. Trois familles libres (OFL 1.1), embarquées par `uiresources.qrc` :
 * - IBM Plex Sans : toute l'interface ;
 * - DM Mono : timecodes, valeurs chiffrées, règles (la chasse fixe partagée avec la marque) ;
 * - JetBrains Mono NL : terminal, code, journal de la Console (tracés de cadres et blocs, sans ligatures).
 * Les écritures absentes de ces polices (arabe, chinois…) passent par la police du système, glyphe par glyphe.
 * `KORACUT_NO_FONT` garde la police du système, pour comparer.
 */
namespace KoraCutFonts {

inline QString uiFamily()
{
    return QStringLiteral("IBM Plex Sans");
}

inline QString numbersFamily()
{
    return QStringLiteral("DM Mono");
}

inline QString codeFamily()
{
    return QStringLiteral("JetBrains Mono NL");
}

/** Charge les fichiers de `:/koracut/fonts/` une seule fois. Rend faux si la police d'interface manque. */
inline bool registerAll()
{
    static const bool ok = [] {
        const QStringList files = {QStringLiteral("IBMPlexSans-Regular.ttf"),   QStringLiteral("IBMPlexSans-Medium.ttf"),
                                   QStringLiteral("IBMPlexSans-SemiBold.ttf"),  QStringLiteral("IBMPlexSans-Bold.ttf"),
                                   QStringLiteral("IBMPlexSans-Italic.ttf"),    QStringLiteral("DMMono-Regular.ttf"),
                                   QStringLiteral("DMMono-Medium.ttf"),         QStringLiteral("JetBrainsMonoNL-Regular.ttf"),
                                   QStringLiteral("JetBrainsMonoNL-Bold.ttf"),  QStringLiteral("JetBrainsMonoNL-Italic.ttf")};
        for (const QString &file : files) {
            QFontDatabase::addApplicationFont(QStringLiteral(":/koracut/fonts/") + file);
        }
        return QFontDatabase::hasFamily(uiFamily());
    }();
    return ok;
}

/** Vrai quand les polices embarquées sont en service (pas de KORACUT_NO_FONT, fichiers trouvés). */
inline bool active()
{
    return !qEnvironmentVariableIsSet("KORACUT_NO_FONT") && registerAll();
}

/** La chasse fixe des chiffres (timecodes, règles, compteurs), en pixels. */
inline QFont numbers(int pixelSize = KoraCutMetrics::FontMono)
{
    QFont f = active() ? QFont(numbersFamily()) : QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    f.setPixelSize(pixelSize);
    return f;
}

/** La chasse fixe du code et du terminal, en pixels. */
inline QFont code(int pixelSize = KoraCutMetrics::FontDense)
{
    QFont f = active() ? QFont(codeFamily()) : QFontDatabase::systemFont(QFontDatabase::FixedFont);
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    f.setPixelSize(pixelSize);
    return f;
}

} // namespace KoraCutFonts
