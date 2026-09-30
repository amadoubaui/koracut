/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutscripttable.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "koracut/koracutagenttools.h"
#include "koracut/koracutfonts.h"
#include "koracut/koracutmetrics.h"

#include <KLocalizedString>

#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

bool noDialogs()
{
    // Pendant un outil d'agent ou une commande de la porte, aucune fenêtre ne s'ouvre (document 18).
    return qApp && qApp->property("koracut_sans_fenetre").toBool();
}

QString defaultColumnTitle(const QString &type)
{
    if (type == QLatin1String("duree")) {
        return i18nc("@title:column new script column", "Duration");
    }
    if (type == QLatin1String("media")) {
        return i18nc("@title:column new script column", "Shots");
    }
    if (type == QLatin1String("case")) {
        return i18nc("@title:column new script column", "Checked");
    }
    if (type == QLatin1String("choix")) {
        return i18nc("@title:column new script column", "Status");
    }
    return i18nc("@title:column new script column", "Notes");
}

QStringList defaultChoices()
{
    return {i18nc("@item script status", "To shoot"), i18nc("@item script status", "Shot"), i18nc("@item script status", "Edited")};
}

// Un point rond pour une scène reliée à la timeline, dans l'en-tête de ligne.
QIcon linkDot()
{
    static const QIcon icon = []() {
        QPixmap pix(QSize(16, 16) * 2);
        pix.setDevicePixelRatio(2);
        pix.fill(Qt::transparent);
        QPainter p(&pix);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x0D, 0x99, 0xFF));
        p.drawEllipse(QPointF(8, 8), 3.5, 3.5);
        return QIcon(pix);
    }();
    return icon;
}

QStringList binClipNames()
{
    QStringList names;
    if (!pCore || !pCore->projectItemModel()) {
        return names;
    }
    for (const QString &id : pCore->projectItemModel()->getAllClipIds()) {
        if (const auto clip = pCore->projectItemModel()->getClipByBinID(id)) {
            names << clip->clipName();
        }
    }
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);
    return names;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Le modèle

KoraCutScriptTableModel::KoraCutScriptTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void KoraCutScriptTableModel::setTable(const KoraCutScriptBlock &table)
{
    beginResetModel();
    m_table = table;
    endResetModel();
}

int KoraCutScriptTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_table.rows.size());
}

int KoraCutScriptTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_table.columns.size());
}

QString KoraCutScriptTableModel::columnType(int column) const
{
    return column >= 0 && column < m_table.columns.size() ? m_table.columns.at(column).type : QString();
}

QVariant KoraCutScriptTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_table.rows.size() || index.column() >= m_table.columns.size()) {
        return {};
    }
    const KoraCutScriptColumn &c = m_table.columns.at(index.column());
    const QString value = m_table.rows.at(index.row()).cells.value(c.id);
    const bool check = c.type == QLatin1String("case");
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return check ? QVariant() : QVariant(value);
    case Qt::CheckStateRole:
        return check ? QVariant(value.isEmpty() ? Qt::Unchecked : Qt::Checked) : QVariant();
    case Qt::ToolTipRole:
        return value.size() > 40 ? QVariant(value) : QVariant();
    case Qt::FontRole:
        if (c.type == QLatin1String("duree")) {
            // Les durées et timecodes en chiffres à chasse fixe, comme les timecodes de l'application.
            return KoraCutFonts::numbers(KoraCutMetrics::FontDense);
        }
        return {};
    case Qt::TextAlignmentRole:
        if (c.type == QLatin1String("duree")) {
            return int(Qt::AlignRight | Qt::AlignTop);
        }
        if (check) {
            return int(Qt::AlignCenter);
        }
        return int(Qt::AlignLeft | Qt::AlignTop);
    default:
        return {};
    }
}

bool KoraCutScriptTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() >= m_table.rows.size() || index.column() >= m_table.columns.size()) {
        return false;
    }
    const KoraCutScriptColumn &c = m_table.columns.at(index.column());
    KoraCutScriptRow &row = m_table.rows[index.row()];
    QString text;
    if (role == Qt::CheckStateRole) {
        text = value.toInt() == Qt::Checked ? QStringLiteral("1") : QString();
    } else if (role == Qt::EditRole) {
        text = KoraCutScriptBlock::normalizedValue(c.type, value.toString());
    } else {
        return false;
    }
    if (row.cells.value(c.id) == text) {
        return false;
    }
    if (text.isEmpty()) {
        row.cells.remove(c.id);
    } else {
        row.cells.insert(c.id, text);
    }
    Q_EMIT dataChanged(index, index);
    Q_EMIT edited(role == Qt::CheckStateRole ? i18nc("@action undo text", "Script: check scene") : i18nc("@action undo text", "Script: edit cell"),
                  role == Qt::CheckStateRole ? QString() : QStringLiteral("cellule:%1:%2:%3").arg(m_table.id, row.id, c.id));
    return true;
}

Qt::ItemFlags KoraCutScriptTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    if (columnType(index.column()) == QLatin1String("case")) {
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

QVariant KoraCutScriptTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal) {
        if (section < 0 || section >= m_table.columns.size()) {
            return {};
        }
        const KoraCutScriptColumn &c = m_table.columns.at(section);
        if (role == Qt::DisplayRole) {
            return c.title;
        }
        if (role == Qt::ToolTipRole) {
            return i18nc("@info:tooltip script column header, %1 is the column type", "%1 · double-click to rename, right-click for more",
                         KoraCutScript::columnTypeLabel(c.type));
        }
        return {};
    }
    if (section < 0 || section >= m_table.rows.size()) {
        return {};
    }
    const KoraCutScriptRow &r = m_table.rows.at(section);
    if (role == Qt::DisplayRole) {
        return QString::number(section + 1);
    }
    if (role == Qt::DecorationRole && r.isLinked()) {
        return linkDot();
    }
    if (role == Qt::ToolTipRole) {
        const double fps = pCore ? pCore->getCurrentFps() : 25.;
        if (r.marker >= 0) {
            return i18nc("@info:tooltip script scene linked to a marker, %1 is a timecode", "Marker at %1 · double-click to go there",
                         KoraCutAgentTools::timecode(r.marker, fps > 0 ? fps : 25.));
        }
        if (r.zoneIn >= 0) {
            return i18nc("@info:tooltip script scene linked to a zone, %1 and %2 are timecodes", "Zone %1 to %2 · double-click to go there",
                         KoraCutAgentTools::timecode(r.zoneIn, fps > 0 ? fps : 25.), KoraCutAgentTools::timecode(r.zoneOut, fps > 0 ? fps : 25.));
        }
        return i18nc("@info:tooltip script row number", "Drag to reorder");
    }
    return {};
}

void KoraCutScriptTableModel::commitReset(const QString &undoText)
{
    endResetModel();
    Q_EMIT edited(undoText, QString());
}

int KoraCutScriptTableModel::addRow(int at)
{
    beginResetModel();
    const QString id = m_table.addRow(at);
    commitReset(i18nc("@action undo text", "Script: add row"));
    return m_table.rowIndex(id);
}

void KoraCutScriptTableModel::removeRow(int row)
{
    if (row < 0 || row >= m_table.rows.size()) {
        return;
    }
    beginResetModel();
    m_table.rows.removeAt(row);
    commitReset(i18nc("@action undo text", "Script: delete row"));
}

void KoraCutScriptTableModel::duplicateRow(int row)
{
    if (row < 0 || row >= m_table.rows.size()) {
        return;
    }
    beginResetModel();
    m_table.duplicateRow(row);
    commitReset(i18nc("@action undo text", "Script: duplicate row"));
}

void KoraCutScriptTableModel::moveRow(int from, int to)
{
    if (from < 0 || from >= m_table.rows.size() || to < 0 || to >= m_table.rows.size() || from == to) {
        return;
    }
    beginResetModel();
    m_table.moveRow(from, to);
    commitReset(i18nc("@action undo text", "Script: move row"));
}

int KoraCutScriptTableModel::addColumn(const QString &title, const QString &type, const QStringList &choices)
{
    beginResetModel();
    m_table.addColumn(title, type, choices);
    commitReset(i18nc("@action undo text", "Script: add column"));
    return int(m_table.columns.size()) - 1;
}

void KoraCutScriptTableModel::removeColumn(int column)
{
    if (column < 0 || column >= m_table.columns.size()) {
        return;
    }
    beginResetModel();
    m_table.removeColumn(column);
    commitReset(i18nc("@action undo text", "Script: delete column"));
}

void KoraCutScriptTableModel::renameColumn(int column, const QString &title)
{
    if (column < 0 || column >= m_table.columns.size() || title.trimmed().isEmpty() || m_table.columns.at(column).title == title.trimmed()) {
        return;
    }
    m_table.columns[column].title = title.trimmed();
    Q_EMIT headerDataChanged(Qt::Horizontal, column, column);
    Q_EMIT edited(i18nc("@action undo text", "Script: rename column"), QString());
}

void KoraCutScriptTableModel::setColumnType(int column, const QString &type, const QStringList &choices)
{
    if (column < 0 || column >= m_table.columns.size() || !KoraCutScript::columnTypes().contains(type)) {
        return;
    }
    beginResetModel();
    KoraCutScriptColumn &c = m_table.columns[column];
    c.type = type;
    c.choices = type == QLatin1String("choix") ? choices : QStringList();
    // Les valeurs suivent : une case ne garde que coché ou non.
    for (KoraCutScriptRow &r : m_table.rows) {
        if (r.cells.contains(c.id)) {
            const QString v = KoraCutScriptBlock::normalizedValue(type, r.cells.value(c.id));
            if (v.isEmpty()) {
                r.cells.remove(c.id);
            } else {
                r.cells.insert(c.id, v);
            }
        }
    }
    commitReset(i18nc("@action undo text", "Script: change column type"));
}

void KoraCutScriptTableModel::moveColumn(int from, int to)
{
    if (from < 0 || from >= m_table.columns.size() || to < 0 || to >= m_table.columns.size() || from == to) {
        return;
    }
    beginResetModel();
    m_table.moveColumn(from, to);
    commitReset(i18nc("@action undo text", "Script: move column"));
}

void KoraCutScriptTableModel::setTitle(const QString &title)
{
    if (m_table.text == title) {
        return;
    }
    m_table.text = title;
    Q_EMIT edited(i18nc("@action undo text", "Script: rename table"), QStringLiteral("tableau:%1").arg(m_table.id));
}

void KoraCutScriptTableModel::unlinkRow(int row)
{
    if (row < 0 || row >= m_table.rows.size() || !m_table.rows.at(row).isLinked()) {
        return;
    }
    KoraCutScriptRow &r = m_table.rows[row];
    r.marker = -1;
    r.zoneIn = r.zoneOut = -1;
    Q_EMIT headerDataChanged(Qt::Vertical, row, row);
    Q_EMIT edited(i18nc("@action undo text", "Script: unlink scene"), QString());
}

void KoraCutScriptTableModel::linkRowToZone(int row, int in, int out)
{
    if (row < 0 || row >= m_table.rows.size() || in < 0 || out <= in) {
        return;
    }
    KoraCutScriptRow &r = m_table.rows[row];
    r.zoneIn = in;
    r.zoneOut = out;
    Q_EMIT headerDataChanged(Qt::Vertical, row, row);
    Q_EMIT edited(i18nc("@action undo text", "Script: link scene to zone"), QString());
}

// ---------------------------------------------------------------------------------------------
// Le délégué

KoraCutScriptDelegate::KoraCutScriptDelegate(QTableView *view)
    : QStyledItemDelegate(view)
    , m_view(view)
{
}

QWidget *KoraCutScriptDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const auto *model = qobject_cast<const KoraCutScriptTableModel *>(index.model());
    const QString type = model ? model->columnType(index.column()) : QString();
    if (type == QLatin1String("choix")) {
        auto *combo = new QComboBox(parent);
        combo->setObjectName(QStringLiteral("koracutScriptCellCombo"));
        combo->addItem(QString());
        combo->addItems(model->table().columns.at(index.column()).choices);
        return combo;
    }
    if (type == QLatin1String("media")) {
        // Un plan du chutier, ou n'importe quel texte (« plan drone ») : la liste propose, elle n'impose pas.
        auto *combo = new QComboBox(parent);
        combo->setObjectName(QStringLiteral("koracutScriptCellCombo"));
        combo->setEditable(true);
        combo->setInsertPolicy(QComboBox::NoInsert);
        combo->addItems(binClipNames());
        if (combo->completer()) {
            combo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
            combo->completer()->setFilterMode(Qt::MatchContains);
        }
        return combo;
    }
    auto *edit = new QLineEdit(parent);
    edit->setObjectName(QStringLiteral("koracutScriptCellEdit"));
    edit->setFrame(false);
    if (type == QLatin1String("duree")) {
        edit->setFont(KoraCutFonts::numbers(KoraCutMetrics::FontDense));
        edit->setAlignment(Qt::AlignRight);
        edit->setPlaceholderText(i18nc("@info:placeholder duration cell", "5 s or 00:00:05:00"));
    }
    Q_UNUSED(option)
    return edit;
}

void KoraCutScriptDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    const QString value = index.data(Qt::EditRole).toString();
    if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        if (combo->isEditable()) {
            combo->setEditText(value);
        } else {
            const int i = combo->findText(value);
            if (i < 0 && !value.isEmpty()) {
                combo->addItem(value);
            }
            combo->setCurrentIndex(std::max(0, combo->findText(value)));
        }
        return;
    }
    QStyledItemDelegate::setEditorData(editor, index);
}

void KoraCutScriptDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    if (auto *combo = qobject_cast<QComboBox *>(editor)) {
        model->setData(index, combo->currentText().trimmed(), Qt::EditRole);
        return;
    }
    if (auto *edit = qobject_cast<QLineEdit *>(editor)) {
        model->setData(index, edit->text(), Qt::EditRole);
        return;
    }
    QStyledItemDelegate::setModelData(editor, model, index);
}

bool KoraCutScriptDelegate::eventFilter(QObject *object, QEvent *event)
{
    // Entrée valide et descend d'une ligne, comme dans un tableur. Tab et Maj+Tab restent au
    // délégué de Qt : cellule suivante ou précédente, en édition.
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        auto *editor = qobject_cast<QWidget *>(object);
        if (editor && (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !(key->modifiers() & Qt::ShiftModifier)) {
            auto *combo = qobject_cast<QComboBox *>(editor);
            if (combo && combo->view() && combo->view()->isVisible()) {
                return QStyledItemDelegate::eventFilter(object, event);
            }
            Q_EMIT commitData(editor);
            Q_EMIT closeEditor(editor, QAbstractItemDelegate::NoHint);
            if (m_view) {
                QPointer<QTableView> view = m_view;
                QTimer::singleShot(0, view, [view]() {
                    const QModelIndex current = view->currentIndex();
                    if (current.isValid() && current.row() + 1 < view->model()->rowCount()) {
                        view->setCurrentIndex(current.sibling(current.row() + 1, current.column()));
                    }
                });
            }
            return true;
        }
    }
    return QStyledItemDelegate::eventFilter(object, event);
}

// ---------------------------------------------------------------------------------------------
// La vue

KoraCutScriptTableView::KoraCutScriptTableView(KoraCutScriptTableModel *model, QWidget *parent)
    : QTableView(parent)
    , m_model(model)
{
    setObjectName(QStringLiteral("koracutScriptTable"));
    setModel(model);
    setItemDelegate(new KoraCutScriptDelegate(this));
    setWordWrap(true);
    setTabKeyNavigation(true);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setSelectionBehavior(QAbstractItemView::SelectItems);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setEditTriggers(QAbstractItemView::SelectedClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed |
                    QAbstractItemView::DoubleClicked);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    horizontalHeader()->setStretchLastSection(false);
    horizontalHeader()->setHighlightSections(false);
    horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    horizontalHeader()->setMinimumSectionSize(40);
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    verticalHeader()->setSectionsMovable(true);
    verticalHeader()->setHighlightSections(false);
    verticalHeader()->setDefaultAlignment(Qt::AlignCenter);
    verticalHeader()->setMinimumSectionSize(KoraCutMetrics::RowList);
    // Glisser le numéro d'une ligne la déplace : on remet l'en-tête en place et on déplace la scène
    // dans le modèle, qui en fait une étape d'annulation.
    connect(verticalHeader(), &QHeaderView::sectionMoved, this, [this](int, int oldVisual, int newVisual) {
        if (m_movingSection) {
            return;
        }
        m_movingSection = true;
        verticalHeader()->moveSection(newVisual, oldVisual);
        m_movingSection = false;
        m_model->moveRow(oldVisual, newVisual);
        setCurrentIndex(m_model->index(newVisual, std::max(0, currentIndex().column())));
    });
    connect(verticalHeader(), &QHeaderView::sectionDoubleClicked, this, [this](int row) {
        if (row >= 0 && row < m_model->table().rows.size() && m_model->table().rows.at(row).isLinked()) {
            Q_EMIT seekRequested(m_model->table().rows.at(row).linkedFrame());
        }
    });
    connect(model, &QAbstractItemModel::modelReset, this, [this]() {
        applyColumnWidths();
        fitHeight();
    });
    connect(model, &QAbstractItemModel::dataChanged, this, [this]() { QTimer::singleShot(0, this, &KoraCutScriptTableView::fitHeight); });
    connect(horizontalHeader(), &QHeaderView::sectionResized, this, [this]() { QTimer::singleShot(0, this, &KoraCutScriptTableView::fitHeight); });
    applyColumnWidths();
}

void KoraCutScriptTableView::applyColumnWidths()
{
    for (int c = 0; c < m_model->columnCount(); ++c) {
        const QString type = m_model->columnType(c);
        const QString id = m_model->table().columns.at(c).id;
        int width = 180;
        if (type == QLatin1String("duree")) {
            width = 72;
        } else if (type == QLatin1String("case")) {
            width = 52;
        } else if (type == QLatin1String("media") || type == QLatin1String("choix")) {
            width = 130;
        } else if (id == QLatin1String("scene")) {
            width = 140;
        } else if (id == QLatin1String("son")) {
            width = 130;
        }
        setColumnWidth(c, width);
    }
}

void KoraCutScriptTableView::fitHeight()
{
    resizeRowsToContents();
    int height = horizontalHeader()->isVisible() ? horizontalHeader()->height() : 0;
    for (int r = 0; r < m_model->rowCount(); ++r) {
        height += rowHeight(r);
    }
    height += 2 * frameWidth();
    if (horizontalScrollBar()->maximum() > 0 || horizontalHeader()->length() > viewport()->width()) {
        height += horizontalScrollBar()->sizeHint().height();
    }
    setFixedHeight(std::max(height, horizontalHeader()->height() + 2 * frameWidth() + 4));
}

void KoraCutScriptTableView::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Double-clic sur une scène reliée à la timeline : la tête de lecture y va (le crayon reste à
    // un clic sur la cellule déjà choisie, ou à la frappe). Sans lien, le double-clic édite.
    const QModelIndex index = indexAt(event->pos());
    if (index.isValid() && index.row() < m_model->table().rows.size()) {
        const KoraCutScriptRow &row = m_model->table().rows.at(index.row());
        if (row.isLinked() && m_model->columnType(index.column()) != QLatin1String("case")) {
            Q_EMIT seekRequested(row.linkedFrame());
            event->accept();
            return;
        }
    }
    QTableView::mouseDoubleClickEvent(event);
}

void KoraCutScriptTableView::resizeEvent(QResizeEvent *event)
{
    // La hauteur des lignes dépend de la largeur réelle : on la recalcule une fois posée.
    QTableView::resizeEvent(event);
    if (event->size().width() != event->oldSize().width()) {
        QTimer::singleShot(0, this, &KoraCutScriptTableView::fitHeight);
    }
}

void KoraCutScriptTableView::wheelEvent(QWheelEvent *event)
{
    // La molette verticale fait défiler la page du script, pas le tableau (qui n'a pas de barre verticale).
    if (std::abs(event->angleDelta().y()) >= std::abs(event->angleDelta().x())) {
        event->ignore();
        return;
    }
    QTableView::wheelEvent(event);
}

// ---------------------------------------------------------------------------------------------
// Le bloc tableau

KoraCutScriptTableBlock::KoraCutScriptTableBlock(const KoraCutScriptBlock &table, QWidget *parent)
    : QFrame(parent)
    , m_model(new KoraCutScriptTableModel(this))
{
    setObjectName(QStringLiteral("koracutScriptTableBlock"));
    m_model->setTable(table);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(KoraCutMetrics::SpaceTight);

    m_title = new QLineEdit(table.text, this);
    m_title->setObjectName(QStringLiteral("koracutScriptTableTitle"));
    m_title->setFrame(false);
    m_title->setPlaceholderText(i18nc("@info:placeholder", "Table name"));
    connect(m_title, &QLineEdit::editingFinished, this, [this]() { m_model->setTitle(m_title->text().trimmed()); });
    root->addWidget(m_title);

    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(KoraCutMetrics::SpaceTight);
    m_view = new KoraCutScriptTableView(m_model, this);
    row->addWidget(m_view, 1);
    // Le « + » à droite : une colonne de plus, on choisit son type.
    m_addColumn = new QToolButton(this);
    m_addColumn->setObjectName(QStringLiteral("koracutScriptAddColumn"));
    m_addColumn->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    m_addColumn->setIconSize(KoraCutMetrics::iconToolbar());
    m_addColumn->setToolTip(i18nc("@info:tooltip", "Add a column"));
    m_addColumn->setCursor(Qt::PointingHandCursor);
    m_addColumn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    m_addColumn->setFixedWidth(24);
    connect(m_addColumn, &QToolButton::clicked, this, &KoraCutScriptTableBlock::showAddColumnMenu);
    row->addWidget(m_addColumn);
    root->addLayout(row);

    // Le « + » en bas : une ligne de plus.
    m_addRow = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action:button", "New row"), this);
    m_addRow->setObjectName(QStringLiteral("koracutScriptAddRow"));
    m_addRow->setCursor(Qt::PointingHandCursor);
    m_addRow->setIconSize(KoraCutMetrics::iconToolbar());
    connect(m_addRow, &QPushButton::clicked, this, [this]() {
        const int r = m_model->addRow();
        m_view->setFocus();
        m_view->setCurrentIndex(m_model->index(r, 0));
        m_view->edit(m_model->index(r, 0));
    });
    root->addWidget(m_addRow, 0, Qt::AlignLeft);

    connect(m_view, &KoraCutScriptTableView::seekRequested, this, &KoraCutScriptTableBlock::seekRequested);
    connect(m_view, &QWidget::customContextMenuRequested, this, &KoraCutScriptTableBlock::showRowMenu);
    m_view->verticalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_view->verticalHeader(), &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const int r = m_view->verticalHeader()->logicalIndexAt(pos);
        if (r >= 0) {
            m_view->setCurrentIndex(m_model->index(r, std::max(0, m_view->currentIndex().column())));
            showRowMenu(QPoint(0, m_view->rowViewportPosition(r) + 2));
        }
    });
    connect(m_view->horizontalHeader(), &QWidget::customContextMenuRequested, this, &KoraCutScriptTableBlock::showColumnMenu);
    connect(m_view->horizontalHeader(), &QHeaderView::sectionDoubleClicked, this, [this](int column) {
        if (noDialogs() || column < 0 || column >= m_model->columnCount()) {
            return;
        }
        bool ok = false;
        const QString title = QInputDialog::getText(this, i18nc("@title:window", "Rename Column"), i18nc("@label:textbox", "Column name:"), QLineEdit::Normal,
                                                    m_model->table().columns.at(column).title, &ok);
        if (ok) {
            m_model->renameColumn(column, title);
        }
    });
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this, [this]() { Q_EMIT activated(blockId()); });
}

QMenu *KoraCutScriptTableBlock::typeMenu(QWidget *parent, int column)
{
    auto *menu = new QMenu(i18nc("@title:menu", "Column type"), parent);
    for (const QString &type : KoraCutScript::columnTypes()) {
        QAction *a = menu->addAction(KoraCutScript::columnTypeLabel(type));
        a->setCheckable(column >= 0);
        a->setChecked(column >= 0 && m_model->columnType(column) == type);
        connect(a, &QAction::triggered, this, [this, type, column]() {
            if (column < 0) {
                addColumnOfType(type, true);
                return;
            }
            QStringList choices = m_model->table().columns.at(column).choices;
            if (type == QLatin1String("choix") && choices.isEmpty()) {
                choices = defaultChoices();
                if (!noDialogs()) {
                    bool ok = false;
                    const QString text = QInputDialog::getText(this, i18nc("@title:window", "Choices"), i18nc("@label:textbox", "Choices, separated by commas:"),
                                                               QLineEdit::Normal, choices.join(QStringLiteral(", ")), &ok);
                    if (!ok) {
                        return;
                    }
                    choices = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
                    for (QString &c : choices) {
                        c = c.trimmed();
                    }
                }
            }
            m_model->setColumnType(column, type, choices);
        });
    }
    return menu;
}

int KoraCutScriptTableBlock::addColumnOfType(const QString &type, bool askChoices)
{
    QStringList choices;
    if (type == QLatin1String("choix")) {
        choices = defaultChoices();
        if (askChoices && !noDialogs()) {
            bool ok = false;
            const QString text = QInputDialog::getText(this, i18nc("@title:window", "Choices"), i18nc("@label:textbox", "Choices, separated by commas:"),
                                                       QLineEdit::Normal, choices.join(QStringLiteral(", ")), &ok);
            if (!ok) {
                return -1;
            }
            choices = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
            for (QString &c : choices) {
                c = c.trimmed();
            }
        }
    }
    const int column = m_model->addColumn(defaultColumnTitle(type), type, choices);
    // On montre la nouvelle colonne : elle est à droite, souvent hors de vue.
    QTimer::singleShot(0, m_view, [view = m_view, column]() { view->scrollTo(view->model()->index(0, column)); });
    return column;
}

void KoraCutScriptTableBlock::showAddColumnMenu()
{
    QMenu *menu = typeMenu(this, -1);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(m_addColumn->mapToGlobal(QPoint(m_addColumn->width(), 0)));
}

void KoraCutScriptTableBlock::showRowMenu(const QPoint &pos)
{
    QModelIndex index = m_view->indexAt(pos);
    if (!index.isValid()) {
        index = m_view->currentIndex();
    }
    const int row = index.isValid() ? index.row() : -1;
    QMenu menu(this);
    menu.addAction(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action:inmenu script row", "Insert Row Above"), this, [this, row]() {
        m_model->addRow(std::max(0, row));
    });
    menu.addAction(QIcon::fromTheme(QStringLiteral("list-add")), i18nc("@action:inmenu script row", "Insert Row Below"), this, [this, row]() {
        m_model->addRow(row < 0 ? -1 : row + 1);
    });
    if (row >= 0) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), i18nc("@action:inmenu script row", "Duplicate Row"), this, [this, row]() {
            m_model->duplicateRow(row);
        });
        menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18nc("@action:inmenu script row", "Delete Row"), this, [this, row]() {
            m_model->removeRow(row);
        });
        menu.addSeparator();
        const KoraCutScriptRow &r = m_model->table().rows.at(row);
        const QString rowId = r.id;
        if (r.isLinked()) {
            const int frame = r.linkedFrame();
            menu.addAction(QIcon::fromTheme(QStringLiteral("go-jump")), i18nc("@action:inmenu script row", "Go to This Scene"), this, [this, frame]() {
                Q_EMIT seekRequested(frame);
            });
        }
        menu.addAction(QIcon::fromTheme(QStringLiteral("flag")), i18nc("@action:inmenu script row", "Place a Marker Here"), this, [this, rowId]() {
            Q_EMIT placeMarkerRequested(blockId(), rowId);
        });
        menu.addAction(i18nc("@action:inmenu script row", "Link to the Timeline Zone"), this, [this, rowId]() { Q_EMIT linkZoneRequested(blockId(), rowId); });
        if (r.isLinked()) {
            menu.addAction(i18nc("@action:inmenu script row", "Unlink"), this, [this, row]() { m_model->unlinkRow(row); });
        }
        menu.addSeparator();
        // Chantier 5 : la voix off des lignes choisies (ou de la ligne sous la souris).
        QStringList rows = selectedRowIds();
        if (!rows.contains(rowId)) {
            rows = {rowId};
        }
        menu.addAction(QIcon::fromTheme(QStringLiteral("audio-input-microphone")),
                       i18ncp("@action:inmenu script row", "Generate the Voice-over…", "Generate the Voice-over of %1 Rows…", rows.size()), this,
                       [this, rows]() { Q_EMIT voiceOverRequested(blockId(), rows); });
    }
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

QStringList KoraCutScriptTableBlock::selectedRowIds() const
{
    QList<int> rows;
    if (m_view->selectionModel()) {
        for (const QModelIndex &index : m_view->selectionModel()->selectedIndexes()) {
            if (!rows.contains(index.row())) {
                rows << index.row();
            }
        }
    }
    std::sort(rows.begin(), rows.end());
    QStringList ids;
    for (int r : std::as_const(rows)) {
        if (r >= 0 && r < m_model->table().rows.size()) {
            ids << m_model->table().rows.at(r).id;
        }
    }
    return ids;
}

void KoraCutScriptTableBlock::showColumnMenu(const QPoint &pos)
{
    const int column = m_view->horizontalHeader()->logicalIndexAt(pos);
    if (column < 0) {
        return;
    }
    QMenu menu(this);
    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-rename")), i18nc("@action:inmenu script column", "Rename…"), this, [this, column]() {
        Q_EMIT m_view->horizontalHeader()->sectionDoubleClicked(column);
    });
    QMenu *types = typeMenu(&menu, column);
    menu.addMenu(types);
    if (column > 0) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("go-previous")), i18nc("@action:inmenu script column", "Move Left"), this, [this, column]() {
            m_model->moveColumn(column, column - 1);
        });
    }
    if (column + 1 < m_model->columnCount()) {
        menu.addAction(QIcon::fromTheme(QStringLiteral("go-next")), i18nc("@action:inmenu script column", "Move Right"), this, [this, column]() {
            m_model->moveColumn(column, column + 1);
        });
    }
    menu.addSeparator();
    menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), i18nc("@action:inmenu script column", "Delete Column"), this, [this, column]() {
        m_model->removeColumn(column);
    });
    menu.exec(m_view->horizontalHeader()->mapToGlobal(pos));
}
