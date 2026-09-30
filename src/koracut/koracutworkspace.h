/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QString>

class MainWindow;
class QWidget;

/** @brief Le plan de travail selon l'écran : compact (portable) ou large (moniteur).
 *
 * Demande URGENTE d'Amadou Ba du 23 septembre 2026. Sur un MacBook 14 ou 16 pouces, la timeline
 * doit prendre toute la largeur en bas, et le mixeur (les réglages des pistes audio) monter dans
 * la rangée du haut, à côté des médias et effets, de l'inspecteur et du moniteur. Sur un grand
 * moniteur, la timeline peut partager le bas avec le mixeur. La personne garde la main : un
 * réglage force l'un ou l'autre, et « Enregistrer ce plan de travail » marche toujours.
 *
 * Les dispositions livrées (data/layouts/montage*.json) sont produites par arrangeEditing() puis
 * LayoutSaver, à travers l'action de développement « disposition:compact » ou « disposition:large »
 * (KORACUT_SHOT_ACTIONS) avec KORACUT_LAYOUT_OUT=<fichier>. On ne les retouche pas à la main.
 */
namespace KoraCutWorkspace {

/** @brief Réglage « auto », « compact » ou « large » (koracutrc, groupe KoraCut). */
QString screenSetting();
void setScreenSetting(const QString &value);

/** @brief Vrai si la variante compacte doit être chargée : réglage forcé, ou écran de moins de 1900 px de large. */
bool wantsCompact(const QWidget *window);

/** @brief Vrai si la disposition enregistrée dans les réglages date d'avant les dispositions actuelles.
 * Au premier lancement après une nouvelle génération de fichiers de disposition, on repart du module
 * plutôt que de l'état enregistré, pour que le changement se voie. */
bool savedLayoutOutdated();
void markSavedLayoutCurrent();

/** @brief Range les panneaux d'un module (montage, etalonnage, audio, vfx) selon la maquette d'Amadou Ba. */
void arrangeModule(MainWindow *window, const QString &module, bool vertical);

/** @brief Range les panneaux du module Montage. Sert à produire les fichiers de disposition. */
void arrangeEditing(MainWindow *window, bool compact, bool vertical);

/** @brief Range en onglets du bloc de droite les panneaux KoraCut restés flottants après un chargement de disposition. */
void dockStrayPanels();

/** @brief Ferme, avant de charger la disposition @p layoutJson, chaque panneau ouvert qu'elle ne connaît pas.
    Sans ça, KDDockWidgets les laisse à leur ancienne place : les panneaux de l'Export et de l'Étalonnage se
    dessinaient par-dessus l'Inspecteur et les onglets du Montage (29 septembre 2026). */
void closeDocksAbsentFrom(const QByteArray &layoutJson);

/** @brief Écrit la disposition courante dans @p path, avec le bloc kdenliveInfo attendu par LayoutCollection. */
bool writeLayout(const QString &path, const QString &id, const QString &displayName, bool vertical, bool compact);

} // namespace KoraCutWorkspace
