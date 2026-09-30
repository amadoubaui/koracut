/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutterminalpanel.h"
#include "../koracutfonts.h"
#include "koracut/koracutmetrics.h"
#include "koracut/porte/koracutporte.h"
#include "koracut/porte/koracutterminal.h"
#include "koracutpty.h"
#include "koracutterminalview.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabBar>
#include <QTextStream>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int IdRole = Qt::UserRole + 1;

QString clientName()
{
    return i18nc("@item name of the integrated terminal in the console journal", "terminal");
}

QString tempRoot()
{
    return QDir::tempPath() + QStringLiteral("/koracut-terminal-%1").arg(QCoreApplication::applicationPid());
}

#ifndef Q_OS_WIN
// Le shell qui lit nos scripts : celui de la personne s'il parle POSIX, sinon bash.
QString scriptShell()
{
    const QString shell = KoraCutAgentsCli::userShell();
    const QString name = QFileInfo(shell).fileName();
    if (name == QLatin1String("zsh") || name == QLatin1String("bash") || name == QLatin1String("sh") || name == QLatin1String("ksh")) {
        return shell;
    }
    return QStringLiteral("/bin/bash");
}
#endif

// Les arguments du shell interactif : un shell de connexion ailleurs (le PATH de la personne),
// cmd.exe en UTF-8 sous Windows (sans ça, les accents des agents sortent en page de code 850).
QStringList interactiveShellArgs()
{
#ifdef Q_OS_WIN
    return {QStringLiteral("/k"), QStringLiteral("chcp 65001 >nul")};
#else
    return {QStringLiteral("-l")};
#endif
}

#ifdef Q_OS_WIN
// Une ligne colorée dans un script cmd.exe : echo, avec les caractères spéciaux de cmd échappés.
QString printfLine(const QString &text, const QString &style = QStringLiteral("90"))
{
    QString t = text;
    t.replace(QLatin1Char('%'), QStringLiteral("%%"));
    for (const QChar c : {QLatin1Char('^'), QLatin1Char('&'), QLatin1Char('|'), QLatin1Char('<'), QLatin1Char('>'), QLatin1Char('('), QLatin1Char(')')}) {
        t.replace(c, QStringLiteral("^") + c);
    }
    const QChar esc(0x1b);
    return QStringLiteral("echo %1[%2m%3%1[0m\r\n").arg(esc).arg(style, t);
}
#else
QString printfLine(const QString &text, const QString &style = QStringLiteral("90"))
{
    QString t = text;
    t.replace(QLatin1Char('%'), QStringLiteral("%%"));
    t.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    return QStringLiteral("printf '\\033[%1m%s\\033[0m\\n' %2\n").arg(style, KoraCutAgentsCli::shellQuote(t));
}
#endif

// Les arguments MCP juste après la commande de lancement (« goose session --with-extension … »), ou
// à la fin quand l'option avale tout ce qui suit (« claude "…" --mcp-config fichier »).
void insertMcpArguments(QStringList &command, const KoraCutAgentsCli::Agent &agent, const QStringList &mcpArgs)
{
    if (mcpArgs.isEmpty()) {
        return;
    }
    if (agent.mcp.value(QStringLiteral("ligne_position")).toString() == QLatin1String("fin")) {
        command << mcpArgs;
        return;
    }
    const int at = qBound(1, int(agent.launch.size()), int(command.size()));
    for (int i = 0; i < mcpArgs.size(); ++i) {
        command.insert(at + i, mcpArgs.at(i));
    }
}

} // namespace

KoraCutTerminalPanel::KoraCutTerminalPanel(KoraCutPorte *porte, QWidget *parent)
    : QWidget(parent)
    , m_porte(porte)
{
    setObjectName(QStringLiteral("koracutTerminalPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(240);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // La barre : les onglets, puis nouveau shell, l'agent, le terminal du système.
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("koracutTerminalBar"));
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(KoraCutMetrics::SpaceGroup, 2, KoraCutMetrics::SpaceGroup, 2);
    barLayout->setSpacing(KoraCutMetrics::SpaceTight);
    m_tabs = new QTabBar(bar);
    m_tabs->setObjectName(QStringLiteral("koracutTerminalTabs"));
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    m_tabs->setElideMode(Qt::ElideRight);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->setIconSize(KoraCutMetrics::iconDefault());
    barLayout->addWidget(m_tabs, 1);

    m_newTab = new QToolButton(bar);
    m_newTab->setObjectName(QStringLiteral("koracutTerminalNew"));
    m_newTab->setIcon(QIcon::fromTheme(QStringLiteral("list-add")));
    m_newTab->setIconSize(KoraCutMetrics::iconToolbar());
    m_newTab->setAutoRaise(true);
    m_newTab->setToolTip(i18nc("@info:tooltip", "New terminal in the project folder"));
    connect(m_newTab, &QToolButton::clicked, this, [this]() { newShell(); });
    barLayout->addWidget(m_newTab);

    m_agentButton = new QToolButton(bar);
    m_agentButton->setObjectName(QStringLiteral("koracutTerminalAgent"));
    m_agentButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_agentButton->setPopupMode(QToolButton::MenuButtonPopup);
    m_agentButton->setAutoRaise(true);
    m_agentButton->setIconSize(KoraCutMetrics::iconDefault());
    m_agentButton->setToolTip(i18nc("@info:tooltip", "Launch the agent that edits with you, or choose another one"));
    auto *agentMenu = new QMenu(m_agentButton);
    connect(agentMenu, &QMenu::aboutToShow, this, [this, agentMenu]() {
        agentMenu->clear();
        const QString last = KoraCutAgentsCli::lastChoice();
        for (const KoraCutAgentsCli::Agent &a : KoraCutAgentsCli::all()) {
            const bool installed = !KoraCutAgentsCli::locate(a).isEmpty();
            QAction *act = agentMenu->addAction(KoraCutAgentsCli::icon(a, 16),
                                                installed ? a.name : i18nc("@action:inmenu agent not installed yet", "%1 (to install)", a.name));
            act->setCheckable(true);
            act->setChecked(a.id == last);
            const QString id = a.id;
            connect(act, &QAction::triggered, this, [this, id, installed]() {
                if (installed) {
                    launchAgent(id, false);
                } else {
                    KoraCutAgentsCli::setLastChoice(id);
                    m_list->setCurrentRow(-1);
                    showChooser();
                }
            });
        }
        agentMenu->addSeparator();
        agentMenu->addAction(i18nc("@action:inmenu", "Choose the Agent…"), this, &KoraCutTerminalPanel::showChooser);
    });
    m_agentButton->setMenu(agentMenu);
    connect(m_agentButton, &QToolButton::clicked, this, [this]() {
        const QString last = KoraCutAgentsCli::lastChoice();
        const KoraCutAgentsCli::Agent *a = KoraCutAgentsCli::byId(last);
        if (a && !KoraCutAgentsCli::locate(*a).isEmpty()) {
            launchAgent(last, false);
        } else {
            showChooser();
        }
    });
    barLayout->addWidget(m_agentButton);

    m_systemButton = new QToolButton(bar);
    m_systemButton->setObjectName(QStringLiteral("koracutTerminalSystem"));
    m_systemButton->setIcon(QIcon::fromTheme(QStringLiteral("utilities-terminal")));
    m_systemButton->setIconSize(KoraCutMetrics::iconToolbar());
    m_systemButton->setAutoRaise(true);
    m_systemButton->setToolTip(i18nc("@info:tooltip", "Open in the system terminal instead, with the same agent and instructions"));
    connect(m_systemButton, &QToolButton::clicked, this, [this]() { openInSystemTerminal(); });
    barLayout->addWidget(m_systemButton);
    root->addWidget(bar);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("koracutTerminalStack"));
    root->addWidget(m_stack, 1);

    // La page de choix de l'agent.
    auto *scroll = new QScrollArea(m_stack);
    scroll->setObjectName(QStringLiteral("koracutTerminalChooserScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_chooser = new QWidget(scroll);
    m_chooser->setObjectName(QStringLiteral("koracutTerminalChooser"));
    auto *page = new QVBoxLayout(m_chooser);
    page->setContentsMargins(KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceZone);
    page->setSpacing(KoraCutMetrics::SpaceGroup);
    auto *title = new QLabel(i18nc("@title", "Who edits with you?"), m_chooser);
    title->setObjectName(QStringLiteral("koracutTerminalTitle"));
    QFont tf = title->font();
    tf.setPixelSize(KoraCutMetrics::FontTitle);
    tf.setWeight(QFont::DemiBold);
    title->setFont(tf);
    page->addWidget(title);
    auto *intro = new QLabel(i18nc("@info",
                                   "A command line agent works in this terminal, on this project, and you watch it edit. KoraCut writes its "
                                   "instructions in the project folder and connects it to KoraCut. Nothing is installed without your click."),
                             m_chooser);
    intro->setObjectName(QStringLiteral("koracutTerminalIntro"));
    intro->setWordWrap(true);
    page->addWidget(intro);

    auto *messageRow = new QHBoxLayout();
    messageRow->setContentsMargins(0, 0, 0, 0);
    m_message = new QLabel(m_chooser);
    m_message->setObjectName(QStringLiteral("koracutTerminalMessage"));
    m_message->setWordWrap(true);
    m_message->hide();
    messageRow->addWidget(m_message, 1);
    m_saveButton = new QPushButton(i18nc("@action:button", "Save the Project"), m_chooser);
    m_saveButton->setObjectName(QStringLiteral("koracutConsoleButton"));
    m_saveButton->hide();
    connect(m_saveButton, &QPushButton::clicked, this, &KoraCutTerminalPanel::saveProjectRequested);
    messageRow->addWidget(m_saveButton);
    page->addLayout(messageRow);

    m_list = new QListWidget(m_chooser);
    m_list->setObjectName(QStringLiteral("koracutTerminalAgents"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(true);
    m_list->setMinimumHeight(220);
    m_list->setIconSize(QSize(24, 24));
    connect(m_list, &QListWidget::currentRowChanged, this, &KoraCutTerminalPanel::updateDetails);
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this]() {
        if (m_launchButton->isEnabled()) {
            m_launchButton->click();
        }
    });
    page->addWidget(m_list, 1);

    auto *details = new QWidget(m_chooser);
    details->setObjectName(QStringLiteral("koracutTerminalDetails"));
    details->setAttribute(Qt::WA_StyledBackground, true);
    auto *dl = new QVBoxLayout(details);
    dl->setContentsMargins(KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup);
    dl->setSpacing(KoraCutMetrics::SpaceTight);
    m_detailTitle = new QLabel(details);
    m_detailTitle->setObjectName(QStringLiteral("koracutTerminalDetailTitle"));
    QFont df = m_detailTitle->font();
    df.setWeight(QFont::DemiBold);
    m_detailTitle->setFont(df);
    dl->addWidget(m_detailTitle);
    m_detailText = new QLabel(details);
    m_detailText->setObjectName(QStringLiteral("koracutTerminalDetailText"));
    m_detailText->setWordWrap(true);
    dl->addWidget(m_detailText);
    m_detailCommand = new QLabel(details);
    m_detailCommand->setObjectName(QStringLiteral("koracutTerminalDetailCommand"));
    m_detailCommand->setWordWrap(true);
    m_detailCommand->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailCommand->setFont(KoraCutFonts::code(KoraCutMetrics::FontMono));
    dl->addWidget(m_detailCommand);
    m_detailWarning = new QLabel(details);
    m_detailWarning->setObjectName(QStringLiteral("koracutTerminalDetailWarning"));
    m_detailWarning->setWordWrap(true);
    m_detailWarning->hide();
    dl->addWidget(m_detailWarning);
    auto *buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, KoraCutMetrics::SpaceTight, 0, 0);
    buttons->setSpacing(KoraCutMetrics::SpaceGroup);
    m_launchButton = new QPushButton(details);
    m_launchButton->setObjectName(QStringLiteral("koracutConsolePrimaryButton"));
    m_launchButton->setCursor(Qt::PointingHandCursor);
    connect(m_launchButton, &QPushButton::clicked, this, [this]() {
        const QString id = selectedAgentId();
        if (!id.isEmpty()) {
            launchAgent(id, true);
        }
    });
    buttons->addWidget(m_launchButton);
    m_nodeButton = new QPushButton(i18nc("@action:button opens the official Node.js download page", "Get Node.js"), details);
    m_nodeButton->setObjectName(QStringLiteral("koracutConsoleButton"));
    m_nodeButton->setCursor(Qt::PointingHandCursor);
    m_nodeButton->hide();
    connect(m_nodeButton, &QPushButton::clicked, this, []() { QDesktopServices::openUrl(QUrl(QStringLiteral("https://nodejs.org/en/download"))); });
    buttons->addWidget(m_nodeButton);
    m_siteButton = new QPushButton(i18nc("@action:button the agent's official website", "Website"), details);
    m_siteButton->setObjectName(QStringLiteral("koracutConsoleButton"));
    m_siteButton->setCursor(Qt::PointingHandCursor);
    connect(m_siteButton, &QPushButton::clicked, this, [this]() {
        if (const KoraCutAgentsCli::Agent *a = KoraCutAgentsCli::byId(selectedAgentId())) {
            QDesktopServices::openUrl(QUrl(a->doc.isEmpty() ? a->site : a->doc));
        }
    });
    buttons->addWidget(m_siteButton);
    buttons->addStretch(1);
    dl->addLayout(buttons);
    page->addWidget(details);

    auto *other = new QHBoxLayout();
    other->setContentsMargins(0, 0, 0, 0);
    other->setSpacing(KoraCutMetrics::SpaceGroup);
    auto *shellButton = new QPushButton(i18nc("@action:button a terminal without agent", "Plain Terminal"), m_chooser);
    shellButton->setObjectName(QStringLiteral("koracutConsoleButton"));
    shellButton->setCursor(Qt::PointingHandCursor);
    connect(shellButton, &QPushButton::clicked, this, [this]() { newShell(); });
    other->addWidget(shellButton);
    auto *systemButton = new QPushButton(i18nc("@action:button", "In the System Terminal"), m_chooser);
    systemButton->setObjectName(QStringLiteral("koracutConsoleButton"));
    systemButton->setCursor(Qt::PointingHandCursor);
    connect(systemButton, &QPushButton::clicked, this, [this]() { openInSystemTerminal(selectedAgentId()); });
    other->addWidget(systemButton);
    other->addStretch(1);
    page->addLayout(other);
    scroll->setWidget(m_chooser);
    m_stack->addWidget(scroll);

    connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
        if (index < 0) {
            m_stack->setCurrentIndex(0);
            return;
        }
        auto *view = qobject_cast<KoraCutTerminalView *>(m_tabs->tabData(index).value<QObject *>());
        if (view) {
            m_stack->setCurrentWidget(view);
            view->setFocus(Qt::OtherFocusReason);
        }
    });
    connect(m_tabs, &QTabBar::tabCloseRequested, this, &KoraCutTerminalPanel::closeTab);
    connect(m_tabs, &QTabBar::tabBarClicked, this, [this](int index) {
        if (index == m_tabs->currentIndex() && index >= 0) {
            // Un clic sur l'onglet déjà actif ramène au terminal depuis la page de choix.
            auto *view = qobject_cast<KoraCutTerminalView *>(m_tabs->tabData(index).value<QObject *>());
            if (view) {
                m_stack->setCurrentWidget(view);
                view->setFocus(Qt::OtherFocusReason);
            }
        }
    });

    // Couleurs de la page de choix : tokens de la charte produit, le reste vient de la feuille de style.
    setStyleSheet(QStringLiteral("#koracutTerminalDetails { border: 1px solid palette(mid); border-radius: 9px; }"
                                 "#koracutTerminalDetailWarning { color: #FFD166; }"
                                 "#koracutTerminalMessage { color: #FFD166; }"
                                 "#koracutTerminalIntro, #koracutTerminalDetailText { color: palette(placeholder-text); }"
                                 "#koracutTerminalView { background: #1E1E1E; }"));
    refreshChooser();
    m_stack->setCurrentIndex(0);
}

KoraCutTerminalPanel::~KoraCutTerminalPanel()
{
    // Les onglets ferment leurs processus ; le dossier temporaire de nos scripts part avec.
    while (m_tabs->count() > 0) {
        auto *view = qobject_cast<KoraCutTerminalView *>(m_tabs->tabData(0).value<QObject *>());
        m_tabs->removeTab(0);
        delete view;
    }
    QDir(tempRoot()).removeRecursively();
}

void KoraCutTerminalPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_stack->currentIndex() == 0) {
        refreshChooser();
    }
}

QString KoraCutTerminalPanel::selectedAgentId() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(IdRole).toString() : QString();
}

void KoraCutTerminalPanel::setMessage(const QString &text, bool warning)
{
    m_message->setText(text);
    m_message->setVisible(!text.isEmpty());
    m_message->setProperty("warning", warning);
}

void KoraCutTerminalPanel::refreshChooser()
{
    const QString keep = selectedAgentId().isEmpty() ? KoraCutAgentsCli::lastChoice() : selectedAgentId();
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    int selectRow = 0;
    for (const KoraCutAgentsCli::Agent &a : KoraCutAgentsCli::all()) {
        const QString path = KoraCutAgentsCli::locate(a);
        auto *item = new QListWidgetItem(m_list);
        item->setData(IdRole, a.id);
        item->setSizeHint(QSize(100, 46));
        auto *row = new QWidget(m_list);
        auto *rl = new QHBoxLayout(row);
        rl->setContentsMargins(KoraCutMetrics::SpaceGroup, 4, KoraCutMetrics::SpaceGroup, 4);
        rl->setSpacing(KoraCutMetrics::SpaceGroup);
        auto *logo = new QLabel(row);
        logo->setPixmap(KoraCutAgentsCli::icon(a, 24).pixmap(24, 24));
        logo->setFixedSize(24, 24);
        rl->addWidget(logo);
        auto *texts = new QVBoxLayout();
        texts->setContentsMargins(0, 0, 0, 0);
        texts->setSpacing(0);
        auto *name = new QLabel(a.name, row);
        QFont nf = name->font();
        nf.setWeight(QFont::DemiBold);
        name->setFont(nf);
        texts->addWidget(name);
        QStringList needs;
        for (const QString &n : a.needs) {
            if (n == QLatin1String("compte")) {
                needs << i18nc("@info what the agent needs", "account");
            } else if (n == QLatin1String("abonnement")) {
                needs << i18nc("@info what the agent needs", "subscription");
            } else if (n == QLatin1String("cle")) {
                needs << i18nc("@info what the agent needs", "API key");
            } else if (n == QLatin1String("local")) {
                needs << i18nc("@info what the agent needs", "local models");
            }
        }
        QString sub = a.publisher;
        if (!needs.isEmpty()) {
            sub += QStringLiteral(" · ") + needs.join(i18nc("@info separator between needs, like ' or '", " or "));
        }
        if (a.free) {
            sub += QStringLiteral(" · ") + i18nc("@info the agent has a real free offer", "free offer");
        }
        auto *subLabel = new QLabel(sub, row);
        subLabel->setObjectName(QStringLiteral("koracutTerminalIntro"));
        QFont sf = subLabel->font();
        sf.setPixelSize(KoraCutMetrics::FontDense);
        subLabel->setFont(sf);
        texts->addWidget(subLabel);
        rl->addLayout(texts, 1);
        auto *state = new QLabel(row);
        if (!path.isEmpty()) {
            state->setText(i18nc("@info agent state", "Installed"));
            state->setStyleSheet(QStringLiteral("color: #4ADE80;"));
            state->setToolTip(path);
        } else if (a.install.command.isEmpty()) {
            state->setText(i18nc("@info agent state", "Not on this system"));
            state->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
        } else {
            state->setText(i18nc("@info agent state", "To install"));
            state->setStyleSheet(QStringLiteral("color: #FFD166;"));
        }
        QFont stf = state->font();
        stf.setPixelSize(KoraCutMetrics::FontDense);
        state->setFont(stf);
        rl->addWidget(state);
        m_list->setItemWidget(item, row);
        if (a.id == keep) {
            selectRow = m_list->count() - 1;
        }
    }
    if (m_list->count() > 0) {
        m_list->setCurrentRow(selectRow);
    }
    // Le bouton de la barre montre l'agent retenu.
    const KoraCutAgentsCli::Agent *last = KoraCutAgentsCli::byId(KoraCutAgentsCli::lastChoice());
    if (last) {
        m_agentButton->setIcon(KoraCutAgentsCli::icon(*last, 16));
        m_agentButton->setText(last->name);
    } else {
        m_agentButton->setIcon(QIcon::fromTheme(QStringLiteral("system-run")));
        m_agentButton->setText(i18nc("@action:button", "Agent"));
    }
    updateDetails();
}

void KoraCutTerminalPanel::updateDetails()
{
    const KoraCutAgentsCli::Agent *a = KoraCutAgentsCli::byId(selectedAgentId());
    if (!a) {
        m_detailTitle->clear();
        m_detailText->clear();
        m_detailCommand->clear();
        m_launchButton->setEnabled(false);
        m_launchButton->setText(i18nc("@action:button", "Launch"));
        return;
    }
    const QString path = KoraCutAgentsCli::locate(*a);
    m_detailTitle->setText(a->publisher.isEmpty() ? a->name : i18nc("@title agent name and who makes it", "%1, by %2", a->name, a->publisher));
    QString text = a->needText;
    if (!a->notes.isEmpty()) {
        text += (text.isEmpty() ? QString() : QStringLiteral(" ")) + a->notes;
    }
    m_detailText->setText(text);
    const QStringList missing = path.isEmpty() ? KoraCutAgentsCli::missingRequirements(*a) : QStringList();
    bool needsNode = false;
    for (const QString &m : missing) {
        needsNode = needsNode || m.startsWith(QLatin1String("node"));
    }
    if (!path.isEmpty()) {
        m_detailCommand->setText(i18nc("@info where the agent is installed", "Installed: %1", QDir::toNativeSeparators(path)));
        m_launchButton->setText(i18nc("@action:button %1 agent name", "Launch %1", a->name));
        m_launchButton->setEnabled(true);
    } else if (!a->install.command.isEmpty()) {
        m_detailCommand->setText(i18nc("@info the official install command, shown before it runs", "Official installation: %1", a->install.command));
        m_launchButton->setText(i18nc("@action:button %1 agent name", "Install and Launch %1", a->name));
        m_launchButton->setEnabled(missing.isEmpty());
    } else {
        m_detailCommand->setText(i18nc("@info", "No official installation for this system."));
        m_launchButton->setText(i18nc("@action:button %1 agent name", "Launch %1", a->name));
        m_launchButton->setEnabled(false);
    }
    if (!missing.isEmpty()) {
        QString warning;
        for (const QString &m : missing) {
            if (m.startsWith(QLatin1String("node"))) {
                const QString version = m.section(QStringLiteral(">="), 1);
                warning += i18nc("@info %1 minimum Node.js version", "Node.js %1 or later is needed first: install it from the official site, then come back.",
                                 version.isEmpty() ? QStringLiteral("18") : version);
            } else if (m.startsWith(QLatin1String("python"))) {
                warning += i18nc("@info", "Python 3 is needed first.");
            } else if (m == QLatin1String("wsl")) {
                warning += i18nc("@info", "On Windows, this agent runs in WSL.");
            } else {
                warning += i18nc("@info %1 a tool name", "%1 is needed first.", m);
            }
            warning += QLatin1Char(' ');
        }
        m_detailWarning->setText(warning.trimmed());
    }
    m_detailWarning->setVisible(!missing.isEmpty());
    m_nodeButton->setVisible(needsNode);
    m_siteButton->setEnabled(!a->site.isEmpty() || !a->doc.isEmpty());
}

void KoraCutTerminalPanel::showChooser()
{
    refreshChooser();
    m_stack->setCurrentIndex(0);
}

KoraCutTerminalView *KoraCutTerminalPanel::currentView() const
{
    const int index = m_tabs->currentIndex();
    return index < 0 ? nullptr : qobject_cast<KoraCutTerminalView *>(m_tabs->tabData(index).value<QObject *>());
}

int KoraCutTerminalPanel::tabCount() const
{
    return m_tabs->count();
}

void KoraCutTerminalPanel::focusTerminal()
{
    if (KoraCutTerminalView *view = currentView(); view && m_stack->currentWidget() == view) {
        view->setFocus(Qt::ShortcutFocusReason);
    }
}

void KoraCutTerminalPanel::openHere()
{
    if (KoraCutTerminalView *view = currentView()) {
        m_stack->setCurrentWidget(view);
        view->setFocus(Qt::ShortcutFocusReason);
        return;
    }
    const QString last = KoraCutAgentsCli::lastChoice();
    const KoraCutAgentsCli::Agent *a = KoraCutAgentsCli::byId(last);
    if (a && !KoraCutAgentsCli::locate(*a).isEmpty()) {
        if (launchAgent(last, false)) {
            return;
        }
    }
    showChooser();
}

KoraCutTerminalView *KoraCutTerminalPanel::addTerminal(const QString &title, const QIcon &icon)
{
    auto *view = new KoraCutTerminalView(m_stack);
    m_stack->addWidget(view);
    const int index = m_tabs->addTab(icon, title);
    m_tabs->setTabData(index, QVariant::fromValue<QObject *>(view));
    m_tabs->setTabToolTip(index, title);
    connect(view, &KoraCutTerminalView::titleChanged, this, [this, view](const QString &t) {
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (m_tabs->tabData(i).value<QObject *>() == view) {
                m_tabs->setTabToolTip(i, t);
            }
        }
    });
    connect(view, &KoraCutTerminalView::restartRequested, this, [this, view]() {
        QString dir = QDir::homePath();
        const KoraCutPorte::Reply info = m_porte ? m_porte->call(clientName(), QStringLiteral("projet.info"), {}) : KoraCutPorte::Reply();
        if (info.ok() && !info.result.toObject().value(QStringLiteral("dossier")).toString().isEmpty()) {
            dir = info.result.toObject().value(QStringLiteral("dossier")).toString();
        }
        const QString link = ctlLinkDirectory();
        const QProcessEnvironment env = KoraCutAgentsCli::terminalEnvironment(m_porte ? m_porte->porteFilePath() : QString(), m_porte ? m_porte->reglage() : QString(),
                                                                              link.isEmpty() ? QStringList() : QStringList{link});
        view->start(KoraCutAgentsCli::userShell(), interactiveShellArgs(), dir, env);
    });
    m_tabs->setCurrentIndex(index);
    m_stack->setCurrentWidget(view);
    view->setFocus(Qt::OtherFocusReason);
    return view;
}

void KoraCutTerminalPanel::closeTab(int index)
{
    auto *view = qobject_cast<KoraCutTerminalView *>(m_tabs->tabData(index).value<QObject *>());
    if (view && view->pty()->hasForegroundJob()) {
        const auto answer = QMessageBox::question(this, i18nc("@title:window", "Close the Terminal"),
                                                  i18nc("@info", "A program is still running in this tab (%1). Close it anyway?", m_tabs->tabText(index)));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    m_tabs->removeTab(index);
    if (view) {
        m_stack->removeWidget(view);
        view->pty()->terminate();
        view->deleteLater();
    }
    if (m_tabs->count() == 0) {
        showChooser();
    }
}

QString KoraCutTerminalPanel::ctlLinkDirectory()
{
    if (!m_linkDir.isEmpty() && QFileInfo(m_linkDir).isDir()) {
        return m_linkDir;
    }
    const QString ctlDir = KoraCutTerminal::ctlDirectory();
    if (ctlDir.isEmpty()) {
        return QString();
    }
#ifdef Q_OS_WIN
    m_linkDir = ctlDir;
#else
    // koracut-ctl seul dans un dossier à part : son voisinage (les outils de l'application) reste hors du PATH.
    const QString dir = tempRoot() + QStringLiteral("/bin");
    QDir().mkpath(dir);
    const QString link = dir + QStringLiteral("/koracut-ctl");
    QFile::remove(link);
    if (!QFile::link(ctlDir + QStringLiteral("/koracut-ctl"), link)) {
        return QString();
    }
    m_linkDir = dir;
#endif
    return m_linkDir;
}

bool KoraCutTerminalPanel::prepareProject(QString &dir, KoraCutAgentInit::Context &ctx, QString &message)
{
    if (!m_porte) {
        message = i18nc("@info", "The command port is not available.");
        return false;
    }
    const KoraCutPorte::Reply info = m_porte->call(clientName(), QStringLiteral("projet.info"), {});
    if (!info.ok()) {
        message = i18nc("@info", "No project is open yet.");
        m_saveButton->hide();
        return false;
    }
    ctx.project = info.result.toObject();
    dir = ctx.project.value(QStringLiteral("dossier")).toString();
    if (dir.isEmpty()) {
        message = i18nc("@info", "Save the project first: the agent works in its folder, next to the rushes.");
        m_saveButton->show();
        return false;
    }
    m_saveButton->hide();
    const KoraCutPorte::Reply medias = m_porte->call(clientName(), QStringLiteral("medias.lister"), QJsonObject{{QStringLiteral("limite"), 200}});
    if (medias.ok()) {
        ctx.medias = medias.result.toObject().value(QStringLiteral("medias")).toArray();
    }
    ctx.reglage = m_porte->reglage();
    ctx.porteFile = m_porte->porteFilePath();
    ctx.mcpScript = KoraCutAgentInit::findMcpServer();
    ctx.nodePath = KoraCutAgentsCli::findNode();
    ctx.skills = KoraCutAgentInit::findSkills();
#ifndef Q_OS_WIN
    // Un lanceur du serveur MCP sans espace dans son chemin, pour les agents qui prennent le serveur en
    // une seule chaîne (Goose). Il vit le temps de la session, comme le lien vers koracut-ctl.
    if (!ctx.mcpScript.isEmpty() && !ctlLinkDirectory().isEmpty()) {
        const QString wrapper = ctlLinkDirectory() + QStringLiteral("/koracut-mcp");
        QFile w(wrapper);
        if (w.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream out(&w);
            out << "#!/bin/sh\n";
            out << "export KORACUT_PORTE=" << KoraCutAgentsCli::shellQuote(ctx.porteFile) << "\n";
            out << "exec " << KoraCutAgentsCli::shellQuote(ctx.nodePath.isEmpty() ? QStringLiteral("node") : ctx.nodePath) << " "
                << KoraCutAgentsCli::shellQuote(ctx.mcpScript) << " \"$@\"\n";
            out.flush();
            w.close();
            QFile::setPermissions(wrapper, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            ctx.mcpWrapper = wrapper;
        }
    }
#endif
    return true;
}

bool KoraCutTerminalPanel::startScript(KoraCutTerminalView *view, const QString &dir, const QString &body)
{
#ifdef Q_OS_WIN
    // Sous Windows, le même script en cmd.exe : /k garde le shell ouvert dans le dossier du projet
    // quand l'agent s'arrête, comme « exec $SHELL -l » ailleurs.
    QDir().mkpath(tempRoot());
    const QString script = tempRoot() + QStringLiteral("/lancer-%1.cmd").arg(++m_scriptCount);
    QFile f(script);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QTextStream out(&f);
    out << "@echo off\r\n";
    out << "chcp 65001 >nul\r\n";
    out << "cd /d " << KoraCutAgentsCli::shellQuote(QDir::toNativeSeparators(dir)) << " || exit /b 1\r\n";
    out << QString(body).replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\n'), QStringLiteral("\r\n"));
    out.flush();
    f.close();
    const QString link = ctlLinkDirectory();
    const QProcessEnvironment env =
        KoraCutAgentsCli::terminalEnvironment(m_porte ? m_porte->porteFilePath() : QString(), m_porte ? m_porte->reglage() : QString(), link.isEmpty() ? QStringList() : QStringList{link});
    QString error;
    if (!view->start(KoraCutAgentsCli::userShell(), {QStringLiteral("/d"), QStringLiteral("/k"), QDir::toNativeSeparators(script)}, dir, env, &error)) {
        view->feed(QByteArray("\x1b[31m") + i18nc("@info %1 technical reason", "The terminal could not start (%1).", error).toUtf8() + QByteArray("\x1b[0m\r\n"));
        return false;
    }
    return true;
#else
    const QString shell = scriptShell();
    QDir().mkpath(tempRoot());
    const QString script = tempRoot() + QStringLiteral("/lancer-%1.sh").arg(++m_scriptCount);
    QFile f(script);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    QTextStream out(&f);
    out << "cd " << KoraCutAgentsCli::shellQuote(dir) << " || exit 1\n";
    out << body;
    // Quand l'agent s'arrête, l'onglet garde un shell ouvert dans le dossier du projet.
    out << "exec " << KoraCutAgentsCli::shellQuote(KoraCutAgentsCli::userShell()) << " -l\n";
    out.flush();
    f.close();
    QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString link = ctlLinkDirectory();
    const QProcessEnvironment env =
        KoraCutAgentsCli::terminalEnvironment(m_porte ? m_porte->porteFilePath() : QString(), m_porte ? m_porte->reglage() : QString(), link.isEmpty() ? QStringList() : QStringList{link});
    // -l : le PATH de la personne (Homebrew, nvm) ; -i : son .zshrc aussi, là où nvm se charge souvent.
    QString error;
    if (!view->start(shell, {QStringLiteral("-l"), QStringLiteral("-i"), script}, dir, env, &error)) {
        view->feed(QByteArray("\x1b[31m") + i18nc("@info %1 technical reason", "The terminal could not start (%1).", error).toUtf8() + QByteArray("\x1b[0m\r\n"));
        return false;
    }
    return true;
#endif
}

KoraCutTerminalView *KoraCutTerminalPanel::newShell()
{
    QString dir = QDir::homePath();
    if (m_porte) {
        const KoraCutPorte::Reply info = m_porte->call(clientName(), QStringLiteral("projet.info"), {});
        const QString projectDir = info.ok() ? info.result.toObject().value(QStringLiteral("dossier")).toString() : QString();
        if (!projectDir.isEmpty()) {
            dir = projectDir;
        }
    }
    if (!KoraCutPty::isSupported()) {
        QString error;
        KoraCutTerminal::open(dir, m_porte ? m_porte->porteFilePath() : QString(), KoraCutTerminal::ctlDirectory(), QString(), error);
        return nullptr;
    }
    ++m_shellCount;
    auto *view = addTerminal(m_shellCount == 1 ? i18nc("@title:tab", "Terminal") : i18nc("@title:tab %1 number", "Terminal %1", m_shellCount),
                             QIcon::fromTheme(QStringLiteral("utilities-terminal")));
    const QString link = ctlLinkDirectory();
    const QProcessEnvironment env = KoraCutAgentsCli::terminalEnvironment(m_porte ? m_porte->porteFilePath() : QString(), m_porte ? m_porte->reglage() : QString(),
                                                                          link.isEmpty() ? QStringList() : QStringList{link});
    QString error;
    if (!view->start(KoraCutAgentsCli::userShell(), interactiveShellArgs(), dir, env, &error)) {
        view->feed(QByteArray("\x1b[31m") + i18nc("@info %1 technical reason", "The terminal could not start (%1).", error).toUtf8() + QByteArray("\x1b[0m\r\n"));
    }
    return view;
}

bool KoraCutTerminalPanel::launchAgent(const QString &id, bool install)
{
    const KoraCutAgentsCli::Agent *agent = KoraCutAgentsCli::byId(id);
    if (!agent) {
        return false;
    }
    KoraCutAgentsCli::setLastChoice(id);
    QString dir;
    QString message;
    KoraCutAgentInit::Context ctx;
    if (!prepareProject(dir, ctx, message)) {
        showChooser();
        setMessage(message, true);
        return false;
    }
    setMessage(QString(), false);
    const QString exe = KoraCutAgentsCli::locate(*agent);
    if (exe.isEmpty()) {
        if (!install) {
            showChooser();
            setMessage(i18nc("@info %1 agent name", "%1 is not installed on this computer yet.", agent->name), true);
            return false;
        }
        if (agent->install.command.isEmpty() || !KoraCutAgentsCli::missingRequirements(*agent).isEmpty()) {
            showChooser();
            return false;
        }
    }
    if (!KoraCutPty::isSupported()) {
        return openInSystemTerminal(id);
    }
    QStringList errors;
    const QStringList written = KoraCutAgentInit::writeForAgent(dir, *agent, ctx, &errors);
    QStringList names;
    for (const QString &w : written) {
        names << QDir(dir).relativeFilePath(w);
    }

    QString body;
    body += printfLine(i18nc("@info first line in the terminal; %1 agent, %2 files", "KoraCut: %1 in this project. Instructions written: %2.", agent->name,
                             names.isEmpty() ? i18nc("@info no file", "none") : names.join(QStringLiteral(", "))));
    for (const QString &e : std::as_const(errors)) {
        body += printfLine(i18nc("@info %1 technical reason", "Not written: %1", e), QStringLiteral("33"));
    }
    if (exe.isEmpty()) {
        body += printfLine(i18nc("@info %1 agent name", "KoraCut installs %1 with its official command:", agent->name), QStringLiteral("1"));
        body += printfLine(QStringLiteral("  ") + agent->install.command, QStringLiteral("36"));
#ifdef Q_OS_WIN
        // Les commandes d'installation de Windows sont souvent en PowerShell (« irm … | iex »).
        if (agent->install.shell == QLatin1String("powershell")) {
            body += QStringLiteral("powershell -NoProfile -ExecutionPolicy Bypass -Command %1\n").arg(KoraCutAgentsCli::shellQuote(agent->install.command));
        } else {
            body += QStringLiteral("call ") + agent->install.command + QLatin1Char('\n');
        }
        // Le PATH de ce shell ne voit pas encore ce qui vient d'être installé : on relit celui de l'utilisateur.
        body += QStringLiteral("for /f \"usebackq tokens=2,*\" %%A in (`reg query HKCU\\Environment /v Path 2^>nul`) do set \"PATH=%%B;%PATH%\"\n");
        body += QStringLiteral("where %1 >nul 2>nul\nif errorlevel 1 (\n").arg(agent->executables.value(0));
        body += QStringLiteral("  ") + printfLine(i18nc("@info", "The installation did not finish. Read the messages above, then close this tab and try again."),
                                                  QStringLiteral("31"));
        body += QStringLiteral("  goto :eof\n)\n");
    }
#else
        body += agent->install.command + QLatin1Char('\n');
        // Un installeur qui pose sa commande dans ~/.local/bin ne met pas toujours le PATH de ce shell à jour.
        body += QStringLiteral("hash -r 2>/dev/null\n");
        body += QStringLiteral("if ! command -v %1 >/dev/null 2>&1; then\n").arg(KoraCutAgentsCli::shellQuote(agent->executables.value(0)));
        body += QStringLiteral("  ") + printfLine(i18nc("@info", "The installation did not finish. Read the messages above, then close this tab and try again."),
                                                  QStringLiteral("31"));
        body += QStringLiteral("  exec %1 -l\nfi\n").arg(KoraCutAgentsCli::shellQuote(KoraCutAgentsCli::userShell()));
    }
#endif
    QStringList command = KoraCutAgentsCli::launchCommand(*agent, KoraCutAgentInit::firstMessage());
    if (!exe.isEmpty() && !command.isEmpty()) {
        command[0] = exe;
    }
    const QStringList mcpArgs = KoraCutAgentInit::mcpLaunchArguments(*agent, ctx);
    insertMcpArguments(command, *agent, mcpArgs);
#ifdef Q_OS_WIN
    // « call » : un agent installé par npm est un .cmd, qui sans lui ne rendrait pas la main au script.
    body += QStringLiteral("call ");
#endif
    body += KoraCutAgentsCli::shellLine(command) + QLatin1Char('\n');

    auto *view = addTerminal(agent->name, KoraCutAgentsCli::icon(*agent, 16));
    const bool ok = startScript(view, dir, body);
    refreshChooser();
    if (m_tabs->count() > 0) {
        m_stack->setCurrentWidget(view);
        view->setFocus(Qt::OtherFocusReason);
    }
    return ok;
}

bool KoraCutTerminalPanel::openInSystemTerminal(const QString &agentId)
{
    QString dir;
    QString message;
    KoraCutAgentInit::Context ctx;
    if (!prepareProject(dir, ctx, message)) {
        showChooser();
        setMessage(message, true);
        return false;
    }
    const QString id = agentId.isEmpty() ? KoraCutAgentsCli::lastChoice() : agentId;
    const KoraCutAgentsCli::Agent *agent = KoraCutAgentsCli::byId(id);
    QString command;
    if (agent && !KoraCutAgentsCli::locate(*agent).isEmpty()) {
        KoraCutAgentInit::writeForAgent(dir, *agent, ctx);
        QStringList args = KoraCutAgentsCli::launchCommand(*agent, KoraCutAgentInit::firstMessage());
        const QStringList mcpArgs = KoraCutAgentInit::mcpLaunchArguments(*agent, ctx);
        insertMcpArguments(args, *agent, mcpArgs);
        command = KoraCutAgentsCli::shellLine(args);
    }
    QString error;
    if (!KoraCutTerminal::open(dir, ctx.porteFile, KoraCutTerminal::ctlDirectory(), command, error)) {
        showChooser();
        setMessage(i18nc("@info", "No terminal could be opened (%1).", error), true);
        return false;
    }
    return true;
}

QString KoraCutTerminalPanel::debugCommand(const QString &command)
{
    const QString verb = command.section(QLatin1Char('='), 0, 0);
    const QString arg = command.section(QLatin1Char('='), 1);
    if (verb == QLatin1String("ouvrir")) {
        openHere();
    } else if (verb == QLatin1String("choisir")) {
        if (!arg.isEmpty()) {
            KoraCutAgentsCli::setLastChoice(arg);
            m_list->setCurrentRow(-1);
        }
        showChooser();
    } else if (verb == QLatin1String("shell")) {
        newShell();
    } else if (verb == QLatin1String("agent")) {
        return QStringLiteral("agent %1 %2").arg(arg, launchAgent(arg, false) ? QStringLiteral("ok") : QStringLiteral("echec"));
    } else if (verb == QLatin1String("installer")) {
        return QStringLiteral("installer %1 %2").arg(arg, launchAgent(arg, true) ? QStringLiteral("ok") : QStringLiteral("echec"));
    } else if (verb == QLatin1String("taper")) {
        if (KoraCutTerminalView *view = currentView()) {
            view->sendText(arg + QLatin1Char('\r'));
        }
    } else if (verb == QLatin1String("touche")) {
        if (KoraCutTerminalView *view = currentView()) {
            const QString key = arg.toLower();
            view->sendText(key == QLatin1String("entree") ? QStringLiteral("\r") : key == QLatin1String("echap") ? QStringLiteral("\x1b") : arg);
        }
    } else if (verb == QLatin1String("fermer")) {
        if (m_tabs->currentIndex() >= 0) {
            closeTab(m_tabs->currentIndex());
        }
    } else if (verb == QLatin1String("systeme")) {
        return QStringLiteral("systeme %1").arg(openInSystemTerminal(arg) ? QStringLiteral("ok") : QStringLiteral("echec"));
    }
    // Toujours rendre l'état : onglets, agents, écran.
    QJsonObject state;
    QJsonArray tabs;
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *view = qobject_cast<KoraCutTerminalView *>(m_tabs->tabData(i).value<QObject *>());
        tabs.append(QJsonObject{{QStringLiteral("titre"), m_tabs->tabText(i)},
                                {QStringLiteral("actif"), i == m_tabs->currentIndex()},
                                {QStringLiteral("processus"), view && view->isRunning()},
                                {QStringLiteral("colonnes"), view ? view->columns() : 0},
                                {QStringLiteral("lignes"), view ? view->rows() : 0}});
    }
    state.insert(QStringLiteral("onglets"), tabs);
    state.insert(QStringLiteral("page"), m_stack->currentIndex() == 0 ? QStringLiteral("choix") : QStringLiteral("terminal"));
    QJsonArray agents;
    for (const KoraCutAgentsCli::Agent &a : KoraCutAgentsCli::all()) {
        agents.append(QJsonObject{{QStringLiteral("id"), a.id}, {QStringLiteral("installe"), KoraCutAgentsCli::locate(a)}});
    }
    state.insert(QStringLiteral("agents"), agents);
    state.insert(QStringLiteral("dernier"), KoraCutAgentsCli::lastChoice());
    if (KoraCutTerminalView *view = currentView()) {
        state.insert(QStringLiteral("ecran"), QJsonArray::fromStringList(view->screenLines()));
    }
    return QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
}
