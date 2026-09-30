/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutpanelfocus.h"

#include "core.h"
#include "kdenlivesettings.h"
#include "mainwindow.h"

#include <kddockwidgets/LayoutSaver.h>
#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/DockWidget.h>
#include <kddockwidgets/qtcommon/View.h>

#include <KLocalizedString>
#include <QApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMouseEvent>
#include <QPointer>
#include <QTimer>
#include <QWidget>

namespace {

QByteArray s_savedLayout;
bool s_active = false;
qint64 s_lastTripleClick = 0;

QWidget *widgetOf(KDDockWidgets::Core::DockWidget *dock)
{
    return dock ? KDDockWidgets::QtCommon::View_qt::asQWidget(dock->view()) : nullptr;
}

/** Remet les réglages que LayoutManagement réapplique après chaque chargement de disposition. */
void afterRestore()
{
    if (pCore->window()) {
        pCore->window()->setSingleMonitor(KdenliveSettings::singleMonitor());
    }
    if (!KdenliveSettings::showtitlebars()) {
        Q_EMIT pCore->hideBars(true);
    }
}

/** Compte les clics rapides au même endroit. On observe sans jamais consommer : la barre garde
 *  son comportement normal (choisir un onglet, glisser un panneau). */
class TripleClickFilter : public QObject
{
public:
    TripleClickFilter(QWidget *bar, std::function<KDDockWidgets::Core::DockWidget *(const QPoint &)> dockAt)
        : QObject(bar)
        , m_dockAt(std::move(dockAt))
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonDblClick) {
            return QObject::eventFilter(watched, event);
        }
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() != Qt::LeftButton) {
            return false;
        }
        const QPoint pos = mouse->position().toPoint();
        const bool chained = m_clock.isValid() && m_clock.elapsed() <= QApplication::doubleClickInterval() &&
                             (pos - m_lastPos).manhattanLength() <= 2 * QApplication::startDragDistance();
        m_count = chained ? m_count + 1 : 1;
        m_lastPos = pos;
        m_clock.restart();
        if (m_count >= 3) {
            m_count = 0;
            s_lastTripleClick = QDateTime::currentMSecsSinceEpoch();
            QPointer<KDDockWidgets::Core::DockWidget> dock = m_dockAt(pos);
            // Après le clic : la barre termine d'abord ce qu'elle a commencé (un début de glisser).
            QTimer::singleShot(0, qApp, [dock]() {
                if (dock) {
                    KoraCutPanelFocus::toggle(dock);
                } else if (KoraCutPanelFocus::isActive()) {
                    KoraCutPanelFocus::toggle(nullptr);
                }
            });
        }
        return false;
    }

private:
    std::function<KDDockWidgets::Core::DockWidget *(const QPoint &)> m_dockAt;
    QElapsedTimer m_clock;
    QPoint m_lastPos;
    int m_count = 0;
};

} // namespace

namespace KoraCutPanelFocus {

qint64 lastTripleClick()
{
    return s_lastTripleClick;
}

bool isActive()
{
    return s_active;
}

void forget()
{
    s_active = false;
    s_savedLayout.clear();
}

void toggle(KDDockWidgets::Core::DockWidget *dock)
{
    if (s_active) {
        KDDockWidgets::LayoutSaver saver(KDDockWidgets::RestoreOption_RelativeToMainWindow);
        const QByteArray layout = s_savedLayout;
        forget();
        if (!layout.isEmpty() && saver.restoreLayout(layout)) {
            afterRestore();
        }
        pCore->displayMessage(i18nc("@info:status", "Panels restored"), InformationMessage, 1500);
        return;
    }
    if (!dock || dock->isFloating()) {
        return;
    }
    KDDockWidgets::LayoutSaver saver;
    s_savedLayout = saver.serializeLayout();
    s_active = true;
    const auto docks = KDDockWidgets::DockRegistry::self()->dockwidgets();
    for (KDDockWidgets::Core::DockWidget *other : docks) {
        if (other != dock && other->isOpen() && !other->isFloating()) {
            other->forceClose();
        }
    }
    dock->setAsCurrentTab();
    // Fermer des panneaux recrée des groupes : leurs barres de titre reviendraient à l'écran.
    if (!KdenliveSettings::showtitlebars()) {
        Q_EMIT pCore->hideBars(true);
    }
    pCore->displayMessage(i18nc("@info:status", "Panel maximized. Triple-click its bar to restore everything"), InformationMessage, 2000);
}

void toggleFocused()
{
    if (s_active) {
        toggle(nullptr);
        return;
    }
    toggle(dockContaining(QApplication::focusWidget()));
}

KDDockWidgets::Core::DockWidget *dockContaining(const QWidget *widget)
{
    if (!widget) {
        return nullptr;
    }
    const auto docks = KDDockWidgets::DockRegistry::self()->dockwidgets();
    for (KDDockWidgets::Core::DockWidget *dock : docks) {
        QWidget *w = widgetOf(dock);
        if (w && dock->isOpen() && (w == widget || w->isAncestorOf(widget))) {
            return dock;
        }
    }
    return nullptr;
}

void watchTripleClick(QWidget *bar, std::function<KDDockWidgets::Core::DockWidget *(const QPoint &)> dockAt)
{
    if (bar) {
        bar->installEventFilter(new TripleClickFilter(bar, std::move(dockAt)));
    }
}

}
