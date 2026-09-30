/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QMap>
#include <QPointer>
#include <QWidget>

class KoraCutPrefsPage;
class QButtonGroup;
class QGridLayout;
class QToolButton;
class QPushButton;
class QComboBox;

/** @brief Une vignette de thème : un aperçu de fenêtre dans les couleurs du thème, et son nom.
 * Un bouton : la porte de commande la clique par son nom (`ui.fenetre faire=bouton:Matrix`). */
class KoraCutThemeTile : public QAbstractButton
{
    Q_OBJECT
public:
    KoraCutThemeTile(const QString &id, const QString &name, const QMap<QString, QColor> &preview, QWidget *parent = nullptr);
    QString themeId() const { return m_id; }
    void setActive(bool active);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QString m_id;
    QMap<QString, QColor> m_preview;
    bool m_active{false};
    bool m_hover{false};
};

/** @brief La page Apparence des Préférences : mode (automatique, clair, sombre), thèmes, onglets.
 * Tout s'applique tout de suite, par le moteur de thèmes (KoraCutThemes), comme les agents. */
class KoraCutAppearance : public QObject
{
    Q_OBJECT
public:
    /** @p tabPosition : la liste « Position des onglets » de Kdenlive, rangée ici. */
    KoraCutAppearance(KoraCutPrefsPage *page, QComboBox *tabPosition);

private:
    void rebuildTiles();
    void refreshState();
    KoraCutPrefsPage *m_page;
    QWidget *m_modeBox{nullptr};
    QList<QToolButton *> m_modeButtons;
    QWidget *m_tilesBox{nullptr};
    QGridLayout *m_tiles{nullptr};
    QList<KoraCutThemeTile *> m_tileList;
    QPushButton *m_remove{nullptr};
};
