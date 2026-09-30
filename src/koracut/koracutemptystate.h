/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QRect>
#include <QString>

class QPainter;
class QPalette;

/** @brief L'état vide, modèle unique de la charte, peint dans le viewport d'une vue.
 *
 * Quatre éléments, dans cet ordre, d'après NN/g (*Designing Empty States in Complex
 * Applications*) : dire l'état du système, donner un indice d'apprentissage, ouvrir un chemin
 * direct vers la tâche qui remplira la zone.
 *
 * 1. pictogramme 32 px à 38 %
 * 2. titre, cran Page (16 / 600)
 * 3. phrase, cran Base (12 / 400) à 62 %
 * 4. bouton secondaire, facultatif
 *
 * Pourquoi une fonction de peinture et pas un widget : un état vide s'affiche dans le viewport
 * d'une `QAbstractItemView`, et y poser un widget par-dessus demande de le montrer et de le cacher
 * au bon moment, donc d'écouter le modèle de chaque vue. Le `paintEvent`, lui, sait déjà si la vue
 * est vide. Le bouton devient cliquable en gardant la zone que `paint()` renvoie et en testant le
 * clic contre elle dans le `mousePressEvent` de la vue.
 *
 * Si la place manque, on ne peint rien plutôt que de rogner : mieux vaut un vide franc qu'un
 * texte coupé.
 */
namespace KoraCutEmptyState {

/** @brief Ce qu'on affiche. Un @p bouton vide veut dire « pas de bouton ». */
struct Modele
{
    QString icone;
    QString titre;
    QString phrase;
    QString bouton;
};

/** @brief Peint l'état vide centré dans @p rect.
 *  @return la zone du bouton, vide s'il n'y en a pas ou si la place manque. */
QRect paint(QPainter &painter, const QRect &rect, const QPalette &palette, const Modele &modele);

} // namespace KoraCutEmptyState
