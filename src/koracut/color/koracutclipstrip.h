/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

// La bande des plans et la galerie du module Étalonnage (document 22), comme dans la page Color de
// DaVinci Resolve : une vignette étalonnée par plan (numéro, piste, nom), le plan courant encadré ;
// clic : on passe au plan ; clic milieu sur une vignette : son grade est copié vers les plans
// sélectionnés ; la galerie garde des stills (image et grade) à réappliquer.

#include <QHash>
#include <QImage>
#include <QSet>
#include <QTimer>
#include <QWidget>

class QListWidget;

class KoraCutClipStrip : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutClipStrip(QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private:
    QRect tileRect(int index) const;
    int tileH() const;
    int tileW() const;
    int tileAt(const QPoint &pos) const;
    void reload();
    QList<int> m_clips;
    QSet<int> m_selected;
    QHash<int, QImage> m_images;
    QTimer m_refresh;
    int m_scroll = 0;
    int m_imagesH = 0;
};

class KoraCutGallery : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutGallery(QWidget *parent = nullptr);

private:
    void reload();
    QListWidget *m_list = nullptr;
};
