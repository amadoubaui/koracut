/*
    SPDX-FileCopyrightText: 2017 Nicolas Carion
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "assets/model/assetparametermodel.hpp"
#include "audiomixer/mixermanager.hpp"
#include "bin/projectclip.h"
#include "doc/docundostack.hpp"
#include "kdenlivesettings.h"
#include "bin/projectfolder.h"
#include "bin/clipcreator.hpp"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "koracut/koracutpanelfocus.h"
#include "koracut/koracutthemes.h"
#include "mainwindow.h"
#include "monitor/monitor.h"
#include "monitor/monitormanager.h"
#include "monitor/monitorproxy.h"
#include "project/projectmanager.h"
#include "qmltypes/thumbnailprovider.h"
#include "timelinecontroller.h"
#include "timelinetabs.hpp"
#include "timelinewidget.h"

#include <kddockwidgets/core/DockRegistry.h>
#include <kddockwidgets/core/DockWidget.h>

#include <QApplication>
#include <QDateTime>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <KMessageBox>
#include <KXMLGUIFactory>
#include <QInputDialog>
#include <QMenu>
#include <QPainter>
#include <QQuickItem>
#include <QRegularExpression>
#include <QTabBar>
#include <QToolButton>

// KoraCut : l'orientation de chaque séquence, dans une propriété du document (uuid → h ou v).
static const QString kOrientations = QStringLiteral("koracut:orientations");

static QJsonObject orientations()
{
    if (!pCore->currentDoc()) {
        return {};
    }
    return QJsonDocument::fromJson(pCore->currentDoc()->getDocumentProperty(kOrientations).toUtf8()).object();
}

static void saveOrientations(const QJsonObject &map)
{
    pCore->currentDoc()->setDocumentProperty(kOrientations, QString::fromUtf8(QJsonDocument(map).toJson(QJsonDocument::Compact)));
}

// Le rectangle de l'image dans l'onglet : couché pour une séquence horizontale, debout pour une
// verticale. Trait de 1,25 px dans la couleur du texte du thème : plein sur l'onglet choisi (état
// « On » de l'icône), atténué sur les autres (27 septembre 2026, discret comme le reste de la barre).
static QIcon orientationIcon(bool vertical)
{
    static QHash<QString, QIcon> icons;
    const QColor on = KoraCutThemes::token(QStringLiteral("texte"), 200);
    const QColor off = KoraCutThemes::token(QStringLiteral("texte"), 110);
    const QString key = QString::number(vertical) + on.name(QColor::HexArgb) + off.name(QColor::HexArgb);
    auto it = icons.find(key);
    if (it != icons.end()) {
        return it.value();
    }
    QIcon icon;
    for (int scale : {1, 2}) {
        for (const auto &[state, color] : {std::pair{QIcon::On, on}, std::pair{QIcon::Off, off}}) {
            QPixmap pm(QSize(14, 14) * scale);
            pm.setDevicePixelRatio(scale);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(color, 1.25));
            const QRectF r = vertical ? QRectF(4.125, 2.125, 5.75, 9.75) : QRectF(2.125, 4.125, 9.75, 5.75);
            p.drawRoundedRect(r, 1.5, 1.5);
            p.end();
            icon.addPixmap(pm, QIcon::Normal, state);
            icon.addPixmap(pm, QIcon::Active, state);
        }
    }
    icons.insert(key, icon);
    return icon;
}

TimelineContainer::TimelineContainer(QWidget *parent)
    : QWidget(parent)
{
}

QSize TimelineContainer::sizeHint() const
{
    return QSize(800, pCore->window()->height() / 2);
}

TimelineTabs::TimelineTabs(QWidget *parent)
    : QTabWidget(parent)
    , m_activeTimeline(nullptr)
{
    // KoraCut : le nom sert à décrire la découpe des vues (« H[principale,vue-1] »).
    setObjectName(QStringLiteral("principale"));
    // KoraCut, 27 septembre 2026 : la barre des séquences, posée avant tout réglage d'onglets
    // (QTabWidget les applique à la barre qu'il a au moment de l'appel).
    setTabBar(new KoraCutSequenceTabBar(this));
    // KoraCut, 24 septembre 2026 : la barre reste visible même avec une seule séquence, comme
    // Chrome (l'onglet et le « + »), décision du coordinateur pour Amadou Ba.
    setTabBarAutoHide(false);
    setTabsClosable(false);
    setDocumentMode(true);
    setMovable(true);
    // KoraCut, 24 septembre 2026 : des onglets à la Chrome. Le « + » suit le dernier onglet, la
    // croix apparaît au survol, un onglet glissé hors de la barre part dans une vue à côté.
    setupDriver(this, true);
    connect(this, &TimelineTabs::currentChanged, this, &TimelineTabs::connectCurrent);
    connect(this, &TimelineTabs::tabCloseRequested, this, &TimelineTabs::closeTimelineByIndex);
    connect(pCore.get(), &Core::saveTimelinePreview, this, &TimelineTabs::saveTimelinePreview);
    // KoraCut : l'icône d'orientation prend la couleur du texte du thème, on la refait au changement.
    connect(KoraCutThemes::self(), &KoraCutThemes::themeChanged, this, &TimelineTabs::refreshViews);
    // La zone qui porte les vues : un seul enfant (ces onglets) tant qu'on ne découpe pas.
    m_splitHost = new KoraCutTimelineSplitHost();
    m_splitHost->setRoot(this);
}

TimelineTabs::~TimelineTabs()
{
    // clear source
    for (TimelineWidget *timeline : allTimelines()) {
        timeline->setSource(QUrl());
    }
    for (const QPointer<KoraCutTimelinePane> &pane : std::as_const(m_panes)) {
        delete pane.data();
    }
}

// ---- KoraCut : la barre d'onglets d'une vue -----------------------------------------------------

void TimelineTabs::setupDriver(QTabWidget *pane, bool main)
{
    auto *driver = new KoraCutTabBarDriver(pane, main);
    m_drivers << driver;
    driver->setCloseToolTip(main ? i18n("Close Sequence") : i18nc("@info:tooltip", "Back to tabs"));
    if (QToolButton *add = driver->addButton()) {
        add->setToolTip(i18n("Add Timeline Sequence"));
        add->setWhatsThis(
            i18n("Add Timeline Sequence. This will create a new timeline for editing. Each timeline corresponds to a Sequence Clip in the Project Bin"));
        connect(driver, &KoraCutTabBarDriver::addRequested, this, &TimelineTabs::showAddMenu);
    }
    connect(driver, &KoraCutTabBarDriver::closeRequested, this, [this, pane](QWidget *page) {
        auto *timeline = qobject_cast<TimelineWidget *>(page);
        if (timeline == nullptr) {
            return;
        }
        if (pane == this) {
            if (allTimelines().size() > 1) {
                closeTimelineByIndex(indexOf(timeline));
            }
        } else {
            // Fermer une vue côte à côte remet la séquence dans les onglets.
            dockBackSequence(timeline->getUuid());
        }
    });
    connect(driver, &KoraCutTabBarDriver::renamed, this, [](QWidget *page, const QString &name) {
        auto *timeline = qobject_cast<TimelineWidget *>(page);
        if (timeline == nullptr) {
            return;
        }
        const QString id = pCore->projectItemModel()->getSequenceId(timeline->getUuid());
        std::shared_ptr<ProjectClip> clip = pCore->projectItemModel()->getClipByBinID(id);
        if (clip) {
            clip->rename(name, 0);
        }
    });
    connect(driver, &KoraCutTabBarDriver::dragMoved, this, [this](QWidget *page, const QPoint &global) { updateDrop(page, global, false); });
    connect(driver, &KoraCutTabBarDriver::dropped, this, [this](QWidget *page, const QPoint &global) { updateDrop(page, global, true); });
    connect(driver, &KoraCutTabBarDriver::dragCancelled, this, [this]() { m_splitHost->hideDropZone(); });
    driver->setDragOutAllowed([this](QWidget *) { return allTimelines().size() > 1; });
    // Un clic sur l'onglet déjà choisi d'une vue inactive la rend active (sans changement
    // d'onglet, currentChanged ne viendrait pas).
    connect(pane->tabBar(), &QTabBar::tabBarClicked, this, [this, pane](int index) {
        if (index >= 0 && index == pane->currentIndex() && pane->widget(index) != m_activeTimeline) {
            activateTimeline(static_cast<TimelineWidget *>(pane->widget(index)));
        }
    });
    auto *veil = new KoraCutTimelineVeil(pane);
    connect(veil, &KoraCutTimelineVeil::clicked, this, [this, pane]() {
        if (auto *timeline = qobject_cast<TimelineWidget *>(pane->currentWidget())) {
            activateTimeline(timeline);
        }
    });
    // KoraCut, 22 septembre 2026 : trois clics sur l'onglet de séquence agrandissent la timeline
    // (koracutpanelfocus.h). Le double clic, lui, renomme la séquence : il attend donc
    // l'intervalle de double clic du système, et s'efface si un troisième clic est arrivé.
    KoraCutPanelFocus::watchTripleClick(pane->tabBar(), [this](const QPoint &) { return KoraCutPanelFocus::dockContaining(this); });
    connect(pane->tabBar(), &QTabBar::tabBarDoubleClicked, this, [driver](int index) {
        const qint64 asked = QDateTime::currentMSecsSinceEpoch();
        QTimer::singleShot(QApplication::doubleClickInterval(), driver, [driver, index, asked]() {
            if (index >= 0 && KoraCutPanelFocus::lastTripleClick() < asked) {
                driver->startRename(index);
            }
        });
    });
    // KoraCut, 22 septembre 2026 : un menu sur l'onglet (renommer, dupliquer, poser à côté,
    // détacher, fermer).
    pane->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(pane->tabBar(), &QWidget::customContextMenuRequested, this, [this, pane](const QPoint &pos) { showTabMenu(pane, pos); });
}

KoraCutTabBarDriver *TimelineTabs::driverOf(const QTabWidget *pane) const
{
    for (KoraCutTabBarDriver *driver : m_drivers) {
        if (driver->tabs() == pane) {
            return driver;
        }
    }
    return nullptr;
}

QWidget *TimelineTabs::splitHost() const
{
    return m_splitHost;
}

QList<QTabWidget *> TimelineTabs::allPanes() const
{
    QList<QTabWidget *> panes{const_cast<TimelineTabs *>(this)};
    for (const QPointer<KoraCutTimelinePane> &pane : m_panes) {
        if (pane) {
            panes << pane.data();
        }
    }
    return panes;
}

QList<TimelineWidget *> TimelineTabs::allTimelines() const
{
    QList<TimelineWidget *> result;
    for (QTabWidget *pane : allPanes()) {
        for (int i = 0; i < pane->count(); i++) {
            result << static_cast<TimelineWidget *>(pane->widget(i));
        }
    }
    return result;
}

QTabWidget *TimelineTabs::paneOf(const QWidget *timeline) const
{
    if (timeline == nullptr) {
        return nullptr;
    }
    for (QTabWidget *pane : allPanes()) {
        if (pane->indexOf(const_cast<QWidget *>(timeline)) >= 0) {
            return pane;
        }
    }
    return nullptr;
}

QString TimelineTabs::sequenceTabText(const QUuid &uuid) const
{
    for (QTabWidget *pane : allPanes()) {
        for (int i = 0; i < pane->count(); i++) {
            if (static_cast<TimelineWidget *>(pane->widget(i))->getUuid() == uuid) {
                return KLocalizedString::removeAcceleratorMarker(pane->tabText(i));
            }
        }
    }
    return QString();
}

// ---- Les onglets d'origine ---------------------------------------------------------------------

void TimelineTabs::updateWindowTitle()
{
    // Show current timeline name in Window title if we have multiple sequences but only one opened
    if (m_activeTimeline == nullptr || pCore->currentDoc()->closing) {
        return;
    }
    const QList<TimelineWidget *> timelines = allTimelines();
    if (timelines.size() == 1 && pCore->projectItemModel()->sequenceCount() > 1) {
        const QString name = sequenceTabText(timelines.first()->getUuid());
        pCore->window()->setWindowTitle(pCore->currentDoc()->description(name));
        m_activeTimeline->model()->updateVisibleSequenceName(name);
    } else {
        pCore->window()->setWindowTitle(pCore->currentDoc()->description());
        m_activeTimeline->model()->updateVisibleSequenceName(QString());
    }
}

bool TimelineTabs::raiseTimeline(const QUuid &uuid)
{
    for (int i = 0; i < count(); i++) {
        TimelineWidget *timeline = static_cast<TimelineWidget *>(widget(i));
        if (timeline->getUuid() == uuid) {
            if (i != currentIndex()) {
                setCurrentIndex(i);
            } else if (timeline != m_activeTimeline && !m_panes.isEmpty()) {
                // KoraCut : déjà devant, mais une autre vue pilotait le moniteur.
                activateTimeline(timeline);
            }
            return true;
        }
    }
    // KoraCut : la séquence est dans une vue côte à côte.
    return activateSequence(uuid);
}

void TimelineTabs::setModified(const QUuid &uuid, bool modified)
{
    for (QTabWidget *pane : allPanes()) {
        for (int i = 0; i < pane->count(); i++) {
            TimelineWidget *timeline = static_cast<TimelineWidget *>(pane->widget(i));
            if (timeline->getUuid() == uuid) {
                // KoraCut : l'icône de l'onglet dit l'orientation ; « modifié » se lit dans la barre du haut.
                Q_UNUSED(modified)
                pane->setTabIcon(i, orientationIcon(isSequenceVertical(uuid)));
                return;
            }
        }
    }
}

TimelineWidget *TimelineTabs::addTimeline(const QUuid uuid, int ix, const QString &tabName, std::shared_ptr<TimelineItemModel> timelineModel,
                                          MonitorProxy *proxy, bool openInMonitor)
{
    QMutexLocker lk(&m_lock);
    if (allTimelines().size() == 1 && m_activeTimeline) {
        m_activeTimeline->model()->updateVisibleSequenceName(QString());
    }
    disconnect(this, &TimelineTabs::currentChanged, this, &TimelineTabs::connectCurrent);
    TimelineWidget *newTimeline = new TimelineWidget(uuid, this);
    newTimeline->setTimelineMenu(m_timelineClipMenu, m_timelineCompositionMenu, m_timelineMenu, m_guideMenu, m_timelineRulerMenu, m_editGuideAction,
                                 m_headerMenu, m_thumbsMenu, m_timelineSubtitleClipMenu, m_timelineAddClipMenu);
    newTimeline->setModel(timelineModel, proxy);
    int newIndex = 0;
    if (ix == -1 || ix >= count()) {
        newIndex = addTab(newTimeline, tabName);
    } else {
        newIndex = insertTab(ix, newTimeline, tabName);
    }
    setCurrentIndex(newIndex);
    // KoraCut : dans un projet qui a déjà des orientations, une séquence neuve prend celle du moment.
    QJsonObject map = orientations();
    if (!map.isEmpty() && !map.contains(uuid.toString(QUuid::WithoutBraces))) {
        map.insert(uuid.toString(QUuid::WithoutBraces), pCore->isVertical() ? QStringLiteral("v") : QStringLiteral("h"));
        saveOrientations(map);
    }
    lk.unlock();
    doConnectCurrent(newIndex, openInMonitor);
    connect(this, &TimelineTabs::currentChanged, this, &TimelineTabs::connectCurrent);
    refreshViews();
    return newTimeline;
}

void TimelineTabs::connectCurrent(int ix)
{
    if (m_moving) {
        return;
    }
    doConnectCurrent(ix, true);
}

void TimelineTabs::doConnectCurrent(int ix, bool openInMonitor)
{
    activateTimeline((ix >= 0 && ix < count()) ? static_cast<TimelineWidget *>(widget(ix)) : nullptr, openInMonitor);
}

void TimelineTabs::activateTimeline(TimelineWidget *target, bool openInMonitor)
{
    QMutexLocker lk(&m_lock);
    QUuid previousTab = QUuid();
    TimelineWidget *previous = nullptr;
    int restPosition = -1;
    if (m_activeTimeline && m_activeTimeline->model()) {
        previous = m_activeTimeline;
        previousTab = m_activeTimeline->getUuid();
        // KoraCut : la position où la séquence restera figée si elle reste affichée à côté.
        restPosition = pCore->getMonitorPosition();
        pCore->window()->disableMulticam();
        if (openInMonitor && !pCore->currentDoc()->loading) {
            if (pCore->isMediaCapturing()) {
                pCore->switchCapture();
            } else if (pCore->isMediaMonitoring()) {
                pCore->setAudioMonitoring(false);
            }
            int pos = pCore->getMonitorPosition();
            m_activeTimeline->model()->updateDuration();
            std::pair<int, int> durations = m_activeTimeline->model()->durations();
            qDebug() << "::::: GOT SEQUENCES DURATIONS: " << durations;
            if (durations.second > 0) {
                const QString seqName = sequenceTabText(previousTab);
                // A sequence was made shorter, this will resize its instance in other sequences. Warn user
                if (KMessageBox::questionTwoActions(this,
                                                    i18n("The timeline sequence <b>%1</b> was shortened.<br/>Resize all instances in other timelines ?<br>Not "
                                                         "resizing will temporarily keep the current duration in all other sequences.",
                                                         seqName),
                                                    {}, KGuiItem(i18nc("@action:button", "Resize")),
                                                    KGuiItem(i18nc("@action:button", "Don't Resize"))) == KMessageBox::PrimaryAction) {
                    durations.second = 0;
                }
            }
            pCore->bin()->updateSequenceClip(previousTab, durations, pos);
        }
        pCore->window()->disconnectTimeline(m_activeTimeline);
        disconnectTimeline(m_activeTimeline);
    } else {
        qDebug() << "==== NO PREVIOUS TIMELINE";
    }
    if (target == nullptr || pCore->currentDoc()->closing) {
        m_activeTimeline = nullptr;
        qDebug() << "==== ABORTING NO TIMELINE AVAILABLE";
        return;
    }
    if (previous && previous != target) {
        park(previous, true, restPosition);
    }
    m_activeTimeline = target;
    if (m_activeTimeline->model() == nullptr || m_activeTimeline->model()->m_closing) {
        // Closing app
        qDebug() << "++++++++++++\n\nCLOSING APP\n\n+++++++++++++";
        return;
    }
    park(m_activeTimeline, false);
    if (openInMonitor) {
        applyOrientation(m_activeTimeline);
        pCore->window()->connectTimeline();
        connectTimeline(m_activeTimeline);
        updateWindowTitle();
        if (!m_activeTimeline->model()->isLoading) {
            pCore->bin()->sequenceActivated();
            pCore->projectManager()->polishTimelines({m_activeTimeline->getUuid()});
        }
        // Wait a few milliseconds to allow for the qml view to display
        pCore->monitorManager()->projectMonitor()->refreshMonitorTimer.start();
    } else {
        connectTimeline(m_activeTimeline);
    }
    lk.unlock();
    refreshViews();
}

void TimelineTabs::renameTab(const QUuid &uuid, const QString &name)
{
    for (QTabWidget *pane : allPanes()) {
        for (int i = 0; i < pane->count(); i++) {
            if (static_cast<TimelineWidget *>(pane->widget(i))->getUuid() == uuid) {
                pane->tabBar()->setTabText(i, name);
                pCore->projectManager()->setTimelineProperty(uuid, QStringLiteral("kdenlive:clipname"), name);
                updateWindowTitle();
                if (KoraCutTabBarDriver *driver = driverOf(pane)) {
                    driver->refresh();
                }
                return;
            }
        }
    }
}

void TimelineTabs::closeTimelineByIndex(int ix)
{
    if (ix < 0 || ix >= count()) {
        return;
    }
    TimelineWidget *timeline = static_cast<TimelineWidget *>(widget(ix));
    if (timeline == m_activeTimeline) {
        Q_EMIT timeline->model()->requestClearAssetView(-1);
        pCore->clearTimeRemap();
        pCore->mixer()->unsetModel();
        pCore->window()->disableMulticam();
        m_activeTimeline->model()->updateDuration();
        // timeline->controller()->saveSequenceProperties();
    }
    const QString seqName = KLocalizedString::removeAcceleratorMarker(tabText(ix));
    std::shared_ptr<TimelineItemModel> model = timeline->model();
    const QUuid uuid = timeline->getUuid();
    const QString id = pCore->projectItemModel()->getSequenceId(uuid);
    Fun undo = [uuid, id, model]() { return pCore->projectManager()->openTimeline(id, -1, uuid, -1, false, model); };
    Fun redo = [this, uuid]() {
        pCore->projectManager()->closeTimeline(uuid, false, false);
        // KoraCut : la séquence a pu changer de place depuis (glissée dans une vue à côté).
        TimelineWidget *timeline = getTimeline(uuid);
        if (timeline == nullptr) {
            return true;
        }
        if (paneOf(timeline) != this) {
            dockBackSequence(uuid);
        }
        const int index = indexOf(timeline);
        // KoraCut : si une vue à côté pilote le moniteur, fermer un onglet de la vue principale ne
        // doit pas lui voler la main.
        const bool keepActive = m_activeTimeline && m_activeTimeline != timeline && paneOf(m_activeTimeline) != this;
        m_moving = keepActive;
        removeTab(index);
        m_moving = false;
        timeline->blockSignals(true);
        if (timeline == m_activeTimeline) {
            pCore->window()->disconnectTimeline(timeline);
            disconnectTimeline(timeline);
        }
        if (m_activeTimeline == timeline) {
            m_activeTimeline = nullptr;
        }
        delete timeline;
        cleanupPanes();
        if (m_activeTimeline == nullptr && count() > 0 && !pCore->currentDoc()->closing) {
            doConnectCurrent(currentIndex(), true);
        }
        updateWindowTitle();
        refreshViews();
        return true;
    };
    redo();
    pCore->pushUndo(undo, redo, i18n("Close %1", seqName));
}

TimelineWidget *TimelineTabs::getCurrentTimeline() const
{
    return m_activeTimeline;
}

void TimelineTabs::closeTimelineTab(const QUuid uuid, bool checkActiveClosed)
{
    QMutexLocker lk(&m_lock);
    const int currentCount = allTimelines().size();
    bool activeTimelineClosed = false;
    disconnect(this, &TimelineTabs::currentChanged, this, &TimelineTabs::connectCurrent);
    bool closing = pCore->currentDoc()->closing;
    for (QTabWidget *pane : allPanes()) {
        bool found = false;
        for (int i = 0; i < pane->count(); i++) {
            TimelineWidget *timeline = static_cast<TimelineWidget *>(pane->widget(i));
            if (uuid == timeline->getUuid()) {
                // KoraCut : retirer un onglet d'une vue secondaire ne doit pas en activer un autre.
                m_moving = pane != this;
                pane->removeTab(i);
                m_moving = false;
                timeline->blockSignals(true);
                if (timeline == m_activeTimeline) {
                    activeTimelineClosed = true;
                    Q_EMIT showSubtitle(-1);
                    pCore->window()->disconnectTimeline(timeline, closing);
                    disconnectTimeline(timeline);
                    m_activeTimeline = nullptr;
                }
                delete timeline;
                // pCore->projectManager()->closeTimeline(uuid);
                cleanupPanes();
                if (currentCount == 2) {
                    updateWindowTitle();
                }
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
    }
    lk.unlock();
    if (closing) {
        // We are closing document, no need to reconnect
        return;
    }
    connect(this, &TimelineTabs::currentChanged, this, &TimelineTabs::connectCurrent);
    // if the tab is being closed by an undo action,
    // we need to trigger the connection of the remaining tab, as the undo stack won't trigger a currentChanged signal
    if (checkActiveClosed && activeTimelineClosed && count() > 0) {
        connectCurrent(currentIndex());
    }
    refreshViews();
}

void TimelineTabs::connectTimeline(TimelineWidget *timeline)
{
    int position = pCore->currentDoc()->getSequenceProperty(timeline->getUuid(), QStringLiteral("position"), QString::number(0)).toInt();
    pCore->monitorManager()->projectMonitor()->getControllerProxy()->setCursorPosition(position);
    connect(timeline, &TimelineWidget::focusProjectMonitor, pCore->monitorManager(), &MonitorManager::focusProjectMonitor, Qt::DirectConnection);
    connect(this, &TimelineTabs::changeZoom, timeline, &TimelineWidget::slotChangeZoom);
    connect(this, &TimelineTabs::fitZoom, timeline, &TimelineWidget::slotFitZoom);
    connect(timeline->controller(), &TimelineController::showTransitionModel, this, &TimelineTabs::showTransitionModel);
    connect(timeline->controller(), &TimelineController::showMixModel, this, &TimelineTabs::showMixModel);
    connect(timeline->controller(), &TimelineController::updateZoom, this, [&](double value) { Q_EMIT updateZoom(getCurrentTimeline()->zoomForScale(value)); });
    connect(timeline->controller(), &TimelineController::showItemEffectStack, this, &TimelineTabs::showItemEffectStack);
    connect(timeline->controller(), &TimelineController::showSubtitle, this, &TimelineTabs::showSubtitle);
    connect(timeline->controller(), &TimelineController::updateAssetPosition, this, &TimelineTabs::updateAssetPosition);
    connect(timeline->controller(), &TimelineController::centerView, timeline, &TimelineWidget::slotCenterView);

    connect(pCore->monitorManager()->projectMonitor(), &Monitor::zoneUpdated, m_activeTimeline, &TimelineWidget::zoneUpdated);
    connect(pCore->monitorManager()->projectMonitor(), &Monitor::zoneUpdatedWithUndo, m_activeTimeline, &TimelineWidget::zoneUpdatedWithUndo);
    connect(m_activeTimeline, &TimelineWidget::zoneMoved, pCore->monitorManager()->projectMonitor(), &Monitor::slotLoadClipZone);
    connect(pCore->monitorManager()->projectMonitor(), &Monitor::addTimelineEffect, m_activeTimeline->controller(),
            &TimelineController::addEffectToCurrentClip);
    QQmlEngine::setObjectOwnership(pCore->monitorManager()->projectMonitor()->getControllerProxy(), QQmlEngine::CppOwnership);
    Q_EMIT timeline->controller()->selectionChanged();
    timeline->setEnabled(true);
    timeline->setMouseTracking(true);
    timeline->focusTimeline();
}

void TimelineTabs::disconnectTimeline(TimelineWidget *timeline)
{
    timeline->setEnabled(false);
    timeline->setMouseTracking(false);
    disconnect(timeline, &TimelineWidget::focusProjectMonitor, pCore->monitorManager(), &MonitorManager::focusProjectMonitor);
    disconnect(this, &TimelineTabs::changeZoom, timeline, &TimelineWidget::slotChangeZoom);
    disconnect(this, &TimelineTabs::fitZoom, timeline, &TimelineWidget::slotFitZoom);
    disconnect(timeline->controller(), &TimelineController::showTransitionModel, this, &TimelineTabs::showTransitionModel);
    disconnect(timeline->controller(), &TimelineController::showMixModel, this, &TimelineTabs::showMixModel);
    disconnect(timeline->controller(), &TimelineController::showItemEffectStack, this, &TimelineTabs::showItemEffectStack);
    disconnect(timeline->controller(), &TimelineController::showSubtitle, this, &TimelineTabs::showSubtitle);
    disconnect(timeline->controller(), &TimelineController::updateAssetPosition, this, &TimelineTabs::updateAssetPosition);

    disconnect(pCore->monitorManager()->projectMonitor(), &Monitor::zoneUpdated, timeline, &TimelineWidget::zoneUpdated);
    disconnect(pCore->monitorManager()->projectMonitor(), &Monitor::zoneUpdatedWithUndo, timeline, &TimelineWidget::zoneUpdatedWithUndo);
    disconnect(timeline, &TimelineWidget::zoneMoved, pCore->monitorManager()->projectMonitor(), &Monitor::slotLoadClipZone);
    disconnect(pCore->monitorManager()->projectMonitor(), &Monitor::addTimelineEffect, timeline->controller(), &TimelineController::addEffectToCurrentClip);
}

void TimelineTabs::buildClipMenu()
{
    // Timeline clip menu
    if (m_timelineClipMenu) {
        // Timeline clip menu already built
        return;
    }
    m_timelineClipMenu = new QMenu(this);
    KActionCollection *coll = pCore->window()->actionCollection();
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("edit_copy")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("duplicate_timeline_clip")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("paste_effects")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("delete_effects")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("group_clip")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("ungroup_clip")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("edit_item_duration")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("clip_split")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("clip_switch")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("delete_timeline_clip")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("extract_clip")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("replace_timeline_clip")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("save_to_bin")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("send_sequence")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("copy_to_sequence")));

    QMenu *markerMenu = static_cast<QMenu *>(pCore->window()->factory()->container(QStringLiteral("markers"), pCore->window()));
    if (markerMenu) {
        m_timelineClipMenu->addMenu(markerMenu);
    }
    QMenu *alignMenu = new QMenu(i18n("Align to Reference"), this);
    if (alignMenu) {
        m_timelineClipMenu->addMenu(alignMenu);
    }
    alignMenu->addAction(coll->action(QStringLiteral("set_audio_align_ref")));
    alignMenu->addAction(coll->action(QStringLiteral("align_audio")));
    alignMenu->addAction(coll->action(QStringLiteral("set_timecode_ref")));
    alignMenu->addAction(coll->action(QStringLiteral("align_timecode")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("edit_item_speed")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("edit_item_remap")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("clip_in_project_tree")));
    m_timelineClipMenu->addAction(coll->action(QStringLiteral("cut_timeline_clip")));
}

void TimelineTabs::setTimelineMenu(QMenu *compositionMenu, QMenu *timelineMenu, QMenu *guideMenu, QMenu *timelineRulerMenu, QAction *editGuideAction,
                                   QMenu *headerMenu, QMenu *thumbsMenu, QMenu *subtitleClipMenu, QMenu *addClipMenu)
{
    buildClipMenu();
    m_timelineCompositionMenu = compositionMenu;
    m_timelineMenu = timelineMenu;
    m_timelineRulerMenu = timelineRulerMenu;
    m_guideMenu = guideMenu;
    m_headerMenu = headerMenu;
    m_thumbsMenu = thumbsMenu;
    m_headerMenu->addMenu(m_thumbsMenu);
    m_timelineSubtitleClipMenu = subtitleClipMenu;
    m_editGuideAction = editGuideAction;
    m_timelineAddClipMenu = addClipMenu;
}

const QStringList TimelineTabs::openedSequences()
{
    QStringList result;
    for (TimelineWidget *timeline : allTimelines()) {
        result << timeline->getUuid().toString();
    }
    return result;
}

TimelineWidget *TimelineTabs::getTimeline(const QUuid uuid) const
{
    for (TimelineWidget *tl : allTimelines()) {
        if (tl->getUuid() == uuid) {
            return tl;
        }
    }
    return nullptr;
}

void TimelineTabs::slotNextSequence()
{
    // KoraCut : on tourne dans les onglets de la vue qui pilote le moniteur.
    QTabWidget *pane = m_activeTimeline ? paneOf(m_activeTimeline) : this;
    if (pane == nullptr || pane->count() == 0) {
        pane = this;
    }
    int max = pane->count();
    int focus = pane->currentIndex() + 1;
    if (focus >= max) {
        focus = 0;
    }
    pane->setCurrentIndex(focus);
}

void TimelineTabs::slotPreviousSequence()
{
    QTabWidget *pane = m_activeTimeline ? paneOf(m_activeTimeline) : this;
    if (pane == nullptr || pane->count() == 0) {
        pane = this;
    }
    int focus = pane->currentIndex() - 1;
    if (focus < 0) {
        focus = pane->count() - 1;
    }
    pane->setCurrentIndex(focus);
}

void TimelineTabs::showTabMenu(QTabWidget *pane, const QPoint &pos)
{
    const int index = pane->tabBar()->tabAt(pos);
    if (index < 0) {
        return;
    }
    auto *timeline = static_cast<TimelineWidget *>(pane->widget(index));
    if (index != pane->currentIndex()) {
        pane->setCurrentIndex(index);
    } else if (timeline != m_activeTimeline) {
        activateTimeline(timeline);
    }
    const QPointer<TimelineWidget> target = timeline;
    QMenu menu(this);
    QAction *rename = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), i18nc("@action:inmenu sequence tab", "Rename…"));
    QAction *duplicate = menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18nc("@action:inmenu sequence tab", "Duplicate"));
    // L'orientation de la séquence (document 13, § 5 bis).
    menu.addSeparator();
    const bool vertical = isSequenceVertical(timeline->getUuid());
    QAction *horizontal = menu.addAction(orientationIcon(false), i18nc("@action:inmenu sequence tab", "Horizontal"));
    QAction *upright = menu.addAction(orientationIcon(true), i18nc("@action:inmenu sequence tab", "Vertical"));
    horizontal->setCheckable(true);
    upright->setCheckable(true);
    horizontal->setChecked(!vertical);
    upright->setChecked(vertical);
    // Poser à côté : la séquence sort de ses onglets et s'affiche à côté de la vue. Il faut
    // qu'il reste au moins une séquence dans la vue qu'elle quitte.
    QMap<QAction *, KoraCutSplit::Side> places;
    if (pane->count() > 1) {
        menu.addSeparator();
        const QIcon sideBySide = QIcon::fromTheme(QStringLiteral("view-split-left-right"));
        const QIcon stacked = QIcon::fromTheme(QStringLiteral("view-split-top-bottom"));
        places.insert(menu.addAction(sideBySide, i18nc("@action:inmenu sequence tab", "Place on the Right")), KoraCutSplit::Side::Right);
        places.insert(menu.addAction(sideBySide, i18nc("@action:inmenu sequence tab", "Place on the Left")), KoraCutSplit::Side::Left);
        places.insert(menu.addAction(stacked, i18nc("@action:inmenu sequence tab", "Place Above")), KoraCutSplit::Side::Top);
        places.insert(menu.addAction(stacked, i18nc("@action:inmenu sequence tab", "Place Below")), KoraCutSplit::Side::Bottom);
    }
    QAction *back = nullptr;
    if (pane != this) {
        if (places.isEmpty()) {
            menu.addSeparator();
        }
        back = menu.addAction(QIcon::fromTheme(QStringLiteral("view-restore")), i18nc("@action:inmenu sequence tab", "Back to Tabs"));
    }
    menu.addSeparator();
    const bool detachee = pCore->window() && pCore->window()->timelineIsDetached();
    QAction *detacher = menu.addAction(QIcon::fromTheme(QStringLiteral("window-new")), detachee ? i18n("Reattach Sequences") : i18n("Detach This Sequence"));
    QAction *fermer = nullptr;
    if (allTimelines().size() > 1) {
        fermer = menu.addAction(QIcon::fromTheme(QStringLiteral("dialog-close")), i18n("Close Sequence"));
    }
    QAction *chosen = menu.exec(pane->tabBar()->mapToGlobal(pos));
    if (chosen == nullptr || target.isNull()) {
        return;
    }
    if (chosen == rename) {
        if (KoraCutTabBarDriver *driver = driverOf(pane)) {
            driver->startRename(pane->indexOf(target));
        }
    } else if (chosen == duplicate) {
        duplicateSequence(target);
    } else if (chosen == horizontal || chosen == upright) {
        if ((chosen == upright) != vertical) {
            setSequenceOrientation(target->getUuid(), chosen == upright);
        }
    } else if (places.contains(chosen)) {
        moveTimeline(target, pane, places.value(chosen));
    } else if (chosen == back) {
        dockBackSequence(target->getUuid());
    } else if (chosen == detacher) {
        if (pCore->window()) {
            pCore->window()->slotDetachTimeline();
        }
    } else if (chosen == fermer) {
        if (paneOf(target) != this) {
            dockBackSequence(target->getUuid());
        }
        closeTimelineByIndex(indexOf(target));
    }
}

void TimelineTabs::onTabBarDoubleClicked(int index)
{
    if (index == -1) {
        // No action when double clicking in empty space
        return;
    }
    // KoraCut : on renomme sur place, dans l'onglet, comme dans Chrome.
    if (KoraCutTabBarDriver *driver = driverOf(this)) {
        driver->startRename(index);
    }
}

void TimelineTabs::saveTimelinePreview(const QString &path)
{
    int imageWidth = 600;
    int imageHeight = size().height() * imageWidth / size().width();
    QPixmap bitmap(size());
    QPainter painter(&bitmap);
    render(&painter);
    painter.end();
    QPixmap scaled = bitmap.scaled(imageWidth, imageHeight);
    scaled.save(path);
}

// ---- KoraCut : les vues côte à côte -------------------------------------------------------------

void TimelineTabs::duplicateSequence(TimelineWidget *timeline)
{
    // Comme « Dupliquer le clip » du chutier, sur le clip de la séquence.
    const QString id = pCore->projectItemModel()->getSequenceId(timeline->getUuid());
    if (id.isEmpty()) {
        return;
    }
    pCore->selectBinClip(id, false);
    pCore->triggerAction(QStringLiteral("duplicate_clip"));
}

void TimelineTabs::park(TimelineWidget *timeline, bool parked, int position)
{
    // Une séquence affichée mais inactive garde sa tête de lecture là où on l'a laissée, au lieu
    // de suivre le moniteur (qui lit une autre séquence) : rien ne bouge, rien ne se redessine.
    // Voir koraCutParked dans Timeline.qml.
    if (timeline == nullptr || timeline->rootObject() == nullptr) {
        return;
    }
    if (parked) {
        timeline->rootObject()->setProperty("koraCutParkedPosition", position);
    }
    timeline->rootObject()->setProperty("koraCutParked", parked);
}

KoraCutTimelinePane *TimelineTabs::newPane()
{
    auto *pane = new KoraCutTimelinePane();
    pane->setObjectName(QStringLiteral("vue-%1").arg(++m_paneSerial));
    setupDriver(pane, false);
    connect(pane, &QTabWidget::currentChanged, this, [this, pane](int index) {
        if (m_moving || index < 0) {
            return;
        }
        activateTimeline(static_cast<TimelineWidget *>(pane->widget(index)));
    });
    m_panes << pane;
    return pane;
}

void TimelineTabs::cleanupPanes()
{
    const bool wasMoving = m_moving;
    m_moving = true;
    // La vue principale ne reste jamais vide tant qu'il y a des séquences : la première vue
    // secondaire y revient.
    if (count() == 0) {
        for (const QPointer<KoraCutTimelinePane> &pane : std::as_const(m_panes)) {
            if (pane && pane->count() > 0) {
                QWidget *current = pane->currentWidget();
                while (pane->count() > 0) {
                    QWidget *page = pane->widget(0);
                    const QString text = pane->tabText(0);
                    const QIcon icon = pane->tabIcon(0);
                    pane->removeTab(0);
                    addTab(page, icon, text);
                }
                setCurrentWidget(current);
                break;
            }
        }
    }
    const QList<QPointer<KoraCutTimelinePane>> panes = m_panes;
    for (const QPointer<KoraCutTimelinePane> &pane : panes) {
        if (pane && pane->count() == 0) {
            m_drivers.removeAll(driverOf(pane));
            m_splitHost->remove(pane);
            m_panes.removeAll(pane);
            pane->deleteLater();
        }
    }
    m_panes.removeAll(nullptr);
    m_moving = wasMoving;
}

bool TimelineTabs::moveTimeline(TimelineWidget *timeline, QTabWidget *target, KoraCutSplit::Side side, QString *error)
{
    using KoraCutSplit::Side;
    const auto fail = [error](const QString &why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    QTabWidget *source = paneOf(timeline);
    if (source == nullptr) {
        return fail(QStringLiteral("sequence_fermee"));
    }
    if (side == Side::Center && target == nullptr) {
        target = this;
    }
    if (side == Side::Center) {
        if (target == source) {
            return fail(QStringLiteral("deja_la"));
        }
    } else if (allTimelines().size() < 2) {
        return fail(QStringLiteral("une_seule_sequence"));
    } else if ((target == source || (target == nullptr && m_panes.isEmpty())) && source->count() == 1) {
        // Rien à découper : la séquence est seule dans sa vue.
        return fail(QStringLiteral("seule_dans_sa_vue"));
    }
    const int from = source->indexOf(timeline);
    const QString text = source->tabText(from);
    const QIcon icon = source->tabIcon(from);
    m_moving = true;
    source->removeTab(from);
    if (side == Side::Center) {
        target->setCurrentIndex(target->addTab(timeline, icon, text));
    } else {
        KoraCutTimelinePane *pane = newPane();
        pane->addTab(timeline, icon, text);
        m_splitHost->place(pane, target, side);
    }
    timeline->show();
    cleanupPanes();
    m_moving = false;
    if (timeline != m_activeTimeline) {
        activateTimeline(timeline);
    }
    refreshViews();
    return true;
}

void TimelineTabs::refreshViews()
{
    if (pCore->currentDoc() && pCore->currentDoc()->closing) {
        return;
    }
    const bool split = !m_panes.isEmpty();
    const int total = allTimelines().size();
    QTabWidget *activePane = paneOf(m_activeTimeline);
    for (QTabWidget *pane : allPanes()) {
        for (int i = 0; i < pane->count(); i++) {
            const QUuid uuid = static_cast<TimelineWidget *>(pane->widget(i))->getUuid();
            const bool vertical = isSequenceVertical(uuid);
            pane->setTabIcon(i, orientationIcon(vertical));
            const QString name = KLocalizedString::removeAcceleratorMarker(pane->tabText(i));
            pane->setTabToolTip(i, vertical ? i18nc("@info:tooltip %1 sequence name", "%1, vertical sequence", name)
                                            : i18nc("@info:tooltip %1 sequence name", "%1, horizontal sequence", name));
        }
        if (KoraCutTabBarDriver *driver = driverOf(pane)) {
            driver->setActivePane(!split || pane == activePane);
            driver->setClosable(pane != this || total > 1);
            driver->setTotal(total);
            driver->refresh();
        }
        if (auto *veil = pane->findChild<KoraCutTimelineVeil *>(QString(), Qt::FindDirectChildrenOnly)) {
            veil->setActive(split && pane->currentWidget() != nullptr && pane->currentWidget() != m_activeTimeline);
        }
    }
}

void TimelineTabs::updateDrop(QWidget *page, const QPoint &globalPos, bool drop)
{
    using KoraCutSplit::Side;
    auto *timeline = qobject_cast<TimelineWidget *>(page);
    if (timeline == nullptr) {
        m_splitHost->hideDropZone();
        return;
    }
    const QPoint point = m_splitHost->mapFromGlobal(globalPos);
    const QRect all = m_splitHost->rect();
    QTabWidget *target = nullptr;
    Side side = Side::Center;
    QRect zone;
    bool found = false;
    if (all.contains(point)) {
        // Tout contre un bord de la zone : la séquence prend ce côté de toute la zone.
        const int edge = 16;
        if (point.x() < all.left() + edge) {
            side = Side::Left;
        } else if (point.x() > all.right() - edge) {
            side = Side::Right;
        } else if (point.y() > all.bottom() - edge) {
            side = Side::Bottom;
        }
        if (side != Side::Center) {
            zone = KoraCutSplit::zoneRect(all, side);
            found = true;
        } else {
            for (QTabWidget *pane : allPanes()) {
                const QRect r(pane->mapTo(m_splitHost, QPoint(0, 0)), pane->size());
                if (!pane->isVisible() || !r.contains(point)) {
                    continue;
                }
                const int header = pane->tabBar()->isVisible() ? pane->tabBar()->geometry().bottom() + 1 : 0;
                side = KoraCutSplit::sideAt(r, point, header);
                zone = side == Side::Center ? r : KoraCutSplit::zoneRect(r.adjusted(0, header, 0, 0), side);
                target = pane;
                found = true;
                break;
            }
        }
    }
    // Les mêmes règles que moveTimeline, pour ne montrer que ce qui se fera.
    QTabWidget *source = paneOf(timeline);
    bool valid = found && source;
    if (valid) {
        if (side == Side::Center) {
            valid = target != nullptr && target != source;
        } else if ((target == source || (target == nullptr && m_panes.isEmpty())) && source->count() == 1) {
            valid = false;
        }
    }
    if (!drop) {
        m_splitHost->showDropZone(zone, sequenceTabText(timeline->getUuid()), point, valid);
        return;
    }
    m_splitHost->hideDropZone();
    if (valid) {
        moveTimeline(timeline, target, side);
    }
}

bool TimelineTabs::placeSequence(const QUuid &uuid, const QUuid &targetUuid, KoraCutSplit::Side side, QString *error)
{
    TimelineWidget *timeline = getTimeline(uuid);
    if (timeline == nullptr) {
        if (error) {
            *error = QStringLiteral("sequence_fermee");
        }
        return false;
    }
    QTabWidget *target = nullptr;
    TimelineWidget *anchor = nullptr;
    if (!targetUuid.isNull()) {
        anchor = getTimeline(targetUuid);
        if (anchor == nullptr || anchor == timeline) {
            if (error) {
                *error = anchor ? QStringLiteral("meme_sequence") : QStringLiteral("cible_fermee");
            }
            return false;
        }
        target = paneOf(anchor);
    }
    if (!moveTimeline(timeline, target, side, error)) {
        return false;
    }
    // « À droite de A » : A doit être celle qu'on voit dans sa vue.
    if (anchor && side != KoraCutSplit::Side::Center) {
        if (QTabWidget *pane = paneOf(anchor)) {
            m_moving = true;
            pane->setCurrentWidget(anchor);
            m_moving = false;
        }
        refreshViews();
    }
    return true;
}

bool TimelineTabs::dockBackSequence(const QUuid &uuid)
{
    TimelineWidget *timeline = getTimeline(uuid);
    if (timeline == nullptr) {
        return false;
    }
    if (paneOf(timeline) == this) {
        return true;
    }
    return moveTimeline(timeline, this, KoraCutSplit::Side::Center);
}

bool TimelineTabs::activateSequence(const QUuid &uuid)
{
    TimelineWidget *timeline = getTimeline(uuid);
    QTabWidget *pane = paneOf(timeline);
    if (pane == nullptr) {
        return false;
    }
    m_moving = true;
    pane->setCurrentWidget(timeline);
    m_moving = false;
    if (timeline != m_activeTimeline) {
        activateTimeline(timeline);
    }
    refreshViews();
    return true;
}

QJsonArray TimelineTabs::views() const
{
    QJsonArray list;
    int paneIndex = 0;
    for (QTabWidget *pane : allPanes()) {
        for (int i = 0; i < pane->count(); i++) {
            auto *timeline = static_cast<TimelineWidget *>(pane->widget(i));
            QJsonObject entry{{QStringLiteral("sequence"), timeline->getUuid().toString(QUuid::WithoutBraces)},
                              {QStringLiteral("nom"), KLocalizedString::removeAcceleratorMarker(pane->tabText(i))},
                              {QStringLiteral("vue"), pane->objectName()},
                              {QStringLiteral("vue_index"), paneIndex},
                              {QStringLiteral("onglet"), i},
                              {QStringLiteral("affichee"), pane->currentIndex() == i},
                              {QStringLiteral("active"), timeline == m_activeTimeline},
                              {QStringLiteral("orientation"), isSequenceVertical(timeline->getUuid()) ? QStringLiteral("verticale") : QStringLiteral("horizontale")}};
            if (pane->currentIndex() == i && m_splitHost->isVisible()) {
                const QPoint at = timeline->mapTo(m_splitHost, QPoint(0, 0));
                entry.insert(QStringLiteral("place"), QJsonObject{{QStringLiteral("x"), at.x()},
                                                                  {QStringLiteral("y"), at.y()},
                                                                  {QStringLiteral("largeur"), timeline->width()},
                                                                  {QStringLiteral("hauteur"), timeline->height()}});
            }
            list.append(entry);
        }
        ++paneIndex;
    }
    return list;
}

bool TimelineTabs::isSequenceVertical(const QUuid &uuid) const
{
    const QString o = orientations().value(uuid.toString(QUuid::WithoutBraces)).toString();
    return o.isEmpty() ? pCore->isVertical() : o == QLatin1String("v");
}

void TimelineTabs::setSequenceOrientation(const QUuid &uuid, bool vertical)
{
    if (!pCore->currentDoc()) {
        return;
    }
    // La première fois : chaque séquence reçoit l'orientation qu'elle a aujourd'hui (celle du
    // projet), pour qu'aucune ne change en passant d'un onglet à l'autre.
    QJsonObject map = orientations();
    if (map.isEmpty()) {
        const QString now = pCore->isVertical() ? QStringLiteral("v") : QStringLiteral("h");
        const QMap<QUuid, QString> sequences = pCore->projectItemModel()->getAllSequenceClips();
        for (auto it = sequences.cbegin(); it != sequences.cend(); ++it) {
            map.insert(it.key().toString(QUuid::WithoutBraces), now);
        }
    }
    map.insert(uuid.toString(QUuid::WithoutBraces), vertical ? QStringLiteral("v") : QStringLiteral("h"));
    saveOrientations(map);
    pCore->currentDoc()->setModified(true);
    if (m_activeTimeline && m_activeTimeline->getUuid() == uuid) {
        applyOrientation(m_activeTimeline);
        pCore->window()->connectTimeline();
        pCore->monitorManager()->projectMonitor()->refreshMonitorTimer.start();
    }
    refreshViews();
}

void TimelineTabs::applyOrientation(TimelineWidget *timeline)
{
    if (timeline == nullptr) {
        return;
    }
    const QString o = orientations().value(timeline->getUuid().toString(QUuid::WithoutBraces)).toString();
    if (!o.isEmpty() && pCore->koraCutSetVertical(o == QLatin1String("v"))) {
        // La disposition vient de changer (vertical ou horizontal) : le moniteur Programme devant,
        // sinon la colonne du moniteur montre la Source, vide.
        const auto raiseProgram = []() {
            if (auto *dock = KDDockWidgets::DockRegistry::self()->dockByName(QStringLiteral("projectmonitor"))) {
                dock->setAsCurrentTab();
            }
        };
        raiseProgram();
        QTimer::singleShot(0, this, raiseProgram);
    }
}

QUuid TimelineTabs::createSequence(bool vertical)
{
    auto model = pCore->projectItemModel();
    if (!model || !pCore->currentDoc()) {
        return {};
    }
    // Comme projet.sequence creer (porte de commande) : nom libre suivant, pistes des réglages,
    // dossier des séquences s'il existe, une étape d'annulation.
    const QStringList names = pCore->currentDoc()->getSequenceNames();
    int n = pCore->projectManager()->getTimelinesCount() + 1;
    QString name = i18n("Sequence %1", n);
    while (names.contains(name)) {
        name = i18n("Sequence %1", ++n);
    }
    QString parent = model->getRootFolder()->clipId();
    if (model->defaultSequencesFolder() > -1) {
        if (auto folder = model->getFolderByBinId(QString::number(model->defaultSequencesFolder()))) {
            parent = folder->clipId();
        }
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    const QString binId = ClipCreator::createPlaylistClipWithUndo(name, {KdenliveSettings::audiotracks(), KdenliveSettings::videotracks()}, parent, model, undo, redo);
    if (binId.isEmpty() || binId == QLatin1String("-1")) {
        undo();
        return {};
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Create sequence %1", name));
    auto clip = model->getClipByBinID(binId);
    const QUuid uuid = clip ? clip->getSequenceUuid() : QUuid();
    if (!uuid.isNull()) {
        if (!getTimeline(uuid)) {
            pCore->projectManager()->openTimeline(binId, -1, uuid);
        }
        setSequenceOrientation(uuid, vertical);
        raiseTimeline(uuid);
    }
    return uuid;
}

void TimelineTabs::showAddMenu()
{
    // Le « + » : horizontale ou verticale, à la taille du projet (1920 × 1080 ou 1080 × 1920).
    const int big = std::max(pCore->getProjectProfile().width(), pCore->getProjectProfile().height());
    const int small = std::min(pCore->getProjectProfile().width(), pCore->getProjectProfile().height());
    QMenu menu(this);
    QAction *h = menu.addAction(orientationIcon(false), i18nc("@action:inmenu %1 width, %2 height", "Horizontal Sequence (%1 × %2)", big, small));
    QAction *v = menu.addAction(orientationIcon(true), i18nc("@action:inmenu %1 width, %2 height", "Vertical Sequence (%1 × %2)", small, big));
    KoraCutTabBarDriver *driver = driverOf(this);
    QWidget *anchor = driver && driver->addButton() ? static_cast<QWidget *>(driver->addButton()) : static_cast<QWidget *>(tabBar());
    QAction *chosen = menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
    if (chosen == h || chosen == v) {
        createSequence(chosen == v);
    }
}

QString TimelineTabs::layoutState() const
{
    if (m_panes.isEmpty()) {
        return QString();
    }
    const QJsonObject root = m_splitHost->serialize([this](QWidget *w) {
        QJsonObject leaf;
        auto *pane = qobject_cast<QTabWidget *>(w);
        if (pane == nullptr) {
            return leaf;
        }
        QJsonArray sequences;
        for (int i = 0; i < pane->count(); i++) {
            sequences.append(static_cast<TimelineWidget *>(pane->widget(i))->getUuid().toString(QUuid::WithoutBraces));
        }
        leaf.insert(QStringLiteral("sequences"), sequences);
        if (auto *current = qobject_cast<TimelineWidget *>(pane->currentWidget())) {
            leaf.insert(QStringLiteral("affichee"), current->getUuid().toString(QUuid::WithoutBraces));
        }
        if (pane == this) {
            leaf.insert(QStringLiteral("principale"), true);
        }
        return leaf;
    });
    return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("format"), 1}, {QStringLiteral("decoupe"), root}}).toJson(QJsonDocument::Compact));
}

bool TimelineTabs::restoreLayoutState(const QString &state)
{
    const QJsonObject doc = QJsonDocument::fromJson(state.toUtf8()).object();
    if (doc.value(QStringLiteral("format")).toInt() != 1 || !m_panes.isEmpty()) {
        return false;
    }
    // Une seule vue principale, sinon on reste en onglets.
    int principal = 0;
    std::function<void(const QJsonObject &)> count = [&](const QJsonObject &node) {
        principal += node.value(QStringLiteral("principale")).toBool() ? 1 : 0;
        for (const QJsonValue &v : node.value(QStringLiteral("vues")).toArray()) {
            count(v.toObject());
        }
    };
    count(doc.value(QStringLiteral("decoupe")).toObject());
    if (principal != 1) {
        return false;
    }
    QList<QPair<QTabWidget *, QUuid>> shown;
    m_moving = true;
    const bool done = m_splitHost->restore(doc.value(QStringLiteral("decoupe")).toObject(), [this, &shown](const QJsonObject &leaf) -> QWidget * {
        QTabWidget *pane = this;
        if (!leaf.value(QStringLiteral("principale")).toBool()) {
            KoraCutTimelinePane *p = newPane();
            for (const QJsonValue &v : leaf.value(QStringLiteral("sequences")).toArray()) {
                TimelineWidget *timeline = getTimeline(QUuid(v.toString()));
                // Une séquence qui n'est plus ouverte, ou déjà placée, est ignorée.
                if (timeline == nullptr || paneOf(timeline) != this) {
                    continue;
                }
                const int from = indexOf(timeline);
                const QString text = tabText(from);
                const QIcon icon = tabIcon(from);
                removeTab(from);
                p->addTab(timeline, icon, text);
            }
            if (p->count() == 0) {
                m_panes.removeAll(p);
                m_drivers.removeAll(driverOf(p));
                p->deleteLater();
                return nullptr;
            }
            pane = p;
        }
        shown.append({pane, QUuid(leaf.value(QStringLiteral("affichee")).toString())});
        return pane;
    });
    for (const auto &[pane, uuid] : std::as_const(shown)) {
        if (TimelineWidget *timeline = getTimeline(uuid); timeline && paneOf(timeline) == pane) {
            pane->setCurrentWidget(timeline);
        }
    }
    if (!done) {
        // Découpe illisible : tout revient en onglets.
        for (const QPointer<KoraCutTimelinePane> &pane : std::as_const(m_panes)) {
            while (pane && pane->count() > 0) {
                QWidget *page = pane->widget(0);
                const QString text = pane->tabText(0);
                const QIcon icon = pane->tabIcon(0);
                pane->removeTab(0);
                addTab(page, icon, text);
            }
        }
    }
    cleanupPanes();
    m_moving = false;
    // La séquence active (rouverte en dernier) reste active ; sa vue montre bien celle-là.
    if (QTabWidget *pane = paneOf(m_activeTimeline)) {
        m_moving = true;
        pane->setCurrentWidget(m_activeTimeline);
        m_moving = false;
    }
    refreshViews();
    return done;
}

QString TimelineTabs::layoutDescription() const
{
    return m_splitHost->describe();
}

TimelineWidget *TimelineTabs::timelineFor(const QString &nameOrIndex) const
{
    const QList<TimelineWidget *> timelines = allTimelines();
    bool isNumber = false;
    const int n = nameOrIndex.toInt(&isNumber);
    if (isNumber) {
        return n >= 1 && n <= timelines.size() ? timelines.at(n - 1) : nullptr;
    }
    for (TimelineWidget *timeline : timelines) {
        if (sequenceTabText(timeline->getUuid()).compare(nameOrIndex.trimmed(), Qt::CaseInsensitive) == 0 ||
            timeline->getUuid() == QUuid(nameOrIndex.trimmed())) {
            return timeline;
        }
    }
    return nullptr;
}

QString TimelineTabs::koraCutDebugCommand(const QString &command)
{
    // « etat », « poser=<séquence>:<côté>[:<à côté de>] », « onglet=<séquence> », « activer=<séquence> »,
    // « glisser=<séquence>@<fx>;<fy> » (montre le repère de dépôt à cette place de la zone, en
    // fractions), « lacher » (pose là où glisser l'a montré), « renommer=<séquence> ». Une séquence se
    // désigne par son rang (1, 2…), son nom ou son uuid.
    static QPointer<TimelineWidget> dragged;
    static QPoint dragPoint;
    const QString verb = command.section(QLatin1Char('='), 0, 0);
    const QString rest = command.section(QLatin1Char('='), 1);
    QString result;
    if (verb == QLatin1String("poser")) {
        TimelineWidget *timeline = timelineFor(rest.section(QLatin1Char(':'), 0, 0));
        TimelineWidget *anchor = rest.section(QLatin1Char(':'), 2).isEmpty() ? nullptr : timelineFor(rest.section(QLatin1Char(':'), 2));
        bool ok = false;
        const KoraCutSplit::Side side = KoraCutSplit::sideFromName(rest.section(QLatin1Char(':'), 1, 1), &ok);
        QString error;
        const bool done = timeline && ok && placeSequence(timeline->getUuid(), anchor ? anchor->getUuid() : QUuid(), side, &error);
        result = done ? QStringLiteral("ok") : QStringLiteral("refus %1").arg(error.isEmpty() ? QStringLiteral("parametres") : error);
    } else if (verb == QLatin1String("onglet")) {
        TimelineWidget *timeline = timelineFor(rest);
        result = timeline && dockBackSequence(timeline->getUuid()) ? QStringLiteral("ok") : QStringLiteral("refus");
    } else if (verb == QLatin1String("activer")) {
        TimelineWidget *timeline = timelineFor(rest);
        result = timeline && activateSequence(timeline->getUuid()) ? QStringLiteral("ok") : QStringLiteral("refus");
    } else if (verb == QLatin1String("glisser")) {
        dragged = timelineFor(rest.section(QLatin1Char('@'), 0, 0));
        // « 0.9;0.5 » : la virgule sépare déjà les actions de KORACUT_SHOT_ACTIONS.
        const QStringList where = rest.section(QLatin1Char('@'), 1).split(QRegularExpression(QStringLiteral("[;,x]")));
        dragPoint = m_splitHost->mapToGlobal(QPoint(int(where.value(0).toDouble() * m_splitHost->width()), int(where.value(1).toDouble() * m_splitHost->height())));
        updateDrop(dragged, dragPoint, false);
        result = dragged ? QStringLiteral("ok") : QStringLiteral("refus");
    } else if (verb == QLatin1String("lacher")) {
        updateDrop(dragged, dragPoint, true);
        result = QStringLiteral("ok");
    } else if (verb == QLatin1String("orienter")) {
        TimelineWidget *timeline = timelineFor(rest.section(QLatin1Char(':'), 0, 0));
        if (timeline) {
            setSequenceOrientation(timeline->getUuid(), rest.section(QLatin1Char(':'), 1, 1).startsWith(QLatin1Char('v')));
        }
        result = timeline ? QStringLiteral("ok") : QStringLiteral("refus");
    } else if (verb == QLatin1String("nouvelle")) {
        result = createSequence(rest.startsWith(QLatin1Char('v'))).isNull() ? QStringLiteral("refus") : QStringLiteral("ok");
    } else if (verb == QLatin1String("menu-plus")) {
        QTimer::singleShot(0, this, &TimelineTabs::showAddMenu);
        result = QStringLiteral("ok");
    } else if (verb == QLatin1String("renommer")) {
        TimelineWidget *timeline = timelineFor(rest);
        QTabWidget *pane = paneOf(timeline);
        if (KoraCutTabBarDriver *driver = driverOf(pane)) {
            driver->startRename(pane->indexOf(timeline));
            result = QStringLiteral("ok");
        } else {
            result = QStringLiteral("refus");
        }
    }
    // Toujours l'état à la fin : la découpe, puis une ligne par séquence.
    QStringList lines{QStringLiteral("%1 decoupe=%2 profil=%3x%4")
                          .arg(result.isEmpty() ? QStringLiteral("etat") : result, layoutDescription())
                          .arg(pCore->getProjectProfile().width())
                          .arg(pCore->getProjectProfile().height())};
    for (const QJsonValue &v : views()) {
        const QJsonObject o = v.toObject();
        const QJsonObject place = o.value(QStringLiteral("place")).toObject();
        const QString orient = isSequenceVertical(QUuid(o.value(QStringLiteral("sequence")).toString())) ? QStringLiteral("v") : QStringLiteral("h");
        lines << QStringLiteral("  %1 vue=%2 affichee=%3 active=%4%5")
                     .arg(o.value(QStringLiteral("nom")).toString(), o.value(QStringLiteral("vue")).toString() + QStringLiteral(" orientation=") + orient)
                     .arg(o.value(QStringLiteral("affichee")).toBool() ? 1 : 0)
                     .arg(o.value(QStringLiteral("active")).toBool() ? 1 : 0)
                     .arg(place.isEmpty() ? QString()
                                          : QStringLiteral(" place=%1,%2 %3x%4")
                                                .arg(place.value(QStringLiteral("x")).toInt())
                                                .arg(place.value(QStringLiteral("y")).toInt())
                                                .arg(place.value(QStringLiteral("largeur")).toInt())
                                                .arg(place.value(QStringLiteral("hauteur")).toInt()));
    }
    return lines.join(QLatin1Char('\n'));
}
