/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QUndoCommand>
#include <functional>
#include <memory>
#include <vector>

class DocUndoStack;
class TimelineItemModel;

namespace KoraCutAgents {
/** @brief L'état de la liaison avec le service d'agents, tel que la pastille et le panneau le montrent. */
enum class LinkState { NoService, Connecting, Connected, Error };
}

/** @brief La liaison avec le service d'agents, vue par la session : envoyer un message, en recevoir.
 *
 * Le client WebSocket (koracutagentclient.h) en est l'implémentation réelle ; le test en fournit une
 * en mémoire. Un message = un objet JSON, dont le champ « type » dit ce que c'est
 * (`Agents in-app/PROTOCOLE.md`).
 */
class KoraCutAgentLink : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void send(const QJsonObject &message) = 0;
    virtual KoraCutAgents::LinkState state() const = 0;
    /** @brief Un mot sur l'état (adresse, erreur), pour l'infobulle. */
    virtual QString stateMessage() const { return {}; }

Q_SIGNALS:
    void messageReceived(const QJsonObject &message);
    void stateChanged(KoraCutAgents::LinkState state);
};

/** @brief Tout ce qu'un tour d'agent a fait, en une seule étape d'annulation.
 *
 * Les commandes que les outils ont poussées pendant le tour sont mises de côté par
 * DocUndoStack::beginCollecting(), puis regroupées ici. Annuler les défait de la dernière à la
 * première ; rétablir les refait dans l'ordre. Comme FunctionalUndoCommand, le premier redo() (celui
 * que QUndoStack::push appelle) ne fait rien : tout est déjà exécuté.
 */
class KoraCutTurnCommand : public QUndoCommand
{
public:
    KoraCutTurnCommand(std::vector<QUndoCommand *> steps, const QString &text);
    ~KoraCutTurnCommand() override;
    void undo() override;
    void redo() override;
    int steps() const { return int(m_steps.size()); }

private:
    std::vector<QUndoCommand *> m_steps;
    bool m_undone{false};
};

/** @brief Une session d'agents : les tours, la confirmation, le budget, le macro-undo.
 *
 * Chantier 3 (23 septembre 2026). Le service décide (il tient la boucle du modèle), l'application
 * exécute : chaque `appel_outil` reçu passe par KoraCutAgentTools::call() sur la timeline courante,
 * et son résultat repart en `resultat_outil`. Un tour commence à `demande` et finit à `fin_tour` ;
 * tout ce qu'il a changé tient dans une seule étape d'annulation intitulée
 * « Le Monteur : <la demande> ». Un tour sans action ne laisse rien dans l'historique.
 *
 * Les outils marqués `x_confirmation` dans `Agents in-app/outils/*.json` répondent
 * `confirmation_requise` tant que la personne n'a pas dit oui dans le panneau ; un plan accepté vaut
 * accord pour les outils qu'il nomme, pendant ce tour. Écraser un fichier et lancer un rendu se
 * confirment toujours à part.
 */
class KoraCutAgentSession : public QObject
{
    Q_OBJECT
public:
    enum class PlanDecision { Apply, Modify, Cancel };

    struct ToolDefinition
    {
        QString name;
        QString description;
        QJsonObject inputSchema;
        bool confirmation{false};
    };

    explicit KoraCutAgentSession(KoraCutAgentLink *link, QObject *parent = nullptr);
    ~KoraCutAgentSession() override;

    /** @brief Les outils que cette application sait exécuter : ceux de KoraCutAgentTools plus ceux de la
     *  session (annuler_dernier_tour), avec leur schéma lu dans les ressources `:/koracut/agents/`. */
    static QList<ToolDefinition> availableTools();
    /** @brief Nom d'un agent pour l'écran, avec l'article : « le Monteur ». */
    static QString agentDisplayName(const QString &agentId);

    void setTimelineProvider(std::function<std::shared_ptr<TimelineItemModel>()> provider);
    void setUndoStackProvider(std::function<std::shared_ptr<DocUndoStack>()> provider);
    void setPerson(const QString &firstName);
    void setMachineProfile(const QString &profile);
    void setCallBudget(int budget);
    int callBudget() const { return m_budget; }

    /** @brief Le message `bonjour` : version, outils et leurs schémas. Envoyé à chaque connexion. */
    QJsonObject hello() const;

    KoraCutAgents::LinkState linkState() const;
    bool turnOpen() const { return m_turn != nullptr; }
    QString turnId() const;
    /** @brief Nombre d'actions du tour ouvert qui ont changé le projet. */
    int turnActions() const;
    /** @brief Les appels d'outils du tour ouvert. */
    int turnCalls() const;

    // ---- Ce que la personne fait dans le panneau
    /** @brief Envoie une demande : ferme le tour ouvert s'il y en a un, et en commence un autre. */
    void sendRequest(const QString &text);
    void answerConfirmation(const QString &callId, bool accepted);
    void answerQuestion(const QString &questionId, const QString &text);
    void decidePlan(const QString &planId, PlanDecision decision, const QString &comment = QString());
    /** @brief Annule tout le tour ouvert : ses actions sont défaites (et restent rétablissables). */
    void cancelTurn();

Q_SIGNALS:
    void linkStateChanged(KoraCutAgents::LinkState state, const QString &message);
    /** @brief Le service dit où il en est (« pret », « occupe », « quota », « erreur »). */
    void serviceStatus(const QString &status, const QString &message);
    void personSaid(const QString &text);
    void agentSaid(const QString &agent, const QString &text);
    void toolCalled(const QString &agent, const QString &tool, const QJsonObject &input, const QJsonObject &result);
    void confirmationRequested(const QString &callId, const QString &agent, const QString &tool, const QJsonObject &input);
    void questionAsked(const QString &questionId, const QString &agent, const QString &text, const QStringList &choices);
    void planProposed(const QString &planId, const QString &agent, const QJsonObject &plan);
    void turnStarted(const QString &turnId, const QString &text);
    void turnFinished(const QString &turnId, const QString &agent, const QString &summary, int calls, bool undoable);
    void turnCancelled(const QString &turnId);
    void errorReceived(const QString &code, const QString &message);

private:
    struct Turn;
    void onMessage(const QJsonObject &message);
    void onLinkState(KoraCutAgents::LinkState state);
    void handleToolCall(const QJsonObject &message);
    QJsonObject executeTool(const QString &tool, const QJsonObject &input, bool &touched);
    QJsonObject undoLastTurn(const QJsonObject &input);
    void closeTurn(const QString &agent, const QString &summary, int calls, bool cancelled);
    void sendToolResult(const QString &callId, const QString &tool, const QJsonObject &result, qint64 elapsedMs);
    QJsonObject context() const;
    std::shared_ptr<TimelineItemModel> timeline() const;
    std::shared_ptr<DocUndoStack> undoStack() const;

    KoraCutAgentLink *m_link{nullptr};
    std::function<std::shared_ptr<TimelineItemModel>()> m_timelineProvider;
    std::function<std::shared_ptr<DocUndoStack>()> m_undoStackProvider;
    std::unique_ptr<Turn> m_turn;
    QString m_firstName;
    QString m_machineProfile;
    int m_budget{30};
    int m_turnCounter{0};
    QSet<QString> m_confirmationTools;
};
