/*
    SPDX-FileCopyrightText: 2017 Jean-Baptiste Mardelle
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "timeline2/model/timelineitemmodel.hpp"
#include "timelinecontroller.h"
#include <QQuickWidget>

class ThumbnailProvider;
class QSortFilterProxyModel;
class MonitorProxy;
class MediaCapture;
class QMenu;
class QActionGroup;
class KoraCutAnimateMenu;
class KoraCutColorMenu;

class TimelineWidget : public QQuickWidget
{
    Q_OBJECT

public:
    TimelineWidget(const QUuid uuid, QWidget *parent = Q_NULLPTR);
    ~TimelineWidget() override;
    /** @brief Sets the model shown by this widget */
    void setModel(const std::shared_ptr<TimelineItemModel> &model, MonitorProxy *proxy);
    /** @brief Load the marker model (created after model is instantiated) */
    void loadMarkerModel();

    /** @brief Return the project's tractor
     */
    Mlt::Tractor *tractor();
    TimelineController *controller();
    std::shared_ptr<TimelineItemModel> model();
    QPair<int, int> getAvTracksCount() const;
    /** @brief calculate zoom level for a scale */
    int zoomForScale(double value) const;
    /** @brief Give keyboard focus to timeline qml */
    void focusTimeline();
    /** @brief Initiate timeline clip context menu */
    void setTimelineMenu(QMenu *clipMenu, QMenu *compositionMenu, QMenu *timelineMenu, QMenu *guideMenu, QMenu *timelineRulerMenu, QAction *editGuideAction,
                         QMenu *headerMenu, QMenu *thumbsMenu, QMenu *subtitleClipMenu, QMenu *addClipMenu);
    void updateAddClipMenuStatus();
    /** @brief KoraCut : ouvre le menu du clip @p cid à @p globalPos (captures, essais). @return le menu. */
    QMenu *koraCutShowClipMenu(int cid, const QPoint &globalPos);
    KoraCutAnimateMenu *koraCutAnimateMenu() const { return m_koraCutAnimate; }
    /** @brief KoraCut : les sous-menus Couleur du clip et Couleur de la piste (document 13). */
    KoraCutColorMenu *koraCutColorMenu() const { return m_koraCutColors; }
    /** @brief KoraCut : ouvre le menu de la tête de piste @p tid à @p globalPos (captures). @return le menu. */
    QMenu *koraCutShowHeaderMenu(int tid, const QPoint &globalPos);
    bool loading;
    void connectSubtitleModel(bool firstConnect);
    const QUuid &getUuid() const;
    bool hasSubtitles() const;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    /** @brief KoraCut : attrape le pincement du trackpad pour zoomer la timeline. */
    bool event(QEvent *event) override;

public Q_SLOTS:
    void slotChangeZoom(int value, bool zoomOnMouse);
    void slotFitZoom();
    /** @brief Center timeline view on current timeline cursor position */
    void slotCenterView();
    void zoneUpdated(const QPoint &zone);
    void zoneUpdatedWithUndo(const QPoint &oldZone, const QPoint &newZone);
    /** @brief Favorite effects have changed, reload model for context menu */
    void updateEffectFavorites();
    /** @brief Favorite transitions have changed, reload model for context menu */
    void updateTransitionFavorites();
    /** @brief Bin clip drag ended, make sure we correctly processed the drop */
    void endDrag();
    /** @brief Start an audio recording on track tid*/
    void startAudioRecord(int tid);
    void stopAudioRecord();
    /** @brief Show menu to switch track target audio stream */
    void showTargetMenu(int tid = -1);
    /** @brief Focus qml item under mouse in timeline, for example after app looses focus or a menu showed up*/
    void regainFocus();

private Q_SLOTS:
    void slotUngrabHack();
    void slotResetContextPos(QAction *);
    void showClipMenu(int cid);
    void showMixMenu(int cid);
    void showCompositionMenu();
    void showTimelineMenu();
    void showRulerMenu();
    void showHeaderMenu();
    void showSubtitleClipMenu();
    void emitMousePos(int);

private:
    TimelineController timelineController;
    QMenu *m_timelineClipMenu;
    QMenu *m_timelineMixMenu;
    QMenu *m_timelineCompositionMenu;
    QMenu *m_timelineMenu;
    QMenu *m_timelineRulerMenu;
    QMenu *m_guideMenu;
    QMenu *m_headerMenu;
    QMenu *m_targetsMenu;
    QActionGroup *m_targetsGroup{nullptr};
    QMenu *m_thumbsMenu;
    QMenu *m_addClipMenu;
    QMenu *m_favEffects;
    QMenu *m_favCompositions;
    QAction *m_editGuideAcion;
    QMenu *m_timelineSubtitleClipMenu;
    /** @brief KoraCut : les sous-menus Animer et Transition vers le plan suivant du menu d'un clip. */
    KoraCutAnimateMenu *m_koraCutAnimate{nullptr};
    KoraCutColorMenu *m_koraCutColors{nullptr};
    QAction *m_koraCutColorsSeparator{nullptr};
    static const int comboScale[];
    std::unique_ptr<QSortFilterProxyModel> m_sortModel;
    /** @brief Keep last scale before fit to restore it on second click */
    double m_prevScale;
    /** @brief Keep last scroll position before fit to restore it on second click */
    int m_scrollPos;
    /** @brief Returns an alphabetically sorted list of favorite effects or transitions */
    const QMap<QString, QString> sortedItems(const QStringList &items, bool isTransition);
    QPoint m_clickPos;
    QMetaObject::Connection m_addMenuConnection;
    QUuid m_uuid;
    /** @brief KoraCut : agrandissement cumulé du pincement en cours, un cran par 0,12. */
    double m_pinchAccumulated{0.};

Q_SIGNALS:
    void focusProjectMonitor();
    void zoneMoved(const QPoint &zone);
};
