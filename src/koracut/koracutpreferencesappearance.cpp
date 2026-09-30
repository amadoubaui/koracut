/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutpreferencesappearance.h"
#include "core.h"
#include "koracutmetrics.h"
#include "koracutpreferences.h"
#include "koracutthemes.h"
#include "mainwindow.h"

#include <KActionCollection>
#include <KLocalizedString>
#include <KMessageBox>

#include <QAction>
#include <QButtonGroup>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QToolButton>

namespace {
constexpr int TileWidth = 152;
constexpr int TilePreview = 84;
constexpr int TileColumns = 4;
}

// ---- La vignette ------------------------------------------------------------------------------

KoraCutThemeTile::KoraCutThemeTile(const QString &id, const QString &name, const QMap<QString, QColor> &preview, QWidget *parent)
    : QAbstractButton(parent)
    , m_id(id)
    , m_preview(preview)
{
    setText(name);
    setToolTip(name);
    setAccessibleName(name);
    setObjectName(QStringLiteral("prefs.apparence.theme.%1").arg(id));
    setProperty("koracutId", objectName());
    setCheckable(false);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
}

QSize KoraCutThemeTile::sizeHint() const
{
    return {TileWidth, TilePreview + 30};
}

void KoraCutThemeTile::setActive(bool active)
{
    if (m_active != active) {
        m_active = active;
        update();
    }
}

void KoraCutThemeTile::enterEvent(QEnterEvent *event)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(event);
}

void KoraCutThemeTile::leaveEvent(QEvent *event)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void KoraCutThemeTile::paintEvent(QPaintEvent *)
{
    using KoraCutPrefsColors::token;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor fond = m_preview.value(QStringLiteral("fond"), QColor(0x1E, 0x1E, 0x1E));
    const QColor surface = m_preview.value(QStringLiteral("surface"), fond.lighter(130));
    const QColor texte = m_preview.value(QStringLiteral("texte"), Qt::white);
    const QColor accent = m_preview.value(QStringLiteral("accent"), QColor(0x0D, 0x99, 0xFF));

    // Un aperçu de fenêtre, en petit : barre du haut, panneau latéral, deux lignes de texte, un bouton.
    const QRectF frame(2, 2, width() - 4, TilePreview);
    QPainterPath clip;
    clip.addRoundedRect(frame, KoraCutMetrics::RadiusCard, KoraCutMetrics::RadiusCard);
    p.save();
    p.setClipPath(clip);
    p.fillRect(frame, fond);
    p.fillRect(QRectF(frame.left(), frame.top(), frame.width(), 12), surface);
    p.fillRect(QRectF(frame.left(), frame.top() + 12, frame.width() * 0.28, frame.height() - 12), surface);
    QColor line = texte;
    line.setAlphaF(0.9);
    p.fillRect(QRectF(frame.left() + frame.width() * 0.36, frame.top() + 24, frame.width() * 0.44, 5), line);
    line.setAlphaF(0.45);
    p.fillRect(QRectF(frame.left() + frame.width() * 0.36, frame.top() + 35, frame.width() * 0.32, 4), line);
    p.fillRect(QRectF(frame.left() + frame.width() * 0.36, frame.top() + 44, frame.width() * 0.38, 4), line);
    QPainterPath button;
    button.addRoundedRect(QRectF(frame.left() + frame.width() * 0.36, frame.bottom() - 22, 40, 12), 3, 3);
    p.fillPath(button, accent);
    p.restore();

    // Contour : accent quand le thème est à l'écran, filet discret sinon.
    p.setBrush(Qt::NoBrush);
    if (m_active) {
        p.setPen(QPen(token("accent"), 2));
        p.drawRoundedRect(frame.adjusted(-1, -1, 1, 1), KoraCutMetrics::RadiusCard + 1, KoraCutMetrics::RadiusCard + 1);
    } else {
        p.setPen(QPen(m_hover ? token("bordure_survol") : token("voile", 40), 1));
        p.drawRoundedRect(frame.adjusted(0.5, 0.5, -0.5, -0.5), KoraCutMetrics::RadiusCard, KoraCutMetrics::RadiusCard);
    }
    if (hasFocus() && window()->testAttribute(Qt::WA_KeyboardFocusChange) && !m_active) {
        p.setPen(QPen(token("accent"), 2));
        p.drawRoundedRect(frame.adjusted(-1, -1, 1, 1), KoraCutMetrics::RadiusCard + 1, KoraCutMetrics::RadiusCard + 1);
    }

    QFont f = font();
    f.setPixelSize(KoraCutMetrics::FontBase);
    f.setWeight(m_active ? QFont::DemiBold : QFont::Normal);
    p.setFont(f);
    p.setPen(token("texte"));
    const QRect textRect(2, TilePreview + 8, width() - 4, 18);
    p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(f).elidedText(text(), Qt::ElideRight, textRect.width()));
}

// ---- La page ----------------------------------------------------------------------------------

KoraCutAppearance::KoraCutAppearance(KoraCutPrefsPage *page, QComboBox *tabPosition)
    : QObject(page)
    , m_page(page)
{
    // Le mode : un contrôle segmenté (charte : un choix exclusif se dessine d'un seul corps).
    auto *modeCard = page->addCard(i18nc("@title:group preferences", "Mode"));
    m_modeBox = new QWidget;
    m_modeBox->setObjectName(QStringLiteral("koracutSegmented"));
    m_modeBox->setAttribute(Qt::WA_StyledBackground);
    auto *ml = new QHBoxLayout(m_modeBox);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(0);
    auto *group = new QButtonGroup(m_modeBox);
    group->setExclusive(true);
    const QList<QPair<QString, QString>> modes = {{QStringLiteral("systeme"), i18nc("@option:radio theme mode, follow the system", "Automatic")},
                                                  {QStringLiteral("clair"), i18nc("@option:radio theme mode", "Light")},
                                                  {QStringLiteral("sombre"), i18nc("@option:radio theme mode", "Dark")}};
    for (const auto &m : modes) {
        auto *b = new QToolButton(m_modeBox);
        b->setText(m.second);
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setObjectName(QStringLiteral("prefs.apparence.mode.%1").arg(m.first));
        b->setProperty("koracutId", b->objectName());
        b->setProperty("koracutSegment", true);
        b->setProperty("koracutNoChevron", true);
        b->setMinimumWidth(92);
        b->setCursor(Qt::PointingHandCursor);
        group->addButton(b);
        ml->addWidget(b);
        m_modeButtons << b;
        const QString which = m.first;
        connect(b, &QToolButton::clicked, this, [this, which]() {
            QString error;
            if (!KoraCutThemes::self()->apply(which, &error)) {
                KMessageBox::error(m_page, error);
            }
            refreshState();
        });
    }
    modeCard->addRow(i18nc("@label preferences", "Appearance"), m_modeBox,
                     i18nc("@info preferences", "Automatic follows the light or dark setting of your system, and changes with it."), QStringLiteral("mode"));

    // Les thèmes : des vignettes. Un clic applique, tout de suite.
    auto *themesCard = page->addCard(i18nc("@title:group preferences", "Themes"));
    m_tilesBox = new QWidget;
    m_tilesBox->setObjectName(QStringLiteral("prefs.apparence.themes"));
    m_tiles = new QGridLayout(m_tilesBox);
    m_tiles->setContentsMargins(0, 4, 0, 4);
    m_tiles->setHorizontalSpacing(KoraCutMetrics::SpacePanel);
    m_tiles->setVerticalSpacing(KoraCutMetrics::SpacePanel);
    auto *tilesRow = themesCard->addBlock(m_tilesBox, QStringLiteral("themes"));
    tilesRow->addKeywords(i18nc("@info search keywords of the theme list", "theme color colors dark light contrast"));

    // Installer, ouvrir le dossier, supprimer un thème créé ; demander aux agents.
    auto *install = new QPushButton(i18nc("@action:button", "Install a Theme…"));
    install->setObjectName(QStringLiteral("prefs.apparence.installer"));
    auto *folder = new QPushButton(i18nc("@action:button", "Open the Themes Folder"));
    folder->setObjectName(QStringLiteral("prefs.apparence.dossier"));
    m_remove = new QPushButton(i18nc("@action:button delete the theme on screen, one that was created", "Delete This Theme"));
    m_remove->setObjectName(QStringLiteral("prefs.apparence.supprimer"));
    for (QPushButton *b : {install, folder, m_remove}) {
        b->setCursor(Qt::PointingHandCursor);
    }
    const auto trigger = [](const char *name) {
        if (pCore && pCore->window()) {
            if (QAction *a = pCore->window()->actionCollection()->action(QString::fromLatin1(name))) {
                a->trigger();
            }
        }
    };
    connect(install, &QPushButton::clicked, this, [this, trigger]() {
        trigger("koracut_install_theme");
        rebuildTiles();
    });
    connect(folder, &QPushButton::clicked, this, [trigger]() { trigger("koracut_open_themes_folder"); });
    connect(m_remove, &QPushButton::clicked, this, [this]() {
        const QString id = KoraCutThemes::self()->currentId();
        if (KMessageBox::warningContinueCancel(m_page, i18nc("@info", "Delete this theme? This cannot be undone."), QString(), KStandardGuiItem::del()) !=
            KMessageBox::Continue) {
            return;
        }
        QString error;
        if (!KoraCutThemes::self()->remove(id, &error)) {
            KMessageBox::error(m_page, error);
        }
    });
    themesCard->addRow(i18nc("@label preferences", "More themes"), KoraCutPrefsCard::line({install, folder, m_remove}),
                       i18nc("@info preferences",
                             "You can also ask the agents for a theme, in your own words: “a green on black Matrix theme”, “softer contrast”, “go back as before”."),
                       QStringLiteral("plus"));

    if (tabPosition) {
        auto *ui = page->addCard(i18nc("@title:group preferences", "Panels"));
        ui->addRow(i18nc("@label preferences", "Tab position"), tabPosition, i18nc("@info preferences", "Where the tabs of grouped panels sit."));
    }

    connect(KoraCutThemes::self(), &KoraCutThemes::themeChanged, this, &KoraCutAppearance::refreshState);
    connect(KoraCutThemes::self(), &KoraCutThemes::listChanged, this, &KoraCutAppearance::rebuildTiles);
    rebuildTiles();
}

void KoraCutAppearance::rebuildTiles()
{
    qDeleteAll(m_tileList);
    m_tileList.clear();
    const auto themes = KoraCutThemes::self()->list();
    int i = 0;
    for (const KoraCutThemes::Theme &t : themes) {
        auto *tile = new KoraCutThemeTile(t.id, t.name, t.preview, m_tilesBox);
        const QString id = t.id;
        connect(tile, &QAbstractButton::clicked, this, [this, id]() {
            QString error;
            if (!KoraCutThemes::self()->apply(id, &error)) {
                KMessageBox::error(m_page, error);
            }
            refreshState();
        });
        m_tiles->addWidget(tile, i / TileColumns, i % TileColumns, Qt::AlignLeft | Qt::AlignTop);
        m_tileList << tile;
        ++i;
    }
    m_tiles->setColumnStretch(TileColumns, 1);
    refreshState();
}

void KoraCutAppearance::refreshState()
{
    KoraCutThemes *themes = KoraCutThemes::self();
    const QString current = themes->currentId();
    const QString mode = themes->mode();
    bool created = false;
    QString base = QStringLiteral("dark");
    const auto list = themes->list();
    for (const KoraCutThemes::Theme &t : list) {
        if (t.id == current) {
            created = t.created;
            base = t.base;
        }
    }
    for (KoraCutThemeTile *tile : std::as_const(m_tileList)) {
        tile->setActive(tile->themeId() == current);
    }
    const QString segment = mode == QLatin1String("system") ? QStringLiteral("systeme") : (base == QLatin1String("light") ? QStringLiteral("clair") : QStringLiteral("sombre"));
    for (QToolButton *b : std::as_const(m_modeButtons)) {
        b->setChecked(b->objectName().endsWith(QLatin1Char('.') + segment));
    }
    if (m_remove) {
        m_remove->setVisible(created);
    }
}
