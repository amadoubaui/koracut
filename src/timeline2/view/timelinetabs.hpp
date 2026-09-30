/*
    SPDX-FileCopyrightText: 2017 Nicolas Carion
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "koracut/koracuttimelinesplit.h"

#include <QJsonArray>
#include <QMutex>
#include <QPointer>
#include <QTabWidget>
#include <memory>

class TimelineWidget;
class TimelineItemModel;
class AssetParameterModel;
class EffectStackModel;
class MonitorProxy;
class QMenu;

/** @class TimelineContainer
    @brief This is a class that extends QTabWidget to provide additional functionality related to timeline tabs
 */
class TimelineContainer : public QWidget
{

public:
    TimelineContainer(QWidget *parent);

protected:
    QSize sizeHint() const override;
};

class TimelineTabs : public QTabWidget
{
    Q_OBJECT

public:
    /** @brief Construct the tabs as well as the widget for the main timeline */
    TimelineTabs(QWidget *parent = nullptr);
    ~TimelineTabs() override;

    /** @brief Returns a pointer to the current timeline */
    TimelineWidget *getCurrentTimeline() const;
    /** @brief Activate a timeline tab by uuid */
    bool raiseTimeline(const QUuid &uuid);
    void disconnectTimeline(TimelineWidget *timeline);
    /** @brief Store timeline menus */
    void setTimelineMenu(QMenu *compositionMenu, QMenu *timelineMenu, QMenu *guideMenu, QMenu *timelineRulerMenu, QAction *editGuideAction, QMenu *headerMenu,
                         QMenu *thumbsMenu, QMenu *subtitleClipMenu, QMenu *addClipMenu);
    /** @brief Mark a tab as modified */
    void setModified(const QUuid &uuid, bool modified);
    /** @brief Returns the uuid list for opened timeline tabs. */
    const QStringList openedSequences();
    /** @brief Get a timeline tab by uuid. */
    TimelineWidget *getTimeline(const QUuid uuid) const;
    /** @brief We display the current tab's name in window title if the tab bar is hidden
     */
    void updateWindowTitle();
    /** @brief Build the timeline clip menu with dynamic actions. */
    void buildClipMenu();

    // ---- KoraCut, 24 septembre 2026 : plusieurs séquences visibles côte à côte (document 13, § 5).
    // Les onglets restent la vue principale ; une séquence glissée hors de la barre part dans une
    // vue secondaire (KoraCutTimelinePane), posée à gauche, à droite, en haut ou en bas. Une seule
    // séquence est « active » à la fois : elle pilote le moniteur, les raccourcis et l'inspecteur.
    // Les autres restent affichées, figées sur leur position, et un clic les rend actives.

    /** @brief La zone qui porte les vues : c'est elle que la fenêtre pose dans le panneau Timeline. */
    QWidget *splitHost() const;
    /** @brief Pose la séquence à côté d'une autre (target nul : à côté de toute la zone), ou dans
     *  ses onglets (Center). Rend faux et remplit error si ce n'est pas possible. */
    bool placeSequence(const QUuid &uuid, const QUuid &target, KoraCutSplit::Side side, QString *error = nullptr);
    /** @brief Remet la séquence dans les onglets de la vue principale. */
    bool dockBackSequence(const QUuid &uuid);
    /** @brief Rend active une séquence ouverte (sans changer sa place). */
    bool activateSequence(const QUuid &uuid);
    /** @brief Les vues : une entrée par séquence ouverte, avec sa vue, sa place et son état. */
    QJsonArray views() const;
    /** @brief La découpe en texte (« H[principale,vue-1] »). */
    QString layoutDescription() const;
    /** @brief KoraCut : l'orientation d'une séquence (document 13, § 5 bis). Sans réglage, celle du projet. */
    bool isSequenceVertical(const QUuid &uuid) const;
    /** @brief Donne une orientation à une séquence ; si elle est active, le profil se retourne. */
    void setSequenceOrientation(const QUuid &uuid, bool vertical);
    /** @brief Crée une séquence horizontale ou verticale, l'ouvre et la rend active. */
    QUuid createSequence(bool vertical);
    /** @brief La découpe à enregistrer dans le projet (JSON compact) ; vide sans vue côte à côte. */
    QString layoutState() const;
    /** @brief Repose une découpe enregistrée par layoutState(). Rend faux si rien n'a été posé. */
    bool restoreLayoutState(const QString &state);
    /** @brief Pseudo-actions de capture « timelines:… » (KORACUT_SHOT_ACTIONS). */
    QString koraCutDebugCommand(const QString &command);

protected:
    /** @brief Helper function to connect a timeline's signals/slots*/
    void connectTimeline(TimelineWidget *timeline);
    /** @brief KoraCut : le menu d'un onglet de séquence, sur un clic droit. */
    void showTabMenu(QTabWidget *pane, const QPoint &pos);

Q_SIGNALS:
    /** @brief Change the level of zoom
        This is an input signal, forwarded to the timelines
     */
    void changeZoom(int value, bool zoomOnMouse);
    void fitZoom();
    /** @brief Requests that a given parameter model is displayed in the asset panel */
    void showTransitionModel(int tid, std::shared_ptr<AssetParameterModel>);
    /** @brief Requests that a given mix is displayed in the asset panel */
    void showMixModel(int cid, std::shared_ptr<AssetParameterModel>, bool refreshOnly);
    /** @brief Requests that a given effectstack model is displayed in the asset panel */
    void showItemEffectStack(const QString &clipName, std::shared_ptr<EffectStackModel>, QSize size, bool showKeyframes);
    void showSubtitle(int itemId);
    void updateAssetPosition(int itemId, const QUuid uuid);
    /** @brief Zoom level changed in timeline, update slider
     */
    void updateZoom(int);

public Q_SLOTS:
    TimelineWidget *addTimeline(const QUuid uuid, int ix, const QString &tabName, std::shared_ptr<TimelineItemModel> timelineModel, MonitorProxy *proxy,
                                bool openInMonitor = true);
    void connectCurrent(int ix);
    void doConnectCurrent(int ix, bool openInMonitor = true);
    void closeTimelineByIndex(int ix);
    void closeTimelineTab(const QUuid uuid, bool checkActiveClosed = false);
    void renameTab(const QUuid &uuid, const QString &name);
    void slotNextSequence();
    void slotPreviousSequence();

private Q_SLOTS:
    void onTabBarDoubleClicked(int index);
    /** @brief Save an image of the timeline
     */
    void saveTimelinePreview(const QString &path);

private:
    TimelineWidget *m_activeTimeline;
    QMenu *m_timelineClipMenu{nullptr};
    QMenu *m_timelineCompositionMenu;
    QMenu *m_timelineMenu;
    QMenu *m_timelineRulerMenu;
    QMenu *m_guideMenu;
    QMenu *m_headerMenu;
    QMenu *m_thumbsMenu;
    QAction *m_editGuideAction;
    QMenu *m_timelineSubtitleClipMenu;
    QMenu *m_timelineAddClipMenu;
    QMutex m_lock;

    // KoraCut : les vues côte à côte.
    /** Toutes les séquences ouvertes : les onglets principaux, puis ceux des vues secondaires. */
    QList<TimelineWidget *> allTimelines() const;
    /** La vue (this ou une vue secondaire) qui porte cette séquence. */
    QTabWidget *paneOf(const QWidget *timeline) const;
    QList<QTabWidget *> allPanes() const;
    KoraCutTabBarDriver *driverOf(const QTabWidget *pane) const;
    QString sequenceTabText(const QUuid &uuid) const;
    TimelineWidget *timelineFor(const QString &nameOrIndex) const;
    /** Rend active cette séquence : ce que faisait doConnectCurrent pour un index. */
    void activateTimeline(TimelineWidget *target, bool openInMonitor = true);
    /** Fige ou libère la tête de lecture d'une séquence affichée mais inactive. */
    void park(TimelineWidget *timeline, bool parked, int position = -1);
    /** Déplace une séquence (moteur de placeSequence). */
    bool moveTimeline(TimelineWidget *timeline, QTabWidget *target, KoraCutSplit::Side side, QString *error = nullptr);
    KoraCutTimelinePane *newPane();
    /** Retire les vues vides, garde la vue principale remplie. */
    void cleanupPanes();
    /** Trait d'accent, voiles, croix, « + », barre visible ou non. */
    void refreshViews();
    void updateDrop(QWidget *page, const QPoint &globalPos, bool drop);
    void duplicateSequence(TimelineWidget *timeline);
    void setupDriver(QTabWidget *pane, bool main);
    /** Retourne le profil si la séquence a une autre orientation que le projet. */
    void applyOrientation(TimelineWidget *timeline);
    void showAddMenu();
    KoraCutTimelineSplitHost *m_splitHost{nullptr};
    QList<QPointer<KoraCutTimelinePane>> m_panes;
    QList<KoraCutTabBarDriver *> m_drivers;
    bool m_moving{false};
    int m_paneSerial{0};
};
