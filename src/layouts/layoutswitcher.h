/*
SPDX-FileCopyrightText: 2012 Jean-Baptiste Mardelle <jb@kdenlive.org>
SPDX-FileCopyrightText: 2014 Till Theato <root@ttill.de>
SPDX-FileCopyrightText: 2020 Julius Künzel <julius.kuenzel@kde.org>
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <QAbstractButton>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QIcon>
#include <QList>
#include <QPair>
#include <QStringList>
#include <QWidget>

/** @brief Un onglet du sélecteur : identifiant, nom, icône et infobulle. */
struct LayoutTab
{
    QString id;
    QString label;
    QIcon icon;
    QString tooltip;
};

/** @brief Les onglets de modules de la barre du haut.
 *
 * C'est le sélecteur de dispositions de Kdenlive, redessiné en onglets (icône plus nom, 28 px).
 * Le signal et le mécanisme ne changent pas : un onglet reste une disposition.
 */
class LayoutSwitcher : public QWidget
{
    Q_OBJECT
public:
    explicit LayoutSwitcher(QWidget *parent = nullptr);
    void setLayouts(const QList<LayoutTab> &tabs, const QString &currentLayout = QString());
    QString currentLayout() const;
    void setCurrentLayout(const QString &layoutId);
    /** @brief Icônes seules, sans le nom : sous 1280 px de large. */
    void setCompact(bool compact);
    bool isCompact() const { return m_compact; }
    /** @brief Largeur des onglets avec leurs noms, mesurée au moment où ils sont construits. */
    int naturalWidth() const { return m_naturalWidth; }

Q_SIGNALS:
    void layoutSelected(const QString &layoutName);

private:
    QButtonGroup *m_buttonGroup;
    QHBoxLayout *m_layout;
    QString m_currentLayoutId;
    QList<LayoutTab> m_tabs;
    bool m_compact{false};
    int m_naturalWidth{0};

    void buttonClicked(QAbstractButton *button);
    void applyCompactMode();
};
