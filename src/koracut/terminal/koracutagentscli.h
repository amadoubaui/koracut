/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QIcon>
#include <QJsonObject>
#include <QList>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

/** @brief Les agents en ligne de commande que KoraCut sait installer, préparer et lancer dans son
 *  terminal (Claude Code, Codex, Gemini CLI, OpenCode…).
 *
 * Le catalogue est `data/koracut-agents-cli.json`, embarqué avec les logos (`data/icons/agents-cli/`).
 * Rien ne s'installe sans un clic : ce module dit seulement ce qui est là, ce qui manque, et quelles
 * commandes lancer (document 18, journal du 25 septembre 2026).
 */
namespace KoraCutAgentsCli {

struct Install
{
    QString command;       ///< la commande officielle, vide si ce système n'est pas pris en charge
    QString shell;         ///< « powershell » sous Windows quand il le faut
    QString script;        ///< un script livré avec KoraCut (agents/<script>) qui explique, demande l'accord, puis installe
    QStringList requirements;///< node>=18, python>=3.10, brew, git, wsl
};

struct Agent
{
    QString id;
    QString name;
    QString publisher;
    QString logo;          ///< chemin de ressource du logo, vide : pastille avec l'initiale
    bool logoMono{false};  ///< logo d'une seule couleur, à teindre en clair sur fond sombre
    QString color;         ///< couleur de marque (pastille, liseré)
    QString site;
    QString doc;
    QString license;
    QStringList needs;     ///< compte, abonnement, cle, local
    QString needText;
    bool free{false};
    QStringList executables;
    Install install;
    QStringList launch;
    QStringList firstMessage; ///< « {message} » est remplacé ; vide : l'agent ne sait pas le prendre
    bool firstMessageKeepsSession{true};
    QStringList instructions; ///< les fichiers lus au démarrage, relatifs au dossier du projet
    bool readsAgentsMd{false};
    QJsonObject mcp;          ///< format, fichier, ligne, utilisateur
    QString notes;
};

/** @brief Le catalogue, lu une fois. */
const QList<Agent> &all();
const Agent *byId(const QString &id);
/** @brief Le catalogue brut (essais), et le chemin d'où il a été lu. */
QString cataloguePath();

/** @brief Les dossiers où chercher les exécutables : le PATH, puis ceux où les installeurs officiels
 *  posent leurs commandes (~/.local/bin, ~/.npm-global/bin, /opt/homebrew/bin, ~/.bun/bin…). */
QStringList searchPaths();
/** @brief Le chemin complet de @p name dans searchPaths(), vide sinon. */
QString findExecutable(const QString &name);
/** @brief L'exécutable de l'agent, vide s'il n'est pas installé. */
QString locate(const Agent &agent);
/** @brief Node.js et sa version majeure (0 si absent). Lance « node --version » une fois par session. */
QString findNode(int *major = nullptr);
/** @brief Ce qui manque pour installer l'agent (« node>=20 » par exemple), vide si rien. */
QStringList missingRequirements(const Agent &agent);

/** @brief La commande de lancement, avec le premier message si l'agent sait le prendre. */
QStringList launchCommand(const Agent &agent, const QString &firstMessage);
/** @brief Une ligne de shell sûre (guillemets simples) pour ces arguments. */
QString shellLine(const QStringList &args);
QString shellQuote(const QString &s);

/** @brief Le dernier agent choisi (koracutrc, [KoraCut] terminalAgent), vide si aucun. */
QString lastChoice();
void setLastChoice(const QString &id);

/** @brief Le logo prêt à poser (pastille avec l'initiale quand il n'y a pas de logo). */
QIcon icon(const Agent &agent, int size = 24);

/** @brief L'environnement d'un processus lancé dans le terminal : celui de l'application sans ce que
 *  l'environnement de développement y ajoute, TERM, la porte, et @p extraPath devant le PATH. */
QProcessEnvironment terminalEnvironment(const QString &porteFile, const QString &reglage, const QStringList &extraPath);

/** @brief Le shell de la personne ($SHELL, sinon zsh ou bash ; cmd.exe sous Windows). */
QString userShell();

} // namespace KoraCutAgentsCli
