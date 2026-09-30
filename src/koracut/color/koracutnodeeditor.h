/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

// L'éditeur de nœuds du module Étalonnage (document 22), comme le Node Editor de DaVinci Resolve :
// la source à gauche, la sortie à droite ; chaque correcteur a une entrée RVB (triangle vert) et une
// entrée de clé (triangle bleu), une sortie RVB et une sortie de clé (carrés) ; les mélangeurs
// parallèle et calque ont une entrée par branche. On glisse d'une sortie vers une entrée pour relier,
// on clique le numéro pour couper un nœud, Option-S / P / L / O / Y ajoutent comme dans Resolve.

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <QTimer>
#include <QWidget>

class QLineEdit;
class QToolButton;

class KoraCutNodeEditor : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutNodeEditor(QWidget *parent = nullptr);

    /** @brief Les gestes du menu et des raccourcis (actions de la fenêtre) passent aussi par ici. */
    void addSerial();
    void addSerialBefore();
    void appendNode();
    void addParallel();
    void addLayer();
    void addOutside();
    void addSplitterCombiner();
    void toggleCurrentNode();
    void resetCurrentNode();
    void deleteCurrentNode();
    void renameCurrentNode();
    void cleanUp();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    struct Port
    {
        int node = -2; // -1 : source, -3 : sortie
        bool output = false;
        bool key = false;
        int slot = -1; // entrée d'un mélangeur
    };
    QRectF nodeRect(const QJsonObject &node) const;
    QPointF portPos(const Port &p) const;
    Port portAt(const QPointF &pos) const;
    int nodeAt(const QPointF &pos) const;
    QRectF numberRect(const QRectF &r) const;
    QPointF sourcePos() const;
    QPointF outputPos() const;
    void reload();
    void refreshThumbnails();
    void commit(const QJsonObject &graph, const QString &gesture);
    void selectNode(int id);
    void fitView();

    QJsonObject m_graph;
    int m_current = 1;
    bool m_thumbs = true;
    double m_zoom = 1.;
    QPointF m_pan{0, 0};
    // Glisser d'un nœud ou d'un lien
    int m_dragNode = -1;
    QPointF m_dragOffset;
    bool m_moved = false;
    Port m_linkFrom;
    QPointF m_linkPos;
    bool m_linking = false;
    bool m_panning = false;
    QPointF m_panStart;
    QHash<int, QImage> m_thumbCache;
    QTimer m_thumbTimer;
    QLineEdit *m_rename = nullptr;
    QToolButton *m_scopeClip = nullptr;
    QToolButton *m_scopeTimeline = nullptr;
    QToolButton *m_thumbButton = nullptr;
};
