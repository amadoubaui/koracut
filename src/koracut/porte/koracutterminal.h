/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QString>

/** @brief « Ouvrir un terminal ici » : le terminal du système dans le dossier du projet, la porte connue,
 *  koracut-ctl dans le chemin, et Claude Code lancé si la personne l'a.
 *
 * Demande d'Amadou Ba du 23 septembre 2026 à 23 h 42 (document 18). Avant d'ouvrir, KoraCut écrit dans
 * le dossier du projet un CLAUDE.md court (ou KORACUT.md et une ligne d'import si un CLAUDE.md existe
 * déjà) : ce qu'est ce projet, comment parler à KoraCut, les règles, le renvoi aux compétences.
 */
namespace KoraCutTerminal {

/** @brief La commande lancée dans le terminal : le réglage `terminalCommande` (groupe KoraCut), sinon
 *  « claude » si Claude Code est trouvé, sinon rien (le shell seul). */
QString defaultCommand();
/** @brief Le chemin complet de Claude Code, vide s'il n'est pas installé. */
QString findClaude();
/** @brief Le dossier où est koracut-ctl (à côté de koracut), vide s'il n'est pas trouvé. */
QString ctlDirectory();

/** @brief Écrit le bloc KoraCut de CLAUDE.md dans @p projectDir depuis @p projectInfo (le résultat de
 *  projet.info), le texte de la personne gardé autour. Rend le fichier écrit, vide si rien n'a pu l'être. */
QString writeProjectNotes(const QString &projectDir, const QJsonObject &projectInfo);

/** @brief Ouvre le terminal. @p command vide : le shell seul. Rend false avec @p error rempli si rien
 *  ne s'est ouvert. */
bool open(const QString &projectDir, const QString &porteFile, const QString &ctlDir, const QString &command, QString &error);

} // namespace KoraCutTerminal
