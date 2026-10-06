/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "koracutagentinit.h"

#include <QJsonObject>
#include <QHash>
#include <QPointer>
#include <QWidget>

class KoraCutPorte;
class KoraCutTerminalView;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTabBar;
class QTimer;
class QToolButton;

/** @brief Le panneau Terminal (dock `terminal`, ⌃`) : un vrai terminal dans la fenêtre, comme dans
 *  VS Code, ouvert dans le dossier du projet, et le choix de l'agent qui monte (document 18, journal
 *  du 25 septembre 2026).
 *
 * Plusieurs onglets. La page « Choisir l'agent » montre les agents en ligne de commande du catalogue,
 * leur logo, ce qu'il faut pour s'en servir, installé ou pas. Un clic lance l'agent choisi (ou son
 * installation officielle, sous les yeux de la personne, puis l'agent) après avoir écrit dans le
 * dossier du projet ses fichiers d'instructions et sa configuration MCP. Le dernier choix est gardé.
 * Rien ne tourne tant qu'aucun onglet n'est ouvert.
 */
class KoraCutTerminalPanel : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutTerminalPanel(KoraCutPorte *porte, QWidget *parent = nullptr);
    ~KoraCutTerminalPanel() override;

    /** @brief L'onglet en cours s'il y en a un ; sinon le dernier agent choisi s'il est installé ; sinon
     *  la page de choix. */
    void openHere();
    /** @brief Un onglet avec le shell de la personne, dans le dossier du projet. */
    KoraCutTerminalView *newShell();
    /** @brief Écrit les instructions et le MCP de l'agent, puis le lance dans un nouvel onglet. Avec
     *  @p install, lance d'abord son installation officielle s'il manque. Rend false si rien n'a démarré. */
    bool launchAgent(const QString &id, bool install);
    /** @brief « Monter avec Claude Code » : tout en un clic. Vérifie l'installation (propose de l'installer
     *  par la voie officielle, accord demandé dans le terminal), écrit les consignes du projet, pose les
     *  compétences, branche KoraCut et lance Claude Code dans le dossier du projet. Un onglet Claude Code
     *  déjà ouvert est ramené devant. Projet pas enregistré : le dit, propose de l'enregistrer, et reprend
     *  ensuite (resumePending). */
    bool editWithClaudeCode();
    /** @brief « Monter avec un agent » : le dernier agent choisi s'il est installé (son onglet ramené devant s'il
     *  tourne déjà), sinon la page de choix, avec les deux voies. */
    bool editWithAgent();
    /** @brief La page de choix montre aussi la voie « dans le chat, avec ta clé » (version avec agents). */
    void setChatWayAvailable(bool available);
    /** @brief Le dossier mis en tête du PATH du terminal : koracut-ctl, hyperframes, les gardes (npx, ffmpeg, melt). */
    static void writeGuards(const QString &dir);
    /** @brief Relance l'agent resté en attente d'un projet enregistré, s'il y en a un. */
    void resumePending();
    /** @brief La même chose dans le terminal du système (l'option d'avant, et le seul chemin sous Windows
     *  tant que ConPTY n'est pas écrit). */
    bool openInSystemTerminal(const QString &agentId = QString());
    void showChooser();
    void focusTerminal();
    KoraCutTerminalView *currentView() const;
    int tabCount() const;

    /** @brief Les essais sans souris (KORACUT_SHOT_ACTIONS, « terminal:… »). Rend une ligne à écrire. */
    QString debugCommand(const QString &command);

    /** @brief La famille « terminal » de la porte (document 18, 3 octobre 2026) : une IA du dehors ouvre
     *  le terminal, y écrit ses demandes à l'agent qui monte et lit ce qu'il répond. @p tab : vide (l'onglet
     *  de Claude Code s'il tourne, sinon l'onglet devant), « claude », un rang (1 au début) ou un titre. */
    KoraCutTerminalView *porteView(const QString &tab) const;
    /** @brief Les onglets, la page, le message, et l'écran de l'onglet visé ; avec @p history, les
     *  dernières lignes de tout son texte (défilement compris). */
    QJsonObject porteState(const QString &tab, int history) const;
    /** @brief Écrit @p text dans l'onglet visé, comme tapé au clavier, puis Entrée si @p enter. */
    bool porteWrite(const QString &tab, const QString &text, bool enter, QString &error);
    /** @brief Une touche par son nom : entree, echap, tab, maj-tab, haut, bas, gauche, droite, retour, ctrl-c. */
    bool porteKey(const QString &tab, const QString &key, QString &error);

Q_SIGNALS:
    /** @brief « Enregistrer le projet » depuis la page de choix. */
    void saveProjectRequested();
    /** @brief « Dans le chat de KoraCut, avec ta clé » : ouvrir les Connexions IA. */
    void connectModelRequested();
    /** @brief Le menu « Place du terminal » : droite, bas, haut, gauche, agents. */
    void placeRequested(const QString &place);

protected:
    void showEvent(QShowEvent *event) override;

private:
    KoraCutTerminalView *addTerminal(const QString &title, const QIcon &icon);
    bool startScript(KoraCutTerminalView *view, const QString &dir, const QString &body);
    bool prepareProject(QString &dir, KoraCutAgentInit::Context &ctx, QString &message);
    QString ctlLinkDirectory();
    void linkHyperframes(const QString &dir);
    void closeTab(int index);
    void refreshChooser();
    void updateDetails();
    QString selectedAgentId() const;
    void setMessage(const QString &text, bool warning);
    /** @brief Le clic sur le bouton principal de la fiche : lance, installe, ou demande d'abord d'enregistrer le projet. */
    void launchFromButton();
    /** @brief Suit les installations en cours (un fichier d'état par agent, écrit par le script du terminal). */
    void pollInstalls();
    QString installStateFile(const QString &id) const;
    /** @brief Les lignes de script qui installent Node.js par le script livré (accord demandé dans le terminal). */
    QString nodeInstallBody() const;

    KoraCutPorte *m_porte{nullptr};
    QTabBar *m_tabs{nullptr};
    QToolButton *m_newTab{nullptr};
    QToolButton *m_agentButton{nullptr};
    QToolButton *m_systemButton{nullptr};
    QStackedWidget *m_stack{nullptr};
    QWidget *m_chooser{nullptr};
    QLabel *m_message{nullptr};
    QPushButton *m_saveButton{nullptr};
    QListWidget *m_list{nullptr};
    QLabel *m_detailTitle{nullptr};
    QLabel *m_detailText{nullptr};
    QLabel *m_detailCommand{nullptr};
    QLabel *m_detailWarning{nullptr};
    QPushButton *m_launchButton{nullptr};
    QPushButton *m_siteButton{nullptr};
    QPushButton *m_nodeButton{nullptr};
    QToolButton *m_placeButton{nullptr};
    QWidget *m_chatWay{nullptr};
    QTimer *m_installTimer{nullptr};
    QHash<QString, QString> m_installs;      ///< agent en cours d'installation -> son fichier d'état
    QHash<QString, QString> m_installResult; ///< agent -> « ok », « echec », « annule » (dernier essai de la session)
    QPointer<KoraCutTerminalView> m_claudeView;
    QString m_pendingAgent;   ///< l'agent à lancer dès que le projet a un dossier
    QString m_lastSkills;     ///< la dernière phrase sur les compétences (essais)
    QString m_linkDir;
    int m_shellCount{0};
    int m_scriptCount{0};
};
