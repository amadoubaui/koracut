/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QList>
#include <QString>

/** @brief Les modules de KoraCut : ce qui remplace les 5 dispositions de Kdenlive.
 *
 * Un module est une disposition de panneaux plus un mode de travail. Sept modules sont visibles
 * dans la barre du haut ; l'Accueil, lui, est le symbole de gauche. Décisions dans
 * « 9 - UX de l'application.md ».
 */
namespace KoraCutModules {

struct Module
{
    QString id;       ///< identifiant de la disposition (nom du fichier dans data/layouts)
    QString name;     ///< nom affiché dans l'onglet
    QString iconPath; ///< SVG dans les ressources
    int number;       ///< numéro du raccourci : Maj+2 à Maj+8 (Maj+1 est l'Accueil)
    bool ready;       ///< false : le module n'a pas encore de contenu
    int chantier;     ///< numéro du chantier dans TACHES.md, affiché quand le module est en construction
};

/** @brief Les sept modules, dans l'ordre de la barre du haut. */
const QList<Module> &all();

/** @brief Le module d'identifiant @p id, ou nullptr. */
const Module *byId(const QString &id);

/** @brief Vrai si @p id est un module encore vide (Photo, Illustration, Export). */
bool isUnderConstruction(const QString &id);

} // namespace KoraCutModules
