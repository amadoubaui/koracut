/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QString>
#include <QStringList>

/** @brief Les compétences KoraCut (dossiers `koracut-*` avec un SKILL.md) livrées avec l'application et
 *  posées dans le dossier que lit Claude Code, `~/.claude/skills/` (document 18, journal du 1er octobre 2026).
 *
 * Règles : on ne touche qu'aux dossiers `koracut-*` ; une compétence que la personne a modifiée est
 * gardée telle quelle ; une compétence identique n'est pas recopiée. Un fichier témoin
 * (`.koracut-skills.json`) garde l'empreinte de ce que KoraCut a posé : c'est lui qui dit si un dossier
 * a été retouché depuis. Rien ne tourne en fond : la pose se fait au lancement de Claude Code.
 */
namespace KoraCutSkills {

struct Result
{
    QStringList installed; ///< posées pour la première fois
    QStringList updated;   ///< remplacées par la version de l'application
    QStringList kept;      ///< différentes et modifiées par la personne : gardées
    QStringList removed;   ///< retirées de l'application, jamais retouchées : enlevées
    int unchanged{0};      ///< déjà à jour
    QString error;         ///< vide si tout s'est bien passé
    bool ok() const { return error.isEmpty(); }
    int total() const { return int(installed.size() + updated.size() + kept.size()) + unchanged; }
};

/** @brief Le dossier des compétences livrées avec l'application, vide s'il manque. */
QString bundledRoot();
/** @brief Là où Claude Code lit les compétences de la personne : `~/.claude/skills`. */
QString userRoot();
/** @brief L'empreinte d'un dossier de compétence : noms et contenus de ses fichiers, dans l'ordre. */
QString fingerprint(const QString &dir);
/** @brief Pose les compétences `koracut-*` de @p source dans @p dest selon les règles ci-dessus. */
Result install(const QString &source, const QString &dest);
/** @brief Une phrase courte pour le terminal (« 74 compétences KoraCut à jour, 2 gardées »). */
QString summary(const Result &result);

} // namespace KoraCutSkills
