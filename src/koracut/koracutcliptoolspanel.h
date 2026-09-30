/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QList>
#include <QMetaObject>
#include <QTimer>
#include <QWidget>
#include <memory>

class QAbstractButton;
class QButtonGroup;
class QLabel;
class QProgressBar;
class QPushButton;
class QToolButton;
class TimelineItemModel;

/** @brief La section « Outils » de l'inspecteur (document 24), en tête de la pile d'effets.
 *
 * Comme le panneau de réglages de CapCut, mais dans la charte de l'application : pour le son d'un
 * plan, réduire le bruit (trois niveaux), améliorer la voix, normaliser le volume et les effets de
 * voix en petites cartes ; pour l'image, stabiliser (trois niveaux). Un clic pose, un clic retire,
 * une étape d'annulation à chaque fois ; les effets posés restent réglables plus bas, dans la pile.
 */
class KoraCutClipToolsPanel : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutClipToolsPanel(QWidget *parent = nullptr);

    /** @brief Montre les outils de ce clip de la timeline (-1 : se cache). */
    void showClip(int clipId);
    int clipId() const { return m_clipId; }

private:
    std::shared_ptr<TimelineItemModel> timeline() const;
    void scheduleRefresh();
    void refresh();
    void watchStack();
    void applySound(int tool, const QString &variant);
    void normalize();
    void stabilize(const QString &level);
    QWidget *toolRow(const QString &icon, const QString &text, const QString &tip, QWidget *control);
    QWidget *segments(const QList<QPair<QString, QString>> &items, const QList<QString> &tips, QButtonGroup *group);

    int m_clipId{-1};
    std::weak_ptr<TimelineItemModel> m_timeline;
    QList<QMetaObject::Connection> m_watch;
    QTimer m_refreshTimer;
    bool m_updating{false};

    QWidget *m_sound{nullptr};
    QAbstractButton *m_denoiseSwitch{nullptr};
    QWidget *m_denoiseLevels{nullptr};
    QButtonGroup *m_denoiseGroup{nullptr};
    QAbstractButton *m_enhanceSwitch{nullptr};
    QPushButton *m_normalizeButton{nullptr};
    QButtonGroup *m_effectGroup{nullptr};
    QList<QToolButton *> m_effectCards;
    QToolButton *m_effectClear{nullptr};

    QWidget *m_image{nullptr};
    QAbstractButton *m_stabilizeSwitch{nullptr};
    QWidget *m_stabilizeLevels{nullptr};
    QButtonGroup *m_stabilizeGroup{nullptr};
    QWidget *m_stabilizeProgressRow{nullptr};
    QProgressBar *m_stabilizeProgress{nullptr};
    QLabel *m_stabilizeState{nullptr};
};
