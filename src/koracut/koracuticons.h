/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

/** @brief Les SVG de KoraCut, rendus à la couleur voulue.
 *
 * Les fichiers de data/koracut/ portent « currentColor » : Qt SVG ne connaît pas ce mot-clé,
 * on le remplace donc par la couleur demandée avant de rendre. Le rendu est fait à la densité
 * de l'écran pour rester net sur Retina.
 */
namespace KoraCutIcons {

/** @brief Rend @p resourcePath à @p size points, en @p color, pour un écran de densité @p dpr. */
QPixmap pixmap(const QString &resourcePath, int size, const QColor &color, qreal dpr = 1.0);

/** @brief Une icône à deux états : @p color normal, @p activeColor sélectionné et survolé. */
QIcon icon(const QString &resourcePath, int size, const QColor &color, const QColor &activeColor, qreal dpr = 1.0);

} // namespace KoraCutIcons
