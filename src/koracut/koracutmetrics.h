/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QSize>
#include <QtGlobal>

/** @brief Les tailles d'icônes de la charte KoraCut, au même endroit pour toute l'application.
 *
 * Charte `2 - Charte graphique KoraCut.md`, section « Icônes » : trait 1,5 sur un viewBox de 24,
 * 16 px par défaut, 14 px dans les barres d'outils, 12 à 13 px dans l'inspecteur.
 *
 * Kdenlive calcule presque partout sa taille d'icône avec `style()->pixelMetric(PM_SmallIconSize)`
 * ou la hauteur de la police. Les deux donnent des valeurs différentes d'un panneau à l'autre, et
 * jusqu'à 22 px dans la barre d'outils de la timeline : c'est ce que le fondateur a vu à l'écran
 * le 20 septembre 2026 (« c'est trop gros et épais, il y a des tailles bizarres »). On passe donc
 * par ces trois constantes partout où une icône de thème est posée.
 *
 * Ce qui n'est PAS concerné : les vignettes du chutier, les aperçus de transitions, de dispositions
 * et de LUT, les étiquettes de couleur et les indicateurs de piste. Ce ne sont pas des icônes de
 * thème, ils gardent leur taille calculée.
 */
namespace KoraCutMetrics {

/** Taille par défaut : listes, dialogues, transport des moniteurs, onglets de modules. */
constexpr int IconDefault = 16;
/** Barres d'outils denses : chutier, timeline, effets, barre d'état, navigateur de médias. */
constexpr int IconToolbar = 14;
/** Inspecteur : pile d'effets, images clés, géométrie. La charte autorise 12 à 13. */
constexpr int IconInspector = 13;
/** Barre latérale de navigation d'une fenêtre à pages (Préférences) : icône au-dessus du libellé.
 *  La charte ne prévoit pas ce cas ; 32 px, le défaut de Qt, écrase les libellés sur trois lignes. */
constexpr int IconNavigation = 22;

inline QSize iconDefault()
{
    return QSize(IconDefault, IconDefault);
}

inline QSize iconToolbar()
{
    return QSize(IconToolbar, IconToolbar);
}

inline QSize iconInspector()
{
    return QSize(IconInspector, IconInspector);
}

/** @brief Borne une taille calculée (hauteur de police, PM_SmallIconSize) aux valeurs de la charte.
 *
 * Garde la logique d'origine de Kdenlive, qui suit la taille du texte, mais empêche le résultat
 * de sortir de la fourchette. Par défaut : entre 13 et 16 px.
 */
inline int clampIcon(int computed, int mini = IconInspector, int maxi = IconDefault)
{
    return qBound(mini, computed, maxi);
}

/** @name Échelle typographique
 *
 * Six crans, la graisse comme second axe (« 11 - Refonte UI-UX », 3.2). Avant la refonte, toute
 * l'interface était à 11 px, c'est-à-dire sous le minimum normatif de Microsoft (12 px en Regular)
 * et au cran « secondaire » d'Apple : d'où la sensation de plat. On garde 11 comme cran dense
 * volontaire pour les listes longues, et la base monte à 12.
 */
///@{
/** Titres de groupe, badges, timecodes de règle. Capitales, 600, interlettrage +0,4. */
constexpr int FontLabel = 10;
/** Listes d'effets, valeurs de l'inspecteur, barre d'état, timeline. */
constexpr int FontDense = 11;
/** Libellés, onglets de panneaux, boutons, noms de pistes, en-têtes de colonnes. */
constexpr int FontBase = 12;
/** Onglets de modules, nom du projet, titres de panneau. 600. */
constexpr int FontTitle = 13;
/** Titres de la page Export, titres d'état vide. 600. */
constexpr int FontPage = 16;
/** Écran d'accueil seulement. 600, interlettrage -0,3. */
constexpr int FontHome = 22;
/** Chasse fixe des timecodes et des règles (DM Mono, `koracutfonts.h`). 10 px : la taille qu'avait
 *  Menlo sur Mac, que la timeline et les moniteurs prennent comme mesure de base. */
constexpr int FontMono = 10;
///@}

/** @name Rayons
 *
 * Quatre valeurs, et pas les neuf de la première charte. Neuf rayons, personne ne les tient en
 * tête, donc personne ne les applique : à l'écran presque rien n'était arrondi.
 */
///@{
/** Tout ce qui pave la fenêtre bord à bord : panneaux, barres, timeline, têtes de pistes. */
constexpr int RadiusNone = 0;
/** Boutons, champs, chips, onglets, sélection d'une ligne, segments d'un contrôle segmenté. */
constexpr int RadiusControl = 4;
/** Cartes et vignettes : préréglages d'export, projets récents, miniatures du chutier. */
constexpr int RadiusCard = 6;
/** Surfaces flottantes : menus, infobulles, popovers, palette de commandes. */
constexpr int RadiusFloating = 8;
/** Clips de la timeline, règle d'Adobe : retombe à zéro sous ClipRadiusFloor de large. */
constexpr int RadiusClip = 3;
/** En dessous de cette largeur, un clip n'est plus arrondi du tout. */
constexpr int ClipRadiusFloor = 24;
///@}

/** @name Grille d'espacement
 *
 * Base 4 px, échelle fermée. Les demi-crans 6, 10 et 14 de la première charte ne servaient qu'à
 * masquer l'absence de règle, ils sont supprimés.
 */
///@{
/** Entre deux boutons d'un même groupe. */
constexpr int SpaceTight = 4;
/** Entre deux groupes de boutons. */
constexpr int SpaceGroup = 8;
/** Entre un contrôle et son libellé, et entre deux zones. */
constexpr int SpaceZone = 12;
/** Entre le bord d'un panneau et son contenu. */
constexpr int SpacePanel = 16;
///@}

/** @name Boutons à menu
 *
 * Le chevron d'un bouton à menu vit dans une gouttière à lui, à droite de l'icône. Avant, Qt le
 * posait dans le coin bas-droit du bouton, c'est-à-dire par-dessus le glyphe : les deux dessins
 * se mélangeaient et rien ne disait qu'un menu allait s'ouvrir. Le NN/g (*Split Buttons*) est net
 * sur les deux points : la petite flèche seule est très souvent lue comme décorative, il lui faut
 * donc de la place et du contraste ; et le filet de séparation est le seul signal qui dit que
 * deux comportements différents vivent dans ce qui ressemble à un seul bouton.
 */
///@{
/** Largeur de la gouttière du chevron. Le NN/g signale que deux cibles minuscules côte à côte
 *  font rater le clic : 18 px laissent une zone confortable, et surtout ils laissent 3 px entre
 *  le bord d'une icône de 14 px centrée et le chevron, donc plus de recouvrement. */
constexpr int MenuGutter = 18;
/** Largeur du chevron lui-même, en pixels. */
constexpr int MenuChevron = 6;
///@}

/** @name Rythme vertical */
///@{
/** Une ligne d'arbre ou de liste. */
constexpr int RowList = 26;
/** Une ligne d'inspecteur. */
constexpr int RowInspector = 24;
/** Un onglet de panneau. */
constexpr int RowTab = 30;
///@}

/** @name Mouvement
 *
 * Deux durées utiles et une courbe. Au-delà de 200 ms, dans un logiciel de montage, la latence
 * perçue coûte plus cher que la confirmation du geste.
 */
///@{
/** Survol, pression, bascule d'un interrupteur. */
constexpr int MotionFast = 80;
/** Sélection, bascule d'onglet, teinte d'une tête de piste. */
constexpr int MotionState = 120;
/** Ouverture d'un panneau, apparition d'un menu, arrivée d'un message. */
constexpr int MotionPanel = 180;
///@}

} // namespace KoraCutMetrics
