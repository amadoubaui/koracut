/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QPoint>
#include <functional>

class QWidget;
namespace KDDockWidgets {
namespace Core {
class DockWidget;
}
}

/** @brief Agrandir un panneau à toute la zone de travail, puis le remettre à sa place.
 *
 * Demande d'Amadou Ba du 22 septembre 2026 : trois clics rapides sur la barre d'un panneau
 * (ses onglets, ou la barre d'outils de la timeline) l'agrandissent ; trois clics de plus le
 * remettent où il était. Même geste que dans Premiere (touche `) ou DaVinci.
 *
 * Mécanique : on photographie la disposition (LayoutSaver), on ferme les autres panneaux
 * ancrés, et au retour on restaure la photo. Rien n'est perdu, les panneaux flottants ne
 * bougent pas. Changer de module ou de plan de travail oublie la photo.
 */
namespace KoraCutPanelFocus {

/** @brief Agrandit @p dock, ou rétablit la disposition si un panneau est déjà agrandi. */
void toggle(KDDockWidgets::Core::DockWidget *dock);

/** @brief Même chose pour le panneau qui a le focus clavier (action du menu Plan de travail). */
void toggleFocused();

/** @brief Vrai tant qu'un panneau est agrandi. */
bool isActive();

/** @brief Oublie l'état agrandi sans rien restaurer (une autre disposition vient d'être chargée). */
void forget();

/** @brief Le panneau qui contient @p widget, ou nullptr. */
KDDockWidgets::Core::DockWidget *dockContaining(const QWidget *widget);

/** @brief Heure (QDateTime::currentMSecsSinceEpoch) du dernier triple clic repéré, 0 si aucun.
 * Sert à annuler une action de double clic qui n'était que le début d'un triple clic. */
qint64 lastTripleClick();

/** @brief Écoute les triples clics sur @p bar. @p dockAt dit quel panneau vise le clic. */
void watchTripleClick(QWidget *bar, std::function<KDDockWidgets::Core::DockWidget *(const QPoint &)> dockAt);

}
