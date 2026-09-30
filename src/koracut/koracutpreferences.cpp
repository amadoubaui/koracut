/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutpreferences.h"
#include "koracutmetrics.h"
#include "koracutthemes.h"

#include <KLocalizedString>
#include <KPageDialog>
#include <KPageWidget>
#include <KPageWidgetItem>
#include <KTitleWidget>

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolBar>
#include <QVBoxLayout>

namespace {
constexpr int SidebarWidth = 232;
constexpr int ColumnMaxWidth = 760;
constexpr int RowMinHeight = 44;
constexpr int NavRow = 28;
constexpr int SwitchWidth = 34;
constexpr int SwitchHeight = 20;
const char *RoleId = "koracutId";

// Un contrôle où l'on tape du texte : on n'enregistre pas au milieu d'une frappe (un chemin à moitié
// écrit serait vérifié et refusé), on attend que le champ perde le focus.
bool isTextEntry(QWidget *w)
{
    if (!w) {
        return false;
    }
    if (auto *line = qobject_cast<QLineEdit *>(w)) {
        return !line->isReadOnly() && !qobject_cast<QAbstractSpinBox *>(line->parentWidget()) && !qobject_cast<QComboBox *>(line->parentWidget());
    }
    return qobject_cast<QPlainTextEdit *>(w) || qobject_cast<QTextEdit *>(w);
}

// Une page de Kdenlive gardée entière : ses libellés perdent le « : » final, comme nos lignes.
void polishLabels(QWidget *root)
{
    const auto labels = root->findChildren<QLabel *>();
    for (QLabel *l : labels) {
        QString t = l->text();
        if (l->textFormat() != Qt::RichText && !t.contains(QLatin1Char('<')) && t.trimmed().endsWith(QLatin1Char(':'))) {
            t = t.trimmed();
            t.chop(1);
            l->setText(t.trimmed());
        }
    }
}

bool explicitlyHidden(QWidget *w)
{
    return w->testAttribute(Qt::WA_WState_ExplicitShowHide) && w->testAttribute(Qt::WA_WState_Hidden);
}

QString cleanText(QString text)
{
    text.remove(QLatin1Char('&'));
    text = text.trimmed();
    // Les libellés de Kdenlive finissent par « : » ; dans une ligne à deux colonnes, il ne sert plus.
    while (text.endsWith(QLatin1Char(':'))) {
        text.chop(1);
        text = text.trimmed();
    }
    return text;
}

// Les textes visibles d'un contrôle composé (cases, boutons radio, étiquettes, onglets).
QString childTexts(QWidget *root)
{
    QStringList out;
    const auto labels = root->findChildren<QLabel *>();
    for (QLabel *l : labels) {
        out << l->text();
    }
    const auto buttons = root->findChildren<QAbstractButton *>();
    for (QAbstractButton *b : buttons) {
        out << b->text() << b->toolTip();
    }
    const auto groups = root->findChildren<QGroupBox *>();
    for (QGroupBox *g : groups) {
        out << g->title();
    }
    const auto tabs = root->findChildren<QTabWidget *>();
    for (QTabWidget *t : tabs) {
        for (int i = 0; i < t->count(); ++i) {
            out << t->tabText(i);
        }
    }
    if (auto *b = qobject_cast<QAbstractButton *>(root)) {
        out << b->text();
    }
    return out.join(QLatin1Char(' '));
}
} // namespace

QColor KoraCutPrefsColors::token(const char *name, int alpha)
{
    return KoraCutThemes::token(QString::fromLatin1(name), alpha);
}

namespace {
// Peint le fond « fond » d'une fenêtre à cartes et suit les changements de thème.
class CardWindowPainter : public QObject
{
public:
    explicit CardWindowPainter(QWidget *window)
        : QObject(window)
        , m_window(window)
    {
        window->installEventFilter(this);
        connect(KoraCutThemes::self(), &KoraCutThemes::themeChanged, this, [this]() {
            m_window->setStyleSheet(KoraCutPrefsColors::styleSheet());
            m_window->update();
        });
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_window && event->type() == QEvent::Paint) {
            QPainter p(m_window);
            p.fillRect(m_window->rect(), KoraCutPrefsColors::token("fond"));
        }
        return false;
    }

private:
    QWidget *m_window;
};
}

void KoraCutPrefsColors::applyCardWindow(QWidget *window)
{
    window->setStyleSheet(styleSheet());
    new CardWindowPainter(window);
}

// ---- La ligne ---------------------------------------------------------------------------------

KoraCutPrefsRow::KoraCutPrefsRow(const QString &label, const QString &help, QWidget *control, Kind kind, QWidget *parent)
    : QWidget(parent)
    , m_control(control)
    , m_kind(kind)
{
    setObjectName(QStringLiteral("koracutPrefsRow"));
    const bool hidden = control && explicitlyHidden(control);
    if (!label.isEmpty()) {
        m_label = new QLabel(label, this);
        m_label->setObjectName(QStringLiteral("koracutPrefsRowLabel"));
        m_label->setWordWrap(true);
        m_label->setTextFormat(Qt::PlainText);
    }
    if (!help.isEmpty()) {
        m_help = new QLabel(help, this);
        m_help->setObjectName(QStringLiteral("koracutPrefsRowHelp"));
        m_help->setWordWrap(true);
        m_help->setTextFormat(Qt::PlainText);
    }
    if (kind == Kind::Inline) {
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(KoraCutMetrics::SpacePanel, 10, KoraCutMetrics::SpacePanel, 10);
        h->setSpacing(24);
        auto *text = new QVBoxLayout;
        text->setSpacing(2);
        text->setContentsMargins(0, 0, 0, 0);
        if (m_label) {
            text->addWidget(m_label);
        }
        if (m_help) {
            text->addWidget(m_help);
        }
        h->addLayout(text, 1);
        if (control) {
            h->addWidget(control, 0, Qt::AlignVCenter | Qt::AlignRight);
        }
        setMinimumHeight(RowMinHeight);
    } else {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(KoraCutMetrics::SpacePanel, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpacePanel, KoraCutMetrics::SpaceZone);
        v->setSpacing(6);
        if (m_label) {
            v->addWidget(m_label);
        }
        if (m_help) {
            v->addWidget(m_help);
        }
        if (control) {
            // Un bouton seul ne s'étire pas sur toute la carte.
            if (qobject_cast<QAbstractButton *>(control)) {
                v->addWidget(control, 0, Qt::AlignLeft);
            } else {
                v->addWidget(control);
            }
        }
    }
    if (control) {
        // Le code de Kdenlive cache parfois un contrôle (sans objet sur ce système) : la ligne suit.
        control->installEventFilter(this);
        if (hidden) {
            control->hide();
            setVisible(false);
        }
        // Une liste déroulante garde une largeur raisonnable ; le texte long s'abrège dedans.
        if (kind == Kind::Inline) {
            if (auto *combo = qobject_cast<QComboBox *>(control)) {
                combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
                combo->setMinimumContentsLength(18);
            } else if (auto *line = qobject_cast<QLineEdit *>(control); line && line->maximumWidth() == QWIDGETSIZE_MAX) {
                line->setMinimumWidth(240);
            }
        }
    }
}

QString KoraCutPrefsRow::label() const
{
    return m_label ? m_label->text() : QString();
}

QString KoraCutPrefsRow::searchText() const
{
    QString text = label() + QLatin1Char(' ') + (m_help ? m_help->text() : QString()) + m_keywords;
    if (m_control && !m_toggle) {
        text += QLatin1Char(' ') + childTexts(m_control);
    }
    if (m_control) {
        text += QLatin1Char(' ') + m_control->toolTip();
    }
    if (auto *card = qobject_cast<KoraCutPrefsCard *>(parentWidget() ? parentWidget()->parentWidget() : nullptr)) {
        text += QLatin1Char(' ') + card->title();
    }
    return text;
}

void KoraCutPrefsRow::setMatched(bool matched)
{
    if (m_matched != matched) {
        m_matched = matched;
        update();
    }
}

void KoraCutPrefsRow::setFirst(bool first)
{
    if (m_first != first) {
        m_first = first;
        update();
    }
}

void KoraCutPrefsRow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (m_matched && m_kind != Kind::Block) {
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()).adjusted(4, 3, -4, -3), KoraCutMetrics::RadiusControl, KoraCutMetrics::RadiusControl);
        p.fillPath(path, KoraCutPrefsColors::token("accent", 40));
    }
    if (!m_first) {
        // Un filet entre deux lignes d'une même carte, en retrait comme dans les réglages du Mac.
        p.setRenderHint(QPainter::Antialiasing, false);
        p.setPen(KoraCutPrefsColors::token("voile", 23));
        p.drawLine(KoraCutMetrics::SpacePanel, 0, width() - KoraCutMetrics::SpacePanel, 0);
    }
}

void KoraCutPrefsRow::mouseReleaseEvent(QMouseEvent *event)
{
    // Toute la ligne d'un interrupteur répond au clic, pas seulement l'interrupteur.
    if (m_toggle && m_toggle->isEnabled() && rect().contains(event->position().toPoint())) {
        m_toggle->setFocus(Qt::MouseFocusReason);
        m_toggle->click();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

bool KoraCutPrefsRow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_control) {
        if (event->type() == QEvent::HideToParent) {
            setVisible(false);
        } else if (event->type() == QEvent::ShowToParent) {
            setVisible(true);
        }
    }
    if (m_toggle && watched == m_toggle && event->type() == QEvent::Paint) {
        // L'interrupteur : une piste arrondie, un bouton rond. Accent quand il est ouvert.
        QPainter p(m_toggle);
        p.setRenderHint(QPainter::Antialiasing);
        const bool on = m_toggle->isChecked();
        const bool enabled = m_toggle->isEnabled();
        QRectF track(1, (m_toggle->height() - SwitchHeight + 2) / 2.0, SwitchWidth - 2, SwitchHeight - 2);
        if (!enabled) {
            p.setOpacity(0.38);
        }
        if (on) {
            p.setPen(Qt::NoPen);
            p.setBrush(KoraCutPrefsColors::token("accent"));
        } else {
            p.setPen(QPen(KoraCutPrefsColors::token("bordure"), 1));
            p.setBrush(KoraCutPrefsColors::token("champ"));
        }
        p.drawRoundedRect(track.adjusted(0.5, 0.5, -0.5, -0.5), track.height() / 2, track.height() / 2);
        const qreal d = track.height() - 6;
        const qreal x = on ? track.right() - 3 - d : track.left() + 3;
        p.setPen(Qt::NoPen);
        p.setBrush(on ? KoraCutPrefsColors::token("texte_sur_accent") : KoraCutPrefsColors::token("texte_secondaire"));
        p.drawEllipse(QRectF(x, track.top() + 3, d, d));
        if (m_toggle->hasFocus() && m_toggle->window()->testAttribute(Qt::WA_KeyboardFocusChange)) {
            p.setOpacity(1);
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(KoraCutPrefsColors::token("accent"), 2));
            p.drawRoundedRect(QRectF(m_toggle->rect()).adjusted(1, 1, -1, -1), (m_toggle->height() - 2) / 2.0, (m_toggle->height() - 2) / 2.0);
        }
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

// ---- La carte ---------------------------------------------------------------------------------

KoraCutPrefsCard::KoraCutPrefsCard(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("koracutPrefsCard"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(KoraCutMetrics::SpaceGroup);
    m_title = new QLabel(title, this);
    m_title->setObjectName(QStringLiteral("koracutPrefsCardTitle"));
    m_title->setContentsMargins(KoraCutMetrics::SpaceTight, 0, 0, 0);
    m_title->setVisible(!title.isEmpty());
    v->addWidget(m_title);
    m_body = new QWidget(this);
    m_body->setObjectName(QStringLiteral("koracutPrefsCardBody"));
    m_body->installEventFilter(this);
    m_rowsLayout = new QVBoxLayout(m_body);
    m_rowsLayout->setContentsMargins(0, 2, 0, 2);
    m_rowsLayout->setSpacing(0);
    v->addWidget(m_body);
}

QString KoraCutPrefsCard::title() const
{
    return m_title->text();
}

KoraCutPrefsRow *KoraCutPrefsCard::add(KoraCutPrefsRow *row, const QString &key)
{
    row->setParent(m_body);
    m_rowsLayout->addWidget(row);
    m_rows << row;
    row->installEventFilter(this);
    if (QWidget *c = row->control()) {
        QString name = key;
        if (name.isEmpty()) {
            name = c->objectName().startsWith(QLatin1String("kcfg_")) ? c->objectName().mid(5) : c->objectName();
        }
        if (!name.isEmpty() && (!m_section.isEmpty() || !m_prefix.isEmpty())) {
            const QString id = m_prefix.isEmpty() ? QStringLiteral("prefs.%1.%2").arg(m_section, name) : m_prefix + name;
            c->setProperty(RoleId, id);
            // Un champ kcfg_ garde son nom : KConfigDialogManager le relit à chaque changement.
            if (!c->objectName().startsWith(QLatin1String("kcfg_"))) {
                c->setObjectName(id);
            }
            if (!row->label().isEmpty() && c->accessibleName().isEmpty()) {
                c->setAccessibleName(row->label());
            }
        }
        // Les champs de réglage rangés dans un conteneur (une ligne à plusieurs contrôles, une page de
        // Kdenlive gardée entière) ont aussi leur nom stable : prefs.<section>.<réglage>.
        if (!m_section.isEmpty()) {
            const auto inner = c->findChildren<QWidget *>();
            for (QWidget *w : inner) {
                if (w->objectName().startsWith(QLatin1String("kcfg_")) && !w->property(RoleId).isValid()) {
                    w->setProperty(RoleId, QStringLiteral("prefs.%1.%2").arg(m_section, w->objectName().mid(5)));
                }
            }
        }
    }
    refreshFirst();
    return row;
}

KoraCutPrefsRow *KoraCutPrefsCard::addRow(const QString &label, QWidget *control, const QString &help, const QString &key)
{
    return add(new KoraCutPrefsRow(cleanText(label), help, control, KoraCutPrefsRow::Kind::Inline), key);
}

KoraCutPrefsRow *KoraCutPrefsCard::addToggle(QCheckBox *box, const QString &help, const QString &key, const QString &label)
{
    const QString text = cleanText(label.isEmpty() ? box->text() : label);
    box->setText(QString());
    box->setFixedSize(SwitchWidth + 2, SwitchHeight + 4);
    box->setAttribute(Qt::WA_TransparentForMouseEvents);
    box->setAccessibleName(text);
    auto *row = new KoraCutPrefsRow(text, help, box, KoraCutPrefsRow::Kind::Inline);
    row->m_toggle = box;
    box->installEventFilter(row);
    row->setCursor(Qt::PointingHandCursor);
    return add(row, key);
}

KoraCutPrefsRow *KoraCutPrefsCard::addStacked(const QString &label, QWidget *control, const QString &help, const QString &key)
{
    return add(new KoraCutPrefsRow(cleanText(label), help, control, KoraCutPrefsRow::Kind::Stacked), key);
}

KoraCutPrefsRow *KoraCutPrefsCard::addBlock(QWidget *widget, const QString &key)
{
    polishLabels(widget);
    return add(new KoraCutPrefsRow(QString(), QString(), widget, KoraCutPrefsRow::Kind::Block), key);
}

QWidget *KoraCutPrefsCard::line(const QList<QWidget *> &widgets, QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *h = new QHBoxLayout(box);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(KoraCutMetrics::SpaceGroup);
    for (QWidget *w : widgets) {
        if (w) {
            h->addWidget(w);
        }
    }
    return box;
}

namespace {
// Les widgets d'une disposition de Kdenlive, remis dans un conteneur à nous (la disposition part avec
// sa ligne de formulaire). Vide : nullptr.
QWidget *wrapLayout(QLayout *layout)
{
    QList<QWidget *> widgets;
    const std::function<void(QLayout *)> collect = [&widgets, &collect](QLayout *l) {
        for (int i = 0; i < l->count(); ++i) {
            QLayoutItem *item = l->itemAt(i);
            if (item->widget()) {
                widgets << item->widget();
            } else if (item->layout()) {
                collect(item->layout());
            }
        }
    };
    collect(layout);
    if (widgets.isEmpty()) {
        return nullptr;
    }
    if (widgets.size() == 1) {
        return widgets.first();
    }
    auto *box = new QWidget;
    QBoxLayout *l = qobject_cast<QVBoxLayout *>(layout) ? static_cast<QBoxLayout *>(new QVBoxLayout(box)) : static_cast<QBoxLayout *>(new QHBoxLayout(box));
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(KoraCutMetrics::SpaceGroup);
    for (QWidget *w : std::as_const(widgets)) {
        l->addWidget(w);
    }
    return box;
}
} // namespace

void KoraCutPrefsCard::addForm(QFormLayout *form)
{
    while (form && form->rowCount() > 0) {
        const QFormLayout::TakeRowResult r = form->takeRow(0);
        QWidget *labelWidget = r.labelItem ? r.labelItem->widget() : nullptr;
        QWidget *field = nullptr;
        if (r.fieldItem) {
            if (r.fieldItem->widget()) {
                field = r.fieldItem->widget();
            } else if (r.fieldItem->layout()) {
                field = wrapLayout(r.fieldItem->layout());
            }
        }
        QString text;
        if (auto *label = qobject_cast<QLabel *>(labelWidget)) {
            text = label->text();
            label->hide();
        } else if (labelWidget) {
            // Un contrôle à la place du libellé (une case suivie d'un nombre) : les deux sur la ligne.
            field = field ? line({labelWidget, field}) : labelWidget;
        }
        if (!field) {
            continue;
        }
        if (auto *box = qobject_cast<QCheckBox *>(field); box && text.isEmpty()) {
            addToggle(box);
        } else if (text.isEmpty()) {
            addBlock(field);
        } else {
            addRow(text, field);
        }
    }
}

void KoraCutPrefsCard::refreshFirst()
{
    bool first = true;
    for (KoraCutPrefsRow *r : std::as_const(m_rows)) {
        if (explicitlyHidden(r)) {
            continue;
        }
        r->setFirst(first);
        first = false;
    }
    // Une carte dont toutes les lignes sont cachées disparaît avec son titre.
    setVisible(!first);
}

bool KoraCutPrefsCard::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_body && event->type() == QEvent::Paint) {
        QPainter p(m_body);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(m_body->rect()), KoraCutMetrics::RadiusCard, KoraCutMetrics::RadiusCard);
        p.fillPath(path, KoraCutPrefsColors::token("surface"));
        return false;
    }
    if (qobject_cast<KoraCutPrefsRow *>(watched) && (event->type() == QEvent::ShowToParent || event->type() == QEvent::HideToParent)) {
        refreshFirst();
    }
    return QWidget::eventFilter(watched, event);
}

// ---- La page ----------------------------------------------------------------------------------

KoraCutPrefsPage::KoraCutPrefsPage(const QString &id, const QString &title, const QString &help, bool scroll, QWidget *parent)
    : QWidget(parent)
    , m_id(id)
    , m_titleText(title)
    , m_scrolls(scroll)
{
    setObjectName(QStringLiteral("prefs.page.%1").arg(id));
    setProperty(RoleId, QStringLiteral("prefs.%1").arg(id));
    // Une colonne centrée, de largeur bornée : sur un grand écran, les lignes ne s'étirent pas sur 1400 px.
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(32, 0, 32, 0);
    auto *column = new QWidget(this);
    column->setObjectName(QStringLiteral("koracutPrefsColumn"));
    column->setMaximumWidth(scroll ? ColumnMaxWidth : QWIDGETSIZE_MAX);
    if (scroll) {
        outer->addStretch(1);
        outer->addWidget(column, 100);
        outer->addStretch(1);
    } else {
        outer->addWidget(column);
    }
    m_content = new QVBoxLayout(column);
    m_content->setContentsMargins(0, 24, 0, 32);
    m_content->setSpacing(24);

    auto *header = new QWidget(column);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(KoraCutMetrics::SpaceZone);
    auto *texts = new QVBoxLayout;
    texts->setSpacing(4);
    auto *titleLabel = new QLabel(title, header);
    titleLabel->setObjectName(QStringLiteral("koracutPrefsTitle"));
    texts->addWidget(titleLabel);
    if (!help.isEmpty()) {
        auto *helpLabel = new QLabel(help, header);
        helpLabel->setObjectName(QStringLiteral("koracutPrefsHelp"));
        helpLabel->setWordWrap(true);
        helpLabel->setTextFormat(Qt::PlainText);
        texts->addWidget(helpLabel);
    }
    hl->addLayout(texts, 1);
    auto *restore = new QPushButton(i18nc("@action:button restore the default values of this preferences page", "Restore Defaults"), header);
    restore->setObjectName(QStringLiteral("prefs.%1.retablir").arg(id));
    restore->setProperty("koracutFlat", true);
    restore->setToolTip(i18nc("@info:tooltip", "Put back the default values of this page only."));
    restore->setVisible(false);
    restore->setCursor(Qt::PointingHandCursor);
    connect(restore, &QPushButton::clicked, this, [this]() {
        if (m_restoreHandler) {
            m_restoreHandler();
        }
    });
    m_restore = restore;
    hl->addWidget(restore, 0, Qt::AlignTop);
    m_content->addWidget(header);
    if (scroll) {
        m_content->addStretch(1);
    }
}

KoraCutPrefsCard *KoraCutPrefsPage::addCard(const QString &title)
{
    auto *card = new KoraCutPrefsCard(title, this);
    card->setSection(m_id);
    m_cards << card;
    if (m_scrolls) {
        m_content->insertWidget(m_content->count() - 1, card);
    } else {
        m_content->addWidget(card);
    }
    return card;
}

void KoraCutPrefsPage::addWidget(QWidget *widget, int stretch)
{
    polishLabels(widget);
    widget->setParent(this);
    m_blocks << widget;
    if (m_scrolls) {
        m_content->insertWidget(m_content->count() - 1, widget, stretch);
    } else {
        m_content->addWidget(widget, stretch);
    }
}

void KoraCutPrefsPage::setRestoreHandler(const std::function<void()> &handler)
{
    m_restoreHandler = handler;
    m_restore->setVisible(bool(handler));
}

QList<KoraCutPrefsRow *> KoraCutPrefsPage::rows() const
{
    QList<KoraCutPrefsRow *> out;
    for (KoraCutPrefsCard *c : m_cards) {
        out << c->rows();
    }
    return out;
}

void KoraCutPrefsPage::reveal(QWidget *row)
{
    for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
        if (auto *area = qobject_cast<QScrollArea *>(w)) {
            area->ensureWidgetVisible(row, 0, 48);
            return;
        }
    }
}

// ---- La barre latérale ------------------------------------------------------------------------

namespace {
constexpr int RoleSection = Qt::UserRole;
constexpr int RoleCount = Qt::UserRole + 1;
constexpr int RoleGroup = Qt::UserRole + 2;
}

void KoraCutPrefsNavDelegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRect r = option.rect;
    if (index.data(RoleGroup).toBool()) {
        // Titre de groupe : le cran Étiquette de la charte (10 px, 600, capitales).
        QFont f = option.font;
        f.setPixelSize(KoraCutMetrics::FontLabel);
        f.setWeight(QFont::DemiBold);
        f.setCapitalization(QFont::AllUppercase);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 0.4);
        p->setFont(f);
        p->setPen(KoraCutPrefsColors::token("texte_secondaire"));
        p->drawText(r.adjusted(10, 0, -8, -5), Qt::AlignLeft | Qt::AlignBottom, index.data(Qt::DisplayRole).toString());
        p->restore();
        return;
    }
    const bool selected = option.state & QStyle::State_Selected;
    const bool hover = option.state & QStyle::State_MouseOver;
    const QRectF bg = QRectF(r).adjusted(0, 1, 0, -1);
    if (selected) {
        QPainterPath path;
        path.addRoundedRect(bg, KoraCutMetrics::RadiusControl, KoraCutMetrics::RadiusControl);
        // Accent doux de la charte (18 %) : la sélection d'une ligne de liste n'est pas un aplat plein.
        p->fillPath(path, KoraCutPrefsColors::token("accent", 46));
    } else if (hover) {
        QPainterPath path;
        path.addRoundedRect(bg, KoraCutMetrics::RadiusControl, KoraCutMetrics::RadiusControl);
        p->fillPath(path, KoraCutPrefsColors::token("voile", 18));
    }
    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    const int iconSize = KoraCutMetrics::IconDefault;
    const QRect iconRect(r.left() + 8, r.top() + (r.height() - iconSize) / 2, iconSize, iconSize);
    icon.paint(p, iconRect, Qt::AlignCenter, selected ? QIcon::Active : QIcon::Normal);
    QFont f = option.font;
    f.setPixelSize(KoraCutMetrics::FontBase);
    f.setWeight(selected ? QFont::Medium : QFont::Normal);
    p->setFont(f);
    const int count = index.data(RoleCount).isValid() ? index.data(RoleCount).toInt() : -1;
    QRect textRect = r.adjusted(8 + iconSize + 10, 0, -8, 0);
    if (count > 0) {
        QFont cf = f;
        cf.setPixelSize(KoraCutMetrics::FontDense);
        cf.setWeight(QFont::Normal);
        p->setFont(cf);
        p->setPen(KoraCutPrefsColors::token("texte_secondaire"));
        const QString n = QString::number(count);
        p->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, n);
        textRect.setRight(textRect.right() - QFontMetrics(cf).horizontalAdvance(n) - 8);
        p->setFont(f);
    }
    p->setPen(KoraCutPrefsColors::token("texte"));
    p->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(f).elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width()));
    p->restore();
}

QSize KoraCutPrefsNavDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    Q_UNUSED(option)
    if (index.data(RoleGroup).toBool()) {
        return {SidebarWidth - 24, index.row() == 0 ? 22 : 34};
    }
    return {SidebarWidth - 24, NavRow};
}

// ---- La coque ---------------------------------------------------------------------------------

KoraCutPrefsShell::KoraCutPrefsShell(KPageDialog *dialog, KPageWidget *pages, QDialogButtonBox *buttons)
    : QObject(dialog)
    , m_dialog(dialog)
    , m_pages(pages)
    , m_buttons(buttons)
{
    m_applyTimer.setSingleShot(true);
    m_applyTimer.setInterval(500);
    connect(&m_applyTimer, &QTimer::timeout, this, &KoraCutPrefsShell::applyNow);
    m_noteTimer.setSingleShot(true);
    m_noteTimer.setInterval(1800);

    m_sidebar = new QWidget(dialog);
    m_sidebar->setObjectName(QStringLiteral("koracutPrefsSidebar"));
    m_sidebar->setFixedWidth(SidebarWidth);
    m_sidebar->installEventFilter(this);
    auto *v = new QVBoxLayout(m_sidebar);
    v->setContentsMargins(KoraCutMetrics::SpaceZone, KoraCutMetrics::SpacePanel, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceZone);
    v->setSpacing(KoraCutMetrics::SpaceZone);

    m_search = new QLineEdit(m_sidebar);
    m_search->setObjectName(QStringLiteral("prefs.recherche"));
    m_search->setProperty(RoleId, QStringLiteral("prefs.recherche"));
    m_search->setPlaceholderText(i18nc("@info:placeholder search field of the preferences", "Search a setting"));
    m_search->setClearButtonEnabled(true);
    m_search->addAction(QIcon::fromTheme(QStringLiteral("edit-find")), QLineEdit::LeadingPosition);
    m_search->setAccessibleName(i18nc("@info:placeholder search field of the preferences", "Search a setting"));
    m_search->installEventFilter(this);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) { search(text); });
    v->addWidget(m_search);

    m_nav = new QListWidget(m_sidebar);
    m_nav->setObjectName(QStringLiteral("prefs.navigation"));
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_nav->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_nav->setItemDelegate(new KoraCutPrefsNavDelegate(m_nav));
    m_nav->setMouseTracking(true);
    m_nav->setFocusPolicy(Qt::StrongFocus);
    m_nav->setAttribute(Qt::WA_MacShowFocusRect, false);
    m_nav->viewport()->setAutoFillBackground(false);
    m_nav->setAutoFillBackground(false);
    m_nav->setIconSize(QSize(KoraCutMetrics::IconDefault, KoraCutMetrics::IconDefault));
    m_nav->setAccessibleName(i18nc("@title sections of the preferences", "Sections"));
    connect(m_nav, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (!item || m_syncing) {
            return;
        }
        const QString id = item->data(RoleSection).toString();
        if (!id.isEmpty()) {
            showSection(id);
        }
    });
    v->addWidget(m_nav, 1);

    m_empty = new QLabel(m_sidebar);
    m_empty->setObjectName(QStringLiteral("koracutPrefsEmpty"));
    m_empty->setWordWrap(true);
    m_empty->setVisible(false);
    v->addWidget(m_empty);

    connect(KoraCutThemes::self(), &KoraCutThemes::themeChanged, this, &KoraCutPrefsShell::restyle);
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *old, QWidget *) {
        // On sort d'un champ de texte avec des changements en attente : on enregistre.
        if (m_dirty && old != m_search && isTextEntry(old) && m_dialog && m_dialog->isAncestorOf(old) && m_pending && m_pending()) {
            m_applyTimer.start(0);
        }
    });
}

void KoraCutPrefsShell::addGroup(const QString &id, const QString &title)
{
    m_groups.append({id, title});
    m_currentGroup = id;
    auto *item = new QListWidgetItem(title, m_nav);
    item->setData(RoleGroup, true);
    item->setData(RoleSection, QString());
    item->setData(Qt::UserRole + 3, id);
    item->setFlags(Qt::NoItemFlags);
}

void KoraCutPrefsShell::addSection(KoraCutPrefsPage *page, const QString &icon, KPageWidgetItem *item)
{
    Section s;
    s.id = page->id();
    s.group = m_currentGroup;
    s.page = page;
    s.item = item;
    s.nav = new QListWidgetItem(QIcon::fromTheme(icon), page->title(), m_nav);
    s.nav->setData(RoleSection, s.id);
    s.nav->setData(Qt::UserRole + 3, m_currentGroup);
    s.nav->setToolTip(page->title());
    // Le titre de KPageView n'a plus de place dans sa grille (setPageHeader) : il ne doit pas réapparaître.
    if (item) {
        item->setHeaderVisible(false);
    }
    m_sections << s;
}

void KoraCutPrefsShell::finish()
{
    if (!m_dialog || !m_pages) {
        return;
    }
    // KPageView garde sa liste d'icônes, sa recherche et son titre : on les cache, il ne sert plus
    // que de pile de pages (KConfigDialog y tient ses pages et ses gestionnaires de champs).
    const auto views = m_pages->findChildren<QAbstractItemView *>(Qt::FindDirectChildrenOnly);
    for (QAbstractItemView *view : views) {
        view->hide();
    }
    if (auto *searchBox = m_pages->findChild<QWidget *>(QStringLiteral("KPageView::Search"))) {
        searchBox->hide();
    }
    const auto frames = m_pages->findChildren<QFrame *>(Qt::FindDirectChildrenOnly);
    for (QFrame *f : frames) {
        if (f->frameShape() == QFrame::HLine) {
            f->hide();
        }
    }
    // Une en-tête vide remplace le titre de KPageView, qu'il remontrait à chaque page.
    auto *noHeader = new QWidget(m_pages);
    noHeader->setFixedHeight(0);
    m_pages->setPageHeader(noHeader);
    const auto bars = m_pages->findChildren<QToolBar *>(Qt::FindDirectChildrenOnly);
    for (QToolBar *bar : bars) {
        bar->setMaximumHeight(0);
    }
    m_pages->setFocusProxy(nullptr);
    if (auto *grid = qobject_cast<QGridLayout *>(m_pages->layout())) {
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(0);
    }
    // Les pages ne peignent pas leur fond : la page le fait elle-même (voir restyle()).
    for (const Section &s : std::as_const(m_sections)) {
        for (QWidget *w = s.page->parentWidget(); w && w != m_pages; w = w->parentWidget()) {
            if (auto *area = qobject_cast<QScrollArea *>(w)) {
                area->setFrameShape(QFrame::NoFrame);
                area->viewport()->setAutoFillBackground(false);
                area->setAutoFillBackground(false);
                area->setProperty("koracutPrefsArea", true);
            }
        }
        s.page->setAutoFillBackground(false);
        s.page->installEventFilter(this);
    }

    // Le pied : une note à gauche, un seul bouton Fermer. Les réglages s'enregistrent tout de suite.
    auto *footer = new QWidget(m_dialog);
    footer->setObjectName(QStringLiteral("koracutPrefsFooter"));
    footer->installEventFilter(this);
    auto *fl = new QHBoxLayout(footer);
    fl->setContentsMargins(24, 10, 16, 10);
    fl->setSpacing(KoraCutMetrics::SpaceZone);
    m_note = new QLabel(i18nc("@info preferences footer", "Changes are saved as you make them."), footer);
    m_note->setObjectName(QStringLiteral("koracutPrefsNote"));
    fl->addWidget(m_note, 1);
    if (m_buttons) {
        for (auto which : {QDialogButtonBox::Help, QDialogButtonBox::RestoreDefaults, QDialogButtonBox::Apply, QDialogButtonBox::Cancel}) {
            if (QPushButton *b = m_buttons->button(which)) {
                b->hide();
            }
        }
        if (QPushButton *ok = m_buttons->button(QDialogButtonBox::Ok)) {
            ok->setText(i18nc("@action:button close the preferences window", "Close"));
            ok->setIcon(QIcon());
            ok->setObjectName(QStringLiteral("prefs.fermer"));
            ok->setMinimumWidth(96);
        }
        fl->addWidget(m_buttons);
    }
    connect(&m_noteTimer, &QTimer::timeout, this, [this]() { m_note->setText(i18nc("@info preferences footer", "Changes are saved as you make them.")); });

    auto *outer = qobject_cast<QVBoxLayout *>(m_dialog->layout());
    if (outer) {
        outer->removeWidget(m_pages);
        if (m_buttons) {
            outer->removeWidget(m_buttons);
        }
        auto *row = new QHBoxLayout;
        row->setSpacing(0);
        row->setContentsMargins(0, 0, 0, 0);
        row->addWidget(m_sidebar);
        auto *right = new QVBoxLayout;
        right->setSpacing(0);
        right->setContentsMargins(0, 0, 0, 0);
        right->addWidget(m_pages, 1);
        right->addWidget(footer);
        row->addLayout(right, 1);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->addLayout(row, 1);
    }
    connect(m_pages, &KPageWidget::currentPageChanged, this, [this](KPageWidgetItem *current, KPageWidgetItem *) {
        if (m_syncing) {
            return;
        }
        for (const Section &s : std::as_const(m_sections)) {
            if (s.item == current) {
                showSection(s.id);
                return;
            }
        }
    });
    m_dialog->setProperty("koracutSections", sectionIds());
    m_dialog->installEventFilter(this);
    restyle();
}

QStringList KoraCutPrefsShell::sectionIds() const
{
    QStringList ids;
    for (const Section &s : m_sections) {
        ids << s.id;
    }
    return ids;
}

KoraCutPrefsPage *KoraCutPrefsShell::page(const QString &id) const
{
    for (const Section &s : m_sections) {
        if (s.id == id) {
            return s.page;
        }
    }
    return nullptr;
}

QString KoraCutPrefsShell::currentSection() const
{
    return m_dialog ? m_dialog->property("koracutSection").toString() : QString();
}

bool KoraCutPrefsShell::showSection(const QString &id)
{
    for (const Section &s : std::as_const(m_sections)) {
        if (s.id != id) {
            continue;
        }
        m_syncing = true;
        if (m_pages->currentPage() != s.item) {
            m_pages->setCurrentPage(s.item);
        }
        if (m_nav->currentItem() != s.nav) {
            m_nav->setCurrentItem(s.nav);
        }
        m_nav->scrollToItem(s.nav);
        m_syncing = false;
        m_dialog->setProperty("koracutSection", id);
        if (!m_lastSearch.isEmpty()) {
            const QList<QWidget *> hits = matchesIn(s.page, fold(m_lastSearch).split(QLatin1Char(' '), Qt::SkipEmptyParts), true);
            if (!hits.isEmpty()) {
                QTimer::singleShot(0, s.page, [page = s.page, first = QPointer<QWidget>(hits.first())]() {
                    if (first) {
                        page->reveal(first);
                    }
                });
            }
        }
        Q_EMIT sectionChanged(id);
        return true;
    }
    return false;
}

QString KoraCutPrefsShell::fold(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() == QChar::Mark_NonSpacing || c == QLatin1Char('&')) {
            continue;
        }
        out.append(c.toLower());
    }
    return out.simplified();
}

QList<QWidget *> KoraCutPrefsShell::matchesIn(KoraCutPrefsPage *page, const QStringList &words, bool mark)
{
    QList<QWidget *> hits;
    const auto rows = page->rows();
    for (KoraCutPrefsRow *row : rows) {
        bool ok = !words.isEmpty() && !explicitlyHidden(row);
        if (ok) {
            const QString hay = fold(row->searchText());
            for (const QString &w : words) {
                if (!hay.contains(w)) {
                    ok = false;
                    break;
                }
            }
        }
        if (mark) {
            row->setMatched(ok);
        }
        if (ok) {
            hits << row;
        }
    }
    // Les pages gardées entières (Connexions IA, Reconnaissance vocale) : on cherche dans leurs textes.
    const auto blocks = page->blocks();
    for (QWidget *block : blocks) {
        if (words.isEmpty()) {
            continue;
        }
        const QString hay = fold(childTexts(block));
        bool ok = true;
        for (const QString &w : words) {
            if (!hay.contains(w)) {
                ok = false;
                break;
            }
        }
        if (ok) {
            hits << block;
        }
    }
    return hits;
}

int KoraCutPrefsShell::search(const QString &text)
{
    if (m_search->text() != text) {
        const QSignalBlocker blocker(m_search);
        m_search->setText(text);
    }
    m_lastSearch = text.trimmed();
    const QStringList words = fold(m_lastSearch).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    int total = 0;
    QString first;
    QSet<QString> groupsShown;
    for (const Section &s : std::as_const(m_sections)) {
        const int count = matchesIn(s.page, words, true).size();
        bool titleHit = !words.isEmpty();
        const QString title = fold(s.page->title() + QLatin1Char(' ') + s.page->keywords());
        for (const QString &w : words) {
            if (!title.contains(w)) {
                titleHit = false;
                break;
            }
        }
        const bool visible = words.isEmpty() || count > 0 || titleHit;
        s.nav->setHidden(!visible);
        s.nav->setData(RoleCount, words.isEmpty() ? QVariant() : QVariant(count));
        if (visible) {
            groupsShown.insert(s.group);
            if (first.isEmpty()) {
                first = s.id;
            }
        }
        total += count;
    }
    for (int i = 0; i < m_nav->count(); ++i) {
        QListWidgetItem *item = m_nav->item(i);
        if (item->data(RoleGroup).toBool()) {
            item->setHidden(!groupsShown.contains(item->data(Qt::UserRole + 3).toString()));
        }
    }
    const bool none = !words.isEmpty() && first.isEmpty();
    m_empty->setVisible(none);
    if (none) {
        m_empty->setText(i18nc("@info no preference matches the search", "No setting matches “%1”.", m_lastSearch));
    }
    m_dialog->setProperty("koracutSearch", m_lastSearch);
    // La section affichée ne contient rien : on va à la première qui contient quelque chose.
    const QString current = currentSection();
    bool currentVisible = false;
    for (const Section &s : std::as_const(m_sections)) {
        if (s.id == current) {
            currentVisible = !s.nav->isHidden();
        }
    }
    if (!first.isEmpty() && (!currentVisible || (!words.isEmpty() && matchesIn(page(current), words, false).isEmpty()))) {
        // On préfère une section qui a des réglages trouvés à une section qui n'a que son titre.
        for (const Section &s : std::as_const(m_sections)) {
            if (!s.nav->isHidden() && s.nav->data(RoleCount).toInt() > 0) {
                first = s.id;
                break;
            }
        }
        showSection(first);
    } else if (!current.isEmpty()) {
        showSection(current);
    }
    m_nav->viewport()->update();
    return total;
}

void KoraCutPrefsShell::setAutoApply(const std::function<bool()> &pending, const std::function<void()> &apply)
{
    m_pending = pending;
    m_apply = apply;
}

void KoraCutPrefsShell::scheduleApply()
{
    // Pendant l'ouverture, KConfigDialog remplit les champs : ce ne sont pas des changements.
    if (!m_armed) {
        return;
    }
    m_dirty = true;
    if (m_pending && m_pending()) {
        m_applyTimer.start();
    }
}

void KoraCutPrefsShell::applyNow()
{
    if (!m_dirty || !m_pending || !m_pending()) {
        return;
    }
    // Au milieu d'une frappe : on attend que le champ perde le focus (voir le constructeur).
    QWidget *focus = qApp->focusWidget();
    if (m_dialog && m_dialog->isVisible() && focus != m_search && isTextEntry(focus) && m_dialog->isAncestorOf(focus)) {
        return;
    }
    m_dirty = false;
    m_apply();
    showSaved();
}

void KoraCutPrefsShell::flush()
{
    m_applyTimer.stop();
    if (m_pending && m_pending()) {
        m_apply();
    }
}

void KoraCutPrefsShell::showSaved()
{
    if (!m_note) {
        return;
    }
    m_note->setText(i18nc("@info preferences footer, right after a change was saved", "Saved."));
    m_noteTimer.start();
}

QString KoraCutPrefsColors::styleSheet()
{
    using KoraCutPrefsColors::token;
    const auto hex = [](const QColor &c) -> QString { return c.name(QColor::HexArgb); };
    // Les couleurs viennent des jetons du thème : la fenêtre suit Sombre, Clair et tous les thèmes créés.
    return QStringLiteral(
                              "QLabel#koracutPrefsTitle { font-size: %1px; font-weight: 600; color: %2; background: transparent; }"
                              "QLabel#koracutPrefsHelp { font-size: %3px; color: %4; background: transparent; }"
                              "QLabel#koracutPrefsCardTitle { font-size: %3px; font-weight: 600; color: %4; background: transparent; }"
                              "QLabel#koracutPrefsRowLabel { font-size: %3px; color: %2; background: transparent; }"
                              "QLabel#koracutPrefsRowHelp { font-size: %5px; color: %4; background: transparent; }"
                              "QLabel#koracutPrefsNote, QLabel#koracutPrefsEmpty { font-size: %5px; color: %4; background: transparent; }"
                              "QPushButton[koracutFlat=\"true\"] { background: transparent; border: none; color: %6; padding: 4px 8px; font-size: %3px; }"
                              "QPushButton[koracutFlat=\"true\"]:hover { background: %7; border-radius: 4px; }"
                              "QScrollArea[koracutPrefsArea=\"true\"] { background: transparent; border: none; }"
                              "QWidget#koracutSegmented { background: %7; border-radius: 4px; }"
                              "QToolButton[koracutSegment=\"true\"] { background: transparent; border: none; border-radius: 4px; padding: 5px 14px; color: %2; font-size: %3px; }"
                              "QToolButton[koracutSegment=\"true\"]:hover { background: %8; }"
                              "QToolButton[koracutSegment=\"true\"]:checked { background: %6; color: %9; }")
                              .arg(KoraCutMetrics::FontPage)
                              .arg(hex(token("texte")))
                              .arg(KoraCutMetrics::FontBase)
                              .arg(hex(token("texte_secondaire")))
                              .arg(KoraCutMetrics::FontDense)
                              .arg(hex(token("accent")))
                              .arg(hex(token("voile", 18)))
                              .arg(hex(token("voile", 31)))
                              .arg(hex(token("texte_sur_accent")));
}

void KoraCutPrefsShell::restyle()
{
    if (!m_dialog) {
        return;
    }
    const QString sheet = KoraCutPrefsColors::styleSheet();
    // Seulement sur nos propres éléments : les pages de Kdenlive gardées entières restent sous la feuille de l'application.
    m_sidebar->setStyleSheet(sheet);
    for (const Section &s : std::as_const(m_sections)) {
        s.page->setStyleSheet(sheet);
    }
    if (m_note && m_note->parentWidget()) {
        m_note->parentWidget()->setStyleSheet(sheet);
    }
    m_dialog->update();
}

bool KoraCutPrefsShell::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Paint) {
        auto *w = qobject_cast<QWidget *>(watched);
        if (w == m_sidebar) {
            QPainter p(w);
            p.fillRect(w->rect(), KoraCutPrefsColors::token("contenu"));
            return false;
        }
        if (w && w->objectName() == QLatin1String("koracutPrefsFooter")) {
            QPainter p(w);
            p.fillRect(w->rect(), KoraCutPrefsColors::token("fond"));
            p.setPen(KoraCutPrefsColors::token("voile", 23));
            p.drawLine(0, 0, w->width(), 0);
            return false;
        }
        if (qobject_cast<KoraCutPrefsPage *>(w)) {
            QPainter p(w);
            p.fillRect(w->rect(), KoraCutPrefsColors::token("fond"));
            return false;
        }
    }
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Down) {
            m_nav->setFocus(Qt::TabFocusReason);
            return true;
        }
        if (key->key() == Qt::Key_Escape && !m_search->text().isEmpty()) {
            m_search->clear();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            // Entrée ne ferme pas la fenêtre depuis la recherche : elle va au premier réglage trouvé.
            if (KoraCutPrefsPage *p = page(currentSection())) {
                const auto hits = matchesIn(p, fold(m_search->text()).split(QLatin1Char(' '), Qt::SkipEmptyParts), false);
                if (!hits.isEmpty()) {
                    p->reveal(hits.first());
                    if (auto *row = qobject_cast<KoraCutPrefsRow *>(hits.first()); row && row->control()) {
                        row->control()->setFocus(Qt::TabFocusReason);
                    }
                }
            }
            return true;
        }
    }
    if (watched == m_dialog) {
        if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            if (key->matches(QKeySequence::Find)) {
                if (event->type() == QEvent::KeyPress) {
                    m_search->setFocus(Qt::ShortcutFocusReason);
                    m_search->selectAll();
                }
                event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::Hide) {
            flush();
            m_armed = false;
        }
        if (event->type() == QEvent::Show) {
            QTimer::singleShot(1200, this, [this]() { m_armed = true; });
        }
        if (event->type() == QEvent::PaletteChange) {
            restyle();
        }
    }
    return QObject::eventFilter(watched, event);
}
