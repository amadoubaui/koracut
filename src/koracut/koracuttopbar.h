/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QWidget>

class LayoutSwitcher;
class QAction;
class QLabel;
class QLineEdit;
class QMenu;
class QToolButton;
class AgentsPill;
class ProjectNameLabel;

/** @brief La barre du haut de KoraCut : 40 px, au-dessus de la zone des docks.
 *
 * De gauche à droite : le symbole (clic = Accueil), le chevron du menu principal (masqué sur Mac,
 * où la barre de menus reste native), le nom du projet et son état, les sept onglets de modules au
 * centre, puis la pastille Agents, le champ de recherche et le bouton Disposition.
 * Aucun bouton d'export, aucune bascule de thème : décisions de « 9 - UX de l'application.md ».
 */
class KoraCutTopBar : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutTopBar(QWidget *parent = nullptr);

    /** @brief Les onglets de modules, qui reprennent le sélecteur de dispositions de Kdenlive. */
    LayoutSwitcher *moduleTabs() const { return m_tabs; }

    /** @brief Branche le menu principal (KHamburgerMenu). Ne fait rien sur macOS. */
    void setMainMenuAction(QAction *hamburger);

    /** @brief Le menu du bouton Disposition. */
    void setLayoutMenu(QMenu *menu);

    /** @brief Nom du projet et état (« modifié » ou « enregistré »). */
    void setProject(const QString &name, bool modified, const QString &path = QString());

    /** @brief Le champ de recherche, pour lui donner le focus depuis ⌘K. */
    QLineEdit *searchField() const { return m_search; }

    /** @brief Pose le témoin d'enregistrement automatique à droite de l'état du projet. */
    void setAutoSaveIndicator(QWidget *indicator);

    /** @brief L'état de la pastille Agents (chantier 3). */
    enum class AgentsState { NoService, Ready, Working, Error };
    void setAgentsState(AgentsState state, const QString &tooltip);

Q_SIGNALS:
    /** @brief Clic sur le symbole : l'écran d'accueil n'existe pas encore. */
    void homeRequested();
    /** @brief Texte validé dans le champ de recherche. */
    void searchRequested(const QString &text);
    /** @brief Clic sur la pastille Agents : ouvrir le panneau. */
    void agentsRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void refreshPalette();
    void updateCompactMode();
    void applyAgentsColor();

    QToolButton *m_symbol{nullptr};
    QToolButton *m_menuButton{nullptr};
    ProjectNameLabel *m_projectName{nullptr};
    QLabel *m_projectState{nullptr};
    LayoutSwitcher *m_tabs{nullptr};
    AgentsPill *m_agents{nullptr};
    QLineEdit *m_search{nullptr};
    QToolButton *m_layoutButton{nullptr};
    QWidget *m_leftBlock{nullptr};
    QWidget *m_rightBlock{nullptr};
    bool m_compact{false};
    AgentsState m_agentsState{AgentsState::NoService};
};
