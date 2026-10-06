/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutagentpanel.h"
#include "koracutfonts.h"
#include "koracut/ai/koracutaibridge.h"
#include "koracut/ai/koracutaiconnections.h"
#include "koracutagentclient.h"
#include "koracut/ai/koracutaiconnectionspage.h"
#include "koracut/koracutmetrics.h"

#include <KLocalizedString>
#include <functional>

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include "koracut/koracutthemes.h"

#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------------------------
// Le champ de saisie

KoraCutAgentInput::KoraCutAgentInput(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("koracutAgentInput"));
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(this, &QPlainTextEdit::textChanged, this, &KoraCutAgentInput::adjustHeight);
    adjustHeight();
}

QSize KoraCutAgentInput::sizeHint() const
{
    return QSize(200, height());
}

void KoraCutAgentInput::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && !(event->modifiers() & Qt::ShiftModifier)) {
        Q_EMIT submitted();
        event->accept();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void KoraCutAgentInput::resizeEvent(QResizeEvent *event)
{
    QPlainTextEdit::resizeEvent(event);
    if (event->oldSize().width() != event->size().width()) {
        adjustHeight();
    }
}

void KoraCutAgentInput::adjustHeight()
{
    // De une à six lignes, puis la barre de défilement.
    const int lineHeight = fontMetrics().lineSpacing();
    int lines = qBound(1, int(document()->size().height()), 6);
    if (document()->isEmpty() && !placeholderText().isEmpty() && viewport()->width() > 20) {
        // 25 septembre 2026 : le texte d'attente (« Aucun modèle connecté. Clique sur… ») tient
        // souvent sur deux lignes ; le champ n'en montrait qu'une, rognée par le bas.
        const int textWidth = viewport()->width() - qRound(2 * document()->documentMargin());
        const QRect r = fontMetrics().boundingRect(QRect(0, 0, qMax(textWidth, 20), 10000), Qt::TextWordWrap, placeholderText());
        lines = qBound(lines, (r.height() + lineHeight - 1) / lineHeight, 3);
    }
    // Le cadre (1 px), le rembourrage de la feuille de style (6 px en haut et en bas) et la marge du
    // document : l'ancien « + 14 » en oubliait la moitié, et le bas des lettres était coupé.
    const int extra = 2 * frameWidth() + 12 + qRound(2 * document()->documentMargin());
    const int wanted = lines * lineHeight + extra;
    if (height() != wanted) {
        setFixedHeight(wanted);
    }
}

// ---------------------------------------------------------------------------------------------
// Le panneau

namespace {

QString shortValue(const QJsonValue &value)
{
    if (value.isString()) {
        return value.toString();
    }
    if (value.isDouble()) {
        return QString::number(value.toDouble());
    }
    if (value.isBool()) {
        return value.toBool() ? QStringLiteral("oui") : QStringLiteral("non");
    }
    if (value.isObject()) {
        const QJsonObject o = value.toObject();
        if (o.contains(QStringLiteral("tc"))) {
            return o.value(QStringLiteral("tc")).toString();
        }
    }
    if (value.isArray()) {
        QStringList parts;
        for (const QJsonValue &v : value.toArray()) {
            parts << shortValue(v);
        }
        return parts.join(QLatin1Char(' '));
    }
    return QString();
}

} // namespace

KoraCutAgentPanel::KoraCutAgentPanel(KoraCutAgentSession *session, QWidget *parent)
    : QWidget(parent)
    , m_session(session)
{
    setObjectName(QStringLiteral("koracutAgentPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setMinimumWidth(240);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceZone);
    root->setSpacing(KoraCutMetrics::SpaceGroup);

    // L'état en haut, et « Annuler ce tour » tant qu'un tour est ouvert.
    auto *header = new QHBoxLayout();
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(KoraCutMetrics::SpaceGroup);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("koracutAgentStatus"));
    m_status->setTextFormat(Qt::RichText);
    header->addWidget(m_status, 1);
    m_cancelTurn = new QPushButton(i18nc("@action:button undo everything the agent did in this turn", "Cancel this turn"), this);
    m_cancelTurn->setObjectName(QStringLiteral("koracutAgentCancelButton"));
    m_cancelTurn->setCursor(Qt::PointingHandCursor);
    m_cancelTurn->hide();
    connect(m_cancelTurn, &QPushButton::clicked, m_session, &KoraCutAgentSession::cancelTurn);
    header->addWidget(m_cancelTurn);
    // Connexions IA (25 septembre 2026) : brancher un modèle avec sa propre clé, dans les Préférences.
    m_connectModel = new QPushButton(i18nc("@action:button open the AI connections page", "Connect a model"), this);
    m_connectModel->setObjectName(QStringLiteral("koracutAgentButton"));
    m_connectModel->setCursor(Qt::PointingHandCursor);
    m_connectModel->setToolTip(i18nc("@info:tooltip", "Anthropic, OpenAI, Gemini, OpenRouter, Mistral, Groq, local models… with your own key"));
    connect(m_connectModel, &QPushButton::clicked, this, []() { KoraCutAiConnectionsPage::openInPreferences(); });
    header->addWidget(m_connectModel);
    root->addLayout(header);

    // L'autre façon de monter avec une IA (document 18) : un agent en ligne de commande dans le terminal de
    // KoraCut, avec le compte de la personne. « Un agent », pas seulement Claude Code (4 octobre 2026).
    auto *claudeRow = new QHBoxLayout();
    claudeRow->setContentsMargins(0, 0, 0, 0);
    auto *claude = new QPushButton(i18nc("@action:button opens the terminal with the command line agent of your choice", "Edit with an Agent"), this);
    claude->setObjectName(QStringLiteral("koracutAgentButton"));
    claude->setCursor(Qt::PointingHandCursor);
    claude->setToolTip(i18nc("@info:tooltip",
                             "Opens the terminal with your own agent (Claude Code, Codex, Gemini, OpenCode and others) on this project, connected to KoraCut. "
                             "If your agent is missing, KoraCut offers to install it."));
    connect(claude, &QPushButton::clicked, this, &KoraCutAgentPanel::claudeCodeRequested);
    claudeRow->addWidget(claude);
    claudeRow->addStretch(1);
    root->addLayout(claudeRow);

    // La conversation.
    m_scroll = new QScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("koracutAgentScroll"));
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_thread = new QWidget(m_scroll);
    m_thread->setObjectName(QStringLiteral("koracutAgentThread"));
    m_threadLayout = new QVBoxLayout(m_thread);
    m_threadLayout->setContentsMargins(0, 0, KoraCutMetrics::SpaceTight, 0);
    m_threadLayout->setSpacing(KoraCutMetrics::SpaceGroup);
    m_threadLayout->addStretch(1);
    m_scroll->setWidget(m_thread);
    root->addWidget(m_scroll, 1);

    // La saisie : Entrée envoie, Maj+Entrée saute une ligne.
    auto *inputRow = new QHBoxLayout();
    inputRow->setContentsMargins(0, 0, 0, 0);
    inputRow->setSpacing(KoraCutMetrics::SpaceGroup);
    m_input = new KoraCutAgentInput(this);
    // Jamais le focus tout seul (au lancement, par Tab) : sinon KDDockWidgets ramène l'onglet
    // Agents devant les autres, et la personne voit « Aucun service » sans l'avoir demandé.
    m_input->setFocusPolicy(Qt::ClickFocus);
    connect(m_input, &KoraCutAgentInput::submitted, this, &KoraCutAgentPanel::submit);
    inputRow->addWidget(m_input, 1);
    m_send = new QPushButton(i18nc("@action:button", "Send"), this);
    m_send->setObjectName(QStringLiteral("koracutAgentPrimaryButton"));
    m_send->setCursor(Qt::PointingHandCursor);
    connect(m_send, &QPushButton::clicked, this, &KoraCutAgentPanel::submit);
    inputRow->addWidget(m_send, 0, Qt::AlignBottom);
    root->addLayout(inputRow);

    // Ce que la session raconte.
    connect(m_session, &KoraCutAgentSession::linkStateChanged, this, [this](KoraCutAgents::LinkState state, const QString &message) {
        m_linkState = state;
        m_linkMessage = message;
        refreshHeader();
    });
    connect(m_session, &KoraCutAgentSession::serviceStatus, this, [this](const QString &status, const QString &message) {
        m_serviceStatus = status;
        if (!message.isEmpty()) {
            addNote(message);
        }
        refreshHeader();
    });
    connect(m_session, &KoraCutAgentSession::personSaid, this, [this](const QString &text) { addBubble(QString(), text, true); });
    connect(m_session, &KoraCutAgentSession::agentSaid, this, [this](const QString &agent, const QString &text) {
        m_workingAgent = agent;
        addBubble(KoraCutAgentSession::agentDisplayName(agent), text, false);
        refreshHeader();
    });
    connect(m_session, &KoraCutAgentSession::toolCalled, this, [this](const QString &agent, const QString &tool, const QJsonObject &input, const QJsonObject &result) {
        m_workingAgent = agent;
        addToolLine(tool, input, result);
        refreshHeader();
    });
    connect(m_session, &KoraCutAgentSession::confirmationRequested, this,
            [this](const QString &callId, const QString &agent, const QString &tool, const QJsonObject &input) {
                QStringList details;
                for (auto it = input.constBegin(); it != input.constEnd(); ++it) {
                    details << QStringLiteral("%1 %2").arg(it.key(), shortValue(it.value()));
                }
                QWidget *card = addCard(i18nc("@title card asking the person to allow a tool, %1 agent name", "%1 asks your permission", KoraCutAgentSession::agentDisplayName(agent)),
                                        QStringLiteral("%1 · %2").arg(tool, details.join(QStringLiteral(" · "))));
                auto *buttons = new QHBoxLayout();
                buttons->setSpacing(KoraCutMetrics::SpaceGroup);
                auto *yes = new QPushButton(i18nc("@action:button allow the tool", "Allow"), card);
                yes->setObjectName(QStringLiteral("koracutAgentPrimaryButton"));
                auto *no = new QPushButton(i18nc("@action:button refuse the tool", "Refuse"), card);
                no->setObjectName(QStringLiteral("koracutAgentButton"));
                buttons->addWidget(yes);
                buttons->addWidget(no);
                buttons->addStretch(1);
                static_cast<QVBoxLayout *>(card->layout())->addLayout(buttons);
                m_cards.insert(callId, card);
                m_pendingConfirmations << callId;
                auto answer = [this, callId, yes, no](bool accepted) {
                    yes->setEnabled(false);
                    no->setEnabled(false);
                    (accepted ? yes : no)->setProperty("chosen", true);
                    m_pendingConfirmations.removeAll(callId);
                    m_session->answerConfirmation(callId, accepted);
                };
                connect(yes, &QPushButton::clicked, this, [answer]() { answer(true); });
                connect(no, &QPushButton::clicked, this, [answer]() { answer(false); });
                scrollToBottom();
            });
    connect(m_session, &KoraCutAgentSession::questionAsked, this,
            [this](const QString &questionId, const QString &agent, const QString &text, const QStringList &choices) {
                QWidget *card = addCard(KoraCutAgentSession::agentDisplayName(agent), text);
                if (!choices.isEmpty()) {
                    auto *buttons = new QHBoxLayout();
                    buttons->setSpacing(KoraCutMetrics::SpaceGroup);
                    for (const QString &choice : choices) {
                        auto *b = new QPushButton(choice, card);
                        b->setObjectName(QStringLiteral("koracutAgentButton"));
                        connect(b, &QPushButton::clicked, this, [this, questionId, choice, card]() {
                            for (QPushButton *other : card->findChildren<QPushButton *>()) {
                                other->setEnabled(false);
                            }
                            setInputMode(InputMode::Request);
                            m_session->answerQuestion(questionId, choice);
                        });
                        buttons->addWidget(b);
                    }
                    buttons->addStretch(1);
                    static_cast<QVBoxLayout *>(card->layout())->addLayout(buttons);
                }
                setInputMode(InputMode::AnswerQuestion, questionId);
                scrollToBottom();
            });
    connect(m_session, &KoraCutAgentSession::planProposed, this, [this](const QString &planId, const QString &agent, const QJsonObject &plan) {
        QStringList lines;
        int n = 0;
        for (const QJsonValue &v : plan.value(QStringLiteral("etapes")).toArray()) {
            const QJsonObject step = v.toObject();
            lines << QStringLiteral("%1. %2").arg(++n).arg(step.value(QStringLiteral("action")).toString());
        }
        const QJsonArray hypotheses = plan.value(QStringLiteral("hypotheses")).toArray();
        if (!hypotheses.isEmpty()) {
            lines << QString();
            for (const QJsonValue &h : hypotheses) {
                lines << QStringLiteral("· %1").arg(h.toString());
            }
        }
        QWidget *card = addCard(i18nc("@title card showing the plan of an agent, %1 agent name", "Plan of %1", KoraCutAgentSession::agentDisplayName(agent)),
                                plan.value(QStringLiteral("resume")).toString() + QStringLiteral("\n\n") + lines.join(QLatin1Char('\n')));
        auto *buttons = new QHBoxLayout();
        buttons->setSpacing(KoraCutMetrics::SpaceGroup);
        auto *apply = new QPushButton(i18nc("@action:button accept the plan", "Apply"), card);
        apply->setObjectName(QStringLiteral("koracutAgentPrimaryButton"));
        auto *modify = new QPushButton(i18nc("@action:button ask changes to the plan", "Modify"), card);
        modify->setObjectName(QStringLiteral("koracutAgentButton"));
        auto *cancel = new QPushButton(i18nc("@action:button refuse the plan", "Cancel"), card);
        cancel->setObjectName(QStringLiteral("koracutAgentButton"));
        buttons->addWidget(apply);
        buttons->addWidget(modify);
        buttons->addWidget(cancel);
        buttons->addStretch(1);
        static_cast<QVBoxLayout *>(card->layout())->addLayout(buttons);
        m_cards.insert(planId, card);
        m_pendingPlans << planId;
        auto decide = [this, planId, apply, modify, cancel](KoraCutAgentSession::PlanDecision decision, const QString &comment) {
            apply->setEnabled(false);
            modify->setEnabled(false);
            cancel->setEnabled(false);
            m_pendingPlans.removeAll(planId);
            setInputMode(InputMode::Request);
            m_session->decidePlan(planId, decision, comment);
        };
        connect(apply, &QPushButton::clicked, this, [decide]() { decide(KoraCutAgentSession::PlanDecision::Apply, QString()); });
        connect(cancel, &QPushButton::clicked, this, [decide]() { decide(KoraCutAgentSession::PlanDecision::Cancel, QString()); });
        // Modifier : la personne dit ce qu'elle veut changer dans le champ du bas, Entrée envoie.
        connect(modify, &QPushButton::clicked, this, [this, planId]() {
            setInputMode(InputMode::ModifyPlan, planId);
            m_input->setFocus();
        });
        scrollToBottom();
    });
    connect(m_session, &KoraCutAgentSession::turnStarted, this, [this](const QString &, const QString &) {
        m_workingAgent.clear();
        refreshHeader();
    });
    connect(m_session, &KoraCutAgentSession::turnFinished, this, [this](const QString &, const QString &agent, const QString &summary, int calls, bool undoable) {
        if (!summary.isEmpty()) {
            addBubble(KoraCutAgentSession::agentDisplayName(agent), summary, false);
        }
        if (undoable) {
            addNote(i18ncp("@info note under a finished turn", "%1 tool call, one Undo takes it all back.", "%1 tool calls, one Undo takes it all back.", calls));
        }
        m_workingAgent.clear();
        setInputMode(InputMode::Request);
        refreshHeader();
    });
    connect(m_session, &KoraCutAgentSession::turnCancelled, this, [this](const QString &) {
        addNote(i18nc("@info note under a cancelled turn", "Turn cancelled: what the agent did is undone."));
        m_workingAgent.clear();
        setInputMode(InputMode::Request);
        refreshHeader();
    });
    connect(m_session, &KoraCutAgentSession::errorReceived, this, [this](const QString &code, const QString &message) {
        addNote(message.isEmpty() ? code : message);
        refreshHeader();
    });

    connect(KoraCutAiConnections::instance(), &KoraCutAiConnections::changed, this, &KoraCutAgentPanel::refreshHeader);

    m_linkState = m_session->linkState();
    setInputMode(InputMode::Request);
    refreshHeader();
}

void KoraCutAgentPanel::focusInput()
{
    m_input->setFocus(Qt::ShortcutFocusReason);
}

void KoraCutAgentPanel::answerPendingConfirmation(bool accepted)
{
    if (m_pendingConfirmations.isEmpty()) {
        return;
    }
    const QString callId = m_pendingConfirmations.last();
    if (QWidget *card = m_cards.value(callId)) {
        const QList<QPushButton *> buttons = card->findChildren<QPushButton *>();
        // Le premier bouton accorde, le second refuse.
        if (buttons.size() >= 2) {
            (accepted ? buttons.at(0) : buttons.at(1))->click();
            return;
        }
    }
    m_pendingConfirmations.removeAll(callId);
    m_session->answerConfirmation(callId, accepted);
}

void KoraCutAgentPanel::decidePendingPlan(KoraCutAgentSession::PlanDecision decision, const QString &comment)
{
    if (m_pendingPlans.isEmpty()) {
        return;
    }
    const QString planId = m_pendingPlans.last();
    m_pendingPlans.removeAll(planId);
    if (QWidget *card = m_cards.value(planId)) {
        for (QPushButton *b : card->findChildren<QPushButton *>()) {
            b->setEnabled(false);
        }
    }
    setInputMode(InputMode::Request);
    m_session->decidePlan(planId, decision, comment);
}

void KoraCutAgentPanel::showServiceNote(const QString &text)
{
    if (!text.isEmpty()) {
        addNote(text);
    }
}

void KoraCutAgentPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    limitBubbleWidths();
}

void KoraCutAgentPanel::limitBubbleWidths()
{
    const int maxWidth = qMax(120, int(m_scroll->viewport()->width() * 0.86));
    for (const QPointer<QLabel> &bubble : std::as_const(m_bubbles)) {
        if (bubble) {
            bubble->setMaximumWidth(maxWidth);
        }
    }
}

void KoraCutAgentPanel::submit()
{
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty()) {
        return;
    }
    m_input->clear();
    switch (m_mode) {
    case InputMode::ModifyPlan: {
        const QString planId = m_modeId;
        m_pendingPlans.removeAll(planId);
        if (QWidget *card = m_cards.value(planId)) {
            for (QPushButton *b : card->findChildren<QPushButton *>()) {
                b->setEnabled(false);
            }
        }
        setInputMode(InputMode::Request);
        m_session->decidePlan(planId, KoraCutAgentSession::PlanDecision::Modify, text);
        break;
    }
    case InputMode::AnswerQuestion: {
        const QString questionId = m_modeId;
        setInputMode(InputMode::Request);
        m_session->answerQuestion(questionId, text);
        break;
    }
    case InputMode::Request:
        if (m_linkState != KoraCutAgents::LinkState::Connected) {
            // Rien n'est connecté : la demande reste dans le champ, et le panneau dit quoi faire (4 octobre 2026).
            m_input->setPlainText(text);
            showConnectChoice();
            break;
        }
        m_session->sendRequest(text);
        break;
    }
}

void KoraCutAgentPanel::debugType(const QString &text)
{
    m_input->setPlainText(text);
    submit();
}

void KoraCutAgentPanel::showConnectChoice()
{
    if (m_connectCard) {
        m_connectCard->deleteLater();
        m_connectCard = nullptr;
    }
    const bool starting = KoraCutAiConnections::instance()->hasModelConnection() && m_linkState == KoraCutAgents::LinkState::Connecting;
    if (starting) {
        addNote(i18nc("@info", "The agents are starting. Send your request again in a few seconds."));
        return;
    }
    // Le chat a besoin du service d'agents : s'il n'est pas sur cet ordinateur, on le dit au lieu de promettre.
    const bool serviceHere = !KoraCutAgentClient::configuredUrl().isEmpty() || !KoraCutAiBridge::serviceDirectory().isEmpty();
    QString body = i18nc("@info", "No AI is connected yet, so nobody can answer here. Two ways to edit with an AI:");
    QWidget *card = addCard(i18nc("@title", "Connect an AI to edit with you"), body);
    card->setProperty("koracut_choix_connexion", true);
    auto *layout = qobject_cast<QVBoxLayout *>(card->layout());
    auto addWay = [this, card, layout](const QString &title, const QString &text, const QString &button, bool primary, const std::function<void()> &run) {
        auto *wayTitle = new QLabel(title, card);
        wayTitle->setObjectName(QStringLiteral("koracutAgentCardTitle"));
        wayTitle->setWordWrap(true);
        layout->addWidget(wayTitle);
        auto *wayText = new QLabel(text, card);
        wayText->setObjectName(QStringLiteral("koracutAgentCardBody"));
        wayText->setWordWrap(true);
        layout->addWidget(wayText);
        auto *row = new QHBoxLayout();
        row->setContentsMargins(0, 0, 0, 0);
        auto *b = new QPushButton(button, card);
        b->setObjectName(primary ? QStringLiteral("koracutAgentPrimaryButton") : QStringLiteral("koracutAgentButton"));
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, run);
        row->addWidget(b);
        row->addStretch(1);
        layout->addLayout(row);
    };
    addWay(i18nc("@title first way to edit with an AI", "1. In the KoraCut chat, with your key"),
           serviceHere ? i18nc("@info", "Add the API key of your provider (Anthropic, OpenAI, Gemini, a local model and others). Then write here.")
                       : i18nc("@info",
                               "Add the API key of your provider. On this computer the chat cannot answer yet: the agents service is not part of this "
                               "version. For now, use the second way."),
           i18nc("@action:button open the AI connections page", "Connect a model"), serviceHere, []() { KoraCutAiConnectionsPage::openInPreferences(); });
    addWay(i18nc("@title second way to edit with an AI", "2. With your agent in the terminal"),
           i18nc("@info", "Claude Code, Codex, Gemini, OpenCode and others, with your own account. The agent works in the KoraCut terminal and edits in this "
                          "timeline, in front of you. You write your requests there."),
           i18nc("@action:button opens the terminal with the command line agent of your choice", "Edit with an Agent"), !serviceHere,
           [this]() { Q_EMIT claudeCodeRequested(); });
    m_connectCard = card;
    scrollToBottom();
}

void KoraCutAgentPanel::setInputMode(InputMode mode, const QString &id)
{
    m_mode = mode;
    m_modeId = id;
    switch (mode) {
    case InputMode::ModifyPlan:
        m_input->setPlaceholderText(i18nc("@info:placeholder", "Say what to change in the plan, then press Enter"));
        break;
    case InputMode::AnswerQuestion:
        m_input->setPlaceholderText(i18nc("@info:placeholder", "Your answer, then press Enter"));
        break;
    case InputMode::Request:
        m_input->setPlaceholderText(m_linkState == KoraCutAgents::LinkState::Connected
                                        ? i18nc("@info:placeholder", "Tell the agents what to do. Enter sends, Shift+Enter adds a line.")
                                        : (!KoraCutAiConnections::instance()->hasModelConnection()
                                               ? i18nc("@info:placeholder", "Write your request. No AI is connected yet: KoraCut will show you how to connect one.")
                                               : i18nc("@info:placeholder", "The agents are starting…")));
        break;
    }
    m_input->adjustHeight();
    // Le champ reste ouvert même sans connexion : écrire sans rien avoir connecté montre le choix (showConnectChoice).
    const bool usable = m_linkState == KoraCutAgents::LinkState::Connected || m_mode == InputMode::Request;
    m_input->setEnabled(usable);
    m_send->setEnabled(usable);
    if (m_linkState == KoraCutAgents::LinkState::Connected && m_connectCard) {
        m_connectCard->deleteLater();
        m_connectCard = nullptr;
    }
}

void KoraCutAgentPanel::refreshHeader()
{
    QString color = QStringLiteral("#8A8A8A");
    QString text;
    switch (m_linkState) {
    case KoraCutAgents::LinkState::NoService:
        text = i18nc("@info agents service state", "No service");
        break;
    case KoraCutAgents::LinkState::Connecting:
        text = i18nc("@info agents service state", "Connecting…");
        break;
    case KoraCutAgents::LinkState::Error:
        color = KoraCutThemes::token(QStringLiteral("rouge")).name();
        text = i18nc("@info agents service state", "Error");
        break;
    case KoraCutAgents::LinkState::Connected:
        if (m_session->turnOpen()) {
            color = KoraCutThemes::token(QStringLiteral("accent")).name();
            text = i18nc("@info agents service state, %1 agent name", "%1 is working…", KoraCutAgentSession::agentDisplayName(m_workingAgent.isEmpty() ? QStringLiteral("monteur") : m_workingAgent));
        } else if (m_serviceStatus == QLatin1String("quota")) {
            color = KoraCutThemes::token(QStringLiteral("jaune")).name();
            text = i18nc("@info agents service state", "Monthly quota reached");
        } else {
            color = KoraCutThemes::token(QStringLiteral("vert")).name();
            text = i18nc("@info agents service state", "Connected");
        }
        break;
    }
    m_status->setText(QStringLiteral("<span style=\"color:%1; font-size:14px;\">&#9679;</span>&nbsp; %2").arg(color, text.toHtmlEscaped()));
    m_status->setToolTip(m_linkMessage);
    m_cancelTurn->setVisible(m_session->turnOpen());
    m_connectModel->setVisible(!m_session->turnOpen());
    if (m_mode == InputMode::Request) {
        setInputMode(InputMode::Request);
    }
}

void KoraCutAgentPanel::addBubble(const QString &author, const QString &text, bool person)
{
    auto *row = new QWidget(m_thread);
    auto *rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(2);
    if (!author.isEmpty()) {
        auto *name = new QLabel(author, row);
        name->setObjectName(QStringLiteral("koracutAgentAuthor"));
        rowLayout->addWidget(name, 0, person ? Qt::AlignRight : Qt::AlignLeft);
    }
    auto *bubble = new QLabel(text, row);
    bubble->setObjectName(person ? QStringLiteral("koracutAgentBubblePerson") : QStringLiteral("koracutAgentBubbleAgent"));
    bubble->setWordWrap(true);
    bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bubble->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    // Pas de drapeau d'alignement sur la bulle : Qt la dimensionnerait d'après sizeHint() et couperait
    // le texte replié après deux lignes (vu à la capture du 23 septembre 2026). Un ressort dans une
    // rangée la pousse à droite ou à gauche, et sa hauteur suit sa largeur.
    auto *side = new QHBoxLayout();
    side->setContentsMargins(0, 0, 0, 0);
    side->setSpacing(0);
    if (person) {
        side->addStretch(1);
        side->addWidget(bubble);
    } else {
        side->addWidget(bubble);
        side->addStretch(1);
    }
    rowLayout->addLayout(side);
    m_bubbles << bubble;
    m_threadLayout->insertWidget(m_threadLayout->count() - 1, row);
    limitBubbleWidths();
    scrollToBottom();
}

void KoraCutAgentPanel::addToolLine(const QString &tool, const QJsonObject &input, const QJsonObject &result)
{
    // « couper_clip · clip 15 · 00:00:06:00 · ok » : l'outil, deux ou trois valeurs, l'issue.
    QStringList parts{tool};
    int shown = 0;
    for (auto it = input.constBegin(); it != input.constEnd() && shown < 3; ++it) {
        const QString value = shortValue(it.value());
        if (value.isEmpty()) {
            continue;
        }
        QString key = it.key();
        if (key.endsWith(QLatin1String("_id"))) {
            key.chop(3);
            parts << QStringLiteral("%1 %2").arg(key, value);
        } else if (key == QLatin1String("position") || key == QLatin1String("duree") || key == QLatin1String("recherche") || key == QLatin1String("cote") ||
                   key == QLatin1String("type") || key == QLatin1String("nom")) {
            parts << value;
        } else {
            continue;
        }
        ++shown;
    }
    const bool ok = result.value(QStringLiteral("ok")).toBool(false);
    parts << (ok ? QStringLiteral("ok") : result.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString());
    auto *line = new QLabel(parts.join(QStringLiteral(" · ")), m_thread);
    line->setObjectName(ok ? QStringLiteral("koracutAgentToolLine") : QStringLiteral("koracutAgentToolLineError"));
    line->setFont(KoraCutFonts::code(KoraCutMetrics::FontMono));
    line->setWordWrap(true);
    line->setTextInteractionFlags(Qt::TextSelectableByMouse);
    line->setToolTip(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Indented)));
    m_threadLayout->insertWidget(m_threadLayout->count() - 1, line);
    scrollToBottom();
}

void KoraCutAgentPanel::addNote(const QString &text)
{
    auto *note = new QLabel(text, m_thread);
    note->setObjectName(QStringLiteral("koracutAgentNote"));
    note->setWordWrap(true);
    m_threadLayout->insertWidget(m_threadLayout->count() - 1, note);
    scrollToBottom();
}

QWidget *KoraCutAgentPanel::addCard(const QString &title, const QString &body)
{
    auto *card = new QWidget(m_thread);
    card->setObjectName(QStringLiteral("koracutAgentCard"));
    card->setAttribute(Qt::WA_StyledBackground, true);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup, KoraCutMetrics::SpaceZone, KoraCutMetrics::SpaceGroup);
    layout->setSpacing(KoraCutMetrics::SpaceGroup);
    auto *titleLabel = new QLabel(title, card);
    titleLabel->setObjectName(QStringLiteral("koracutAgentCardTitle"));
    titleLabel->setWordWrap(true);
    layout->addWidget(titleLabel);
    auto *bodyLabel = new QLabel(body, card);
    bodyLabel->setObjectName(QStringLiteral("koracutAgentCardBody"));
    bodyLabel->setWordWrap(true);
    bodyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(bodyLabel);
    m_threadLayout->insertWidget(m_threadLayout->count() - 1, card);
    return card;
}

void KoraCutAgentPanel::scrollToBottom()
{
    QTimer::singleShot(0, this, [this]() {
        if (m_scroll) {
            m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
        }
    });
}
