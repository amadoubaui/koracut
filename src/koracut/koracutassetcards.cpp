/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutassetcards.h"

#include "assets/assetlist/model/assettreemodel.hpp"
#include "koracut/koracuteffectpreviews.h"
#include "koracut/koracutthemes.h"

#include <KColorScheme>
#include <KConfigGroup>
#include <KSharedConfig>
#include <KLocalizedString>
#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QKeySequence>
#include <QToolBar>
#include <QSet>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QToolButton>

KoraCutFlatAssetProxy::KoraCutFlatAssetProxy(QObject *parent)
    : QAbstractProxyModel(parent)
{
}

void KoraCutFlatAssetProxy::setSourceModel(QAbstractItemModel *source)
{
    if (sourceModel()) {
        disconnect(sourceModel(), nullptr, this, nullptr);
    }
    QAbstractProxyModel::setSourceModel(source);
    if (source) {
        // Tout changement de l'arbre (recherche, catégorie, favori, rechargement) reconstruit la
        // liste : quelques centaines de lignes, c'est immédiat.
        connect(source, &QAbstractItemModel::modelReset, this, &KoraCutFlatAssetProxy::rebuild);
        connect(source, &QAbstractItemModel::layoutChanged, this, &KoraCutFlatAssetProxy::rebuild);
        connect(source, &QAbstractItemModel::rowsInserted, this, &KoraCutFlatAssetProxy::rebuild);
        connect(source, &QAbstractItemModel::rowsRemoved, this, &KoraCutFlatAssetProxy::rebuild);
        connect(source, &QAbstractItemModel::rowsMoved, this, &KoraCutFlatAssetProxy::rebuild);
        connect(source, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex &topLeft, const QModelIndex &bottomRight, const QList<int> &roles) {
            for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
                const QModelIndex proxy = mapFromSource(topLeft.siblingAtRow(row));
                if (proxy.isValid()) {
                    Q_EMIT dataChanged(proxy, proxy, roles);
                }
            }
        });
    }
    rebuild();
}

void KoraCutFlatAssetProxy::rebuild()
{
    beginResetModel();
    m_rows.clear();
    QStringList categories;
    QSet<QString> seen;
    QAbstractItemModel *source = sourceModel();
    if (source) {
        // Parcours en profondeur : les effets (feuilles), dans l'ordre de l'arbre. Une catégorie est
        // un nœud de premier niveau qui a des enfants.
        std::function<void(const QModelIndex &, const QString &)> walk = [&](const QModelIndex &parent, const QString &category) {
            for (int row = 0; row < source->rowCount(parent); ++row) {
                const QModelIndex child = source->index(row, 0, parent);
                if (source->rowCount(child) > 0) {
                    const QString name = parent.isValid() ? category : child.data(Qt::DisplayRole).toString();
                    if (!parent.isValid()) {
                        categories << name;
                    }
                    walk(child, name);
                } else if (child.data(AssetTreeModel::IdRole).toString() != QLatin1String("root")) {
                    const bool keep = m_category.isEmpty() || category == m_category ||
                                      (m_category == essentialsKey() && essentials().contains(child.data(AssetTreeModel::IdRole).toString()));
                    if (keep && !(m_category == essentialsKey() && seen.contains(child.data(AssetTreeModel::IdRole).toString()))) {
                        seen.insert(child.data(AssetTreeModel::IdRole).toString());
                        m_rows << QPersistentModelIndex(child);
                    }
                }
            }
        };
        walk(QModelIndex(), QString());
        // Dans « Tous », les effets qui changent l'image d'abord, les effets techniques (vignette
        // identique à l'exemple) ensuite : sinon la grille s'ouvrait sur des désentrelaceurs.
        if (m_category.isEmpty()) {
            std::stable_partition(m_rows.begin(), m_rows.end(), [](const QPersistentModelIndex &ix) {
                return !KoraCutEffectPreviews::isNeutral(ix.data(AssetTreeModel::IdRole).toString());
            });
        }
    }
    endResetModel();
    if (categories != m_categories) {
        m_categories = categories;
        Q_EMIT categoriesChanged();
    }
}

QString KoraCutFlatAssetProxy::essentialsKey()
{
    return QStringLiteral("@essentiels");
}

const QSet<QString> &KoraCutFlatAssetProxy::essentials()
{
    // La liste courte des effets de tous les jours, déjà tenue par Kdenlive (data/preferred_effects.txt).
    static const QSet<QString> list = [] {
        QSet<QString> ids;
        QFile file(QStringLiteral(":data/preferred_effects.txt"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            for (const QString &line : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                const QString id = line.trimmed();
                if (!id.isEmpty() && !id.startsWith(QLatin1Char('#'))) {
                    ids.insert(id);
                }
            }
        }
        return ids;
    }();
    return list;
}

void KoraCutFlatAssetProxy::setCategory(const QString &category)
{
    if (category != m_category) {
        m_category = category;
        rebuild();
    }
}

QStringList KoraCutFlatAssetProxy::categories() const
{
    return m_categories;
}

QModelIndex KoraCutFlatAssetProxy::mapToSource(const QModelIndex &proxyIndex) const
{
    if (!proxyIndex.isValid() || proxyIndex.row() < 0 || proxyIndex.row() >= m_rows.size()) {
        return {};
    }
    return m_rows.at(proxyIndex.row());
}

QModelIndex KoraCutFlatAssetProxy::mapFromSource(const QModelIndex &sourceIndex) const
{
    if (!sourceIndex.isValid()) {
        return {};
    }
    const int row = m_rows.indexOf(QPersistentModelIndex(sourceIndex.siblingAtColumn(0)));
    return row < 0 ? QModelIndex() : createIndex(row, 0);
}

QModelIndex KoraCutFlatAssetProxy::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid() || column != 0 || row < 0 || row >= m_rows.size()) {
        return {};
    }
    return createIndex(row, column);
}

QModelIndex KoraCutFlatAssetProxy::parent(const QModelIndex &) const
{
    return {};
}

int KoraCutFlatAssetProxy::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int KoraCutFlatAssetProxy::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 1;
}

QMimeData *KoraCutFlatAssetProxy::mimeData(const QModelIndexList &indexes) const
{
    QModelIndexList sources;
    for (const QModelIndex &ix : indexes) {
        sources << mapToSource(ix);
    }
    return sourceModel() ? sourceModel()->mimeData(sources) : nullptr;
}

Qt::ItemFlags KoraCutFlatAssetProxy::flags(const QModelIndex &index) const
{
    return index.isValid() ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled) : Qt::NoItemFlags;
}

// ---------------------------------------------------------------------------------------------

int KoraCutAssetCardDelegate::thumbWidthForLevel(int level)
{
    // Cran 1 : trois colonnes dans un panneau de 300 px ; cran 2 (celui d'origine) : deux ; crans 3
    // et 4 : une vignette par ligne, moyenne puis pleine largeur.
    static const int widths[] = {0, 88, 128, 184, 264};
    return widths[std::clamp(level, 1, 4)];
}

QSize KoraCutAssetCardDelegate::gridFor(int level, int available, int *thumb)
{
    const int target = thumbWidthForLevel(level);
    const int gap = 12;
    const int columns = std::max(1, available / (target + gap));
    const int cell = std::max(40, available / columns);
    // La vignette s'élargit d'un quart au plus pour combler la case, sans jamais la dépasser.
    const int width = std::max(32, std::min(cell - gap + 4, target + target / 4));
    if (thumb) {
        *thumb = width;
    }
    return {cell, cardSize(width).height()};
}

int KoraCutAssetCardDelegate::thumbWidth() const
{
    return m_thumbWidth > 0 ? m_thumbWidth : KoraCutEffectPreviews::size().width();
}

QSize KoraCutAssetCardDelegate::cardSize(int thumbWidth)
{
    const int width = thumbWidth > 0 ? thumbWidth : KoraCutEffectPreviews::size().width();
    // La vignette (16:9), puis deux lignes de nom en police de liste.
    return {width + 8, width * 9 / 16 + 8 + 2 * QFontMetrics(QApplication::font()).height() + 6};
}

QSize KoraCutAssetCardDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return cardSize(thumbWidth());
}

void KoraCutAssetCardDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    // La carte, centrée dans sa case de grille (la case remplit la largeur du panneau).
    const QSize thumbSize(thumbWidth(), thumbWidth() * 9 / 16);
    const int cardWidth = std::min(option.rect.width() - 4, thumbSize.width() + 8);
    const QRect card(option.rect.left() + (option.rect.width() - cardWidth) / 2, option.rect.top() + 2, cardWidth, option.rect.height() - 4);
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    // La couleur de sélection du thème (#0D99FF dans les thèmes KoraCut), opaque.
    const QColor accent = KColorScheme(QPalette::Active, KColorScheme::Selection).background().color();

    // Fond de carte au survol, comme les lignes de liste, dans le voile du thème (clair ou sombre).
    if (hover || selected) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(KoraCutThemes::token(QStringLiteral("voile"), selected ? 26 : 16));
        painter->drawRoundedRect(card, 6, 6);
    }

    // La vignette, coins arrondis (6 px, la charte), anneau d'accent quand la carte est choisie.
    const QRect thumb(card.left() + (card.width() - thumbSize.width()) / 2, card.top() + 3, thumbSize.width(), thumbSize.height());
    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    QPainterPath clip;
    clip.addRoundedRect(thumb, 6, 6);
    painter->setClipPath(clip);
    if (!icon.isNull()) {
        painter->drawPixmap(thumb, icon.pixmap(thumbSize, painter->device() ? painter->device()->devicePixelRatio() : 1.0));
    } else {
        painter->fillRect(thumb, KoraCutThemes::token(QStringLiteral("surface")));
    }
    painter->setClipping(false);
    if (selected) {
        painter->setPen(QPen(accent, 2));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(thumb).adjusted(1, 1, -1, -1), 6, 6);
    }

    // Favori : une petite étoile dans le coin, en jaune de la charte.
    if (index.data(AssetTreeModel::FavoriteRole).toInt() > 0) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 0, 0, 140));
        const QRectF badge(thumb.right() - 20, thumb.top() + 4, 16, 16);
        painter->drawEllipse(badge);
        painter->setPen(QColor(0xFF, 0xD1, 0x66));
        painter->drawText(badge, Qt::AlignCenter, QStringLiteral("★"));
    }

    // Le nom, deux lignes au plus, coupé proprement.
    const QRect textRect(card.left() + 2, thumb.bottom() + 5, card.width() - 4, card.bottom() - thumb.bottom() - 5);
    painter->setPen(option.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
    QFont font = option.font;
    painter->setFont(font);
    const QString name = index.data(Qt::DisplayRole).toString();
    QFontMetrics fm(font);
    // Coupe à deux lignes : on garde les mots entiers et on termine par « … » si ça déborde.
    QString first;
    QString rest = name;
    const QStringList words = name.split(QLatin1Char(' '));
    QString line;
    int i = 0;
    for (; i < words.size(); ++i) {
        const QString candidate = line.isEmpty() ? words.at(i) : line + QLatin1Char(' ') + words.at(i);
        if (fm.horizontalAdvance(candidate) > textRect.width() && !line.isEmpty()) {
            break;
        }
        line = candidate;
    }
    first = fm.elidedText(line, Qt::ElideRight, textRect.width());
    rest = words.mid(i).join(QLatin1Char(' '));
    const int lineHeight = fm.height();
    painter->drawText(QRect(textRect.left(), textRect.top(), textRect.width(), lineHeight), Qt::AlignHCenter | Qt::AlignTop, first);
    if (!rest.isEmpty()) {
        painter->drawText(QRect(textRect.left(), textRect.top() + lineHeight, textRect.width(), lineHeight), Qt::AlignHCenter | Qt::AlignTop,
                          fm.elidedText(rest, Qt::ElideRight, textRect.width()));
    }
    painter->restore();
}

// ---------------------------------------------------------------------------------------------

KoraCutCategoryChips::KoraCutCategoryChips(KoraCutFlatAssetProxy *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
{
    setObjectName(QStringLiteral("koracutCategoryChips"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    connect(m_model, &KoraCutFlatAssetProxy::categoriesChanged, this, &KoraCutCategoryChips::rebuild);
    // On ouvre sur « Essentiels », comme l'onglet « Tendances » de CapCut : la liste courte d'abord.
    m_current = KoraCutFlatAssetProxy::essentialsKey();
    m_model->setCategory(m_current);
    rebuild();
}

void KoraCutCategoryChips::select(const QString &name)
{
    m_current = name;
    for (int i = 0; i < m_chips.size(); ++i) {
        m_chips.at(i)->setChecked(m_names.at(i) == name);
    }
    m_model->setCategory(name);
    layoutChips(width(), true);
}

void KoraCutCategoryChips::rebuild()
{
    qDeleteAll(m_chips);
    m_chips.clear();
    m_names.clear();
    delete m_more;
    m_more = nullptr;
    QStringList names = m_model->categories();
    // La catégorie choisie a disparu (une recherche l'a vidée) : on revient à « Tous ».
    if (!m_current.isEmpty() && m_current != KoraCutFlatAssetProxy::essentialsKey() && !names.contains(m_current)) {
        m_current.clear();
        m_model->setCategory(QString());
    }
    names.prepend(QString());
    names.prepend(KoraCutFlatAssetProxy::essentialsKey());
    for (const QString &name : std::as_const(names)) {
        auto *chip = new QToolButton(this);
        chip->setObjectName(QStringLiteral("koracutCategoryChip"));
        chip->setText(name == KoraCutFlatAssetProxy::essentialsKey() ? i18nc("@item effect category chip, everyday effects", "Essentials")
                      : name.isEmpty()                               ? i18nc("@item effect category chip, all categories", "All")
                                                                     : name);
        chip->setCheckable(true);
        chip->setChecked(name == m_current);
        chip->setAutoRaise(true);
        chip->setCursor(Qt::PointingHandCursor);
        connect(chip, &QToolButton::clicked, this, [this, name]() { select(name); });
        m_chips << chip;
        m_names << name;
    }
    // « Plus… » : dans un panneau étroit, deux rangées de pastilles suffisent ; les catégories qui
    // ne tiennent pas y sont, dans un menu. Sans cette limite, 26 rangées de pastilles imposaient
    // une hauteur minimale à toute la fenêtre (constaté le 23 septembre 2026, fenêtre étirée à
    // 1218 px de haut, plus que l'écran).
    m_more = new QToolButton(this);
    m_more->setObjectName(QStringLiteral("koracutCategoryChip"));
    m_more->setCheckable(true);
    m_more->setAutoRaise(true);
    m_more->setCursor(Qt::PointingHandCursor);
    m_more->setPopupMode(QToolButton::InstantPopup);
    m_more->setMenu(new QMenu(m_more));
    layoutChips(width(), true);
    updateGeometry();
}

int KoraCutCategoryChips::layoutChips(int width, bool apply) const
{
    // Deux rangées au plus, 4 px entre deux pastilles (la charte). Ce qui ne tient pas va dans
    // « Plus… », posé en fin de deuxième rangée.
    const int gap = 4;
    const int margin = 6;
    const int maxRows = 2;
    const int moreWidth = m_more ? m_more->sizeHint().width() : 0;
    struct Slot
    {
        QRect rect;
        bool shown;
    };
    QList<Slot> slots;
    int x = margin;
    int y = 4;
    int row = 1;
    int lineHeight = 0;
    bool overflow = false;
    for (int i = 0; i < m_chips.size(); ++i) {
        const QSize size = m_chips.at(i)->sizeHint();
        lineHeight = std::max(lineHeight, size.height());
        if (overflow) {
            slots << Slot{QRect(), false};
            continue;
        }
        // Reste-t-il de la place pour cette pastille, et pour « Plus… » si d'autres suivent ?
        const bool lastChip = i == m_chips.size() - 1;
        const int needed = size.width() + (lastChip ? 0 : gap + moreWidth);
        if (x > margin && x + size.width() > width - margin) {
            if (row == maxRows) {
                overflow = true;
                slots << Slot{QRect(), false};
                continue;
            }
            x = margin;
            y += lineHeight + gap;
            ++row;
        }
        if (row == maxRows && !lastChip && x + needed > width - margin) {
            overflow = true;
            slots << Slot{QRect(), false};
            continue;
        }
        slots << Slot{QRect(QPoint(x, y), size), true};
        x += size.width() + gap;
    }
    if (apply) {
        QStringList hidden;
        for (int i = 0; i < m_chips.size(); ++i) {
            m_chips.at(i)->setVisible(slots.at(i).shown);
            if (slots.at(i).shown) {
                m_chips.at(i)->setGeometry(slots.at(i).rect);
            } else {
                hidden << m_names.at(i);
            }
        }
        if (m_more) {
            m_more->setVisible(overflow);
            if (overflow) {
                // Le menu des catégories cachées ; si la catégorie choisie en fait partie, « Plus… »
                // prend son nom et s'allume.
                m_more->menu()->clear();
                for (const QString &name : std::as_const(hidden)) {
                    QAction *action = m_more->menu()->addAction(name);
                    action->setCheckable(true);
                    action->setChecked(name == m_current);
                    auto *self = const_cast<KoraCutCategoryChips *>(this);
                    connect(action, &QAction::triggered, self, [self, name]() { self->select(name); });
                }
                const bool currentHidden = hidden.contains(m_current);
                m_more->setText(currentHidden ? m_current : i18nc("@action:button more effect categories", "More…"));
                m_more->setChecked(currentHidden);
                m_more->setGeometry(QRect(QPoint(x, y), m_more->sizeHint()));
            }
        }
    }
    return y + lineHeight + 6;
}

int KoraCutCategoryChips::heightForWidth(int width) const
{
    return layoutChips(width, false);
}

QSize KoraCutCategoryChips::sizeHint() const
{
    return {width() > 0 ? width() : 260, heightForWidth(width() > 0 ? width() : 260)};
}

QSize KoraCutCategoryChips::minimumSizeHint() const
{
    return {60, heightForWidth(width() > 0 ? width() : 260)};
}

void KoraCutCategoryChips::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    const int height = layoutChips(event->size().width(), true);
    if (height != this->height()) {
        setFixedHeight(height);
    }
}

// ---------------------------------------------------------------------------------------------

KoraCutAssetZoom::KoraCutAssetZoom(const QString &key, int levels, int fallback, QObject *parent)
    : QObject(parent)
    , m_key(key)
    , m_levels(std::max(2, levels))
{
    const KConfigGroup group(KSharedConfig::openConfig(), QStringLiteral("KoraCut"));
    m_level = std::clamp(group.readEntry(m_key, fallback), 0, m_levels - 1);
    const QString smallerKey = QKeySequence(Qt::CTRL | Qt::Key_Minus).toString(QKeySequence::NativeText);
    const QString biggerKey = QKeySequence(Qt::CTRL | Qt::Key_Plus).toString(QKeySequence::NativeText);
    m_smaller = new QAction(QIcon::fromTheme(QStringLiteral("zoom-out")), i18nc("@action:button asset previews", "Smaller Previews"), this);
    m_smaller->setToolTip(i18nc("@info:tooltip %1 keyboard shortcut", "Smaller previews (%1)", smallerKey));
    m_smaller->setObjectName(QStringLiteral("koracut_asset_zoom_out"));
    m_bigger = new QAction(QIcon::fromTheme(QStringLiteral("zoom-in")), i18nc("@action:button asset previews", "Larger Previews"), this);
    m_bigger->setToolTip(i18nc("@info:tooltip %1 keyboard shortcut", "Larger previews (%1)", biggerKey));
    m_bigger->setObjectName(QStringLiteral("koracut_asset_zoom_in"));
    connect(m_smaller, &QAction::triggered, this, [this]() { setLevel(m_level - 1); });
    connect(m_bigger, &QAction::triggered, this, [this]() { setLevel(m_level + 1); });
    updateActions();
}

void KoraCutAssetZoom::install(QToolBar *bar, QAction *after)
{
    const QList<QAction *> actions = bar->actions();
    const int at = after ? int(actions.indexOf(after)) : -1;
    QAction *before = (at >= 0 && at + 1 < actions.size()) ? actions.at(at + 1) : (at < 0 && !actions.isEmpty() ? actions.first() : nullptr);
    bar->insertAction(before, m_smaller);
    bar->insertAction(before, m_bigger);
}

void KoraCutAssetZoom::watch(QWidget *widget)
{
    widget->installEventFilter(this);
}

void KoraCutAssetZoom::followWidth(QWidget *widget)
{
    m_followed << widget;
    widget->installEventFilter(this);
}

void KoraCutAssetZoom::setLevel(int level)
{
    level = std::clamp(level, 0, m_levels - 1);
    if (level == m_level) {
        return;
    }
    m_level = level;
    KConfigGroup group(KSharedConfig::openConfig(), QStringLiteral("KoraCut"));
    group.writeEntry(m_key, m_level);
    updateActions();
    Q_EMIT levelChanged(m_level);
}

void KoraCutAssetZoom::updateActions()
{
    m_smaller->setEnabled(m_level > 0);
    m_bigger->setEnabled(m_level < m_levels - 1);
}

bool KoraCutAssetZoom::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize && m_followed.contains(watched)) {
        auto *resize = static_cast<QResizeEvent *>(event);
        if (resize->size().width() != resize->oldSize().width()) {
            Q_EMIT widthChanged();
        }
        return QObject::eventFilter(watched, event);
    }
    if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress) {
        return QObject::eventFilter(watched, event);
    }
    auto *key = static_cast<QKeyEvent *>(event);
    // ⌘ (Ctrl hors macOS), avec ou sans Maj : « + » s'obtient avec Maj sur bien des claviers.
    const Qt::KeyboardModifiers mods = key->modifiers() & ~(Qt::ShiftModifier | Qt::KeypadModifier);
    if (mods != Qt::ControlModifier) {
        return QObject::eventFilter(watched, event);
    }
    int step = 0;
    if (key->key() == Qt::Key_Plus || key->key() == Qt::Key_Equal) {
        step = 1;
    } else if (key->key() == Qt::Key_Minus || key->key() == Qt::Key_Underscore) {
        step = -1;
    }
    if (step == 0) {
        return QObject::eventFilter(watched, event);
    }
    // ShortcutOverride accepté : la touche vient au panneau au lieu de zoomer la timeline.
    event->accept();
    if (event->type() == QEvent::KeyPress) {
        setLevel(m_level + step);
    }
    return true;
}
