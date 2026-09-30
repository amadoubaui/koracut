/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutagentsession.h"

#include "core.h"
#include "doc/docundostack.hpp"
#include "kdenlive_debug.h"
#include "koracut/koracutagenttools.h"
#include "koracut/koracutvisible.h"
#include "project/projectmanager.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>
#include <QJsonDocument>

// ---------------------------------------------------------------------------------------------
// La commande de tour

KoraCutTurnCommand::KoraCutTurnCommand(std::vector<QUndoCommand *> steps, const QString &text)
    : QUndoCommand(text)
    , m_steps(std::move(steps))
{
}

KoraCutTurnCommand::~KoraCutTurnCommand()
{
    qDeleteAll(m_steps);
}

void KoraCutTurnCommand::undo()
{
    for (auto it = m_steps.rbegin(); it != m_steps.rend(); ++it) {
        (*it)->undo();
    }
    m_undone = true;
}

void KoraCutTurnCommand::redo()
{
    if (!m_undone) {
        // Premier redo, celui de QUndoStack::push : tout est déjà exécuté.
        return;
    }
    for (QUndoCommand *step : m_steps) {
        step->redo();
    }
}

// ---------------------------------------------------------------------------------------------
// Le tour ouvert

struct KoraCutAgentSession::Turn
{
    QString id;
    QString request;
    QString lastAgent;
    int calls{0};
    std::vector<QUndoCommand *> commands;
    /** Outils à confirmation approuvés par un plan accepté, pour ce tour. */
    QSet<QString> approvedTools;
    /** Appels précis (appel_id) que la personne a acceptés dans le panneau. */
    QSet<QString> approvedCalls;
    /** Appels qui attendent la réponse de la personne : appel_id -> { agent, outil, entree }. */
    QHash<QString, QJsonObject> pending;
    /** Plans proposés et pas encore décidés : plan_id -> outils à confirmation qu'ils nomment. */
    QHash<QString, QSet<QString>> pendingPlanTools;
    bool cancelled{false};
};

namespace {

QJsonObject toolError(const QString &code, const QString &message, const QString &advice = QString())
{
    QJsonObject e{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
    if (!advice.isEmpty()) {
        e.insert(QStringLiteral("conseil"), advice);
    }
    return QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("erreur"), e}};
}

// Deux choses se confirment toujours à part, même avec un plan accepté : lancer un rendu, écraser un fichier.
// modeles_export (document 23) : lister, lire, appliquer et ajouter à la file ne demandent rien ; exporter
// et lancer_file lancent un rendu, donc l'accord de la personne, comme exporter.
bool startsRender(const QString &tool, const QJsonObject &input)
{
    const QString action = input.value(QStringLiteral("action")).toString();
    return tool == QLatin1String("modeles_export") && (action == QLatin1String("exporter") || action == QLatin1String("lancer_file"));
}

bool neverCoveredByPlan(const QString &tool, const QJsonObject &input)
{
    return tool == QLatin1String("exporter") || tool == QLatin1String("exporter_serie") || input.value(QStringLiteral("ecraser")).toBool(false) ||
        startsRender(tool, input);
}

const QString kUndoTool = QStringLiteral("annuler_dernier_tour");

// Les définitions lues une fois dans les ressources (Agents in-app/outils/*.json, embarquées par CMake).
const QList<KoraCutAgentSession::ToolDefinition> &allDefinitions()
{
    static const QList<KoraCutAgentSession::ToolDefinition> list = []() {
        QList<KoraCutAgentSession::ToolDefinition> defs;
        const QDir dir(QStringLiteral(":/koracut/agents"));
        const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QString &name : files) {
            QFile file(dir.filePath(name));
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QJsonArray tools = QJsonDocument::fromJson(file.readAll()).array();
            for (const QJsonValue &v : tools) {
                const QJsonObject o = v.toObject();
                KoraCutAgentSession::ToolDefinition def;
                def.name = o.value(QStringLiteral("name")).toString();
                def.description = o.value(QStringLiteral("description")).toString();
                def.inputSchema = o.value(QStringLiteral("input_schema")).toObject();
                def.confirmation = o.value(QStringLiteral("x_confirmation")).toBool(false);
                if (!def.name.isEmpty()) {
                    defs << def;
                }
            }
        }
        if (defs.isEmpty()) {
            qCWarning(KDENLIVE_LOG) << "KoraCut: aucune définition d'outil d'agent dans :/koracut/agents (ressource absente ?)";
        }
        return defs;
    }();
    return list;
}

} // namespace

// ---------------------------------------------------------------------------------------------

KoraCutAgentSession::KoraCutAgentSession(KoraCutAgentLink *link, QObject *parent)
    : QObject(parent)
    , m_link(link)
{
    m_timelineProvider = []() { return pCore->projectManager()->getTimeline(); };
    m_undoStackProvider = []() { return pCore->undoStack(); };
    for (const ToolDefinition &def : allDefinitions()) {
        if (def.confirmation) {
            m_confirmationTools.insert(def.name);
        }
    }
    connect(m_link, &KoraCutAgentLink::messageReceived, this, &KoraCutAgentSession::onMessage);
    connect(m_link, &KoraCutAgentLink::stateChanged, this, &KoraCutAgentSession::onLinkState);
}

KoraCutAgentSession::~KoraCutAgentSession()
{
    if (m_turn) {
        qDeleteAll(m_turn->commands);
    }
}

QList<KoraCutAgentSession::ToolDefinition> KoraCutAgentSession::availableTools()
{
    const QStringList implemented = KoraCutAgentTools::implementedTools();
    QList<ToolDefinition> list;
    for (const ToolDefinition &def : allDefinitions()) {
        if (implemented.contains(def.name) || def.name == kUndoTool) {
            list << def;
        }
    }
    return list;
}

QString KoraCutAgentSession::agentDisplayName(const QString &agentId)
{
    const QString id = agentId.toLower();
    if (id == QLatin1String("chef")) {
        return i18nc("@label agent name, with its article", "the Chief");
    }
    if (id == QLatin1String("monteur")) {
        return i18nc("@label agent name, with its article", "the Editor");
    }
    if (id == QLatin1String("coloriste")) {
        return i18nc("@label agent name, with its article", "the Colorist");
    }
    if (id == QLatin1String("sound_designer") || id == QLatin1String("sound-designer")) {
        return i18nc("@label agent name, with its article", "the Sound designer");
    }
    if (id == QLatin1String("mixeur")) {
        return i18nc("@label agent name, with its article", "the Mixer");
    }
    if (id == QLatin1String("retoucheur")) {
        return i18nc("@label agent name, with its article", "the Retoucher");
    }
    if (id == QLatin1String("compositeur")) {
        return i18nc("@label agent name, with its article", "the Composer");
    }
    if (id == QLatin1String("motion_designer") || id == QLatin1String("motion-designer")) {
        return i18nc("@label agent name, with its article", "the Motion designer");
    }
    return agentId.isEmpty() ? i18nc("@label agent name, with its article", "the Editor") : agentId;
}

void KoraCutAgentSession::setTimelineProvider(std::function<std::shared_ptr<TimelineItemModel>()> provider)
{
    m_timelineProvider = std::move(provider);
}

void KoraCutAgentSession::setUndoStackProvider(std::function<std::shared_ptr<DocUndoStack>()> provider)
{
    m_undoStackProvider = std::move(provider);
}

void KoraCutAgentSession::setPerson(const QString &firstName)
{
    m_firstName = firstName;
}

void KoraCutAgentSession::setMachineProfile(const QString &profile)
{
    m_machineProfile = profile;
}

void KoraCutAgentSession::setCallBudget(int budget)
{
    m_budget = qMax(1, budget);
}

std::shared_ptr<TimelineItemModel> KoraCutAgentSession::timeline() const
{
    return m_timelineProvider ? m_timelineProvider() : nullptr;
}

std::shared_ptr<DocUndoStack> KoraCutAgentSession::undoStack() const
{
    return m_undoStackProvider ? m_undoStackProvider() : nullptr;
}

KoraCutAgents::LinkState KoraCutAgentSession::linkState() const
{
    return m_link ? m_link->state() : KoraCutAgents::LinkState::NoService;
}

QString KoraCutAgentSession::turnId() const
{
    return m_turn ? m_turn->id : QString();
}

int KoraCutAgentSession::turnActions() const
{
    return m_turn ? int(m_turn->commands.size()) : 0;
}

int KoraCutAgentSession::turnCalls() const
{
    return m_turn ? m_turn->calls : 0;
}

QJsonObject KoraCutAgentSession::hello() const
{
    QJsonArray tools;
    const QList<ToolDefinition> defs = availableTools();
    for (const ToolDefinition &def : defs) {
        tools.append(QJsonObject{{QStringLiteral("name"), def.name},
                                 {QStringLiteral("description"), def.description},
                                 {QStringLiteral("input_schema"), def.inputSchema},
                                 {QStringLiteral("confirmation"), def.confirmation}});
    }
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("bonjour")},
                       {QStringLiteral("protocole"), 1},
                       {QStringLiteral("application"), QStringLiteral("KoraCut")},
                       {QStringLiteral("version"), QCoreApplication::applicationVersion()},
                       {QStringLiteral("budget_appels"), m_budget},
                       {QStringLiteral("outils"), tools}};
}

QJsonObject KoraCutAgentSession::context() const
{
    QJsonObject ctx{{QStringLiteral("prenom"), m_firstName},
                    {QStringLiteral("date"), QDate::currentDate().toString(Qt::ISODate)},
                    {QStringLiteral("profil_machine"), m_machineProfile.isEmpty() ? QStringLiteral("moyenne") : m_machineProfile},
                    {QStringLiteral("budget_appels"), m_budget}};
    if (auto tl = timeline()) {
        ctx.insert(QStringLiteral("etat_timeline"), KoraCutAgentTools::call(tl, QStringLiteral("etat_timeline"), {{QStringLiteral("detail"), QStringLiteral("resume")}}));
    }
    return ctx;
}

void KoraCutAgentSession::onLinkState(KoraCutAgents::LinkState state)
{
    if (state == KoraCutAgents::LinkState::Connected) {
        m_link->send(hello());
    } else if (m_turn && state != KoraCutAgents::LinkState::Connecting) {
        // La liaison est tombée au milieu d'un tour : ce qui est fait reste, et s'annule d'un coup.
        closeTurn(m_turn->lastAgent, i18nc("@info summary of a turn cut by a lost connection", "The connection to the service was lost."), m_turn->calls, false);
    }
    Q_EMIT linkStateChanged(state, m_link->stateMessage());
}

// ---------------------------------------------------------------------------------------------
// Depuis le panneau

void KoraCutAgentSession::sendRequest(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    if (linkState() != KoraCutAgents::LinkState::Connected) {
        Q_EMIT errorReceived(QStringLiteral("aucun_service"), i18nc("@info", "No agents service is connected."));
        return;
    }
    if (m_turn) {
        closeTurn(m_turn->lastAgent, QString(), m_turn->calls, false);
    }
    m_turn = std::make_unique<Turn>();
    m_turn->id = QStringLiteral("t%1-%2").arg(++m_turnCounter).arg(QDateTime::currentSecsSinceEpoch());
    m_turn->request = trimmed;
    Q_EMIT personSaid(trimmed);
    Q_EMIT turnStarted(m_turn->id, trimmed);
    m_link->send(QJsonObject{{QStringLiteral("type"), QStringLiteral("demande")},
                             {QStringLiteral("tour_id"), m_turn->id},
                             {QStringLiteral("texte"), trimmed},
                             {QStringLiteral("contexte"), context()}});
}

void KoraCutAgentSession::answerConfirmation(const QString &callId, bool accepted)
{
    if (!m_turn || !m_turn->pending.contains(callId)) {
        return;
    }
    m_turn->pending.remove(callId);
    if (accepted) {
        m_turn->approvedCalls.insert(callId);
    }
    m_link->send(QJsonObject{{QStringLiteral("type"), QStringLiteral("reponse_personne")},
                             {QStringLiteral("tour_id"), m_turn->id},
                             {QStringLiteral("reponse"), QStringLiteral("confirmation")},
                             {QStringLiteral("appel_id"), callId},
                             {QStringLiteral("accord"), accepted}});
}

void KoraCutAgentSession::answerQuestion(const QString &questionId, const QString &text)
{
    if (!m_turn) {
        return;
    }
    Q_EMIT personSaid(text);
    m_link->send(QJsonObject{{QStringLiteral("type"), QStringLiteral("reponse_personne")},
                             {QStringLiteral("tour_id"), m_turn->id},
                             {QStringLiteral("reponse"), QStringLiteral("question")},
                             {QStringLiteral("question_id"), questionId},
                             {QStringLiteral("texte"), text}});
}

void KoraCutAgentSession::decidePlan(const QString &planId, PlanDecision decision, const QString &comment)
{
    if (!m_turn) {
        return;
    }
    QString word;
    switch (decision) {
    case PlanDecision::Apply:
        word = QStringLiteral("applique");
        // Le plan accepté vaut accord pour les outils qu'il nomme, pendant ce tour.
        m_turn->approvedTools.unite(m_turn->pendingPlanTools.value(planId));
        break;
    case PlanDecision::Modify:
        word = QStringLiteral("modifie");
        break;
    case PlanDecision::Cancel:
        word = QStringLiteral("annule");
        break;
    }
    QJsonObject msg{{QStringLiteral("type"), QStringLiteral("reponse_personne")},
                    {QStringLiteral("tour_id"), m_turn->id},
                    {QStringLiteral("reponse"), QStringLiteral("plan")},
                    {QStringLiteral("plan_id"), planId},
                    {QStringLiteral("decision"), word}};
    if (!comment.isEmpty()) {
        msg.insert(QStringLiteral("commentaire"), comment);
        Q_EMIT personSaid(comment);
    }
    m_link->send(msg);
}

void KoraCutAgentSession::cancelTurn()
{
    if (!m_turn) {
        return;
    }
    const QString id = m_turn->id;
    m_turn->cancelled = true;
    m_link->send(QJsonObject{{QStringLiteral("type"), QStringLiteral("annuler_tour")}, {QStringLiteral("tour_id"), id}});
    closeTurn(m_turn->lastAgent, QString(), m_turn->calls, true);
    Q_EMIT turnCancelled(id);
}

// ---------------------------------------------------------------------------------------------
// Depuis le service

void KoraCutAgentSession::onMessage(const QJsonObject &message)
{
    const QString type = message.value(QStringLiteral("type")).toString();
    const QString turnId = message.value(QStringLiteral("tour_id")).toString();
    if (type == QLatin1String("etat")) {
        Q_EMIT serviceStatus(message.value(QStringLiteral("etat")).toString(), message.value(QStringLiteral("message")).toString());
        return;
    }
    if (type == QLatin1String("erreur")) {
        Q_EMIT errorReceived(message.value(QStringLiteral("code")).toString(), message.value(QStringLiteral("message")).toString());
        return;
    }
    // Tout le reste appartient à un tour : le nôtre, sinon on l'ignore (un tour annulé, par exemple).
    if (!m_turn || (!turnId.isEmpty() && turnId != m_turn->id)) {
        if (type == QLatin1String("appel_outil")) {
            sendToolResult(message.value(QStringLiteral("appel_id")).toString(), message.value(QStringLiteral("outil")).toString(),
                           toolError(QStringLiteral("tour_ferme"), QStringLiteral("Ce tour est fermé ou annulé."), QStringLiteral("Attends une nouvelle demande.")), 0);
        }
        return;
    }
    const QString agent = message.value(QStringLiteral("agent")).toString();
    if (!agent.isEmpty()) {
        m_turn->lastAgent = agent;
    }
    if (type == QLatin1String("texte")) {
        Q_EMIT agentSaid(agent, message.value(QStringLiteral("texte")).toString());
    } else if (type == QLatin1String("appel_outil")) {
        handleToolCall(message);
    } else if (type == QLatin1String("question")) {
        QStringList choices;
        for (const QJsonValue &v : message.value(QStringLiteral("propositions")).toArray()) {
            choices << v.toString();
        }
        Q_EMIT questionAsked(message.value(QStringLiteral("question_id")).toString(), agent, message.value(QStringLiteral("texte")).toString(), choices);
    } else if (type == QLatin1String("plan")) {
        const QString planId = message.value(QStringLiteral("plan_id")).toString();
        QSet<QString> tools;
        for (const QJsonValue &step : message.value(QStringLiteral("etapes")).toArray()) {
            for (const QJsonValue &t : step.toObject().value(QStringLiteral("outils")).toArray()) {
                if (m_confirmationTools.contains(t.toString())) {
                    tools.insert(t.toString());
                }
            }
        }
        m_turn->pendingPlanTools.insert(planId, tools);
        Q_EMIT planProposed(planId, agent, message);
    } else if (type == QLatin1String("fin_tour")) {
        closeTurn(agent, message.value(QStringLiteral("resume")).toString(), message.value(QStringLiteral("appels")).toInt(m_turn->calls), false);
    }
}

void KoraCutAgentSession::handleToolCall(const QJsonObject &message)
{
    const QString callId = message.value(QStringLiteral("appel_id")).toString();
    const QString agent = message.value(QStringLiteral("agent")).toString();
    const QString tool = message.value(QStringLiteral("outil")).toString();
    const QJsonObject input = message.value(QStringLiteral("entree")).toObject();
    QElapsedTimer timer;
    timer.start();
    m_turn->calls++;
    QJsonObject result;
    if (m_turn->calls > m_budget) {
        result = toolError(QStringLiteral("budget_epuise"), QStringLiteral("Le budget de %1 appels de ce tour est épuisé.").arg(m_budget),
                           QStringLiteral("Arrête-toi et résume ce qui est fait et ce qui reste."));
    } else if ((m_confirmationTools.contains(tool) || startsRender(tool, input)) && !m_turn->approvedCalls.contains(callId) &&
               (neverCoveredByPlan(tool, input) || !m_turn->approvedTools.contains(tool))) {
        // La personne décide dans le panneau ; le service reviendra avec le même appel_id si elle dit oui.
        m_turn->pending.insert(callId, QJsonObject{{QStringLiteral("agent"), agent}, {QStringLiteral("outil"), tool}, {QStringLiteral("entree"), input}});
        result = toolError(QStringLiteral("confirmation_requise"), QStringLiteral("%1 attend l'accord de la personne.").arg(tool),
                           QStringLiteral("Attends sa réponse, ne relance pas l'outil en boucle."));
        Q_EMIT confirmationRequested(callId, agent, tool, input);
    } else {
        // Le montage visible (25 septembre 2026) : l'écran montre l'endroit, le geste se fait, la
        // réponse part après une courte pause, sans jamais bloquer l'interface. Sans pause (tests,
        // mode coupé, « Accélérer »), tout se fait ici même, comme avant.
        const QString turnId = m_turn->id;
        QString who = agentDisplayName(agent);
        if (!who.isEmpty()) {
            who[0] = who.at(0).toUpper();
        }
        KoraCutVisible::self()->run(
            who, tool, input, this,
            [this, turnId, tool, input]() -> QJsonObject {
                if (!m_turn || m_turn->id != turnId) {
                    return toolError(QStringLiteral("tour_ferme"), QStringLiteral("Ce tour a été annulé pendant le geste."), QStringLiteral("Attends une nouvelle demande."));
                }
                bool touched = false;
                return executeTool(tool, input, touched);
            },
            [this, agent, tool, input, callId, timer](const QJsonObject &done) {
                Q_EMIT toolCalled(agent, tool, input, done);
                sendToolResult(callId, tool, done, timer.elapsed());
            });
        return;
    }
    Q_EMIT toolCalled(agent, tool, input, result);
    sendToolResult(callId, tool, result, timer.elapsed());
}

QJsonObject KoraCutAgentSession::executeTool(const QString &tool, const QJsonObject &input, bool &touched)
{
    touched = false;
    if (tool == kUndoTool) {
        return undoLastTurn(input);
    }
    auto stack = undoStack();
    auto tl = timeline();
    if (!tl) {
        return toolError(QStringLiteral("non_disponible"), QStringLiteral("Aucune séquence ouverte."));
    }
    // Les commandes que l'outil pousse sont mises de côté : le tour les regroupera en une seule étape.
    if (stack) {
        stack->beginCollecting();
    }
    const QJsonObject result = KoraCutAgentTools::call(tl, tool, input);
    if (stack) {
        std::vector<QUndoCommand *> commands = stack->endCollecting();
        touched = !commands.empty();
        m_turn->commands.insert(m_turn->commands.end(), commands.begin(), commands.end());
    }
    return result;
}

QJsonObject KoraCutAgentSession::undoLastTurn(const QJsonObject &input)
{
    auto stack = undoStack();
    if (!stack) {
        return toolError(QStringLiteral("non_disponible"), QStringLiteral("Pas d'historique."));
    }
    const bool restore = input.value(QStringLiteral("retablir")).toBool(false);
    if (restore) {
        const auto *next = dynamic_cast<const KoraCutTurnCommand *>(stack->command(stack->index()));
        if (!next) {
            return toolError(QStringLiteral("rien_a_annuler"), QStringLiteral("Il n'y a aucun tour annulé à rétablir."),
                             QStringLiteral("Rétablir ne marche que juste après annuler_dernier_tour."));
        }
        stack->redo();
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("tour_retabli"), QJsonObject{{QStringLiteral("intitule"), next->text()}, {QStringLiteral("actions"), next->steps()}}},
                           {QStringLiteral("annuler_possible"), true}};
    }
    // Le tour en cours a déjà changé le projet : c'est lui, le dernier tour.
    if (!m_turn->commands.empty()) {
        const int actions = int(m_turn->commands.size());
        for (auto it = m_turn->commands.rbegin(); it != m_turn->commands.rend(); ++it) {
            (*it)->undo();
        }
        qDeleteAll(m_turn->commands);
        m_turn->commands.clear();
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("tour_annule"), QJsonObject{{QStringLiteral("intitule"), m_turn->request}, {QStringLiteral("actions"), actions}}},
                           {QStringLiteral("retablir_possible"), false}};
    }
    if (stack->index() == 0) {
        return toolError(QStringLiteral("rien_a_annuler"), QStringLiteral("Aucun tour d'agent à annuler dans cette session."),
                         QStringLiteral("Dis à la personne qu'il n'y a rien à défaire de ton côté."));
    }
    const auto *last = dynamic_cast<const KoraCutTurnCommand *>(stack->command(stack->index() - 1));
    if (!last) {
        return toolError(QStringLiteral("annulation_impossible"), QStringLiteral("La personne a modifié le montage à la main depuis le dernier tour."),
                         QStringLiteral("N'annule pas par-dessus son travail : explique-lui, elle peut utiliser Édition > Annuler elle-même."));
    }
    const QString title = last->text();
    const int actions = last->steps();
    stack->undo();
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("tour_annule"), QJsonObject{{QStringLiteral("intitule"), title}, {QStringLiteral("actions"), actions}}},
                       {QStringLiteral("retablir_possible"), true}};
}

void KoraCutAgentSession::sendToolResult(const QString &callId, const QString &tool, const QJsonObject &result, qint64 elapsedMs)
{
    QJsonObject msg{{QStringLiteral("type"), QStringLiteral("resultat_outil")},
                    {QStringLiteral("appel_id"), callId},
                    {QStringLiteral("outil"), tool},
                    {QStringLiteral("resultat"), result},
                    {QStringLiteral("duree_ms"), double(elapsedMs)}};
    if (m_turn) {
        msg.insert(QStringLiteral("tour_id"), m_turn->id);
    }
    m_link->send(msg);
}

void KoraCutAgentSession::closeTurn(const QString &agent, const QString &summary, int calls, bool cancelled)
{
    if (!m_turn) {
        return;
    }
    std::unique_ptr<Turn> turn = std::move(m_turn);
    const int actions = int(turn->commands.size());
    bool inHistory = false;
    if (actions > 0) {
        if (auto stack = undoStack()) {
            // « Le Monteur : la demande », coupée à 60 caractères.
            QString request = turn->request.simplified();
            if (request.size() > 60) {
                request = request.left(59) + QChar(0x2026);
            }
            QString name = agentDisplayName(agent.isEmpty() ? turn->lastAgent : agent);
            name[0] = name.at(0).toUpper();
            stack->push(new KoraCutTurnCommand(std::move(turn->commands), i18nc("@item undo step of an agent turn: %1 agent name, %2 request", "%1: %2", name, request)));
            turn->commands.clear();
            inHistory = true;
            if (cancelled) {
                stack->undo();
            }
        } else {
            qDeleteAll(turn->commands);
            turn->commands.clear();
        }
    }
    m_link->send(QJsonObject{{QStringLiteral("type"), QStringLiteral("tour_ferme")},
                             {QStringLiteral("tour_id"), turn->id},
                             {QStringLiteral("actions"), actions},
                             {QStringLiteral("historique"), inHistory},
                             {QStringLiteral("annule"), cancelled}});
    if (!cancelled) {
        Q_EMIT turnFinished(turn->id, agent, summary, calls, inHistory);
    }
}
