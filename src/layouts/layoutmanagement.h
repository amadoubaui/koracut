/*
SPDX-FileCopyrightText: 2012 Jean-Baptiste Mardelle <jb@kdenlive.org>
SPDX-FileCopyrightText: 2014 Till Theato <root@ttill.de>
SPDX-FileCopyrightText: 2020 Julius Künzel <julius.kuenzel@kde.org>
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "layoutcollection.h"
#include <QMap>
#include <QObject>
#include <QTimer>

class KSelectAction;
class KActionCategory;
class QAction;
class QButtonGroup;
class QAbstractButton;
class QHBoxLayout;
class QLabel;
class LayoutSwitcher;

class LayoutManagement : public QObject
{
    Q_OBJECT

public:
    explicit LayoutManagement(QObject *parent);

public Q_SLOTS:
    /** @brief Loads a layout by LayoutInfo. */
    bool slotLoadLayout(LayoutInfo layout, bool onlyIfNoPrevious = false);
    /** @brief Load a layout by its name. */
    bool slotLoadLayoutById(const QString &layoutId);
    bool slotLoadLayoutFromData(const QString &layoutData, bool onlyIfNoPrevious = false);
    /** @brief Load a layout by its name. */
    void adjustLayoutToDar();
    /** @brief KoraCut : un onglet de module a été cliqué. Charge sa disposition, ou montre la page « en construction ». */
    void slotModuleSelected(const QString &moduleId);
    /** @brief KoraCut : remet le module courant dans sa disposition d'origine, celle livrée avec l'application. */
    void resetWorkspace();
    /** @brief KoraCut : recharge la disposition du module si l'écran demande l'autre variante (compact ou large). */
    void recheckScreen();

private Q_SLOTS:
    /** @brief Saves the widget layout. */
    void slotSaveLayout();
    /** @brief Loads a layout from a QAction containing its ID (for Toolbar buttons). */
    void slotLoadLayoutFromAction(QAction *action);
    /** @brief Manage layout. */
    void slotManageLayouts();
    /** @brief Hide the autosave indicator . */
    void hideAutoSave();
    /** @brief Show the autosave indicator for 2 seconds. */
    void startAutoSave();
    /** @brief Update Layout Switcher container palette on palette/theme change. */
    void slotUpdatePalette();

private:
    /** @brief Populates the "load layout" menu. */
    void initializeLayouts();
    /** @brief Updates the autosave icon with highlight color. */
    void updateAutosaveIcon();
    /** @brief KoraCut : construit le menu du bouton « Plan de travail » de la barre du haut. */
    void buildWorkspaceMenu();
    /** @brief KoraCut : faut-il charger la variante verticale ? (réglage Automatique, Horizontale, Verticale) */
    bool wantsVertical() const;
    QWidget *m_container;
    QWidget *m_autosaveContainer;
    QWidget *m_switcherContainer;
    LayoutSwitcher *m_layoutSwitcher;
    QLabel *m_autosaveLabel;
    QTimer m_autosaveDisplayTimer;
    KSelectAction *m_loadLayout;
    QList<QAction *> m_layoutActions;
    LayoutCollection m_layoutCollection;
    KActionCategory *m_layoutCategory;
    QString m_currentLayoutId;
    bool m_firstLayoutLoaded{false};
    /** @brief KoraCut : la variante chargée en dernier par slotLoadLayout(), pour recheckScreen(). */
    bool m_loadedCompact{false};
    bool m_loadedFromFile{false};
};
