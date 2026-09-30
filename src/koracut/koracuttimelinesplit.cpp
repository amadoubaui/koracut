/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracuttimelinesplit.h"
#include "koracutthemes.h"

#include <KLocalizedString>

#include <QApplication>
#include <QEnterEvent>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace KoraCutSplit {

Side sideFromName(const QString &name, bool *ok)
{
    const QString n = name.trimmed().toLower();
    Side side = Side::Center;
    bool found = true;
    if (n == QLatin1String("gauche") || n == QLatin1String("left")) {
        side = Side::Left;
    } else if (n == QLatin1String("droite") || n == QLatin1String("right")) {
        side = Side::Right;
    } else if (n == QLatin1String("haut") || n == QLatin1String("top") || n == QLatin1String("dessus")) {
        side = Side::Top;
    } else if (n == QLatin1String("bas") || n == QLatin1String("bottom") || n == QLatin1String("dessous")) {
        side = Side::Bottom;
    } else if (n == QLatin1String("onglet") || n == QLatin1String("tab") || n == QLatin1String("centre") || n == QLatin1String("center")) {
        side = Side::Center;
    } else {
        found = false;
    }
    if (ok) {
        *ok = found;
    }
    return side;
}

QString sideName(Side side)
{
    switch (side) {
    case Side::Left:
        return QStringLiteral("gauche");
    case Side::Right:
        return QStringLiteral("droite");
    case Side::Top:
        return QStringLiteral("haut");
    case Side::Bottom:
        return QStringLiteral("bas");
    case Side::Center:
        break;
    }
    return QStringLiteral("onglet");
}

Side sideAt(const QRect &area, const QPoint &point, int header)
{
    if (!area.contains(point) || point.y() < area.top() + header) {
        return Side::Center;
    }
    const QRect body = area.adjusted(0, header, 0, 0);
    if (body.width() <= 0 || body.height() <= 0) {
        return Side::Center;
    }
    const double fx = double(point.x() - body.left()) / body.width();
    const double fy = double(point.y() - body.top()) / body.height();
    const std::pair<double, Side> edges[] = {{fx, Side::Left}, {1. - fx, Side::Right}, {fy, Side::Top}, {1. - fy, Side::Bottom}};
    const auto nearest = std::min_element(std::begin(edges), std::end(edges), [](const auto &a, const auto &b) { return a.first < b.first; });
    return nearest->first < 0.3 ? nearest->second : Side::Center;
}

QRect zoneRect(const QRect &area, Side side)
{
    switch (side) {
    case Side::Left:
        return QRect(area.left(), area.top(), area.width() / 2, area.height());
    case Side::Right:
        return QRect(area.left() + area.width() / 2, area.top(), area.width() - area.width() / 2, area.height());
    case Side::Top:
        return QRect(area.left(), area.top(), area.width(), area.height() / 2);
    case Side::Bottom:
        return QRect(area.left(), area.top() + area.height() / 2, area.width(), area.height() - area.height() / 2);
    case Side::Center:
        break;
    }
    return area;
}

} // namespace KoraCutSplit

// ---- Le repère de dépôt ------------------------------------------------------------------------

class KoraCutTimelineSplitHost::DropOverlay : public QWidget
{
public:
    explicit DropOverlay(QWidget *parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        hide();
    }
    QRect zone;
    QString label;
    QPoint cursor;
    bool valid{false};

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor accent(0x0D, 0x99, 0xFF);
        if (valid && zone.isValid()) {
            // L'emplacement que prendra la séquence : accent à 16 %, filet de 2 px, rayon 6.
            QColor fill = accent;
            fill.setAlpha(40);
            p.setPen(QPen(accent, 2));
            p.setBrush(fill);
            p.drawRoundedRect(QRectF(zone).adjusted(3, 3, -3, -3), 6, 6);
        }
        if (!label.isEmpty()) {
            // L'étiquette qui suit le pointeur : le nom de la séquence, sur une pastille de contrôle.
            QFont f = font();
            f.setPixelSize(12);
            f.setWeight(QFont::Medium);
            p.setFont(f);
            const QFontMetrics fm(f);
            const int w = std::min(fm.horizontalAdvance(label) + 20, 260);
            QRect pill(cursor + QPoint(14, 14), QSize(w, 24));
            pill.moveRight(std::min(pill.right(), width() - 4));
            pill.moveBottom(std::min(pill.bottom(), height() - 4));
            const QPalette pal = palette();
            p.setPen(Qt::NoPen);
            p.setBrush(valid ? pal.color(QPalette::Button) : pal.color(QPalette::Window));
            p.drawRoundedRect(QRectF(pill), 6, 6);
            p.setPen(pal.color(QPalette::ButtonText));
            p.drawText(pill.adjusted(10, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, fm.elidedText(label, Qt::ElideRight, w - 20));
        }
    }
};

// ---- La zone et sa découpe ---------------------------------------------------------------------

KoraCutTimelineSplitHost::KoraCutTimelineSplitHost(QWidget *parent)
    : QWidget(parent)
    , m_layout(new QVBoxLayout(this))
    , m_overlay(new DropOverlay(this))
{
    setObjectName(QStringLiteral("koracutTimelineSplit"));
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
}

void KoraCutTimelineSplitHost::setRoot(QWidget *root)
{
    if (m_root) {
        m_layout->removeWidget(m_root);
    }
    m_root = root;
    m_layout->addWidget(root);
}

QSplitter *KoraCutTimelineSplitHost::newSplitter(Qt::Orientation orientation)
{
    auto *splitter = new QSplitter(orientation);
    splitter->setObjectName(QStringLiteral("koracutTimelineSplitter"));
    splitter->setChildrenCollapsible(false);
    // La même gouttière que entre deux panneaux : 4 px de fond, rien d'autre.
    splitter->setHandleWidth(4);
    return splitter;
}

void KoraCutTimelineSplitHost::equalize(QSplitter *splitter)
{
    const int total = splitter->orientation() == Qt::Horizontal ? splitter->width() : splitter->height();
    const int n = splitter->count();
    if (n == 0) {
        return;
    }
    splitter->setSizes(QList<int>(n, std::max(1, total / n)));
}

void KoraCutTimelineSplitHost::place(QWidget *moving, QWidget *target, KoraCutSplit::Side side)
{
    using KoraCutSplit::Side;
    if (side == Side::Center || moving == nullptr || m_root == nullptr) {
        return;
    }
    QWidget *anchor = target ? target : m_root;
    const Qt::Orientation orientation = (side == Side::Left || side == Side::Right) ? Qt::Horizontal : Qt::Vertical;
    const bool after = side == Side::Right || side == Side::Bottom;
    auto *parentSplitter = qobject_cast<QSplitter *>(anchor->parentWidget());
    if (anchor == m_root) {
        parentSplitter = nullptr;
    }
    if (parentSplitter && parentSplitter->orientation() == orientation) {
        // Même sens : la vue prend place à côté, dans le même séparateur.
        const int index = parentSplitter->indexOf(anchor);
        parentSplitter->insertWidget(after ? index + 1 : index, moving);
        moving->show();
        equalize(parentSplitter);
        return;
    }
    // Autre sens : l'ancre et la vue partagent un nouveau séparateur, posé à la place de l'ancre.
    // On ne passe jamais par un parent nul : les timelines sont des QQuickWidget, et une vue sans
    // fenêtre perdrait son contexte de rendu pour rien.
    QSplitter *splitter = newSplitter(orientation);
    if (parentSplitter) {
        const QList<int> sizes = parentSplitter->sizes();
        const int index = parentSplitter->indexOf(anchor);
        parentSplitter->insertWidget(index, splitter);
        splitter->addWidget(anchor);
        parentSplitter->setSizes(sizes);
    } else {
        m_layout->replaceWidget(anchor, splitter);
        splitter->addWidget(anchor);
        m_root = splitter;
    }
    if (after) {
        splitter->addWidget(moving);
    } else {
        splitter->insertWidget(0, moving);
    }
    splitter->show();
    anchor->show();
    moving->show();
    // La taille n'est connue qu'une fois le séparateur posé : on partage au tour suivant.
    QTimer::singleShot(0, splitter, [splitter]() { equalize(splitter); });
    equalize(splitter);
    m_overlay->raise();
}

void KoraCutTimelineSplitHost::remove(QWidget *view)
{
    if (view == nullptr || view == m_root) {
        return;
    }
    auto *splitter = qobject_cast<QSplitter *>(view->parentWidget());
    view->hide();
    view->setParent(this);
    // Un séparateur qui ne garde qu'une vue n'a plus de raison d'être : la vue prend sa place.
    while (splitter && splitter->count() == 1) {
        QWidget *only = splitter->widget(0);
        if (splitter == m_root) {
            m_layout->replaceWidget(splitter, only);
            m_root = only;
        } else if (auto *parentSplitter = qobject_cast<QSplitter *>(splitter->parentWidget())) {
            const QList<int> sizes = parentSplitter->sizes();
            parentSplitter->insertWidget(parentSplitter->indexOf(splitter), only);
            parentSplitter->setSizes(sizes);
        } else {
            break;
        }
        only->show();
        auto *parentSplitter = qobject_cast<QSplitter *>(splitter->parentWidget());
        splitter->hide();
        splitter->setParent(this);
        splitter->deleteLater();
        splitter = parentSplitter;
    }
    m_overlay->raise();
}

static QString describeWidget(const QWidget *w)
{
    if (const auto *s = qobject_cast<const QSplitter *>(w)) {
        QStringList parts;
        for (int i = 0; i < s->count(); ++i) {
            parts << describeWidget(s->widget(i));
        }
        return (s->orientation() == Qt::Horizontal ? QStringLiteral("H[") : QStringLiteral("V[")) + parts.join(QLatin1Char(',')) + QLatin1Char(']');
    }
    return w ? w->objectName() : QString();
}

QString KoraCutTimelineSplitHost::describe() const
{
    return describeWidget(m_root);
}

static QJsonObject serializeWidget(QWidget *w, const std::function<QJsonObject(QWidget *)> &leaf)
{
    auto *s = qobject_cast<QSplitter *>(w);
    if (s == nullptr) {
        return leaf(w);
    }
    QJsonArray views;
    QJsonArray sizes;
    const QList<int> px = s->sizes();
    int total = 0;
    for (int v : px) {
        total += v;
    }
    for (int i = 0; i < s->count(); ++i) {
        views.append(serializeWidget(s->widget(i), leaf));
        // En fractions, arrondies au millième : la fenêtre n'aura pas la même taille à la réouverture.
        sizes.append(total > 0 ? std::round(1000. * px.value(i) / total) / 1000. : 1. / s->count());
    }
    return QJsonObject{{QStringLiteral("sens"), s->orientation() == Qt::Horizontal ? QStringLiteral("h") : QStringLiteral("v")},
                       {QStringLiteral("vues"), views},
                       {QStringLiteral("tailles"), sizes}};
}

QJsonObject KoraCutTimelineSplitHost::serialize(const std::function<QJsonObject(QWidget *)> &leaf) const
{
    return m_root ? serializeWidget(m_root, leaf) : QJsonObject();
}

QWidget *KoraCutTimelineSplitHost::build(const QJsonObject &node, const std::function<QWidget *(const QJsonObject &)> &leaf)
{
    if (!node.contains(QStringLiteral("sens"))) {
        return leaf(node);
    }
    QList<QWidget *> children;
    QList<double> fractions;
    const QJsonArray views = node.value(QStringLiteral("vues")).toArray();
    const QJsonArray sizes = node.value(QStringLiteral("tailles")).toArray();
    for (int i = 0; i < views.size(); ++i) {
        if (QWidget *w = build(views.at(i).toObject(), leaf)) {
            children << w;
            fractions << std::max(0.05, sizes.at(i).toDouble(1. / views.size()));
        }
    }
    if (children.isEmpty()) {
        return nullptr;
    }
    if (children.size() == 1) {
        return children.first();
    }
    QSplitter *splitter = newSplitter(node.value(QStringLiteral("sens")).toString() == QLatin1String("h") ? Qt::Horizontal : Qt::Vertical);
    QList<int> px;
    for (int i = 0; i < children.size(); ++i) {
        splitter->addWidget(children.at(i));
        px << int(fractions.at(i) * 10000);
    }
    // QSplitter ramène ces valeurs à la place réelle, en gardant les proportions.
    splitter->setSizes(px);
    return splitter;
}

bool KoraCutTimelineSplitHost::restore(const QJsonObject &node, const std::function<QWidget *(const QJsonObject &)> &leaf)
{
    if (m_root == nullptr || qobject_cast<QSplitter *>(m_root) || node.isEmpty()) {
        return false;
    }
    QWidget *previous = m_root;
    QWidget *built = build(node, leaf);
    if (built == nullptr || (built != previous && !built->isAncestorOf(previous))) {
        return false;
    }
    if (built != previous) {
        // La vue principale est déjà passée dans un séparateur : la mise en page l'a lâchée.
        m_layout->removeWidget(previous);
        m_layout->addWidget(built);
        m_root = built;
        built->show();
        const QList<QWidget *> all = built->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget *w : all) {
            if (!qobject_cast<QSplitterHandle *>(w)) {
                w->show();
            }
        }
    }
    m_overlay->raise();
    return true;
}

void KoraCutTimelineSplitHost::showDropZone(const QRect &zone, const QString &label, const QPoint &cursor, bool valid)
{
    m_overlay->setGeometry(rect());
    m_overlay->zone = zone;
    m_overlay->label = label;
    m_overlay->cursor = cursor;
    m_overlay->valid = valid;
    m_overlay->show();
    m_overlay->raise();
    m_overlay->update();
}

void KoraCutTimelineSplitHost::hideDropZone()
{
    m_overlay->hide();
}

// ---- La barre des onglets de séquences ---------------------------------------------------------

KoraCutSequenceTabBar::KoraCutSequenceTabBar(QWidget *parent)
    : QTabBar(parent)
{
    setMouseTracking(true);
}

QSize KoraCutSequenceTabBar::tabSizeHint(int index) const
{
    // Largeur naturelle (icône, nom, croix) bornée : un nom court ne fait pas un onglet minuscule,
    // un nom long ne mange pas la barre (il s'abrège, le nom entier est dans l'infobulle).
    QSize size = QTabBar::tabSizeHint(index);
    size.setWidth(std::clamp(size.width(), kMinWidth, kMaxWidth));
    return size;
}

QSize KoraCutSequenceTabBar::minimumTabSizeHint(int index) const
{
    // Quand les onglets ne tiennent plus (huit séquences dans une vue étroite), Qt les réduit
    // jusqu'à cette largeur avant de montrer les flèches de défilement.
    QSize size = QTabBar::minimumTabSizeHint(index);
    size.setWidth(std::max(size.width(), kShrunkWidth));
    return size;
}

int KoraCutSequenceTabBar::naturalExtent() const
{
    int total = 0;
    for (int i = 0; i < count(); ++i) {
        total += tabSizeHint(i).width();
    }
    return total;
}

int KoraCutSequenceTabBar::shrunkExtent() const
{
    int total = 0;
    for (int i = 0; i < count(); ++i) {
        total += minimumTabSizeHint(i).width();
    }
    return total;
}

void KoraCutSequenceTabBar::mouseMoveEvent(QMouseEvent *event)
{
    QTabBar::mouseMoveEvent(event);
    const int hovered = tabAt(event->position().toPoint());
    if (hovered != m_hovered) {
        m_hovered = hovered;
        update();
    }
}

void KoraCutSequenceTabBar::leaveEvent(QEvent *event)
{
    QTabBar::leaveEvent(event);
    if (m_hovered != -1) {
        m_hovered = -1;
        update();
    }
}

void KoraCutSequenceTabBar::paintEvent(QPaintEvent *event)
{
    QTabBar::paintEvent(event);
    if (count() < 2) {
        return;
    }
    // Un trait de 1 px sur 14 px entre deux onglets restés sur le fond. À côté de l'onglet choisi
    // ou survolé, pas de trait : leur surface suffit à les détacher (une limite, pas deux).
    QPainter p(this);
    p.setPen(Qt::NoPen);
    p.setBrush(KoraCutThemes::token(QStringLiteral("voile"), 38));
    const int current = currentIndex();
    for (int i = 0; i < count() - 1; ++i) {
        if (i == current || i + 1 == current || i == m_hovered || i + 1 == m_hovered) {
            continue;
        }
        const QRect left = tabRect(i);
        const QRect right = tabRect(i + 1);
        if (!event->rect().intersects(left.united(right))) {
            continue;
        }
        // Le corps de l'onglet commence sous sa marge haute (4 px, feuille de style).
        const int top = left.top() + 4;
        const int middle = top + (left.bottom() - top) / 2;
        const int x = (left.right() + right.left() + 1) / 2;
        p.drawRect(QRect(x, middle - 7, 1, 14));
    }
}

// ---- La croix d'un onglet ----------------------------------------------------------------------

KoraCutTabCloseButton::KoraCutTabCloseButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setObjectName(QStringLiteral("koracutTabClose"));
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
    resize(sizeHint());
}

QSize KoraCutTabCloseButton::sizeHint() const
{
    // 16 × 16 visibles, plus 4 px en haut : la barre centre la croix sur l'onglet marge comprise,
    // la marge haute de 4 px la faisait remonter de 2 px au-dessus du nom.
    return QSize(16, 20);
}

void KoraCutTabCloseButton::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(event);
}

void KoraCutTabCloseButton::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void KoraCutTabCloseButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(0, 4, 0, 0);
    QColor text = KoraCutThemes::token(QStringLiteral("texte"));
    if (m_hover) {
        p.setPen(Qt::NoPen);
        p.setBrush(KoraCutThemes::token(QStringLiteral("voile"), 36));
        p.drawRoundedRect(box, 4, 4);
    } else {
        text.setAlpha(150);
    }
    // Une croix de 7 px au trait de 1,5 px, comme les icônes Lucide.
    p.setPen(QPen(text, 1.5, Qt::SolidLine, Qt::RoundCap));
    const QPointF c = box.center();
    const double r = 3.25;
    p.drawLine(c + QPointF(-r, -r), c + QPointF(r, r));
    p.drawLine(c + QPointF(-r, r), c + QPointF(r, -r));
}

// ---- Les gestes de la barre d'onglets ----------------------------------------------------------

KoraCutTabBarDriver::KoraCutTabBarDriver(QTabWidget *tabs, bool withAddButton)
    : QObject(tabs)
    , m_tabs(tabs)
{
    QTabBar *bar = tabs->tabBar();
    bar->setObjectName(QStringLiteral("koracutSequenceTabs"));
    bar->setProperty("koracutActive", true);
    bar->setMouseTracking(true);
    bar->setExpanding(false);
    bar->setElideMode(Qt::ElideRight);
    bar->setUsesScrollButtons(true);
    bar->setDrawBase(false);
    bar->setIconSize(QSize(14, 14));
    bar->installEventFilter(this);
    tabs->installEventFilter(this);
    connect(bar, &QTabBar::currentChanged, this, [this]() { updateButtons(); });
    connect(bar, &QTabBar::tabMoved, this, [this]() {
        updateButtons();
        placeAddButton();
    });
    // La bande derrière toute la rangée d'onglets : la barre s'arrête avant le « + », la bande
    // garde la même surface jusqu'au bord.
    m_strip = new QWidget(tabs);
    m_strip->setObjectName(QStringLiteral("koracutSequenceStrip"));
    m_strip->setAttribute(Qt::WA_StyledBackground);
    m_strip->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_strip->hide();
    if (withAddButton) {
        m_add = new QToolButton(tabs);
        m_add->setObjectName(QStringLiteral("koracutSequenceAdd"));
        m_add->setAutoRaise(true);
        m_add->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
        m_add->setIconSize(QSize(14, 14));
        m_add->setFixedSize(24, 24);
        m_add->setFocusPolicy(Qt::NoFocus);
        connect(m_add, &QToolButton::clicked, this, &KoraCutTabBarDriver::addRequested);
        // Combien de séquences sont ouvertes, discrètement au bord droit (à partir de deux).
        m_count = new QLabel(tabs);
        m_count->setObjectName(QStringLiteral("koracutSequenceCount"));
        m_count->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_count->hide();
    }
}

void KoraCutTabBarDriver::setClosable(bool closable)
{
    m_closable = closable;
    updateButtons();
}

void KoraCutTabBarDriver::setCloseToolTip(const QString &tip)
{
    m_closeTip = tip;
    updateButtons();
}

void KoraCutTabBarDriver::setActivePane(bool active)
{
    QTabBar *bar = m_tabs->tabBar();
    if (bar->property("koracutActive").toBool() == active) {
        return;
    }
    bar->setProperty("koracutActive", active);
    bar->style()->unpolish(bar);
    bar->style()->polish(bar);
    bar->update();
}

void KoraCutTabBarDriver::refresh()
{
    updateButtons();
    QTimer::singleShot(0, this, &KoraCutTabBarDriver::placeAddButton);
}

void KoraCutTabBarDriver::updateButtons()
{
    QTabBar *bar = m_tabs->tabBar();
    for (int i = 0; i < bar->count(); ++i) {
        auto *button = qobject_cast<KoraCutTabCloseButton *>(bar->tabButton(i, QTabBar::RightSide));
        if (button == nullptr) {
            button = new KoraCutTabCloseButton(bar);
            connect(button, &QAbstractButton::clicked, this, [this, button]() {
                QTabBar *b = m_tabs->tabBar();
                for (int j = 0; j < b->count(); ++j) {
                    if (b->tabButton(j, QTabBar::RightSide) == button) {
                        Q_EMIT closeRequested(m_tabs->widget(j));
                        return;
                    }
                }
            });
            bar->setTabButton(i, QTabBar::RightSide, button);
        }
        button->setToolTip(m_closeTip);
        // Comme Chrome : la croix de l'onglet choisi et celle de l'onglet survolé. Sa place reste
        // réservée, les onglets ne changent pas de largeur au survol.
        const bool shown = m_closable && (i == bar->currentIndex() || i == m_hovered);
        button->setVisible(shown);
    }
}

void KoraCutTabBarDriver::placeAddButton()
{
    QTabBar *bar = m_tabs->tabBar();
    if (!bar->isVisible() || bar->count() == 0) {
        m_strip->hide();
        if (m_add) {
            m_add->hide();
        }
        return;
    }
    m_strip->setGeometry(0, bar->geometry().top(), m_tabs->width(), bar->height());
    m_strip->show();
    m_strip->lower();
    if (m_add == nullptr) {
        return;
    }
    // Le corps des onglets commence sous leur marge haute (4 px) : le « + » et le compteur se
    // centrent sur lui, pas sur la bande entière.
    const int bodyTop = bar->geometry().top() + 4;
    const int bodyHeight = std::max(0, bar->height() - 4);
    auto *sequenceBar = qobject_cast<KoraCutSequenceTabBar *>(bar);
    const int natural = sequenceBar ? sequenceBar->naturalExtent() : bar->sizeHint().width();
    const int shrunk = sequenceBar ? sequenceBar->shrunkExtent() : natural;
    int reserved = m_add->width() + 12;
    if (m_count) {
        const int total = std::max(m_total, int(bar->count()));
        m_count->setText(i18ncp("@info number of open sequences", "%1 sequence", "%1 sequences", total));
        m_count->adjustSize();
        // Le compteur s'efface quand les onglets ont besoin de sa place : il ne dit rien que les
        // onglets ne montrent déjà. Décidé sur la largeur naturelle des onglets, pas sur leur largeur
        // du moment, pour ne pas clignoter.
        const int countWidth = m_count->width() + 12;
        const bool room = natural + reserved + countWidth + 8 <= m_tabs->width();
        m_count->setVisible(total > 1 && room);
        if (m_count->isVisible()) {
            m_count->move(m_tabs->width() - m_count->width() - 12, bodyTop + (bodyHeight - m_count->height()) / 2);
            m_count->raise();
            reserved += countWidth;
        }
    }
    // Le « + » suit le dernier onglet, comme dans Chrome ; quand les onglets remplissent la
    // largeur, il se range au bord droit et la barre lui laisse sa place. Quand ils défilent, il
    // passe après les flèches de défilement de la barre, jamais dessus.
    const int barWidth = std::max(80, m_tabs->width() - reserved);
    bar->setMaximumWidth(barWidth);
    const bool scrolling = shrunk > barWidth;
    const QRect last = bar->tabRect(bar->count() - 1);
    int x = bar->geometry().left() + last.right() + 4;
    x = std::min(x, m_tabs->width() - reserved + 6);
    if (scrolling) {
        x = bar->geometry().left() + std::min(bar->width(), barWidth) + 4;
    }
    const int y = bodyTop + (bodyHeight - m_add->height()) / 2;
    m_add->move(x, y);
    m_add->show();
    m_add->raise();
}

void KoraCutTabBarDriver::startRename(int index)
{
    QTabBar *bar = m_tabs->tabBar();
    if (index < 0 || index >= bar->count()) {
        return;
    }
    if (m_editor) {
        m_editor->deleteLater();
    }
    QPointer<QWidget> page = m_tabs->widget(index);
    const QString before = KLocalizedString::removeAcceleratorMarker(bar->tabText(index));
    auto *editor = new QLineEdit(bar);
    m_editor = editor;
    editor->setObjectName(QStringLiteral("koracutSequenceRename"));
    editor->setText(before);
    editor->setGeometry(bar->tabRect(index).adjusted(6, 7, -24, -3));
    editor->selectAll();
    editor->show();
    editor->setFocus(Qt::OtherFocusReason);
    editor->installEventFilter(this);
    // Entrée ou clic ailleurs : on garde le nom. Échap : on repart comme avant (voir eventFilter).
    connect(editor, &QLineEdit::editingFinished, this, [this, editor, page, before]() {
        const QString name = editor->text().trimmed();
        const bool keep = !editor->property("koracutCancelled").toBool();
        editor->hide();
        editor->deleteLater();
        if (keep && page && !name.isEmpty() && name != before) {
            Q_EMIT renamed(page, name);
        }
    });
}

bool KoraCutTabBarDriver::eventFilter(QObject *watched, QEvent *event)
{
    QTabBar *bar = m_tabs->tabBar();
    if (watched == m_editor && event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
        m_editor->setProperty("koracutCancelled", true);
        Q_EMIT m_editor->editingFinished();
        return true;
    }
    if (watched == m_tabs) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show) {
            QTimer::singleShot(0, this, &KoraCutTabBarDriver::placeAddButton);
        }
        return false;
    }
    if (watched != bar) {
        return false;
    }
    switch (event->type()) {
    case QEvent::Show:
    case QEvent::Hide:
    case QEvent::Resize:
    case QEvent::Move:
        QTimer::singleShot(0, this, &KoraCutTabBarDriver::placeAddButton);
        break;
    case QEvent::MouseMove: {
        auto *me = static_cast<QMouseEvent *>(event);
        const QPoint pos = me->position().toPoint();
        if ((me->buttons() & Qt::LeftButton) && m_pressPage && !m_dragCancelled) {
            if (!m_dragging) {
                // L'onglet sort de la barre, vers le haut ou le bas : c'est un glisser vers une autre
                // vue. Tant qu'il reste dans la barre, QTabBar le réordonne lui-même.
                const int out = 14;
                const bool outside = pos.y() < -out || pos.y() > bar->height() + out;
                if (outside && (!m_dragOutAllowed || m_dragOutAllowed(m_pressPage))) {
                    m_dragging = true;
                }
            }
            if (m_dragging) {
                Q_EMIT dragMoved(m_pressPage, me->globalPosition().toPoint());
                return true;
            }
        }
        const int hovered = bar->tabAt(pos);
        if (hovered != m_hovered) {
            m_hovered = hovered;
            updateButtons();
        }
        break;
    }
    case QEvent::Leave:
        if (m_hovered != -1) {
            m_hovered = -1;
            updateButtons();
        }
        break;
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            const int index = bar->tabAt(me->position().toPoint());
            m_pressPage = index >= 0 ? m_tabs->widget(index) : nullptr;
            m_pressPos = me->position().toPoint();
            m_dragging = false;
            m_dragCancelled = false;
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            if (m_dragging) {
                // La barre finit d'abord son geste (l'onglet revient à sa place), puis on pose.
                QPointer<QWidget> page = m_pressPage;
                const QPoint global = me->globalPosition().toPoint();
                QTimer::singleShot(0, this, [this, page, global]() {
                    if (page) {
                        Q_EMIT dropped(page, global);
                    } else {
                        Q_EMIT dragCancelled();
                    }
                });
            }
            m_dragging = false;
            m_pressPage = nullptr;
        }
        break;
    }
    case QEvent::KeyPress:
        if (m_dragging && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            m_dragging = false;
            m_dragCancelled = true;
            Q_EMIT dragCancelled();
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

// ---- Le voile d'une vue inactive ---------------------------------------------------------------

KoraCutTimelineVeil::KoraCutTimelineVeil(QTabWidget *pane)
    : QWidget(pane)
    , m_pane(pane)
{
    setObjectName(QStringLiteral("koracutTimelineVeil"));
    setAttribute(Qt::WA_NoSystemBackground);
    setCursor(Qt::PointingHandCursor);
    setToolTip(i18nc("@info:tooltip", "Click to work on this sequence"));
    m_stack = pane->findChild<QStackedWidget *>(QString(), Qt::FindDirectChildrenOnly);
    if (m_stack) {
        m_stack->installEventFilter(this);
    }
    pane->installEventFilter(this);
    hide();
}

void KoraCutTimelineVeil::setActive(bool shown)
{
    m_wanted = shown;
    follow();
}

void KoraCutTimelineVeil::follow()
{
    QWidget *page = m_pane->currentWidget();
    if (!m_wanted || page == nullptr || !m_pane->isVisible()) {
        hide();
        return;
    }
    setGeometry(QRect(page->mapTo(m_pane, QPoint(0, 0)), page->size()));
    show();
    raise();
}

bool KoraCutTimelineVeil::eventFilter(QObject *, QEvent *event)
{
    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::Move:
    case QEvent::Show:
    case QEvent::LayoutRequest:
        QTimer::singleShot(0, this, &KoraCutTimelineVeil::follow);
        break;
    default:
        break;
    }
    return false;
}

void KoraCutTimelineVeil::paintEvent(QPaintEvent *)
{
    // Un voile léger de la couleur du fond : la séquence reste lisible, mais on voit tout de
    // suite laquelle pilote le moniteur.
    QPainter p(this);
    QColor veil = palette().color(QPalette::Window);
    veil.setAlpha(96);
    p.fillRect(rect(), veil);
}

void KoraCutTimelineVeil::mousePressEvent(QMouseEvent *event)
{
    event->accept();
    Q_EMIT clicked();
}

// ---- Une vue secondaire ------------------------------------------------------------------------

KoraCutTimelinePane::KoraCutTimelinePane(QWidget *parent)
    : QTabWidget(parent)
{
    setTabBar(new KoraCutSequenceTabBar(this));
    setDocumentMode(true);
    setMovable(true);
    setTabsClosable(false);
}
