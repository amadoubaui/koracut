/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "koracutscript.h"

#include <QAbstractTableModel>
#include <QFrame>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QTableView>

class QLineEdit;
class QMenu;
class QToolButton;
class QPushButton;

/** @brief Un tableau du script vu par Qt : une ligne par scène, une colonne par colonne du tableau.
 *
 * Le modèle garde sa copie du bloc. Chaque changement la met à jour puis émet edited() : le
 * panneau la recopie dans le script et en fait une étape d'annulation (document 21).
 */
class KoraCutScriptTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit KoraCutScriptTableModel(QObject *parent = nullptr);

    void setTable(const KoraCutScriptBlock &table);
    const KoraCutScriptBlock &table() const { return m_table; }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    /** @name Gestes du tableau, chacun une étape d'annulation */
    ///@{
    int addRow(int at = -1);
    void removeRow(int row);
    void duplicateRow(int row);
    void moveRow(int from, int to);
    int addColumn(const QString &title, const QString &type, const QStringList &choices = {});
    void removeColumn(int column);
    void renameColumn(int column, const QString &title);
    void setColumnType(int column, const QString &type, const QStringList &choices = {});
    void moveColumn(int from, int to);
    void setTitle(const QString &title);
    void unlinkRow(int row);
    void linkRowToZone(int row, int in, int out);
    ///@}

    QString columnType(int column) const;

Q_SIGNALS:
    /** @param mergeKey non vide : une frappe qui peut fusionner avec la précédente. */
    void edited(const QString &undoText, const QString &mergeKey);

private:
    void commitReset(const QString &undoText);
    KoraCutScriptBlock m_table;
};

/** @brief L'éditeur d'une cellule selon le type de colonne : champ, liste de choix, médias du chutier.
 * Entrée valide et descend d'une ligne, Tab passe à la cellule suivante, comme dans un tableur. */
class KoraCutScriptDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit KoraCutScriptDelegate(QTableView *view);
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override;

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    QPointer<QTableView> m_view;
};

/** @brief La vue d'un tableau : hauteur ajustée à ses lignes (la page défile, pas le tableau),
 * lignes à réordonner en glissant leur numéro, double-clic sur une scène reliée = la tête de lecture y va. */
class KoraCutScriptTableView : public QTableView
{
    Q_OBJECT
public:
    explicit KoraCutScriptTableView(KoraCutScriptTableModel *model, QWidget *parent = nullptr);
    void fitHeight();
    void applyColumnWidths();

Q_SIGNALS:
    void seekRequested(int frame);

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    KoraCutScriptTableModel *m_model;
    bool m_movingSection{false};
};

/** @brief Un bloc tableau complet : son nom, la vue, le « + » à droite (une colonne) et le « + » en bas (une ligne). */
class KoraCutScriptTableBlock : public QFrame
{
    Q_OBJECT
public:
    explicit KoraCutScriptTableBlock(const KoraCutScriptBlock &table, QWidget *parent = nullptr);
    KoraCutScriptTableModel *model() const { return m_model; }
    KoraCutScriptTableView *view() const { return m_view; }
    QString blockId() const { return m_model->table().id; }
    /** Ajoute une colonne du type donné, avec un nom par défaut (aussi pour les pseudo-actions). */
    int addColumnOfType(const QString &type, bool askChoices);
    /** Les identifiants des lignes sélectionnées, dans l'ordre du tableau. */
    QStringList selectedRowIds() const;

Q_SIGNALS:
    void seekRequested(int frame);
    void placeMarkerRequested(const QString &blockId, const QString &rowId);
    void linkZoneRequested(const QString &blockId, const QString &rowId);
    /** « Générer la voix off » sur les lignes choisies (chantier 5). */
    void voiceOverRequested(const QString &blockId, const QStringList &rowIds);
    void activated(const QString &blockId);

private:
    void showRowMenu(const QPoint &pos);
    void showColumnMenu(const QPoint &pos);
    void showAddColumnMenu();
    QMenu *typeMenu(QWidget *parent, int column);

    KoraCutScriptTableModel *m_model;
    KoraCutScriptTableView *m_view;
    QLineEdit *m_title;
    QToolButton *m_addColumn;
    QPushButton *m_addRow;
};
