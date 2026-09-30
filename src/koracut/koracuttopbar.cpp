/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracut/koracuttopbar.h"
#include "koracut/koracutthemes.h"
#include "koracut/koracuticons.h"
#include "koracut/koracutmodules.h"
#include "layouts/layoutswitcher.h"

#include <KColorScheme>
#include <KLocalizedString>

#include <QAction>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QKeySequence>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QToolButton>

#include <cmath>
#include <functional>

/** @brief La pastille Agents : un point de 8 px, puis le mot.
 *
 * Quatre états (chantier 3, 23 septembre 2026) : gris « Aucun service », vert « Prêt », bleu qui
 * respire « Au travail », rouge « Erreur ». Un clic ouvre le panneau Agents.
 */
/** @brief Le nom du projet, qui se raccourcit par le milieu au lieu de pousser les modules.
 *
 * KoraCut, 25 septembre 2026 : « Tek Tech - teaser.korac » était coupé net à droite. Le label
 * ne demande aucune largeur au layout (politique Ignored) et ne prend jamais plus que son texte ;
 * il peint lui-même le nom abrégé au milieu (« Tek Tech … teaser »), le nom entier et le chemin
 * restent dans l'infobulle.
 */
class ProjectNameLabel : public QLabel
{
public:
    explicit ProjectNameLabel(QWidget *parent)
        : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(48);
    }

    void setFullText(const QString &text)
    {
        setText(text);
        naturalWidthChanged();
    }

    int naturalWidth() const { return fontMetrics().horizontalAdvance(text()) + 2; }

protected:
    void changeEvent(QEvent *event) override
    {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
            naturalWidthChanged();
        }
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(palette().color(QPalette::WindowText));
        p.setFont(font());
        const QRect r = contentsRect();
        p.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(text(), Qt::ElideMiddle, r.width()));
    }

private:
    void naturalWidthChanged() { setMaximumWidth(qMax(minimumWidth(), naturalWidth())); }
};

class AgentsPill : public QWidget
{
public:
    explicit AgentsPill(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(28);
        setCursor(Qt::PointingHandCursor);
        setToolTip(i18nc("@info:tooltip état des agents IA", "No agents service"));
        m_breath.setInterval(40);
        QObject::connect(&m_breath, &QTimer::timeout, this, [this]() {
            m_phase = (m_phase + 1) % 60;
            update();
        });
    }

    void setDotColor(const QColor &c)
    {
        m_dot = c;
        update();
    }

    void setTextColor(const QColor &c)
    {
        m_text = c;
        update();
    }

    void setBreathing(bool on)
    {
        if (on && !m_breath.isActive()) {
            m_breath.start();
        } else if (!on && m_breath.isActive()) {
            m_breath.stop();
            m_phase = 0;
            update();
        }
    }

    void setClickHandler(std::function<void()> handler) { m_onClick = std::move(handler); }

    QSize sizeHint() const override
    {
        const int textWidth = fontMetrics().horizontalAdvance(label());
        return QSize(8 + 6 + textWidth + 2, 28);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        QColor dot = m_dot;
        if (m_breath.isActive()) {
            // Une respiration de 2,4 s entre 40 % et 100 % d'opacité.
            const qreal t = 0.5 - 0.5 * std::cos(2 * M_PI * m_phase / 60.0);
            dot.setAlphaF(0.4 + 0.6 * t);
        }
        p.setBrush(dot);
        p.drawEllipse(QRectF(0, (height() - 8) / 2.0, 8, 8));
        p.setPen(m_text);
        p.drawText(QRect(14, 0, width() - 14, height()), Qt::AlignVCenter | Qt::AlignLeft, label());
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && rect().contains(event->pos()) && m_onClick) {
            m_onClick();
        }
        QWidget::mouseReleaseEvent(event);
    }

private:
    static QString label() { return i18nc("@label les agents IA de KoraCut", "Agents"); }
    QColor m_dot{QColor(128, 128, 128)};
    QColor m_text{QColor(168, 168, 168)};
    QTimer m_breath;
    int m_phase{0};
    std::function<void()> m_onClick;
};

KoraCutTopBar::KoraCutTopBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("koracutTopBar"));
    setFixedHeight(40);
    setAutoFillBackground(true);
    // Sans cet attribut, la feuille de style ne peint ni le fond ni la ligne du bas d'un QWidget nu.
    setAttribute(Qt::WA_StyledBackground, true);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(8, 0, 8, 0);
    root->setSpacing(10);

    // ---- Gauche : symbole, chevron du menu, nom du projet
    m_leftBlock = new QWidget(this);
    auto *left = new QHBoxLayout(m_leftBlock);
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(6);

    m_symbol = new QToolButton(m_leftBlock);
    m_symbol->setObjectName(QStringLiteral("koracutSymbol"));
    m_symbol->setAutoRaise(true);
    m_symbol->setFocusPolicy(Qt::NoFocus);
    m_symbol->setCursor(Qt::PointingHandCursor);
    m_symbol->setFixedSize(28, 28);
    m_symbol->setIconSize(QSize(20, 20));
    m_symbol->setToolTip(i18nc("@info:tooltip", "KoraCut home"));
    connect(m_symbol, &QToolButton::clicked, this, &KoraCutTopBar::homeRequested);
    left->addWidget(m_symbol);

    m_menuButton = new QToolButton(m_leftBlock);
    m_menuButton->setObjectName(QStringLiteral("koracutMainMenu"));
    m_menuButton->setAutoRaise(true);
    m_menuButton->setFocusPolicy(Qt::NoFocus);
    m_menuButton->setFixedSize(22, 28);
    m_menuButton->setArrowType(Qt::DownArrow);
    m_menuButton->setPopupMode(QToolButton::InstantPopup);
    m_menuButton->setToolTip(i18nc("@info:tooltip", "Main menu"));
    m_menuButton->hide(); // branché plus tard, et jamais sur Mac
    left->addWidget(m_menuButton);

    auto *titleBox = new QWidget(m_leftBlock);
    auto *title = new QHBoxLayout(titleBox);
    title->setContentsMargins(4, 0, 0, 0);
    title->setSpacing(6);
    m_projectName = new ProjectNameLabel(titleBox);
    m_projectName->setFullText(i18nc("@label projet jamais enregistré", "Untitled"));
    m_projectName->setObjectName(QStringLiteral("koracutProjectName"));
    m_projectState = new QLabel(QString(), titleBox);
    m_projectState->setObjectName(QStringLiteral("koracutProjectState"));
    // Le nom prend la place qui reste, jusqu'à sa longueur naturelle ; l'état le suit de près,
    // le ressort final absorbe le reste. Rien ne pousse les modules du centre.
    title->addWidget(m_projectName, 1);
    title->addWidget(m_projectState);
    title->addStretch(0);
    left->addWidget(titleBox, 1);

    root->addWidget(m_leftBlock, 1);

    // ---- Centre : les sept onglets de modules
    m_tabs = new LayoutSwitcher(this);
    m_tabs->setObjectName(QStringLiteral("koracutModuleTabs"));
    root->addWidget(m_tabs, 0, Qt::AlignCenter);

    // ---- Droite : agents, recherche, disposition
    m_rightBlock = new QWidget(this);
    auto *right = new QHBoxLayout(m_rightBlock);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(10);
    right->addStretch();

    m_agents = new AgentsPill(m_rightBlock);
    m_agents->setClickHandler([this]() { Q_EMIT agentsRequested(); });
    right->addWidget(m_agents);

    m_search = new QLineEdit(m_rightBlock);
    m_search->setObjectName(QStringLiteral("koracutSearch"));
    m_search->setFixedWidth(180);
    m_search->setFixedHeight(24);
    m_search->setClearButtonEnabled(false);
    // Le champ ne prend pas le focus au démarrage. C'était le premier widget focalisable de la
    // fenêtre, donc Qt le choisissait, et son anneau d'accent faisait du champ de recherche
    // l'élément le plus voyant de la barre du haut, devant le module actif. La fonction reste
    // atteignable au clavier par ⌘K, qui ouvre la palette de commandes.
    m_search->setFocusPolicy(Qt::ClickFocus);
    // ⌘K sur Mac, Ctrl+K ailleurs : Qt traduit la séquence tout seul.
    m_search->setPlaceholderText(i18nc("@info:placeholder champ de recherche de la barre du haut", "Search   %1",
                                       QKeySequence(Qt::CTRL | Qt::Key_K).toString(QKeySequence::NativeText)));
    connect(m_search, &QLineEdit::returnPressed, this, [this]() { Q_EMIT searchRequested(m_search->text()); });
    right->addWidget(m_search);

    m_layoutButton = new QToolButton(m_rightBlock);
    m_layoutButton->setObjectName(QStringLiteral("koracutLayoutButton"));
    m_layoutButton->setText(i18nc("@action:button ouvre le menu du plan de travail", "Workspace"));
    m_layoutButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_layoutButton->setPopupMode(QToolButton::InstantPopup);
    m_layoutButton->setAutoRaise(true);
    m_layoutButton->setFocusPolicy(Qt::NoFocus);
    m_layoutButton->setFixedHeight(24);
    m_layoutButton->setToolTip(i18nc("@info:tooltip", "Reset the panels, choose a vertical or horizontal layout, maximize a panel"));
    right->addWidget(m_layoutButton);

    root->addWidget(m_rightBlock, 1);

    refreshPalette();
}

void KoraCutTopBar::setMainMenuAction(QAction *hamburger)
{
#ifdef Q_OS_MAC
    // Sur Mac la barre de menus reste native, en haut de l'écran : le chevron n'a rien à ouvrir.
    Q_UNUSED(hamburger)
    m_menuButton->hide();
#else
    if (!hamburger) {
        return;
    }
    m_menuButton->setDefaultAction(hamburger);
    m_menuButton->setArrowType(Qt::DownArrow);
    m_menuButton->setText(QString());
    m_menuButton->show();
#endif
}

void KoraCutTopBar::setAutoSaveIndicator(QWidget *indicator)
{
    if (!indicator) {
        return;
    }
    indicator->setParent(m_projectState->parentWidget());
    auto *box = qobject_cast<QHBoxLayout *>(m_projectState->parentWidget()->layout());
    if (box) {
        // Avant le ressort final, pour rester collé à l'état du projet.
        box->insertWidget(qMax(0, box->count() - 1), indicator);
    }
}

void KoraCutTopBar::setLayoutMenu(QMenu *menu)
{
    m_layoutButton->setMenu(menu);
}

void KoraCutTopBar::setProject(const QString &name, bool modified, const QString &path)
{
    // Le nom sans l'extension (« .koracut ») : elle ne dit rien et prend la place du nom.
    QString shown = name;
    if (!shown.isEmpty()) {
        const QString base = QFileInfo(shown).completeBaseName();
        if (!base.isEmpty()) {
            shown = base;
        }
    }
    m_projectName->setFullText(shown.isEmpty() ? i18nc("@label projet jamais enregistré", "Untitled") : shown);
    m_projectName->setToolTip(path.isEmpty() ? name : path);
    m_projectState->setText(modified ? i18nc("@label état du projet", "· modified") : i18nc("@label état du projet", "· saved"));
}

void KoraCutTopBar::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateCompactMode();
}

void KoraCutTopBar::updateCompactMode()
{
    // Sous 1280 px de large, les onglets gardent l'icône et perdent le nom (charte graphique).
    // On passe aussi en icônes seules plus tôt si les trois blocs de la barre ne tiennent plus côte à côte :
    // à 1366 px, la largeur de référence, les noms touchaient la pastille Agents.
    // Le nom du projet ne compte que pour 200 px au plus : au-delà il se raccourcit par le milieu.
    const int needed = m_leftBlock->sizeHint().width() + qMin(m_projectName->naturalWidth(), 200) + m_tabs->naturalWidth() + m_rightBlock->sizeHint().width() + 60;
    const bool compact = width() < 1280 || width() < needed;
    if (compact != m_compact) {
        m_compact = compact;
        m_tabs->setCompact(compact);
    }
}

void KoraCutTopBar::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        refreshPalette();
        applyAgentsColor();
    }
    // Un changement de thème (feuille de style, police) change les largeurs sans redimensionner la barre :
    // on refait le calcul une fois le thème posé, sinon les onglets restaient en icônes seules
    // (thème clair ou coloré, 28 septembre 2026).
    if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
        QTimer::singleShot(0, this, [this]() { updateCompactMode(); });
    }
}

void KoraCutTopBar::refreshPalette()
{
    KColorScheme scheme(QPalette::Active, KColorScheme::Window);
    const QColor background = scheme.background(KColorScheme::NormalBackground).color();
    const QColor text = scheme.foreground(KColorScheme::NormalText).color();
    const QColor secondary = scheme.foreground(KColorScheme::InactiveText).color();

    QPalette pal = palette();
    pal.setColor(QPalette::Window, background);
    pal.setColor(QPalette::WindowText, text);
    setPalette(pal);

    if (m_agents) {
        m_agents->setTextColor(m_agentsState == AgentsState::NoService ? secondary : text);
        applyAgentsColor();
    }
    if (m_projectState) {
        QPalette sp = m_projectState->palette();
        sp.setColor(QPalette::WindowText, secondary);
        m_projectState->setPalette(sp);
    }
    const qreal dpr = devicePixelRatioF();
    if (m_symbol) {
        m_symbol->setIcon(QIcon(KoraCutIcons::pixmap(QStringLiteral(":/koracut/koracut-symbole.svg"), 20, text, dpr)));
    }
}

void KoraCutTopBar::setAgentsState(AgentsState state, const QString &tooltip)
{
    m_agentsState = state;
    if (m_agents) {
        m_agents->setToolTip(tooltip);
        m_agents->setBreathing(state == AgentsState::Working);
    }
    refreshPalette();
}

void KoraCutTopBar::applyAgentsColor()
{
    if (!m_agents) {
        return;
    }
    // Couleurs du thème (charte : vert « prêt » #4ADE80, accent #0D99FF, rouge #FF7262).
    switch (m_agentsState) {
    case AgentsState::NoService:
        m_agents->setDotColor(KColorScheme(QPalette::Active, KColorScheme::Window).foreground(KColorScheme::InactiveText).color());
        break;
    case AgentsState::Ready:
        m_agents->setDotColor(KoraCutThemes::token(QStringLiteral("vert")));
        break;
    case AgentsState::Working:
        m_agents->setDotColor(KoraCutThemes::token(QStringLiteral("accent")));
        break;
    case AgentsState::Error:
        m_agents->setDotColor(KoraCutThemes::token(QStringLiteral("rouge")));
        break;
    }
}
