/*
SPDX-FileCopyrightText: 2012 Jean-Baptiste Mardelle <jb@kdenlive.org>
SPDX-FileCopyrightText: 2014 Till Theato <root@ttill.de>
SPDX-FileCopyrightText: 2020 Julius Künzel <julius.kuenzel@kde.org>
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "layouts/layoutswitcher.h"
#include <QObject>
#include <QToolButton>

LayoutSwitcher::LayoutSwitcher(QWidget *parent)
    : QWidget(parent)
    , m_buttonGroup(new QButtonGroup(this))
    , m_layout(new QHBoxLayout)
{
    m_layout->setSpacing(2);
    m_layout->setContentsMargins(0, 0, 0, 0);
    setLayout(m_layout);
    m_buttonGroup->setExclusive(true);
    connect(m_buttonGroup, QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked), this, &LayoutSwitcher::buttonClicked);
}

void LayoutSwitcher::buttonClicked(QAbstractButton *button)
{
    if (button) {
        m_currentLayoutId = button->property("layoutid").toString();
        Q_EMIT layoutSelected(m_currentLayoutId);
    }
}

void LayoutSwitcher::setLayouts(const QList<LayoutTab> &tabs, const QString &currentLayout)
{
    // On efface les anciens onglets
    while (QLayoutItem *item = m_layout->takeAt(0)) {
        if (QWidget *w = item->widget()) {
            m_buttonGroup->removeButton(qobject_cast<QAbstractButton *>(w));
            delete w;
        }
        delete item;
    }
    m_tabs = tabs;
    for (const LayoutTab &tab : tabs) {
        auto *btn = new QToolButton(this);
        btn->setObjectName(QStringLiteral("koracutModuleTab"));
        btn->setProperty("layoutid", tab.id);
        btn->setText(tab.label);
        if (!tab.icon.isNull()) {
            btn->setIcon(tab.icon);
            btn->setIconSize(QSize(16, 16));
        }
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
        btn->setToolTip(tab.tooltip.isEmpty() ? tab.label : tab.tooltip);
        btn->setCheckable(true);
        btn->setAutoRaise(true);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFixedHeight(28);
        m_buttonGroup->addButton(btn);
        m_layout->addWidget(btn);
        if (!currentLayout.isEmpty() && tab.id == currentLayout) {
            btn->setChecked(true);
            m_currentLayoutId = tab.id;
        }
    }
    // On mesure la largeur avec les noms avant de passer éventuellement en icônes seules.
    m_naturalWidth = sizeHint().width();
    applyCompactMode();
}

void LayoutSwitcher::setCompact(bool compact)
{
    if (m_compact == compact) {
        return;
    }
    m_compact = compact;
    applyCompactMode();
}

void LayoutSwitcher::applyCompactMode()
{
    const QList<QAbstractButton *> buttons = m_buttonGroup->buttons();
    for (QAbstractButton *b : buttons) {
        auto *btn = qobject_cast<QToolButton *>(b);
        if (!btn) {
            continue;
        }
        if (btn->icon().isNull()) {
            btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
            continue;
        }
        // KoraCut (22 octobre 2026, KC-011) : le nom seul, sans icône à côté (l'icône et le nom disaient deux fois la
        // même chose) ; l'icône seule quand la fenêtre est trop étroite pour les noms.
        btn->setToolButtonStyle(m_compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextOnly);
    }
}

QString LayoutSwitcher::currentLayout() const
{
    QAbstractButton *checked = m_buttonGroup->checkedButton();
    if (checked) {
        return checked->property("layoutid").toString();
    }
    return QString();
}

void LayoutSwitcher::setCurrentLayout(const QString &layoutId)
{
    const QList<QAbstractButton *> buttons = m_buttonGroup->buttons();
    for (QAbstractButton *btn : buttons) {
        if (btn->property("layoutid").toString() == layoutId) {
            QSignalBlocker blocker(m_buttonGroup);
            btn->setChecked(true);
            m_currentLayoutId = layoutId;
            return;
        }
    }
    // Disposition inconnue (chargée depuis un projet) : plus aucun onglet actif
    if (layoutId.isEmpty()) {
        QSignalBlocker blocker(m_buttonGroup);
        m_buttonGroup->setExclusive(false);
        for (QAbstractButton *btn : buttons) {
            btn->setChecked(false);
        }
        m_buttonGroup->setExclusive(true);
        m_currentLayoutId.clear();
    }
}
