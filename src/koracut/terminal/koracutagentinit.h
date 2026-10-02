/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "koracutagentscli.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

/** @brief L'« init » automatique : ce que KoraCut écrit dans le dossier du projet pour qu'un agent en
 *  ligne de commande sache, dès son lancement, qu'il est dans un projet de montage et quelle vidéo on
 *  veut monter (document 18, journal du 25 septembre 2026).
 *
 * Les fichiers d'instructions (CLAUDE.md, AGENTS.md, GEMINI.md…) reçoivent un bloc délimité par deux
 * marqueurs ; ce que la personne a écrit autour est gardé tel quel. Les fichiers de configuration MCP
 * (.mcp.json, opencode.json, .codex/config.toml…) reçoivent le seul serveur `koracut`, le reste est
 * gardé ; un fichier illisible n'est jamais touché.
 */
namespace KoraCutAgentInit {

struct Context
{
    QJsonObject project;   ///< le résultat de projet.info
    QJsonArray medias;     ///< le résultat de medias.lister
    QString reglage;       ///< le réglage de l'instance (koracut-ctl --reglage)
    QString porteFile;     ///< porte-<réglage>.json
    QString nodePath;      ///< node pour lancer le serveur MCP, vide : « node »
    QString mcpScript;     ///< Agents in-app/mcp/dist/main.js ou sa copie installée, vide : pas de MCP
    QString mcpWrapper;    ///< un petit script sans espace dans son chemin qui lance le serveur (Goose), vide sinon
    QStringList skills;    ///< « nom|description|chemin du SKILL.md »
};

QString beginMarker();
QString endMarker();

/** @brief Le texte du bloc (sans les marqueurs) pour @p agentName. */
QString instructionsBlock(const Context &ctx, const QString &agentName);

/** @brief Pose @p block entre les marqueurs dans @p file : crée le fichier, remplace l'ancien bloc, ou
 *  l'ajoute à la fin d'un fichier écrit à la main. Rend false avec @p error si rien n'a été écrit. */
bool writeMarkdownBlock(const QString &file, const QString &block, QString *error = nullptr);

/** @brief Le serveur `koracut` dans la configuration MCP de l'agent (format du catalogue). Rend le
 *  chemin écrit, vide si l'agent n'a pas de fichier de projet ou si rien n'a pu l'être. */
QString writeMcpConfig(const QString &projectDir, const KoraCutAgentsCli::Agent &agent, const Context &ctx, QString *error = nullptr);

/** @brief Les arguments à ajouter à la commande de lancement quand le MCP ne se règle qu'en ligne de
 *  commande (Goose, Copilot CLI). */
QStringList mcpLaunchArguments(const KoraCutAgentsCli::Agent &agent, const Context &ctx);

/** @brief Tout ce que l'agent lit au démarrage : ses fichiers d'instructions et sa configuration MCP.
 *  Rend les fichiers écrits ; @p errors reçoit ce qui n'a pas pu l'être. */
QStringList writeForAgent(const QString &projectDir, const KoraCutAgentsCli::Agent &agent, const Context &ctx, QStringList *errors = nullptr);

/** @brief Le serveur MCP livré avec KoraCut (ou celui du dossier de développement), vide s'il manque. */
QString findMcpServer();
/** @brief Les compétences KoraCut trouvées sur cette machine (~/.claude/skills/koracut-*, Sources/koracut-skills). */
QStringList findSkills();

/** @brief Un fichier ou un dossier livré avec l'application sous `agents/` (scripts d'installation,
 *  compétences, serveur MCP), ou sa source dans le dossier de développement ; vide s'il manque. */
QString findData(const QString &relative);
/** @brief Le dossier des compétences `koracut-*` livrées avec l'application, vide s'il manque. */
QString bundledSkillsRoot();
/** @brief Les compétences qu'un agent lit en premier, dans l'ordre. */
QStringList firstSkills();

/** @brief Le premier message envoyé à l'agent, dans la langue de l'interface. */
QString firstMessage();

} // namespace KoraCutAgentInit
