/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QAbstractProxyModel>
#include <QPersistentModelIndex>
#include <QSet>
#include <QStyledItemDelegate>
#include <QWidget>

/** @brief La vue en cartes des effets : la liste sans ses catégories.
 *
 * Demande URGENTE d'Amadou Ba du 23 septembre 2026 (format carte et format liste, avec une
 * vignette qui montre l'effet). La liste des effets est un arbre (catégories, puis effets) ; une
 * grille de cartes posée sur l'arbre montrerait les catégories. Ce modèle aplatit l'arbre filtré :
 * une ligne par effet visible, dans l'ordre de l'arbre, et suit ses changements (recherche,
 * catégorie, favoris). mapToSource() rend l'index de l'arbre : le glisser-déposer, le double clic,
 * les favoris et le menu contextuel passent par le même code que la liste.
 */
class KoraCutFlatAssetProxy : public QAbstractProxyModel
{
    Q_OBJECT
public:
    explicit KoraCutFlatAssetProxy(QObject *parent = nullptr);

    void setSourceModel(QAbstractItemModel *source) override;
    QModelIndex mapToSource(const QModelIndex &proxyIndex) const override;
    QModelIndex mapFromSource(const QModelIndex &sourceIndex) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    /** @brief Ne garder que les effets de cette catégorie (son nom affiché) ; vide : toutes. */
    void setCategory(const QString &category);
    /** @brief La clé de la pastille « Essentiels » (les effets de data/preferred_effects.txt). */
    static QString essentialsKey();
    static const QSet<QString> &essentials();
    /** @brief Les catégories visibles dans l'arbre filtré, dans l'ordre. */
    QStringList categories() const;

Q_SIGNALS:
    void categoriesChanged();

private:
    void rebuild();
    QList<QPersistentModelIndex> m_rows;
    QStringList m_categories;
    QString m_category;
};

/** @brief Une rangée de pastilles de catégories qui passe à la ligne quand le panneau est étroit. */
class KoraCutCategoryChips : public QWidget
{
    Q_OBJECT
public:
    explicit KoraCutCategoryChips(KoraCutFlatAssetProxy *model, QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuild();
    int layoutChips(int width, bool apply) const;
    void select(const QString &name);
    KoraCutFlatAssetProxy *m_model;
    QList<class QToolButton *> m_chips;
    QStringList m_names;               ///< la clé de chaque pastille, dans le même ordre
    class QToolButton *m_more{nullptr}; ///< « Plus… », les catégories qui ne tiennent pas en deux rangées
    QString m_current;
};

/** @brief Une carte : la vignette de l'effet en 16:9, son nom dessous sur deux lignes au plus.
 *
 * Depuis le 27 septembre 2026, la largeur des cartes suit la taille choisie avec − et + dans la barre
 * du panneau (KoraCutAssetZoom) : la vue pose une grille dont les cases remplissent la largeur, et la
 * carte se centre dans sa case. */
class KoraCutAssetCardDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    /** @brief Taille d'une carte (vignette + nom) pour une vignette de @p thumbWidth px de large. */
    static QSize cardSize(int thumbWidth = 0);
    /** @brief Largeur de la vignette des cartes (0 : celle des vignettes livrées). */
    void setThumbWidth(int width) { m_thumbWidth = width; }
    int thumbWidth() const;

    /** @brief Les tailles de vignette de la grille, par cran (le cran 0 est la liste). */
    static int thumbWidthForLevel(int level);
    /** @brief La grille qui remplit @p available px : autant de colonnes que la taille demandée en
     *  permet, vignette élargie d'un quart au plus pour combler la largeur. Rend la taille d'une case
     *  et écrit la largeur de vignette dans @p thumb. */
    static QSize gridFor(int level, int available, int *thumb);

private:
    int m_thumbWidth{0};
};

/** @brief La taille des vignettes d'un panneau d'effets ou de compositions : − et + dans sa barre,
 *  ⌘− et ⌘+ quand le panneau a le focus (27 septembre 2026, demande d'Amadou Ba : « les images sont
 *  déjà grandes, mets un plus et un moins »). Le cran 0 est la liste, les suivants des grilles de plus
 *  en plus grandes. Le cran est gardé dans koracutrc, groupe [KoraCut]. */
class KoraCutAssetZoom : public QObject
{
    Q_OBJECT
public:
    /** @p key : la clé du réglage ; @p levels : le nombre de crans ; @p fallback : le cran sans réglage. */
    KoraCutAssetZoom(const QString &key, int levels, int fallback, QObject *parent);
    /** Pose − et + dans @p bar, juste après @p after (au début si nul). */
    void install(class QToolBar *bar, QAction *after);
    /** ⌘− et ⌘+ agissent quand ce widget a le focus. */
    void watch(QWidget *widget);
    /** widthChanged() est émis quand ce widget change de largeur (la grille se refait). */
    void followWidth(QWidget *widget);
    int level() const { return m_level; }
    int levels() const { return m_levels; }
    /** Change de cran (borné), l'enregistre et prévient. */
    void setLevel(int level);

Q_SIGNALS:
    void levelChanged(int level);
    void widthChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateActions();
    QString m_key;
    int m_levels;
    int m_level;
    QAction *m_smaller;
    QAction *m_bigger;
    QList<QObject *> m_followed;
};
