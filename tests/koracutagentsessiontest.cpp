/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "catch.hpp"
#include "test_utils.hpp"

#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "koracut/koracutagentsession.h"
#include "koracut/koracutagenttools.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace {

/** Un faux service en mémoire : il garde ce que l'application envoie, et on lui fait dire ce qu'on veut. */
class FakeLink : public KoraCutAgentLink
{
public:
    using KoraCutAgentLink::KoraCutAgentLink;
    void send(const QJsonObject &message) override { sent << message; }
    KoraCutAgents::LinkState state() const override { return m_state; }
    void connectNow()
    {
        m_state = KoraCutAgents::LinkState::Connected;
        Q_EMIT stateChanged(m_state);
    }
    void say(const QJsonObject &message) { Q_EMIT messageReceived(message); }
    QStringList types() const
    {
        QStringList list;
        for (const QJsonObject &m : sent) {
            list << m.value(QStringLiteral("type")).toString();
        }
        return list;
    }
    QJsonObject last() const { return sent.isEmpty() ? QJsonObject() : sent.last(); }
    QJsonObject lastResult() const { return last().value(QStringLiteral("resultat")).toObject(); }
    QString lastTurn() const { return last().value(QStringLiteral("tour_id")).toString(); }
    QList<QJsonObject> sent;

private:
    KoraCutAgents::LinkState m_state{KoraCutAgents::LinkState::NoService};
};

QString errorCode(const QJsonObject &r)
{
    return r.value(QStringLiteral("erreur")).toObject().value(QStringLiteral("code")).toString();
}

int clipCount(const std::shared_ptr<TimelineItemModel> &timeline, int trackId)
{
    int n = 0;
    for (int id : timeline->getItemsInRange(trackId, 0, -1, false)) {
        if (timeline->isClip(id)) {
            ++n;
        }
    }
    return n;
}

QJsonObject call(const QString &id, const QString &tool, const QJsonObject &input, const QString &turn, const QString &agent = QStringLiteral("monteur"))
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("appel_outil")},
                       {QStringLiteral("tour_id"), turn},
                       {QStringLiteral("appel_id"), id},
                       {QStringLiteral("agent"), agent},
                       {QStringLiteral("outil"), tool},
                       {QStringLiteral("entree"), input}};
}

QJsonObject endTurn(const QString &turn, const QString &summary = QStringLiteral("Fait."), int calls = 0)
{
    return QJsonObject{{QStringLiteral("type"), QStringLiteral("fin_tour")},
                       {QStringLiteral("tour_id"), turn},
                       {QStringLiteral("agent"), QStringLiteral("monteur")},
                       {QStringLiteral("resume"), summary},
                       {QStringLiteral("appels"), calls}};
}

} // namespace

TEST_CASE("Session d'agents : les outils annoncés", "[KoraCutAgentSession]")
{
    FakeLink link;
    KoraCutAgentSession session(&link);
    const QList<KoraCutAgentSession::ToolDefinition> tools = KoraCutAgentSession::availableTools();
    REQUIRE_FALSE(tools.isEmpty());
    QStringList names;
    bool deleteNeedsConfirmation = false;
    for (const auto &t : tools) {
        names << t.name;
        if (t.name == QLatin1String("supprimer_clip")) {
            deleteNeedsConfirmation = t.confirmation;
            CHECK(t.inputSchema.value(QStringLiteral("type")).toString() == QStringLiteral("object"));
            CHECK_FALSE(t.description.isEmpty());
        }
    }
    // Tout ce que l'application sait exécuter, plus l'annulation de tour, et rien d'autre.
    const QStringList implemented = KoraCutAgentTools::implementedTools();
    for (const QString &n : implemented) {
        CHECK(names.contains(n));
    }
    CHECK(names.contains(QStringLiteral("annuler_dernier_tour")));
    // Un outil pas encore écrit dans l'application (le séquenceur, chantier 5) n'est pas annoncé ;
    // l'export l'est depuis le lot B (24 septembre 2026).
    CHECK_FALSE(names.contains(QStringLiteral("creer_pattern")));
    CHECK(names.contains(QStringLiteral("exporter")));
    CHECK(deleteNeedsConfirmation);

    // À la connexion, « bonjour » part avec les schémas, sans champ x_.
    link.connectNow();
    REQUIRE(link.types() == QStringList{QStringLiteral("bonjour")});
    const QJsonArray announced = link.last().value(QStringLiteral("outils")).toArray();
    CHECK(announced.size() == tools.size());
    for (const QJsonValue &v : announced) {
        CHECK_FALSE(v.toObject().contains(QStringLiteral("x_kdenlive")));
        CHECK(v.toObject().contains(QStringLiteral("input_schema")));
    }
    const QString editor = KoraCutAgentSession::agentDisplayName(QStringLiteral("monteur"));
    CHECK((editor.contains(QStringLiteral("Monteur")) || editor.contains(QStringLiteral("Editor"))));
}

TEST_CASE("Session d'agents : un tour, une étape d'annulation", "[KoraCutAgentSession]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    const QString red = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 20, true);
    const int v1 = timeline->getTracksIds(false).first();

    FakeLink link;
    KoraCutAgentSession session(&link);
    session.setTimelineProvider([timeline]() { return timeline; });
    session.setUndoStackProvider([undoStack]() { return undoStack; });
    session.setPerson(QStringLiteral("Awa"));
    link.connectNow();
    link.sent.clear();

    QStringList agentTexts;
    QObject::connect(&session, &KoraCutAgentSession::agentSaid, [&agentTexts](const QString &, const QString &t) { agentTexts << t; });
    int finished = 0;
    bool lastUndoable = false;
    QObject::connect(&session, &KoraCutAgentSession::turnFinished, [&](const QString &, const QString &, const QString &, int, bool undoable) {
        ++finished;
        lastUndoable = undoable;
    });

    SECTION("Sans service, rien ne part")
    {
        FakeLink offline;
        KoraCutAgentSession alone(&offline);
        QString code;
        QObject::connect(&alone, &KoraCutAgentSession::errorReceived, [&code](const QString &c, const QString &) { code = c; });
        alone.sendRequest(QStringLiteral("Coupe"));
        CHECK(offline.sent.isEmpty());
        CHECK(code == QStringLiteral("aucun_service"));
        CHECK_FALSE(alone.turnOpen());
    }

    SECTION("Trois appels, une seule étape, et Annuler remet tout")
    {
        const int before = undoStack->count();
        session.sendRequest(QStringLiteral("Pose le plan rouge, coupe-le à 8, déplace le morceau de droite à 100"));
        REQUIRE(session.turnOpen());
        REQUIRE(link.types() == QStringList{QStringLiteral("demande")});
        const QString turn = link.lastTurn();
        const QJsonObject ctx = link.last().value(QStringLiteral("contexte")).toObject();
        CHECK(ctx.value(QStringLiteral("prenom")).toString() == QStringLiteral("Awa"));
        CHECK(ctx.value(QStringLiteral("etat_timeline")).toObject().value(QStringLiteral("ok")).toBool());
        CHECK(ctx.value(QStringLiteral("budget_appels")).toInt() == 30);

        // Une lecture d'abord : aucune action, rien de collecté.
        link.say(call(QStringLiteral("a0"), QStringLiteral("etat_timeline"), {}, turn));
        CHECK(link.lastResult().value(QStringLiteral("ok")).toBool());
        CHECK(session.turnActions() == 0);

        link.say(call(QStringLiteral("a1"), QStringLiteral("ajouter_clip"),
                      {{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("video")}}, turn));
        REQUIRE(link.last().value(QStringLiteral("type")).toString() == QStringLiteral("resultat_outil"));
        CHECK(link.last().value(QStringLiteral("appel_id")).toString() == QStringLiteral("a1"));
        REQUIRE(link.lastResult().value(QStringLiteral("ok")).toBool());
        const int clip = link.lastResult().value(QStringLiteral("clip_id")).toInt();
        CHECK(session.turnActions() == 1);
        // Pendant le tour, rien n'est encore dans l'historique.
        CHECK(undoStack->count() == before);

        link.say(call(QStringLiteral("a2"), QStringLiteral("couper_clip"), {{QStringLiteral("clip_id"), clip}, {QStringLiteral("position"), 8}}, turn));
        REQUIRE(link.lastResult().value(QStringLiteral("ok")).toBool());
        const int right = link.lastResult().value(QStringLiteral("clip_droit_id")).toInt();
        link.say(call(QStringLiteral("a3"), QStringLiteral("deplacer_clip"), {{QStringLiteral("clip_id"), right}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 100}}, turn));
        REQUIRE(link.lastResult().value(QStringLiteral("ok")).toBool());
        CHECK(session.turnActions() == 3);
        CHECK(session.turnCalls() == 4);
        CHECK(timeline->getClipPosition(right) == 100);

        link.say(endTurn(turn, QStringLiteral("Trois gestes, un seul Annuler."), 4));
        CHECK_FALSE(session.turnOpen());
        CHECK(finished == 1);
        CHECK(lastUndoable);
        REQUIRE(undoStack->count() == before + 1);
        CHECK(undoStack->text(before).contains(QStringLiteral("Pose le plan rouge")));
        CHECK(link.last().value(QStringLiteral("type")).toString() == QStringLiteral("tour_ferme"));
        CHECK(link.last().value(QStringLiteral("actions")).toInt() == 3);
        CHECK(link.last().value(QStringLiteral("historique")).toBool());
        // L'ordre des messages : demande, quatre résultats, fermeture.
        CHECK(link.types() == QStringList{QStringLiteral("demande"), QStringLiteral("resultat_outil"), QStringLiteral("resultat_outil"),
                                          QStringLiteral("resultat_outil"), QStringLiteral("resultat_outil"), QStringLiteral("tour_ferme")});

        // Un seul Annuler remet l'état de départ ; Rétablir refait les trois gestes.
        undoStack->undo();
        CHECK(clipCount(timeline, v1) == 0);
        undoStack->redo();
        CHECK(clipCount(timeline, v1) == 2);
        CHECK(timeline->getClipPosition(timeline->getClipByPosition(v1, 100)) == 100);
        undoStack->undo();
        CHECK(clipCount(timeline, v1) == 0);
    }

    SECTION("Un tour sans action ne laisse rien")
    {
        const int before = undoStack->count();
        session.sendRequest(QStringLiteral("Qu'est-ce qu'il y a dans la timeline ?"));
        const QString turn = link.lastTurn();
        link.say(call(QStringLiteral("b1"), QStringLiteral("etat_timeline"), {}, turn));
        link.say(call(QStringLiteral("b2"), QStringLiteral("lister_medias"), {}, turn));
        link.say(endTurn(turn, QStringLiteral("Un média rouge, rien sur les pistes."), 2));
        CHECK(undoStack->count() == before);
        CHECK(finished == 1);
        CHECK_FALSE(lastUndoable);
        CHECK_FALSE(link.last().value(QStringLiteral("historique")).toBool());
    }

    SECTION("Un outil à confirmation attend l'accord, et rien ne bouge")
    {
        session.sendRequest(QStringLiteral("Enlève le plan"));
        const QString turn = link.lastTurn();
        link.say(call(QStringLiteral("c1"), QStringLiteral("ajouter_clip"),
                      {{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("video")}}, turn));
        const int clip = link.lastResult().value(QStringLiteral("clip_id")).toInt();
        REQUIRE(timeline->isClip(clip));

        QString askedCall, askedTool;
        QObject::connect(&session, &KoraCutAgentSession::confirmationRequested, [&](const QString &id, const QString &, const QString &tool, const QJsonObject &) {
            askedCall = id;
            askedTool = tool;
        });
        link.say(call(QStringLiteral("c2"), QStringLiteral("supprimer_clip"), {{QStringLiteral("clip_ids"), QJsonArray{clip}}}, turn));
        CHECK(errorCode(link.lastResult()) == QStringLiteral("confirmation_requise"));
        CHECK(askedCall == QStringLiteral("c2"));
        CHECK(askedTool == QStringLiteral("supprimer_clip"));
        CHECK(timeline->isClip(clip));
        CHECK(session.turnActions() == 1);

        SECTION("La personne refuse")
        {
            session.answerConfirmation(QStringLiteral("c2"), false);
            CHECK(link.last().value(QStringLiteral("type")).toString() == QStringLiteral("reponse_personne"));
            CHECK(link.last().value(QStringLiteral("reponse")).toString() == QStringLiteral("confirmation"));
            CHECK_FALSE(link.last().value(QStringLiteral("accord")).toBool());
            // Le service qui insisterait avec le même appel : toujours pas d'accord.
            link.say(call(QStringLiteral("c2"), QStringLiteral("supprimer_clip"), {{QStringLiteral("clip_ids"), QJsonArray{clip}}}, turn));
            CHECK(errorCode(link.lastResult()) == QStringLiteral("confirmation_requise"));
            CHECK(timeline->isClip(clip));
        }

        SECTION("La personne accepte : le même appel passe, une seule fois")
        {
            session.answerConfirmation(QStringLiteral("c2"), true);
            CHECK(link.last().value(QStringLiteral("accord")).toBool());
            link.say(call(QStringLiteral("c2"), QStringLiteral("supprimer_clip"), {{QStringLiteral("clip_ids"), QJsonArray{clip}}}, turn));
            CHECK(link.lastResult().value(QStringLiteral("ok")).toBool());
            CHECK_FALSE(timeline->isClip(clip));
            CHECK(session.turnActions() == 2);
            // Un autre appel du même outil redemande l'accord.
            link.say(call(QStringLiteral("c3"), QStringLiteral("supprimer_clip"), {{QStringLiteral("clip_ids"), QJsonArray{clip}}}, turn));
            CHECK(errorCode(link.lastResult()) == QStringLiteral("confirmation_requise"));
            link.say(endTurn(turn));
            const int idx = undoStack->index();
            REQUIRE(idx > 0);
            undoStack->undo();
            CHECK(clipCount(timeline, v1) == 0);
        }
    }

    SECTION("Un plan accepté vaut accord pour les outils qu'il nomme")
    {
        session.sendRequest(QStringLiteral("Coupe les silences"));
        const QString turn = link.lastTurn();
        link.say(call(QStringLiteral("d1"), QStringLiteral("ajouter_clip"),
                      {{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("video")}}, turn));
        const int clip = link.lastResult().value(QStringLiteral("clip_id")).toInt();
        QString planId;
        QObject::connect(&session, &KoraCutAgentSession::planProposed, [&planId](const QString &id, const QString &, const QJsonObject &) { planId = id; });
        link.say(QJsonObject{{QStringLiteral("type"), QStringLiteral("plan")},
                             {QStringLiteral("tour_id"), turn},
                             {QStringLiteral("plan_id"), QStringLiteral("p1")},
                             {QStringLiteral("agent"), QStringLiteral("monteur")},
                             {QStringLiteral("resume"), QStringLiteral("Je retire le plan.")},
                             {QStringLiteral("etapes"), QJsonArray{QJsonObject{{QStringLiteral("action"), QStringLiteral("Retirer le plan")},
                                                                             {QStringLiteral("agent"), QStringLiteral("monteur")},
                                                                             {QStringLiteral("outils"), QJsonArray{QStringLiteral("supprimer_clip")}}}}}});
        REQUIRE(planId == QStringLiteral("p1"));

        SECTION("Modifier : rien n'est approuvé")
        {
            session.decidePlan(planId, KoraCutAgentSession::PlanDecision::Modify, QStringLiteral("Garde le premier"));
            CHECK(link.last().value(QStringLiteral("decision")).toString() == QStringLiteral("modifie"));
            CHECK(link.last().value(QStringLiteral("commentaire")).toString() == QStringLiteral("Garde le premier"));
            link.say(call(QStringLiteral("d2"), QStringLiteral("supprimer_clip"), {{QStringLiteral("clip_ids"), QJsonArray{clip}}}, turn));
            CHECK(errorCode(link.lastResult()) == QStringLiteral("confirmation_requise"));
        }

        SECTION("Appliquer : supprimer_clip passe sans autre question, exporter non")
        {
            session.decidePlan(planId, KoraCutAgentSession::PlanDecision::Apply);
            CHECK(link.last().value(QStringLiteral("decision")).toString() == QStringLiteral("applique"));
            link.say(call(QStringLiteral("d2"), QStringLiteral("supprimer_clip"), {{QStringLiteral("clip_ids"), QJsonArray{clip}}}, turn));
            CHECK(link.lastResult().value(QStringLiteral("ok")).toBool());
            CHECK_FALSE(timeline->isClip(clip));
            // Un rendu se confirme toujours à part, même nommé par le plan : l'accord passe avant tout,
            // y compris avant de savoir si l'outil existe déjà dans l'application.
            link.say(call(QStringLiteral("d3"), QStringLiteral("exporter"), {{QStringLiteral("chemin"), QStringLiteral("/tmp/x.mp4")}}, turn));
            CHECK(errorCode(link.lastResult()) == QStringLiteral("confirmation_requise"));
        }
    }

    SECTION("Le budget")
    {
        session.setCallBudget(2);
        session.sendRequest(QStringLiteral("Lis tout"));
        const QString turn = link.lastTurn();
        link.say(call(QStringLiteral("e1"), QStringLiteral("etat_timeline"), {}, turn));
        CHECK(link.lastResult().value(QStringLiteral("ok")).toBool());
        link.say(call(QStringLiteral("e2"), QStringLiteral("etat_timeline"), {}, turn));
        CHECK(link.lastResult().value(QStringLiteral("ok")).toBool());
        link.say(call(QStringLiteral("e3"), QStringLiteral("ajouter_clip"),
                      {{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("video")}}, turn));
        CHECK(errorCode(link.lastResult()) == QStringLiteral("budget_epuise"));
        CHECK(clipCount(timeline, v1) == 0);
    }

    SECTION("Annuler ce tour, et annuler_dernier_tour")
    {
        const int before = undoStack->count();
        session.sendRequest(QStringLiteral("Pose deux plans"));
        const QString turn = link.lastTurn();
        link.say(call(QStringLiteral("f1"), QStringLiteral("ajouter_clip"),
                      {{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("video")}}, turn));
        link.say(call(QStringLiteral("f2"), QStringLiteral("ajouter_clip"),
                      {{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 50}, {QStringLiteral("partie"), QStringLiteral("video")}}, turn));
        REQUIRE(clipCount(timeline, v1) == 2);

        SECTION("La personne annule : tout est défait, le service est prévenu, rétablir reste possible")
        {
            int cancelled = 0;
            QObject::connect(&session, &KoraCutAgentSession::turnCancelled, [&cancelled](const QString &) { ++cancelled; });
            session.cancelTurn();
            CHECK(cancelled == 1);
            CHECK_FALSE(session.turnOpen());
            CHECK(clipCount(timeline, v1) == 0);
            const QStringList types = link.types();
            CHECK(types.at(types.size() - 2) == QStringLiteral("annuler_tour"));
            CHECK(types.last() == QStringLiteral("tour_ferme"));
            CHECK(link.last().value(QStringLiteral("annule")).toBool());
            // L'étape est dans l'historique, annulée : Rétablir la refait.
            CHECK(undoStack->count() == before + 1);
            CHECK(undoStack->index() == before);
            undoStack->redo();
            CHECK(clipCount(timeline, v1) == 2);
            // Un appel qui arriverait encore pour ce tour est refusé.
            link.say(call(QStringLiteral("f3"), QStringLiteral("etat_timeline"), {}, turn));
            CHECK(errorCode(link.lastResult()) == QStringLiteral("tour_ferme"));
        }

        SECTION("L'agent annule son propre tour, puis le tour précédent")
        {
            link.say(call(QStringLiteral("f3"), QStringLiteral("annuler_dernier_tour"), {}, turn));
            REQUIRE(link.lastResult().value(QStringLiteral("ok")).toBool());
            CHECK(link.lastResult().value(QStringLiteral("tour_annule")).toObject().value(QStringLiteral("actions")).toInt() == 2);
            CHECK(clipCount(timeline, v1) == 0);
            CHECK(session.turnActions() == 0);
            link.say(endTurn(turn));
            CHECK(undoStack->count() == before);

            // Un tour qui a agi, puis un second qui l'annule par l'outil, puis rétablit.
            session.sendRequest(QStringLiteral("Pose un plan"));
            const QString turn2 = link.lastTurn();
            link.say(call(QStringLiteral("g1"), QStringLiteral("ajouter_clip"),
                          {{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("video")}}, turn2));
            link.say(endTurn(turn2));
            REQUIRE(undoStack->count() == before + 1);
            session.sendRequest(QStringLiteral("Non, annule"));
            const QString turn3 = link.lastTurn();
            link.say(call(QStringLiteral("h1"), QStringLiteral("annuler_dernier_tour"), {}, turn3, QStringLiteral("chef")));
            REQUIRE(link.lastResult().value(QStringLiteral("ok")).toBool());
            CHECK(link.lastResult().value(QStringLiteral("retablir_possible")).toBool());
            CHECK(link.lastResult().value(QStringLiteral("tour_annule")).toObject().value(QStringLiteral("intitule")).toString().contains(QStringLiteral("Pose un plan")));
            CHECK(clipCount(timeline, v1) == 0);
            link.say(call(QStringLiteral("h2"), QStringLiteral("annuler_dernier_tour"), {{QStringLiteral("retablir"), true}}, turn3, QStringLiteral("chef")));
            REQUIRE(link.lastResult().value(QStringLiteral("ok")).toBool());
            CHECK(clipCount(timeline, v1) == 1);
            link.say(call(QStringLiteral("h3"), QStringLiteral("annuler_dernier_tour"), {{QStringLiteral("retablir"), true}}, turn3, QStringLiteral("chef")));
            CHECK(errorCode(link.lastResult()) == QStringLiteral("rien_a_annuler"));
            link.say(endTurn(turn3));
            // Ce tour-là n'a rien poussé de neuf : toujours une seule entrée.
            CHECK(undoStack->count() == before + 1);

            // Une retouche à la main après le tour bloque l'annulation par l'agent.
            int manual = -1;
            timeline->requestClipInsertion(red, v1, 200, manual, true, true, false);
            REQUIRE(manual != -1);
            session.sendRequest(QStringLiteral("Annule encore"));
            const QString turn4 = link.lastTurn();
            link.say(call(QStringLiteral("i1"), QStringLiteral("annuler_dernier_tour"), {}, turn4, QStringLiteral("chef")));
            CHECK(errorCode(link.lastResult()) == QStringLiteral("annulation_impossible"));
            CHECK(clipCount(timeline, v1) == 2);
            link.say(endTurn(turn4));
        }
    }

    SECTION("Une nouvelle demande ferme le tour précédent")
    {
        const int before = undoStack->count();
        session.sendRequest(QStringLiteral("Pose un plan"));
        const QString turn = link.lastTurn();
        link.say(call(QStringLiteral("j1"), QStringLiteral("ajouter_clip"),
                      {{QStringLiteral("media_id"), red}, {QStringLiteral("piste_id"), v1}, {QStringLiteral("position"), 0}, {QStringLiteral("partie"), QStringLiteral("video")}}, turn));
        session.sendRequest(QStringLiteral("Et maintenant ?"));
        CHECK(undoStack->count() == before + 1);
        CHECK(session.turnId() != turn);
        // Un message en retard du premier tour est ignoré ; un appel est refusé.
        link.say(endTurn(turn));
        CHECK(session.turnOpen());
        link.say(call(QStringLiteral("j2"), QStringLiteral("etat_timeline"), {}, turn));
        CHECK(errorCode(link.lastResult()) == QStringLiteral("tour_ferme"));
        link.say(endTurn(session.turnId()));
        CHECK_FALSE(session.turnOpen());
    }

    timeline.reset();
    pCore->projectManager()->closeCurrentDocument(false, false);
}
