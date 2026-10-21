/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutterminalpanel.h"
#include "../koracutfonts.h"
#include "koracut/koracutmetrics.h"
#include "koracut/porte/koracutporte.h"
#include "koracut/porte/koracutterminal.h"
#include "koracut/motion/koracutmotionrenderer.h"
#include "koracutpty.h"
#include "koracutskills.h"
#include "koracutterminalview.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>
#include <QStackedWidget>
#include <QTabBar>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int IdRole = Qt::UserRole + 1;
const QString kClaudeId = QStringLiteral("claude-code");

// La langue des scripts livrés (agents/installer-*.sh) : celle de l'interface.
QString scriptLanguage()
{
    const QStringList languages = KLocalizedString::languages();
    return languages.isEmpty() ? QStringLiteral("en") : languages.first().left(2);
}

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
QStringList interactiveShellArgs(const QString &linkDir = QString())
{
#ifdef Q_OS_WIN
    Q_UNUSED(linkDir)
    return {QStringLiteral("/k"), QStringLiteral("chcp 65001 >nul")};
#else
    // Un shell de connexion remet les dossiers du système en tête du PATH (path_helper sur macOS) : le dossier de
    // KoraCut (koracut-ctl, hyperframes, la garde npx) repasse devant, puis le shell interactif prend la suite.
    const QString shell = KoraCutAgentsCli::userShell();
    const QString name = QFileInfo(shell).fileName();
    if (!linkDir.isEmpty() && (name == QLatin1String("zsh") || name == QLatin1String("bash") || name == QLatin1String("sh") || name == QLatin1String("ksh"))) {
        return {QStringLiteral("-l"), QStringLiteral("-c"),
                QStringLiteral("PATH=%1:\"$PATH\"; export PATH; exec %2 -i").arg(KoraCutAgentsCli::shellQuote(linkDir), KoraCutAgentsCli::shellQuote(shell))};
    }
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

// La ligne qui lance l'agent : la conversation d'hier reprise s'il y en a une dans ce dossier (Claude Code),
// sinon le premier message ; puis le serveur MCP et, si la personne le veut, sans demande d'accord (KC-008).
QStringList agentCommand(const KoraCutAgentsCli::Agent &agent, const KoraCutAgentInit::Context &ctx, const QString &dir, bool *resumed = nullptr)
{
    QStringList command = KoraCutAgentsCli::resumeCommand(agent, dir);
    if (resumed) {
        *resumed = !command.isEmpty();
    }
    if (command.isEmpty()) {
        command = KoraCutAgentsCli::launchCommand(agent, KoraCutAgentInit::firstMessage());
    }
    insertMcpArguments(command, agent, KoraCutAgentInit::mcpLaunchArguments(agent, ctx));
    KoraCutAgentsCli::applyNoApproval(command, agent);
    return command;
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
    m_agentButton->setText(i18nc("@action:button opens the terminal with the command line agent of your choice", "Edit with an Agent"));
    m_agentButton->setToolTip(i18nc("@info:tooltip",
                                    "Launches your agent here (Claude Code, Codex, Gemini, OpenCode and others), connected to KoraCut, or lets you choose one. "
                                    "KoraCut prepares the project and offers to install a missing agent. You sign in with your own account."));
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
    connect(m_agentButton, &QToolButton::clicked, this, [this]() { editWithAgent(); });
    barLayout->addWidget(m_agentButton);

    // La place du panneau (4 octobre 2026) : à côté de la timeline par défaut, et où la personne veut.
    m_placeButton = new QToolButton(bar);
    m_placeButton->setObjectName(QStringLiteral("koracutTerminalPlace"));
    m_placeButton->setIcon(QIcon::fromTheme(QStringLiteral("view-split-left-right")));
    m_placeButton->setIconSize(KoraCutMetrics::iconToolbar());
    m_placeButton->setAutoRaise(true);
    m_placeButton->setPopupMode(QToolButton::InstantPopup);
    m_placeButton->setToolTip(i18nc("@info:tooltip", "Where the terminal sits: beside the timeline, below, above, or with the Agents panel"));
    auto *placeMenu = new QMenu(m_placeButton);
    const QList<QPair<QString, QString>> places{{QStringLiteral("droite"), i18nc("@action:inmenu terminal place", "Right of the Timeline")},
                                                {QStringLiteral("gauche"), i18nc("@action:inmenu terminal place", "Left of the Timeline")},
                                                {QStringLiteral("bas"), i18nc("@action:inmenu terminal place", "Below the Timeline")},
                                                {QStringLiteral("haut"), i18nc("@action:inmenu terminal place", "Above the Timeline")},
                                                {QStringLiteral("agents"), i18nc("@action:inmenu terminal place", "With the Agents Panel")}};
    for (const auto &place : places) {
        const QString id = place.first;
        placeMenu->addAction(place.second, this, [this, id]() { Q_EMIT placeRequested(id); });
    }
    m_placeButton->setMenu(placeMenu);
    barLayout->addWidget(m_placeButton);

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
    // Les deux voies (4 octobre 2026). La première : le chat de KoraCut avec sa propre clé (Connexions IA).
    m_chatWay = new QWidget(m_chooser);
    auto *chatLayout = new QHBoxLayout(m_chatWay);
    chatLayout->setContentsMargins(0, 0, 0, 0);
    chatLayout->setSpacing(KoraCutMetrics::SpaceGroup);
    auto *chatText = new QLabel(i18nc("@info first way to edit with an AI, in the terminal chooser",
                                      "<b>1. In the KoraCut chat, with your key.</b> You add the API key of your provider, then you write in the Agents panel."),
                                m_chatWay);
    chatText->setObjectName(QStringLiteral("koracutTerminalWay"));
    chatText->setWordWrap(true);
    chatLayout->addWidget(chatText, 1);
    auto *chatButton = new QPushButton(i18nc("@action:button open the AI connections page", "Connect a model"), m_chatWay);
    chatButton->setObjectName(QStringLiteral("koracutConsoleButton"));
    chatButton->setCursor(Qt::PointingHandCursor);
    connect(chatButton, &QPushButton::clicked, this, &KoraCutTerminalPanel::connectModelRequested);
    chatLayout->addWidget(chatButton);
    m_chatWay->hide();
    page->addWidget(m_chatWay);
    auto *intro = new QLabel(i18nc("@info second way to edit with an AI, in the terminal chooser",
                                   "<b>2. With your agent in the terminal.</b> A command line agent works here, with your own account, and edits in the KoraCut "
                                   "timeline while you watch. KoraCut writes its instructions in the project folder and connects it to the application. "
                                   "Nothing is installed without your click."),
                             m_chooser);
    intro->setObjectName(QStringLiteral("koracutTerminalWay"));
    intro->setWordWrap(true);
    page->addWidget(intro);

    m_list = new QListWidget(m_chooser);
    m_list->setObjectName(QStringLiteral("koracutTerminalAgents"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(true);
    m_list->setMinimumHeight(150);
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
    // Le message (projet à enregistrer, installation en cours ou ratée) juste au-dessus du bouton : dans un panneau
    // bas, il était en haut de la page, hors de vue de la personne qui cliquait (4 octobre 2026).
    auto *messageRow = new QHBoxLayout();
    messageRow->setContentsMargins(0, 0, 0, 0);
    m_message = new QLabel(details);
    m_message->setObjectName(QStringLiteral("koracutTerminalMessage"));
    m_message->setWordWrap(true);
    m_message->hide();
    messageRow->addWidget(m_message, 1);
    m_saveButton = new QPushButton(i18nc("@action:button", "Save the Project"), details);
    m_saveButton->setObjectName(QStringLiteral("koracutConsoleButton"));
    m_saveButton->setCursor(Qt::PointingHandCursor);
    m_saveButton->hide();
    connect(m_saveButton, &QPushButton::clicked, this, &KoraCutTerminalPanel::saveProjectRequested);
    messageRow->addWidget(m_saveButton);
    dl->addLayout(messageRow);
    auto *buttons = new QHBoxLayout();
    buttons->setContentsMargins(0, KoraCutMetrics::SpaceTight, 0, 0);
    buttons->setSpacing(KoraCutMetrics::SpaceGroup);
    m_launchButton = new QPushButton(details);
    m_launchButton->setObjectName(QStringLiteral("koracutConsolePrimaryButton"));
    m_launchButton->setCursor(Qt::PointingHandCursor);
    connect(m_launchButton, &QPushButton::clicked, this, &KoraCutTerminalPanel::launchFromButton);
    buttons->addWidget(m_launchButton);
    m_nodeButton = new QPushButton(i18nc("@action:button opens the official Node.js download page", "Node.js Website"), details);
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
    connect(systemButton, &QPushButton::clicked, this, [this]() {
        const QString id = selectedAgentId();
        if (!openInSystemTerminal(id) && !m_saveButton->isHidden()) {
            // Projet sans dossier : la fenêtre d'enregistrement s'ouvre tout de suite, et le terminal suit.
            m_pendingAgent = QStringLiteral("systeme:") + id;
            Q_EMIT saveProjectRequested();
        }
    });
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
                                 "#koracutTerminalMessage[etat=\"ok\"] { color: #4ADE80; }"
                                 "#koracutTerminalMessage[etat=\"erreur\"] { color: #FF7262; }"
                                 "#koracutTerminalView { background: #1E1E1E; }"));
    m_installTimer = new QTimer(this);
    m_installTimer->setInterval(1000);
    connect(m_installTimer, &QTimer::timeout, this, &KoraCutTerminalPanel::pollInstalls);
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
    m_message->setProperty("etat", QString());
    m_message->setStyleSheet(QString()); // relit les règles qui dépendent de « etat »
    if (!text.isEmpty()) {
        // Toujours sous les yeux : la page défile jusqu'au message et au bouton.
        QTimer::singleShot(0, m_message, [this]() {
            if (auto *scroll = qobject_cast<QScrollArea *>(m_stack->widget(0))) {
                scroll->ensureWidgetVisible(m_launchButton, 0, 24);
                scroll->ensureWidgetVisible(m_message, 0, 24);
            }
        });
    }
}

void KoraCutTerminalPanel::setChatWayAvailable(bool available)
{
    m_chatWay->setVisible(available);
}

QString KoraCutTerminalPanel::installStateFile(const QString &id) const
{
    return tempRoot() + QStringLiteral("/installation-%1.etat").arg(id);
}

void KoraCutTerminalPanel::pollInstalls()
{
    bool changed = false;
    const QStringList ids = m_installs.keys();
    for (const QString &id : ids) {
        QFile f(m_installs.value(id));
        const QString state = f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
        if (state.isEmpty() || state == QLatin1String("en_cours")) {
            continue;
        }
        m_installs.remove(id);
        m_installResult.insert(id, state);
        changed = true;
    }
    if (m_installs.isEmpty()) {
        m_installTimer->stop();
    }
    if (changed) {
        refreshChooser();
    }
}

void KoraCutTerminalPanel::launchFromButton()
{
    const QString id = selectedAgentId();
    if (id.isEmpty()) {
        return;
    }
    // Le clic se voit tout de suite : le bouton change de texte avant que quoi que ce soit ne démarre.
    m_launchButton->setEnabled(false);
    m_launchButton->setText(i18nc("@action:button the click was taken, the terminal is opening", "Opening the terminal…"));
    m_launchButton->repaint();
    const bool started = launchAgent(id, true);
    if (!started) {
        updateDetails();
        if (!m_pendingAgent.isEmpty()) {
            // Le projet n'a pas de dossier : la fenêtre d'enregistrement s'ouvre, l'agent part ensuite tout seul.
            Q_EMIT saveProjectRequested();
        }
    }
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
        if (m_installs.contains(a.id)) {
            state->setText(i18nc("@info agent state", "Installing…"));
            state->setStyleSheet(QStringLiteral("color: #0D99FF;"));
        } else if (path.isEmpty() && m_installResult.value(a.id).startsWith(QLatin1String("echec"))) {
            state->setText(i18nc("@info agent state", "Installation failed"));
            state->setStyleSheet(QStringLiteral("color: #FF7262;"));
        } else if (!path.isEmpty()) {
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
    // Le bouton de la barre garde son nom, « Monter avec un agent », et montre le logo de l'agent retenu.
    const KoraCutAgentsCli::Agent *last = KoraCutAgentsCli::byId(KoraCutAgentsCli::lastChoice());
    m_agentButton->setIcon(last ? KoraCutAgentsCli::icon(*last, 16) : QIcon::fromTheme(QStringLiteral("system-run")));
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
    // Node.js qui manque ne bloque plus (4 octobre 2026) : KoraCut l'installe d'abord, dans le terminal, avec l'accord
    // de la personne. Ce qui reste bloquant (Homebrew, WSL, Python) est dit en clair, le bouton du site à côté.
    bool needsNode = false;
    QStringList blocking;
    for (const QString &m : missing) {
        if (m.startsWith(QLatin1String("node")) && !nodeInstallBody().isEmpty()) {
            needsNode = true;
        } else {
            blocking << m;
        }
    }
    const bool installing = m_installs.contains(a->id);
    const QString result = m_installResult.value(a->id);
    if (installing) {
        m_detailCommand->setText(i18nc("@info the official install command, shown before it runs", "Official installation: %1", a->install.command));
        m_launchButton->setText(i18nc("@action:button %1 agent name", "Installing %1…", a->name));
        m_launchButton->setEnabled(false);
    } else if (!path.isEmpty()) {
        m_detailCommand->setText(i18nc("@info where the agent is installed", "Installed: %1", QDir::toNativeSeparators(path)));
        m_launchButton->setText(i18nc("@action:button %1 agent name", "Launch %1", a->name));
        m_launchButton->setEnabled(true);
    } else if (!a->install.command.isEmpty()) {
        m_detailCommand->setText(i18nc("@info the official install command, shown before it runs", "Official installation: %1", a->install.command));
        m_launchButton->setText(needsNode ? i18nc("@action:button %1 agent name", "Install Node.js, then %1", a->name)
                                          : (result.startsWith(QLatin1String("echec")) ? i18nc("@action:button %1 agent name", "Try Again to Install %1", a->name)
                                                                                       : i18nc("@action:button %1 agent name", "Install and Launch %1", a->name)));
        m_launchButton->setEnabled(blocking.isEmpty());
    } else {
        m_detailCommand->setText(i18nc("@info", "No official installation for this system."));
        m_launchButton->setText(i18nc("@action:button %1 agent name", "Launch %1", a->name));
        m_launchButton->setEnabled(false);
    }
    QString warning;
    if (needsNode) {
        warning += i18nc("@info", "This agent needs Node.js, which is not on this computer. KoraCut installs it first, in the terminal, after asking you "
                                  "(official nodejs.org build, in your home folder, no password).");
        warning += QLatin1Char(' ');
    }
    for (const QString &m : std::as_const(blocking)) {
        if (m.startsWith(QLatin1String("node"))) {
            const QString version = m.section(QStringLiteral(">="), 1);
            warning += i18nc("@info %1 minimum Node.js version", "Node.js %1 or later is needed first: install it from the official site, then come back.",
                             version.isEmpty() ? QStringLiteral("18") : version);
        } else if (m.startsWith(QLatin1String("python"))) {
            warning += i18nc("@info", "Python 3 is needed first.");
        } else if (m == QLatin1String("wsl")) {
            warning += i18nc("@info", "On Windows, this agent runs in WSL.");
        } else if (m == QLatin1String("brew")) {
            warning += i18nc("@info", "This agent installs with Homebrew, which is not on this computer. Install Homebrew (brew.sh) first, or pick another agent.");
        } else {
            warning += i18nc("@info %1 a tool name", "%1 is needed first.", m);
        }
        warning += QLatin1Char(' ');
    }
    m_detailWarning->setText(warning.trimmed());
    m_detailWarning->setVisible(!warning.isEmpty());
    m_nodeButton->setVisible(needsNode || blocking.join(QLatin1Char(' ')).contains(QLatin1String("node")));
    m_siteButton->setEnabled(!a->site.isEmpty() || !a->doc.isEmpty());
    // Le résultat de la dernière installation de cet agent, à côté du bouton.
    if (installing) {
        setMessage(i18nc("@info %1 agent name", "%1 is being installed in its terminal tab. Follow it there.", a->name), false);
    } else if (result == QLatin1String("ok") && !path.isEmpty()) {
        setMessage(i18nc("@info %1 agent name", "%1 is installed.", a->name), false);
        m_message->setProperty("etat", QStringLiteral("ok"));
        m_message->setStyleSheet(QString());
    } else if (result.startsWith(QLatin1String("echec"))) {
        const QString code = result.section(QLatin1Char(' '), 1);
        setMessage(code.isEmpty() ? i18nc("@info %1 agent name", "The installation of %1 failed. The reason is in its terminal tab.", a->name)
                                  : i18nc("@info %1 agent name, %2 short reason",
                                          "The installation of %1 failed (%2). The details are in its terminal tab.", a->name, code),
                   true);
        m_message->setProperty("etat", QStringLiteral("erreur"));
        m_message->setStyleSheet(QString());
    } else if (result == QLatin1String("annule")) {
        setMessage(i18nc("@info %1 agent name", "The installation of %1 was cancelled. Nothing was installed.", a->name), false);
    } else if (m_pendingAgent.isEmpty() && m_saveButton->isHidden()) {
        setMessage(QString(), false);
    }
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
        view->start(KoraCutAgentsCli::userShell(), interactiveShellArgs(link), dir, env);
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

void KoraCutTerminalPanel::writeGuards(const QString &dir)
{
#ifdef Q_OS_WIN
    Q_UNUSED(dir)
#else
    // Les gardes du terminal (4 octobre 2026) : l'agent lancé dans KoraCut monte dans KoraCut. `ffmpeg` et `melt`
    // refusent avec un message qui renvoie à la porte ; `ffprobe` (lecture seule) passe au vrai. Ce dossier est en
    // tête du PATH du terminal : c'est un garde-fou, pas une cage (un chemin complet le contourne).
    if (dir.isEmpty()) {
        return;
    }
    const auto write = [&dir](const QString &name, const QString &body) {
        const QString path = dir + QLatin1Char('/') + name;
        QFile file(path);
        if (file.exists() && QFileInfo(path).isSymLink()) {
            QFile::remove(path);
        }
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return;
        }
        file.write(body.toUtf8());
        file.close();
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    };
    const QString refuse = QStringLiteral("#!/bin/sh\n"
                                          "# Écrit par KoraCut : dans ce terminal, la vidéo se monte dans l'application, pas avec %1.\n"
                                          "cat >&2 <<'KORACUT'\n"
                                          "KoraCut : %1 est refusé dans ce terminal.\n"
                                          "Ici, on monte DANS KoraCut : chaque geste passe par les outils koracut (MCP) ou par koracut-ctl,\n"
                                          "et le résultat est la timeline du projet ouvert. Aucune vidéo ne se fabrique à côté.\n"
                                          "  koracut-ctl --aide                 toutes les commandes\n"
                                          "  koracut-ctl timeline etat          ce qu'il y a dans la timeline\n"
                                          "  koracut-ctl medias analyser …      lire un rush (transcription, silences, niveaux, plans)\n"
                                          "  koracut-ctl export lancer …        exporter, quand la personne le demande\n"
                                          "Si KoraCut ne sait pas faire ce qu'il faut, arrête-toi et dis ce qui manque.\n"
                                          "KORACUT\n"
                                          "exit 126\n");
    for (const QString &name : {QStringLiteral("ffmpeg"), QStringLiteral("melt"), QStringLiteral("melt-7"), QStringLiteral("ffplay")}) {
        write(name, refuse.arg(name));
    }
    // ffprobe ne fait que lire : le premier du PATH hors de ce dossier, sinon celui de l'application.
    const QString appProbe = QCoreApplication::applicationDirPath() + QStringLiteral("/ffprobe");
    write(QStringLiteral("ffprobe"), QStringLiteral("#!/bin/sh\n"
                                                    "# Écrit par KoraCut : ffprobe lit un fichier sans rien fabriquer, il reste permis.\n"
                                                    "ici=$(cd \"$(dirname \"$0\")\" && pwd)\n"
                                                    "PATH=$(printf '%s' \"$PATH\" | tr ':' '\\n' | grep -vx \"$ici\" | paste -sd: -)\n"
                                                    "if command -v ffprobe >/dev/null 2>&1; then exec ffprobe \"$@\"; fi\n"
                                                    "if [ -x %1 ]; then exec %1 \"$@\"; fi\n"
                                                    "echo 'KoraCut : ffprobe est introuvable. Lis le fichier par koracut-ctl medias info.' >&2\n"
                                                    "exit 127\n")
                                         .arg(KoraCutAgentsCli::shellQuote(appProbe)));
#endif
}

QString KoraCutTerminalPanel::ctlLinkDirectory()
{
    if (!m_linkDir.isEmpty() && QFileInfo(m_linkDir).isDir()) {
        linkHyperframes(m_linkDir);
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
    // Hors du dossier temporaire de cette session (21 octobre 2026, KC-008) : une fenêtre de terminal à part
    // reste ouverte après la fermeture de KoraCut, et doit retrouver koracut-ctl quand KoraCut revient.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/terminal-bin");
    QDir().mkpath(dir);
    const QString link = dir + QStringLiteral("/koracut-ctl");
    QFile::remove(link);
    if (!QFile::link(ctlDir + QStringLiteral("/koracut-ctl"), link)) {
        return QString();
    }
    m_linkDir = dir;
    writeGuards(m_linkDir);
#endif
    linkHyperframes(m_linkDir);
    return m_linkDir;
}

void KoraCutTerminalPanel::linkHyperframes(const QString &dir)
{
#ifdef Q_OS_WIN
    Q_UNUSED(dir)
#else
    // Le HyperFrames de l'application dans le PATH du terminal (3 octobre 2026) : la commande `hyperframes`, et une
    // garde `npx` qui renvoie `npx hyperframes …` vers lui au lieu de le retélécharger. Tout autre `npx` passe au vrai.
    const QString launcher = KoraCutMotionHost::launcher();
    if (dir.isEmpty() || launcher.isEmpty()) {
        return;
    }
    const QString link = dir + QStringLiteral("/hyperframes");
    if (QFileInfo(link).symLinkTarget() != launcher) {
        QFile::remove(link);
        QFile::link(launcher, link);
    }
    const QString guard = dir + QStringLiteral("/npx");
    if (QFile::exists(guard)) {
        return;
    }
    QFile file(guard);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    QTextStream out(&file);
    out << "#!/bin/sh\n";
    out << "# Écrit par KoraCut : « npx hyperframes … » prend le HyperFrames de l'application, sans rien télécharger.\n";
    out << "ici=$(cd \"$(dirname \"$0\")\" && pwd)\n";
    out << "n=0\n";
    out << "for a in \"$@\"; do\n";
    out << "  n=$((n+1))\n";
    out << "  case \"$a\" in\n";
    out << "    -y|--yes|-q|--quiet|--no-install) ;;\n";
    out << "    hyperframes|hyperframes@*) shift $n; exec \"$ici/hyperframes\" \"$@\" ;;\n";
    out << "    *) break ;;\n";
    out << "  esac\n";
    out << "done\n";
    out << "# Le vrai npx : le premier du PATH hors de ce dossier.\n";
    out << "PATH=$(printf '%s' \"$PATH\" | tr ':' '\\n' | grep -vx \"$ici\" | paste -sd: -)\n";
    out << "exec npx \"$@\"\n";
    out.flush();
    file.close();
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#endif
}

bool KoraCutTerminalPanel::prepareProject(QString &dir, KoraCutAgentInit::Context &ctx, QString &message)
{
    if (!m_porte) {
        message = i18nc("@info", "The command port is not available.");
        return false;
    }
    const KoraCutPorte::Reply info = m_porte->call(clientName(), QStringLiteral("projet.info"), {});
    if (!info.ok()) {
        // Lancé depuis une commande de la porte encore en cours : KoraCut est occupée, pas sans projet.
        message = info.error.value(QStringLiteral("code")).toString() == QLatin1String("occupe")
            ? i18nc("@info", "KoraCut is busy with another command. Try again in a moment.")
            : i18nc("@info", "No project is open yet.");
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
    // Le serveur MCP livré : « koracut-ctl mcp », à côté de l'application. Ni Node ni dossier de développement.
    KoraCutAgentInit::findMcp(KoraCutTerminal::ctlDirectory(), ctx.mcpCommand, ctx.mcpArgs);
    ctx.skills = KoraCutAgentInit::findSkills();
    ctx.hyperframes = KoraCutMotionHost::launcher();
    ctlLinkDirectory(); // refait le lien `hyperframes` si l'installation vient d'arriver
#ifndef Q_OS_WIN
    // Un lanceur du serveur MCP sans espace dans son chemin, pour les agents qui prennent le serveur en
    // une seule chaîne (Goose). Il vit le temps de la session, comme le lien vers koracut-ctl.
    if (ctx.hasMcp() && !ctlLinkDirectory().isEmpty()) {
        const QString wrapper = ctlLinkDirectory() + QStringLiteral("/koracut-mcp");
        QFile w(wrapper);
        if (w.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QTextStream out(&w);
            out << "#!/bin/sh\n";
            out << "export KORACUT_PORTE=" << KoraCutAgentsCli::shellQuote(ctx.porteFile) << "\n";
            out << "exec " << KoraCutAgentsCli::shellLine(QStringList{ctx.mcpCommand} + ctx.mcpArgs) << " \"$@\"\n";
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
    // Le shell de connexion et le .zshrc de la personne ont remis leurs dossiers en tête (path_helper, Homebrew) :
    // celui de KoraCut (koracut-ctl, les gardes ffmpeg et melt) repasse devant avant de lancer l'agent.
    if (const QString first = ctlLinkDirectory(); !first.isEmpty()) {
        out << "PATH=" << KoraCutAgentsCli::shellQuote(first) << ":\"$PATH\"; export PATH\n";
    }
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
    if (!view->start(KoraCutAgentsCli::userShell(), interactiveShellArgs(link), dir, env, &error)) {
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
    // Les compétences KoraCut, posées là où Claude Code les lit, avant de les annoncer dans les consignes.
    QString skillsLine;
    if (id == kClaudeId) {
        const QString bundled = KoraCutSkills::bundledRoot();
        if (!bundled.isEmpty()) {
            const KoraCutSkills::Result skills = KoraCutSkills::install(bundled, KoraCutSkills::userRoot());
            skillsLine = KoraCutSkills::summary(skills);
            m_lastSkills = skillsLine;
        }
    }
    if (!prepareProject(dir, ctx, message)) {
        // Le projet n'a pas encore de dossier : on le dit, on propose de l'enregistrer, et on reprend après.
        m_pendingAgent = m_saveButton->isHidden() ? QString() : id;
        showChooser();
        setMessage(message, true);
        return false;
    }
    m_pendingAgent.clear();
    setMessage(QString(), false);
    const QString exe = KoraCutAgentsCli::locate(*agent);
    bool needsNode = false;
    if (exe.isEmpty()) {
        if (!install) {
            showChooser();
            setMessage(i18nc("@info %1 agent name", "%1 is not installed on this computer yet.", agent->name), true);
            return false;
        }
        // Node.js qui manque s'installe d'abord, dans le même onglet ; un autre prérequis manquant bloque, et la fiche le dit.
        bool blocked = agent->install.command.isEmpty();
        for (const QString &m : KoraCutAgentsCli::missingRequirements(*agent)) {
            if (m.startsWith(QLatin1String("node")) && !nodeInstallBody().isEmpty()) {
                needsNode = true;
            } else {
                blocked = true;
            }
        }
        if (blocked) {
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
    if (!skillsLine.isEmpty()) {
        body += printfLine(skillsLine);
    }
    if (exe.isEmpty()) {
        const QString failed = i18nc("@info", "The installation did not finish. Read the messages above, then close this tab and try again.");
        const QString exeName = agent->executables.value(0);
        // L'état de l'installation, lu par la page de choix : en cours, fini, échec avec la raison, annulé.
        const QString stateFile = installStateFile(id);
        QDir().mkpath(tempRoot());
        {
            QFile sf(stateFile);
            if (sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                sf.write("en_cours\n");
            }
        }
        m_installs.insert(id, stateFile);
        m_installResult.remove(id);
        m_installTimer->start();
        // Un script livré avec KoraCut explique, demande l'accord dans le terminal, puis installe par la
        // voie officielle (Claude Code) ; sinon la commande officielle, déjà montrée avant le clic.
        const QString script = agent->install.script.isEmpty() ? QString() : KoraCutAgentInit::findData(agent->install.script);
        const bool simulated = id == kClaudeId && qEnvironmentVariableIntValue("KORACUT_CLAUDE_ABSENT") > 0;
#ifdef Q_OS_WIN
        if (!script.isEmpty()) {
            body += QStringLiteral("set \"KORACUT_LANGUE=%1\"\n").arg(scriptLanguage());
            if (simulated) {
                body += QStringLiteral("set \"KORACUT_CLAUDE_ABSENT=1\"\nset \"KORACUT_INSTALLER_SIMULER=1\"\n");
            }
            body += QStringLiteral("powershell -NoProfile -ExecutionPolicy Bypass -File %1\n").arg(KoraCutAgentsCli::shellQuote(QDir::toNativeSeparators(script)));
            // Refusé ou échoué : le script l'a dit, l'onglet garde un shell dans le dossier du projet.
            body += QStringLiteral("if errorlevel 1 goto :eof\n");
            body += QStringLiteral("set \"PATH=%USERPROFILE%\\.local\\bin;%PATH%\"\n");
        } else {
            body += printfLine(i18nc("@info %1 agent name", "KoraCut installs %1 with its official command:", agent->name), QStringLiteral("1"));
            body += printfLine(QStringLiteral("  ") + agent->install.command, QStringLiteral("36"));
            // Les commandes d'installation de Windows sont souvent en PowerShell (« irm … | iex »).
            if (agent->install.shell == QLatin1String("powershell")) {
                body += QStringLiteral("powershell -NoProfile -ExecutionPolicy Bypass -Command %1\n").arg(KoraCutAgentsCli::shellQuote(agent->install.command));
            } else {
                body += QStringLiteral("call ") + agent->install.command + QLatin1Char('\n');
            }
        }
        // Le PATH de ce shell ne voit pas encore ce qui vient d'être installé : on relit celui de l'utilisateur.
        body += QStringLiteral("for /f \"usebackq tokens=2,*\" %%A in (`reg query HKCU\\Environment /v Path 2^>nul`) do set \"PATH=%%B;%PATH%\"\n");
        body += QStringLiteral("where %1 >nul 2>nul\nif errorlevel 1 (\n").arg(exeName);
        body += QStringLiteral("  ") + printfLine(failed, QStringLiteral("31"));
        body += QStringLiteral("  goto :eof\n)\n");
#else
        const QString backToShell = QStringLiteral("exec %1 -l\n").arg(KoraCutAgentsCli::shellQuote(KoraCutAgentsCli::userShell()));
        const QString state = KoraCutAgentsCli::shellQuote(stateFile);
        const QString simulate = qEnvironmentVariableIntValue("KORACUT_INSTALLER_SIMULER") > 0 ? QStringLiteral("KORACUT_INSTALLER_SIMULER=1 ") : QString();
        if (needsNode) {
            // Node.js d'abord, par le script livré : il explique, demande l'accord, installe dans le dossier personnel.
            body += nodeInstallBody();
            body += QStringLiteral("koracut_code=$?\nif [ $koracut_code -ne 0 ]; then\n  if [ $koracut_code -eq 2 ]; then echo annule > %1; else echo \"echec Node.js, code $koracut_code\" > %1; fi\n  ").arg(state) +
                    backToShell + QStringLiteral("fi\n");
            body += QStringLiteral("PATH=\"$HOME/.local/share/koracut/node/bin:$HOME/.local/bin:$PATH\"; export PATH\nhash -r 2>/dev/null\n");
        }
        if (!script.isEmpty()) {
            QString line = QStringLiteral("KORACUT_LANGUE=%1 ").arg(scriptLanguage()) + simulate;
            if (simulated) {
                line += QStringLiteral("KORACUT_CLAUDE_ABSENT=1 KORACUT_INSTALLER_SIMULER=1 ");
            }
            body += line + QStringLiteral("/bin/sh ") + KoraCutAgentsCli::shellQuote(script) + QLatin1Char('\n');
            // Refusé ou échoué : le script l'a dit, l'onglet garde un shell dans le dossier du projet.
            body += QStringLiteral("koracut_code=$?\nif [ $koracut_code -ne 0 ]; then\n  if [ $koracut_code -eq 2 ]; then echo annule > %1; else echo \"echec code $koracut_code\" > %1; fi\n  ").arg(state) +
                    backToShell + QStringLiteral("fi\n");
        } else {
            body += printfLine(i18nc("@info %1 agent name", "KoraCut installs %1 with its official command:", agent->name), QStringLiteral("1"));
            body += printfLine(QStringLiteral("  ") + agent->install.command, QStringLiteral("36"));
            if (simulate.isEmpty()) {
                body += agent->install.command + QLatin1Char('\n');
            } else {
                // Essai : la commande est annoncée, pas lancée.
                body += printfLine(QStringLiteral("(essai : la commande n'est pas lancée)"));
                body += QStringLiteral("false\n");
            }
            body += QStringLiteral("koracut_code=$?\n");
        }
        // Un installeur qui pose sa commande dans ~/.local/bin ne met pas le PATH de ce shell à jour, et
        // le dossier n'existait peut-être pas quand KoraCut a préparé le PATH du terminal.
        body += QStringLiteral("PATH=\"$HOME/.local/bin:$PATH\"; export PATH\nhash -r 2>/dev/null\n");
        // En essai (KORACUT_INSTALLER_SIMULER), rien n'a été installé : on passe toujours par la branche de l'échec.
        body += simulate.isEmpty() ? QStringLiteral("if ! command -v %1 >/dev/null 2>&1; then\n").arg(KoraCutAgentsCli::shellQuote(exeName)) : QStringLiteral("if true; then\n");
        body += QStringLiteral("  echo \"echec code ${koracut_code:-0}\" > %1\n").arg(state);
        body += QStringLiteral("  ") + printfLine(failed, QStringLiteral("31"));
        body += QStringLiteral("  ") + backToShell + QStringLiteral("fi\n");
        body += QStringLiteral("echo ok > %1\n").arg(state);
#endif
    }
    bool resumed = false;
    QStringList command = agentCommand(*agent, ctx, dir, &resumed);
    if (!exe.isEmpty() && !command.isEmpty()) {
        command[0] = exe;
    }
    if (resumed) {
        body += printfLine(i18nc("@info line in the terminal", "KoraCut: the last conversation in this folder continues."));
    }
#ifdef Q_OS_WIN
    // « call » : un agent installé par npm est un .cmd, qui sans lui ne rendrait pas la main au script.
    body += QStringLiteral("call ");
#endif
    body += KoraCutAgentsCli::shellLine(command) + QLatin1Char('\n');
    if (id == kClaudeId) {
        // Claude Code s'est arrêté : l'onglet garde un shell, où « claude » tapé à la main n'a pas KoraCut branché.
        body += printfLine(i18nc("@info line in the terminal after Claude Code exits; keep the button name as translated",
                                 "Claude Code has stopped. To start it again connected to KoraCut, click “Edit with an Agent”."));
    }

    auto *view = addTerminal(agent->name, KoraCutAgentsCli::icon(*agent, 16));
    if (id == kClaudeId) {
        m_claudeView = view;
    }
    const bool ok = startScript(view, dir, body);
    refreshChooser();
    if (m_tabs->count() > 0) {
        m_stack->setCurrentWidget(view);
        view->setFocus(Qt::OtherFocusReason);
    }
    return ok;
}

bool KoraCutTerminalPanel::editWithClaudeCode()
{
    // Une session Claude Code déjà ouverte ici : on la ramène devant, on n'en lance pas une seconde.
    if (m_claudeView && m_claudeView->isRunning()) {
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (m_tabs->tabData(i).value<QObject *>() == m_claudeView) {
                m_tabs->setCurrentIndex(i);
                m_stack->setCurrentWidget(m_claudeView);
                m_claudeView->setFocus(Qt::OtherFocusReason);
                return true;
            }
        }
    }
    if (!KoraCutAgentsCli::byId(kClaudeId)) {
        return false;
    }
    // Avec « installer » : un Claude Code absent est proposé par le script, accord demandé dans le terminal.
    return launchAgent(kClaudeId, true);
}

bool KoraCutTerminalPanel::editWithAgent()
{
    const QString last = KoraCutAgentsCli::lastChoice();
    const KoraCutAgentsCli::Agent *agent = KoraCutAgentsCli::byId(last);
    if (!agent || KoraCutAgentsCli::locate(*agent).isEmpty()) {
        // Pas encore d'agent choisi, ou pas installé : la page de choix, avec les deux voies.
        showChooser();
        return false;
    }
    if (last == kClaudeId) {
        return editWithClaudeCode();
    }
    // L'onglet de cet agent tourne déjà : on le ramène devant, on n'en lance pas un second.
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *view = qobject_cast<KoraCutTerminalView *>(m_tabs->tabData(i).value<QObject *>());
        if (view && view->isRunning() && m_tabs->tabText(i) == agent->name) {
            m_tabs->setCurrentIndex(i);
            m_stack->setCurrentWidget(view);
            view->setFocus(Qt::OtherFocusReason);
            return true;
        }
    }
    return launchAgent(last, false);
}

bool KoraCutTerminalPanel::editInSeparateWindow()
{
    QString id = KoraCutAgentsCli::lastChoice();
    if (id.isEmpty()) {
        id = kClaudeId;
    }
    const KoraCutAgentsCli::Agent *agent = KoraCutAgentsCli::byId(id);
    if (!agent || KoraCutAgentsCli::locate(*agent).isEmpty()) {
        // À installer : la page de choix du panneau s'en charge, dans le terminal intégré.
        return false;
    }
    KoraCutAgentsCli::setLastChoice(id);
    if (openInSystemTerminal(id)) {
        return true;
    }
    if (!m_saveButton->isHidden()) {
        // Le projet n'a pas encore de dossier : l'agent doit savoir où il travaille. On l'enregistre d'abord,
        // la fenêtre suit (resumePending).
        m_pendingAgent = QStringLiteral("systeme:") + id;
        Q_EMIT saveProjectRequested();
        return true;
    }
    return false;
}

QString KoraCutTerminalPanel::nodeInstallBody() const
{
#ifdef Q_OS_WIN
    return QString();
#else
    const QString script = KoraCutAgentInit::findData(QStringLiteral("installer-node.sh"));
    if (script.isEmpty()) {
        return QString();
    }
    QString line = QStringLiteral("KORACUT_LANGUE=%1 ").arg(scriptLanguage());
    if (qEnvironmentVariableIntValue("KORACUT_INSTALLER_SIMULER") > 0) {
        // En essai, Node.js est tenu pour absent même si le shell de la machine en trouve un.
        line += QStringLiteral("KORACUT_INSTALLER_SIMULER=1 KORACUT_NODE_ABSENT=1 ");
    }
    return line + QStringLiteral("/bin/sh ") + KoraCutAgentsCli::shellQuote(script) + QLatin1Char('\n');
#endif
}

void KoraCutTerminalPanel::resumePending()
{
    if (m_pendingAgent.isEmpty()) {
        return;
    }
    const QString id = m_pendingAgent;
    m_pendingAgent.clear();
    if (id.startsWith(QLatin1String("systeme:"))) {
        openInSystemTerminal(id.mid(8));
        return;
    }
    launchAgent(id, true);
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
    m_pendingAgent.clear();
    QString command;
    if (agent && !KoraCutAgentsCli::locate(*agent).isEmpty()) {
        if (id == kClaudeId) {
            const QString bundled = KoraCutSkills::bundledRoot();
            if (!bundled.isEmpty()) {
                KoraCutSkills::install(bundled, KoraCutSkills::userRoot());
            }
        }
        KoraCutAgentInit::writeForAgent(dir, *agent, ctx);
        bool resumed = false;
        const QStringList args = agentCommand(*agent, ctx, dir, &resumed);
        command = printfLine(i18nc("@info line in the system terminal", "This window stays open even if KoraCut closes. Open KoraCut again and carry on here."));
        if (resumed) {
            command += printfLine(i18nc("@info line in the terminal", "KoraCut: the last conversation in this folder continues."));
        }
        command += KoraCutAgentsCli::shellLine(args);
    } else if (agent) {
        // Pas installé : le terminal s'ouvre quand même, et dit où l'installer.
        command = printfLine(i18nc("@info %1 agent name", "%1 is not installed yet. Install it from the KoraCut terminal panel (Edit with an Agent), then come back.", agent->name),
                             QStringLiteral("33"))
                      .trimmed();
    }
    // Le dossier de KoraCut en tête du PATH (koracut-ctl, les gardes), pas le dossier de l'application et ses outils.
    const QString link = ctlLinkDirectory();
    QString error;
    if (!KoraCutTerminal::open(dir, ctx.porteFile, link.isEmpty() ? KoraCutTerminal::ctlDirectory() : link, command, error)) {
        showChooser();
        setMessage(i18nc("@info", "No terminal could be opened (%1).", error), true);
        return false;
    }
    setMessage(i18nc("@info", "The system terminal is open on this project."), false);
    return true;
}

KoraCutTerminalView *KoraCutTerminalPanel::porteView(const QString &tab) const
{
    const QString wanted = tab.trimmed();
    if (wanted.isEmpty() || wanted.compare(QLatin1String("claude"), Qt::CaseInsensitive) == 0) {
        if (m_claudeView && m_claudeView->isRunning()) {
            return m_claudeView;
        }
        return wanted.isEmpty() ? currentView() : nullptr;
    }
    bool isNumber = false;
    const int rank = wanted.toInt(&isNumber);
    for (int i = 0; i < m_tabs->count(); ++i) {
        if ((isNumber && rank == i + 1) || (!isNumber && m_tabs->tabText(i).compare(wanted, Qt::CaseInsensitive) == 0)) {
            return qobject_cast<KoraCutTerminalView *>(m_tabs->tabData(i).value<QObject *>());
        }
    }
    return nullptr;
}

QJsonObject KoraCutTerminalPanel::porteState(const QString &tab, int history) const
{
    QJsonObject state;
    QJsonArray tabs;
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *view = qobject_cast<KoraCutTerminalView *>(m_tabs->tabData(i).value<QObject *>());
        tabs.append(QJsonObject{{QStringLiteral("rang"), i + 1},
                                {QStringLiteral("titre"), m_tabs->tabText(i)},
                                {QStringLiteral("devant"), i == m_tabs->currentIndex()},
                                {QStringLiteral("processus"), view && view->isRunning()},
                                {QStringLiteral("claude_code"), view && view == m_claudeView}});
    }
    state.insert(QStringLiteral("onglets"), tabs);
    state.insert(QStringLiteral("page"), m_stack->currentIndex() == 0 ? QStringLiteral("choix") : QStringLiteral("terminal"));
    state.insert(QStringLiteral("message"), m_message->isHidden() ? QString() : m_message->text());
    state.insert(QStringLiteral("en_attente"), m_pendingAgent);
    state.insert(QStringLiteral("competences"), m_lastSkills);
    const KoraCutAgentsCli::Agent *claude = KoraCutAgentsCli::byId(kClaudeId);
    state.insert(QStringLiteral("claude_code_installe"), claude && !KoraCutAgentsCli::locate(*claude).isEmpty());
    state.insert(QStringLiteral("claude_code_ouvert"), m_claudeView && m_claudeView->isRunning());
    if (KoraCutTerminalView *view = porteView(tab)) {
        QStringList screen = view->screenLines();
        while (!screen.isEmpty() && screen.constLast().trimmed().isEmpty()) {
            screen.removeLast();
        }
        state.insert(QStringLiteral("ecran"), QJsonArray::fromStringList(screen));
        state.insert(QStringLiteral("colonnes"), view->columns());
        state.insert(QStringLiteral("lignes"), view->rows());
        state.insert(QStringLiteral("processus"), view->isRunning());
        // Indicatif : les agents en ligne de commande écrivent comment les interrompre tant qu'ils travaillent.
        const QString joined = screen.join(QLatin1Char('\n')).toLower();
        state.insert(QStringLiteral("agent_travaille"), joined.contains(QLatin1String("esc to interrupt")) || joined.contains(QLatin1String("to interrupt)")));
        // Indicatif aussi : un choix numéroté attend une réponse (accord demandé, question de l'agent).
        bool question = joined.contains(QLatin1String("esc to cancel"));
        for (const QString &line : std::as_const(screen)) {
            const QString t = line.trimmed();
            if (t.startsWith(QStringLiteral("❯ 1.")) || t.startsWith(QStringLiteral("> 1."))) {
                question = true;
            }
        }
        state.insert(QStringLiteral("question_ouverte"), question);
        if (history > 0) {
            QStringList all = view->allText().split(QLatin1Char('\n'));
            while (!all.isEmpty() && all.constLast().trimmed().isEmpty()) {
                all.removeLast();
            }
            state.insert(QStringLiteral("historique"), QJsonArray::fromStringList(all.mid(std::max<qsizetype>(0, all.size() - history))));
        }
    }
    return state;
}

bool KoraCutTerminalPanel::porteWrite(const QString &tab, const QString &text, bool enter, QString &error)
{
    KoraCutTerminalView *view = porteView(tab);
    if (!view || !view->isRunning()) {
        error = QStringLiteral("aucun_onglet");
        return false;
    }
    QString body = text;
    body.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    // Un texte de plusieurs lignes part comme un collage : sans ça, chaque saut de ligne validerait la saisie.
    if (body.contains(QLatin1Char('\n'))) {
        view->sendText(QStringLiteral("\x1b[200~") + body + QStringLiteral("\x1b[201~"));
    } else {
        view->sendText(body);
    }
    if (enter) {
        // Entrée à part, un peu après : collée au texte, un agent en plein écran la prend pour un saut de ligne.
        QTimer::singleShot(300, view, [view]() { view->sendText(QStringLiteral("\r")); });
    }
    return true;
}

bool KoraCutTerminalPanel::porteKey(const QString &tab, const QString &key, QString &error)
{
    KoraCutTerminalView *view = porteView(tab);
    if (!view || !view->isRunning()) {
        error = QStringLiteral("aucun_onglet");
        return false;
    }
    static const QHash<QString, QString> keys{{QStringLiteral("entree"), QStringLiteral("\r")},      {QStringLiteral("echap"), QStringLiteral("\x1b")},
                                              {QStringLiteral("tab"), QStringLiteral("\t")},         {QStringLiteral("maj-tab"), QStringLiteral("\x1b[Z")},
                                              {QStringLiteral("haut"), QStringLiteral("\x1b[A")},    {QStringLiteral("bas"), QStringLiteral("\x1b[B")},
                                              {QStringLiteral("droite"), QStringLiteral("\x1b[C")},  {QStringLiteral("gauche"), QStringLiteral("\x1b[D")},
                                              {QStringLiteral("retour"), QStringLiteral("\x7f")},    {QStringLiteral("ctrl-c"), QStringLiteral("\x03")}};
    const auto it = keys.constFind(key.trimmed().toLower());
    if (it == keys.constEnd()) {
        error = QStringLiteral("touche_inconnue");
        return false;
    }
    view->sendText(it.value());
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
    } else if (verb == QLatin1String("monter")) {
        return QStringLiteral("monter %1").arg(editWithAgent() ? QStringLiteral("ok") : QStringLiteral("choix"));
    } else if (verb == QLatin1String("bouton")) {
        // Le bouton principal de la fiche, comme un clic, sans la fenêtre d'enregistrement.
        if (!arg.isEmpty()) {
            KoraCutAgentsCli::setLastChoice(arg);
            m_list->setCurrentRow(-1);
            showChooser();
        }
        const QString before = m_launchButton->text();
        const bool enabled = m_launchButton->isEnabled();
        const bool ok = enabled && launchAgent(selectedAgentId(), true);
        return QStringLiteral("bouton « %1 » %2 : %3 ; message : %4").arg(before, enabled ? QStringLiteral("actif") : QStringLiteral("grisé"), ok ? QStringLiteral("lancé") : QStringLiteral("pas lancé"),
                                                                    m_message->isHidden() ? m_detailWarning->text() : m_message->text());
    } else if (verb == QLatin1String("claude")) {
        return QStringLiteral("claude %1 %2").arg(editWithClaudeCode() ? QStringLiteral("ok") : QStringLiteral("echec"), m_message->text());
    } else if (verb == QLatin1String("reprendre")) {
        resumePending();
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
    state.insert(QStringLiteral("message"), m_message->isHidden() ? QString() : m_message->text());
    state.insert(QStringLiteral("en_attente"), m_pendingAgent);
    state.insert(QStringLiteral("competences"), m_lastSkills);
    pollInstalls();
    state.insert(QStringLiteral("bouton"), m_launchButton->text());
    state.insert(QStringLiteral("bouton_actif"), m_launchButton->isEnabled());
    state.insert(QStringLiteral("avertissement"), m_detailWarning->isHidden() ? QString() : m_detailWarning->text());
    state.insert(QStringLiteral("installations_en_cours"), QJsonArray::fromStringList(m_installs.keys()));
    QJsonObject results;
    for (auto it = m_installResult.constBegin(); it != m_installResult.constEnd(); ++it) {
        results.insert(it.key(), it.value());
    }
    state.insert(QStringLiteral("installations"), results);
    if (KoraCutTerminalView *view = currentView()) {
        state.insert(QStringLiteral("ecran"), QJsonArray::fromStringList(view->screenLines()));
    }
    return QString::fromUtf8(QJsonDocument(state).toJson(QJsonDocument::Compact));
}
