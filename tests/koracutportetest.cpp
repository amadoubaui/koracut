/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
#include "catch.hpp"
#include "test_utils.hpp"

#include "bin/projectclip.h"
#include "core.h"
#include "doc/docundostack.hpp"
#include "doc/kdenlivedoc.h"
#include "koracut/koracutagenttools.h"
#include "koracut/porte/koracutporte.h"
#include "koracut/porte/koracutportemethodes.h"

#include <KActionCollection>

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QTemporaryDir>

namespace {

QString code(const KoraCutPorte::Reply &r)
{
    return r.error.value(QStringLiteral("code")).toString();
}

// Fait tourner la boucle d'événements jusqu'à ce que @p ready soit vrai, ou @p ms écoulées.
bool pump(const std::function<bool()> &ready, int ms = 3000)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (ready()) {
            return true;
        }
    }
    return ready();
}

// Envoie une ligne et attend la réponse portant le même id.
QJsonObject roundTrip(QLocalSocket &socket, const QJsonObject &request)
{
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
    QByteArray buffer;
    QJsonObject found;
    pump([&]() {
        if (socket.bytesAvailable()) {
            buffer += socket.readAll();
        }
        int nl = buffer.indexOf('\n');
        while (nl >= 0) {
            const QJsonObject msg = QJsonDocument::fromJson(buffer.left(nl)).object();
            buffer.remove(0, nl + 1);
            if (msg.value(QStringLiteral("id")) == request.value(QStringLiteral("id"))) {
                found = msg;
                return true;
            }
            nl = buffer.indexOf('\n');
        }
        return false;
    });
    return found;
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

} // namespace

TEST_CASE("Porte : la ligne de commande se lit", "[KoraCutPorte]")
{
    QString method, error;
    QJsonObject params;
    REQUIRE(KoraCutPorte::parseCommandLine(QStringLiteral("timeline etat niveau=resume"), method, params, error));
    CHECK(method == QStringLiteral("timeline.etat"));
    CHECK(params.value(QStringLiteral("niveau")).toString() == QStringLiteral("resume"));
    REQUIRE(KoraCutPorte::parseCommandLine(QStringLiteral("timeline.outil nom=couper_clip args='{\"clip_id\": 12, \"position\": 40}'"), method, params, error));
    CHECK(method == QStringLiteral("timeline.outil"));
    CHECK(params.value(QStringLiteral("args")).toObject().value(QStringLiteral("clip_id")).toInt() == 12);
    REQUIRE(KoraCutPorte::parseCommandLine(QStringLiteral("medias importer chemins='[\"a b.mp4\"]' attendre=false n=3"), method, params, error));
    CHECK(params.value(QStringLiteral("chemins")).toArray().first().toString() == QStringLiteral("a b.mp4"));
    CHECK(params.value(QStringLiteral("attendre")).toBool(true) == false);
    CHECK(params.value(QStringLiteral("n")).toInt() == 3);
    // Sans guillemets autour du JSON (le champ de la Console) : les guillemets intérieurs restent.
    REQUIRE(KoraCutPorte::parseCommandLine(QStringLiteral("timeline outil nom=supprimer_clip args={\"clip_ids\":[999]}"), method, params, error));
    CHECK(params.value(QStringLiteral("args")).toObject().value(QStringLiteral("clip_ids")).toArray().first().toInt() == 999);
    REQUIRE(KoraCutPorte::parseCommandLine(QStringLiteral("historique tour faire=debut titre='deux mots'"), method, params, error));
    CHECK(params.value(QStringLiteral("titre")).toString() == QStringLiteral("deux mots"));
    REQUIRE(KoraCutPorte::parseCommandLine(QStringLiteral("{\"method\": \"app.ping\", \"params\": {}}"), method, params, error));
    CHECK(method == QStringLiteral("app.ping"));
    CHECK_FALSE(KoraCutPorte::parseCommandLine(QStringLiteral("timeline"), method, params, error));
    CHECK_FALSE(KoraCutPorte::parseCommandLine(QStringLiteral("timeline etat resume"), method, params, error));
}

TEST_CASE("Porte : le chemin reste dans le projet", "[KoraCutPorte]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString root = dir.path();
    QDir().mkpath(root + QStringLiteral("/Rushs"));
    CHECK(KoraCutPorteMethodes::isInsideFolder(root, root + QStringLiteral("/Rushs/a.mp4")));
    CHECK(KoraCutPorteMethodes::isInsideFolder(root, root));
    CHECK_FALSE(KoraCutPorteMethodes::isInsideFolder(root, root + QStringLiteral("/../ailleurs.mp4")));
    CHECK_FALSE(KoraCutPorteMethodes::isInsideFolder(root, QStringLiteral("/tmp")));
    CHECK_FALSE(KoraCutPorteMethodes::isInsideFolder(root, root + QStringLiteral("2/a.mp4")));
}

TEST_CASE("Porte : jeton et prise", "[KoraCutPorte]")
{
    QTemporaryDir config;
    REQUIRE(config.isValid());
    KoraCutPorte porte;
    porte.registerMethod(QStringLiteral("app.ping"), QStringLiteral("ping"), {}, [](const KoraCutPorte::Call &) { return KoraCutPorte::Reply::value(QJsonObject{{QStringLiteral("ok"), true}}); });
    const QString reglage = QStringLiteral("koracut-test-%1").arg(QCoreApplication::applicationPid());
    REQUIRE(porte.listen(reglage, config.path()));
    REQUIRE(porte.isListening());
    // porte.json : le chemin de la prise, le jeton, lisible par l'utilisateur seul.
    QFile f(porte.porteFilePath());
    REQUIRE(f.open(QIODevice::ReadOnly));
    const QJsonObject info = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    CHECK(info.value(QStringLiteral("ouverte")).toBool());
    CHECK(info.value(QStringLiteral("prise")).toString() == porte.socketPath());
    CHECK(info.value(QStringLiteral("jeton")).toString() == porte.token());
    CHECK(info.value(QStringLiteral("reglage")).toString() == reglage);
#ifndef Q_OS_WIN
    CHECK((QFileInfo(porte.porteFilePath()).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther)) == 0);
#endif

    SECTION("Un mauvais jeton ferme la prise")
    {
        QLocalSocket socket;
        socket.connectToServer(porte.socketPath());
        REQUIRE(socket.waitForConnected(2000));
        const QJsonObject reply = roundTrip(socket, QJsonObject{{QStringLiteral("id"), 0},
                                                                {QStringLiteral("method"), QStringLiteral("porte.ouvrir")},
                                                                {QStringLiteral("params"), QJsonObject{{QStringLiteral("jeton"), QStringLiteral("faux")}, {QStringLiteral("client"), QStringLiteral("test")}}}});
        CHECK(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("jeton_refuse"));
        pump([&socket]() { return socket.state() != QLocalSocket::ConnectedState; });
        CHECK(socket.state() != QLocalSocket::ConnectedState);
        CHECK(porte.clientCount() == 0);
    }
    SECTION("Sans porte.ouvrir, rien ne passe")
    {
        QLocalSocket socket;
        socket.connectToServer(porte.socketPath());
        REQUIRE(socket.waitForConnected(2000));
        const QJsonObject reply = roundTrip(socket, QJsonObject{{QStringLiteral("id"), 5}, {QStringLiteral("method"), QStringLiteral("app.ping")}});
        CHECK(reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("jeton_refuse"));
    }
    SECTION("Le bon jeton ouvre, puis les méthodes répondent")
    {
        QLocalSocket socket;
        socket.connectToServer(porte.socketPath());
        REQUIRE(socket.waitForConnected(2000));
        const QJsonObject open = roundTrip(socket, QJsonObject{{QStringLiteral("id"), 0},
                                                               {QStringLiteral("method"), QStringLiteral("porte.ouvrir")},
                                                               {QStringLiteral("params"), QJsonObject{{QStringLiteral("jeton"), porte.token()}, {QStringLiteral("client"), QStringLiteral("test 1.0")}}}});
        const QJsonObject result = open.value(QStringLiteral("result")).toObject();
        CHECK(result.value(QStringLiteral("protocole")).toInt() == 1);
        CHECK(result.value(QStringLiteral("methodes")).toArray().contains(QStringLiteral("app.ping")));
        CHECK(porte.clientCount() == 1);
        const QJsonObject ping = roundTrip(socket, QJsonObject{{QStringLiteral("id"), 7}, {QStringLiteral("method"), QStringLiteral("app.ping")}});
        CHECK(ping.value(QStringLiteral("result")).toObject().value(QStringLiteral("ok")).toBool());
        const QJsonObject unknown = roundTrip(socket, QJsonObject{{QStringLiteral("id"), 8}, {QStringLiteral("method"), QStringLiteral("app.cafe")}});
        CHECK(unknown.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QStringLiteral("methode_inconnue"));
        const QJsonObject catalogue = roundTrip(socket, QJsonObject{{QStringLiteral("id"), 9}, {QStringLiteral("method"), QStringLiteral("porte.methodes")}});
        CHECK(catalogue.value(QStringLiteral("result")).toArray().size() == 1);
        socket.disconnectFromServer();
        pump([&porte]() { return porte.clientCount() == 0; });
        CHECK(porte.clientCount() == 0);
    }
    porte.close();
    CHECK_FALSE(QFileInfo::exists(porte.porteFilePath()));
}

TEST_CASE("Porte : les méthodes sur un vrai projet", "[KoraCutPorte]")
{
    auto binModel = pCore->projectItemModel();
    std::shared_ptr<DocUndoStack> undoStack = std::make_shared<DocUndoStack>(nullptr);
    KdenliveDoc document(undoStack);
    pCore->projectManager()->testSetDocument(&document);
    QDateTime documentDate = QDateTime::currentDateTime();
    KdenliveTests::updateTimeline(false, QString(), QString(), documentDate, 0);
    auto timeline = document.getTimeline(document.uuid());
    pCore->projectManager()->testSetActiveTimeline(timeline);

    // Le dossier du projet : un fichier de test copié dedans, et un clip du chutier qui pointe dessus.
    QTemporaryDir project;
    REQUIRE(project.isValid());
    QDir().mkpath(project.path() + QStringLiteral("/Rushs"));
    const QString rush = project.path() + QStringLiteral("/Rushs/entretien.mkv");
    REQUIRE(QFile::copy(sourcesPath + QStringLiteral("/small.mkv"), rush));
    const QString rushId = KdenliveTests::createFileProducer(pCore->getProjectProfile(), binModel, rush);
    REQUIRE_FALSE(rushId.isEmpty());
    const QString red = KdenliveTests::createProducer(pCore->getProjectProfile(), "red", binModel, 40, true);
    const int v1 = timeline->getTracksIds(false).first();
    int cid = -1;
    REQUIRE(timeline->requestClipInsertion(red, v1, 0, cid, true, true, false));
    undoStack->clear();

    KActionCollection actions(static_cast<QObject *>(nullptr));
    auto *toggle = new QAction(QStringLiteral("Essai à cocher"), &actions);
    toggle->setCheckable(true);
    actions.addAction(QStringLiteral("koracut_test_toggle"), toggle);
    auto *disabled = new QAction(QStringLiteral("Grisée"), &actions);
    disabled->setEnabled(false);
    actions.addAction(QStringLiteral("koracut_test_disabled"), disabled);

    KoraCutPorte porte;
    KoraCutPorteContext ctx;
    ctx.actions = &actions;
    ctx.timeline = [timeline]() { return timeline; };
    ctx.undoStack = [undoStack]() { return undoStack; };
    ctx.projectFolder = [&project]() { return project.path(); };
    KoraCutPorteMethodes::registerAll(&porte, ctx);
    const QString client = QStringLiteral("test");

    SECTION("Le catalogue et les actions")
    {
        CHECK(porte.methodNames().contains(QStringLiteral("fichiers.renommer")));
        CHECK(porte.methodNames().contains(QStringLiteral("timeline.outil")));
        const KoraCutPorte::Reply list = porte.call(client, QStringLiteral("actions.lister"), {});
        REQUIRE(list.ok());
        CHECK(list.result.toArray().size() == 2);
        const KoraCutPorte::Reply filtered = porte.call(client, QStringLiteral("actions.lister"), {{QStringLiteral("filtre"), QStringLiteral("cocher")}});
        CHECK(filtered.result.toArray().size() == 1);
        CHECK_FALSE(toggle->isChecked());
        KoraCutPorte::Reply r = porte.call(client, QStringLiteral("actions.declencher"), {{QStringLiteral("nom"), QStringLiteral("koracut_test_toggle")}, {QStringLiteral("cocher"), true}});
        REQUIRE(r.ok());
        CHECK(toggle->isChecked());
        r = porte.call(client, QStringLiteral("actions.declencher"), {{QStringLiteral("nom"), QStringLiteral("koracut_test_toggle")}, {QStringLiteral("cocher"), true}});
        CHECK(toggle->isChecked()); // déjà coché : rien ne bouge
        r = porte.call(client, QStringLiteral("actions.declencher"), {{QStringLiteral("nom"), QStringLiteral("koracut_test_toggle")}});
        CHECK_FALSE(toggle->isChecked());
        CHECK(code(porte.call(client, QStringLiteral("actions.declencher"), {{QStringLiteral("nom"), QStringLiteral("koracut_test_disabled")}})) == QStringLiteral("action_desactivee"));
        CHECK(code(porte.call(client, QStringLiteral("actions.declencher"), {{QStringLiteral("nom"), QStringLiteral("nulle_part")}})) == QStringLiteral("action_inconnue"));
        CHECK(code(porte.call(client, QStringLiteral("actions.declencher"), {})) == QStringLiteral("parametres_invalides"));
        CHECK(porte.call(client, QStringLiteral("actions.etat"), {{QStringLiteral("nom"), QStringLiteral("koracut_test_toggle")}}).result.toObject().value(QStringLiteral("cochable")).toBool());
    }

    SECTION("Un outil de timeline = une étape d'annulation")
    {
        CHECK(undoStack->count() == 0);
        const KoraCutPorte::Reply state = porte.call(client, QStringLiteral("timeline.etat"), {{QStringLiteral("niveau"), QStringLiteral("resume")}});
        REQUIRE(state.ok());
        CHECK(state.result.toObject().value(QStringLiteral("pistes")).toArray().size() == timeline->getTracksCount());
        const KoraCutPorte::Reply cut = porte.call(client, QStringLiteral("timeline.outil"),
                                                   {{QStringLiteral("nom"), QStringLiteral("couper_clip")}, {QStringLiteral("args"), QJsonObject{{QStringLiteral("clip_id"), cid}, {QStringLiteral("position"), 10}}}});
        REQUIRE(cut.ok());
        CHECK(clipCount(timeline, v1) == 2);
        CHECK(undoStack->count() == 1);
        CHECK(porte.call(client, QStringLiteral("historique.lister"), {}).result.toObject().value(QStringLiteral("total")).toInt() == 1);
        REQUIRE(porte.call(client, QStringLiteral("historique.annuler"), {}).ok());
        CHECK(clipCount(timeline, v1) == 1);
        REQUIRE(porte.call(client, QStringLiteral("historique.refaire"), {}).ok());
        CHECK(clipCount(timeline, v1) == 2);
        CHECK(code(porte.call(client, QStringLiteral("timeline.outil"), {{QStringLiteral("nom"), QStringLiteral("faire_le_cafe")}})) == QStringLiteral("outil_inconnu"));
        CHECK(porte.call(client, QStringLiteral("timeline.outils"), {}).result.toArray().size() >= KoraCutAgentTools::implementedTools().size());
    }

    SECTION("Un tour groupe trois commandes en une étape")
    {
        REQUIRE(porte.call(client, QStringLiteral("historique.tour"), {{QStringLiteral("faire"), QStringLiteral("debut")}, {QStringLiteral("titre"), QStringLiteral("trois coupes")}}).ok());
        CHECK(code(porte.call(client, QStringLiteral("historique.tour"), {{QStringLiteral("faire"), QStringLiteral("debut")}})) == QStringLiteral("tour_ouvert"));
        for (int pos : {10, 20, 30}) {
            const int target = timeline->getClipByPosition(v1, pos);
            REQUIRE(porte.call(client, QStringLiteral("timeline.outil"), {{QStringLiteral("nom"), QStringLiteral("couper_clip")}, {QStringLiteral("args"), QJsonObject{{QStringLiteral("clip_id"), target}, {QStringLiteral("position"), pos}}}}).ok());
        }
        CHECK(clipCount(timeline, v1) == 4);
        CHECK(undoStack->count() == 0); // rien dans la pile tant que le tour est ouvert
        const KoraCutPorte::Reply end = porte.call(client, QStringLiteral("historique.tour"), {{QStringLiteral("faire"), QStringLiteral("fin")}});
        REQUIRE(end.ok());
        CHECK(end.result.toObject().value(QStringLiteral("actions")).toInt() == 3);
        REQUIRE(undoStack->count() == 1);
        CHECK(undoStack->text(0) == QStringLiteral("test : trois coupes"));
        undoStack->undo();
        CHECK(clipCount(timeline, v1) == 1);
        undoStack->redo();
        CHECK(clipCount(timeline, v1) == 4);
        CHECK(code(porte.call(client, QStringLiteral("historique.tour"), {{QStringLiteral("faire"), QStringLiteral("fin")}})) == QStringLiteral("aucun_tour"));
        // Abandon : tout est défait, l'étape reste à rétablir.
        REQUIRE(porte.call(client, QStringLiteral("historique.tour"), {{QStringLiteral("faire"), QStringLiteral("debut")}, {QStringLiteral("titre"), QStringLiteral("essai")}}).ok());
        REQUIRE(porte.call(client, QStringLiteral("timeline.outil"), {{QStringLiteral("nom"), QStringLiteral("couper_clip")}, {QStringLiteral("args"), QJsonObject{{QStringLiteral("clip_id"), timeline->getClipByPosition(v1, 5)}, {QStringLiteral("position"), 5}}}}).ok());
        CHECK(clipCount(timeline, v1) == 5);
        REQUIRE(porte.call(client, QStringLiteral("historique.tour"), {{QStringLiteral("faire"), QStringLiteral("abandon")}}).ok());
        CHECK(clipCount(timeline, v1) == 4);
        CHECK(undoStack->count() == 2);
        CHECK(undoStack->canRedo());
    }

    SECTION("Les fichiers restent dans le projet, et le chutier suit")
    {
        const KoraCutPorte::Reply listed = porte.call(client, QStringLiteral("fichiers.lister"), {{QStringLiteral("dossier"), QStringLiteral("Rushs")}});
        REQUIRE(listed.ok());
        const QJsonArray entries = listed.result.toObject().value(QStringLiteral("entrees")).toArray();
        REQUIRE(entries.size() == 1);
        CHECK(entries.first().toObject().value(QStringLiteral("media_id")).toString() == rushId);
        CHECK(entries.first().toObject().value(QStringLiteral("type")).toString() == QStringLiteral("video"));
        CHECK(code(porte.call(client, QStringLiteral("fichiers.lister"), {{QStringLiteral("dossier"), QStringLiteral("..")}})) == QStringLiteral("hors_projet"));
        CHECK(code(porte.call(client, QStringLiteral("fichiers.renommer"), {{QStringLiteral("chemin"), QStringLiteral("../ailleurs.txt")}, {QStringLiteral("nouveau_nom"), QStringLiteral("x.txt")}})) == QStringLiteral("hors_projet"));
        CHECK(code(porte.call(client, QStringLiteral("fichiers.renommer"), {{QStringLiteral("chemin"), QStringLiteral("/etc/hosts")}, {QStringLiteral("nouveau_nom"), QStringLiteral("x")}})) == QStringLiteral("hors_projet"));
        CHECK(code(porte.call(client, QStringLiteral("fichiers.renommer"), {{QStringLiteral("chemin"), QStringLiteral("Rushs/entretien.mkv")}, {QStringLiteral("nouveau_nom"), QStringLiteral("a/b.mkv")}})) == QStringLiteral("parametres_invalides"));
        CHECK(code(porte.call(client, QStringLiteral("fichiers.renommer"), {{QStringLiteral("chemin"), QStringLiteral("Rushs/absent.mkv")}, {QStringLiteral("nouveau_nom"), QStringLiteral("b.mkv")}})) == QStringLiteral("fichier_introuvable"));

        const int before = undoStack->count();
        const KoraCutPorte::Reply renamed = porte.call(client, QStringLiteral("fichiers.renommer"), {{QStringLiteral("chemin"), QStringLiteral("Rushs/entretien.mkv")}, {QStringLiteral("nouveau_nom"), QStringLiteral("01 - entretien Ngor.mkv")}});
        REQUIRE(renamed.ok());
        const QString renamedPath = project.path() + QStringLiteral("/Rushs/01 - entretien Ngor.mkv");
        CHECK(QFileInfo::exists(renamedPath));
        CHECK_FALSE(QFileInfo::exists(rush));
        CHECK(renamed.result.toObject().value(QStringLiteral("medias_suivis")).toInt() == 1);
        auto clip = binModel->getClipByBinID(rushId);
        REQUIRE(clip);
        CHECK(clip->url() == renamedPath);
        CHECK(clip->clipName() == QStringLiteral("01 - entretien Ngor.mkv"));
        CHECK(undoStack->count() == before + 1);
        undoStack->undo();
        CHECK(QFileInfo::exists(rush));
        CHECK(clip->url() == rush);
        undoStack->redo();
        CHECK(clip->url() == renamedPath);

        // Déplacer un dossier entier : le clip suit aussi.
        REQUIRE(porte.call(client, QStringLiteral("fichiers.creer_dossier"), {{QStringLiteral("chemin"), QStringLiteral("Montage")}}).ok());
        CHECK(QFileInfo(project.path() + QStringLiteral("/Montage")).isDir());
        REQUIRE(porte.call(client, QStringLiteral("fichiers.deplacer"), {{QStringLiteral("chemin"), QStringLiteral("Rushs")}, {QStringLiteral("dossier"), QStringLiteral("Montage")}}).ok());
        CHECK(clip->url() == project.path() + QStringLiteral("/Montage/Rushs/01 - entretien Ngor.mkv"));
        CHECK(code(porte.call(client, QStringLiteral("fichiers.deplacer"), {{QStringLiteral("chemin"), QStringLiteral("Montage")}, {QStringLiteral("dossier"), QStringLiteral("Montage/Rushs")}})) == QStringLiteral("parametres_invalides"));
        // La corbeille refuse un média du chutier, et sans accord rien ne part.
        CHECK(code(porte.call(client, QStringLiteral("fichiers.corbeille"), {{QStringLiteral("chemin"), QStringLiteral("Montage/Rushs/01 - entretien Ngor.mkv")}})) == QStringLiteral("media_utilise"));
        QFile note(project.path() + QStringLiteral("/notes.txt"));
        REQUIRE(note.open(QIODevice::WriteOnly));
        note.write("x");
        note.close();
        const KoraCutPorte::Reply trash = porte.call(client, QStringLiteral("fichiers.corbeille"), {{QStringLiteral("chemin"), QStringLiteral("notes.txt")}});
        CHECK(code(trash) == QStringLiteral("confirmation_requise"));
        CHECK(QFileInfo::exists(project.path() + QStringLiteral("/notes.txt")));
        CHECK(porte.pendingConfirmations().size() == 1);
        porte.answerConfirmation(porte.pendingConfirmations().first(), false);
        CHECK(QFileInfo::exists(project.path() + QStringLiteral("/notes.txt")));
    }

    SECTION("Un outil sensible attend l'accord, et l'accord rejoue l'appel")
    {
        CHECK(KoraCutPorteMethodes::confirmationTools().contains(QStringLiteral("supprimer_clip")));
        QString requested;
        QObject::connect(&porte, &KoraCutPorte::confirmationRequested, [&requested](const QString &id, const QString &, const QString &, const QJsonObject &, const QString &) { requested = id; });
        const QJsonObject args{{QStringLiteral("nom"), QStringLiteral("supprimer_clip")}, {QStringLiteral("args"), QJsonObject{{QStringLiteral("clip_ids"), QJsonArray{cid}}}}};
        const KoraCutPorte::Reply gate = porte.call(client, QStringLiteral("timeline.outil"), args);
        CHECK(code(gate) == QStringLiteral("confirmation_requise"));
        CHECK(gate.error.value(QStringLiteral("demande_id")).toString() == requested);
        CHECK(clipCount(timeline, v1) == 1);
        // Rappeler avec un id pas encore accordé : toujours en attente.
        QJsonObject again = args;
        again.insert(QStringLiteral("confirmation"), requested);
        const KoraCutPorte::Reply pending = porte.call(client, QStringLiteral("timeline.outil"), again);
        CHECK(code(pending) == QStringLiteral("confirmation_requise"));
        CHECK(pending.error.value(QStringLiteral("demande_id")).toString() == requested); // la même demande, pas une deuxième
        CHECK(porte.pendingConfirmations().size() == 1);
        porte.answerConfirmation(requested, true);
        CHECK(clipCount(timeline, v1) == 0);
        CHECK(undoStack->count() == 1);
        // Le même appel rejoué avec la demande accordée rend la même réponse, sans refaire.
        const KoraCutPorte::Reply replay = porte.call(client, QStringLiteral("timeline.outil"), again);
        INFO(QString::fromUtf8(QJsonDocument(replay.error).toJson(QJsonDocument::Compact)).toStdString());
        CHECK(replay.ok());
        CHECK(undoStack->count() == 1);
        // Un client de confiance passe sans demander.
        int c2 = -1;
        REQUIRE(timeline->requestClipInsertion(red, v1, 0, c2, true, true, false));
        QString second;
        QObject::connect(&porte, &KoraCutPorte::confirmationRequested, [&second](const QString &id, const QString &, const QString &, const QJsonObject &, const QString &) { second = id; });
        const KoraCutPorte::Reply gate2 = porte.call(client, QStringLiteral("timeline.outil"), {{QStringLiteral("nom"), QStringLiteral("supprimer_clip")}, {QStringLiteral("args"), QJsonObject{{QStringLiteral("clip_ids"), QJsonArray{c2}}}}});
        CHECK(code(gate2) == QStringLiteral("confirmation_requise"));
        porte.answerConfirmation(second, true, true);
        CHECK(clipCount(timeline, v1) == 0);
        int c3 = -1;
        REQUIRE(timeline->requestClipInsertion(red, v1, 0, c3, true, true, false));
        const KoraCutPorte::Reply trusted = porte.call(client, QStringLiteral("timeline.outil"), {{QStringLiteral("nom"), QStringLiteral("supprimer_clip")}, {QStringLiteral("args"), QJsonObject{{QStringLiteral("clip_ids"), QJsonArray{c3}}}}});
        CHECK(trusted.ok());
        CHECK(clipCount(timeline, v1) == 0);
        // Quitter demande toujours, même en confiance.
        CHECK(code(porte.call(client, QStringLiteral("app.quitter"), {})) == QStringLiteral("confirmation_requise"));
    }

    SECTION("Un tour survit à la connexion : koracut-ctl revient à chaque commande")
    {
        QTemporaryDir config;
        REQUIRE(porte.listen(QStringLiteral("koracut-test-tour-%1").arg(QCoreApplication::applicationPid()), config.path()));
        const auto session = [&porte](const QString &method, const QJsonObject &params) {
            QLocalSocket socket;
            socket.connectToServer(porte.socketPath());
            REQUIRE(socket.waitForConnected(2000));
            roundTrip(socket, QJsonObject{{QStringLiteral("id"), 0},
                                          {QStringLiteral("method"), QStringLiteral("porte.ouvrir")},
                                          {QStringLiteral("params"), QJsonObject{{QStringLiteral("jeton"), porte.token()}, {QStringLiteral("client"), QStringLiteral("ctl essai")}}}});
            const QJsonObject reply = roundTrip(socket, QJsonObject{{QStringLiteral("id"), 1}, {QStringLiteral("method"), method}, {QStringLiteral("params"), params}});
            socket.disconnectFromServer();
            pump([&porte]() { return porte.clientCount() == 0; });
            return reply;
        };
        CHECK(session(QStringLiteral("historique.tour"), {{QStringLiteral("faire"), QStringLiteral("debut")}, {QStringLiteral("titre"), QStringLiteral("deux coupes")}}).contains(QStringLiteral("result")));
        for (int pos : {10, 20}) {
            const int target = timeline->getClipByPosition(v1, pos);
            CHECK(session(QStringLiteral("timeline.outil"), {{QStringLiteral("nom"), QStringLiteral("couper_clip")}, {QStringLiteral("args"), QJsonObject{{QStringLiteral("clip_id"), target}, {QStringLiteral("position"), pos}}}}).contains(QStringLiteral("result")));
        }
        CHECK(clipCount(timeline, v1) == 3);
        CHECK(undoStack->count() == 0);
        const QJsonObject end = session(QStringLiteral("historique.tour"), {{QStringLiteral("faire"), QStringLiteral("fin")}});
        CHECK(end.value(QStringLiteral("result")).toObject().value(QStringLiteral("actions")).toInt() == 2);
        REQUIRE(undoStack->count() == 1);
        CHECK(undoStack->text(0) == QStringLiteral("ctl essai : deux coupes"));
        // La confiance suit aussi le nom : accordée une fois, la commande suivante passe.
        const QJsonObject gate = session(QStringLiteral("timeline.outil"), {{QStringLiteral("nom"), QStringLiteral("supprimer_clip")}, {QStringLiteral("args"), QJsonObject{{QStringLiteral("clip_ids"), QJsonArray{timeline->getClipByPosition(v1, 5)}}}}});
        const QString id = gate.value(QStringLiteral("error")).toObject().value(QStringLiteral("demande_id")).toString();
        REQUIRE_FALSE(id.isEmpty());
        porte.answerConfirmation(id, true, true);
        CHECK(clipCount(timeline, v1) == 2);
        // Rejouer avec la demande accordée, depuis une nouvelle connexion du même nom : la même réponse.
        const QJsonObject replay = session(QStringLiteral("timeline.outil"), {{QStringLiteral("nom"), QStringLiteral("supprimer_clip")}, {QStringLiteral("confirmation"), id}, {QStringLiteral("args"), QJsonObject{{QStringLiteral("clip_ids"), QJsonArray{-1}}}}});
        CHECK(replay.contains(QStringLiteral("result")));
        CHECK(clipCount(timeline, v1) == 2);
        const QJsonObject trusted = session(QStringLiteral("timeline.outil"), {{QStringLiteral("nom"), QStringLiteral("supprimer_clip")}, {QStringLiteral("args"), QJsonObject{{QStringLiteral("clip_ids"), QJsonArray{timeline->getClipByPosition(v1, 15)}}}}});
        CHECK(trusted.contains(QStringLiteral("result")));
        CHECK(clipCount(timeline, v1) == 1);
        porte.close();
    }

    SECTION("Ce qui a besoin de la fenêtre le dit")
    {
        CHECK(code(porte.call(client, QStringLiteral("ui.panneaux"), {})) == QStringLiteral("non_disponible"));
        CHECK(code(porte.call(client, QStringLiteral("timeline.tete"), {})) == QStringLiteral("non_disponible"));
        CHECK(porte.call(client, QStringLiteral("app.ping"), {}).ok());
        CHECK(porte.call(client, QStringLiteral("app.version"), {}).result.toObject().value(QStringLiteral("application")).toString() == QStringLiteral("KoraCut"));
        const KoraCutPorte::Reply sel = porte.call(client, QStringLiteral("timeline.selection"), {{QStringLiteral("clip_ids"), QJsonArray{cid}}});
        REQUIRE(sel.ok());
        CHECK(sel.result.toObject().value(QStringLiteral("clip_ids")).toArray().size() == 1);
        const KoraCutPorte::Reply info = porte.call(client, QStringLiteral("medias.info"), {{QStringLiteral("media_id"), rushId}});
        REQUIRE(info.ok());
        CHECK(info.result.toObject().value(QStringLiteral("chemin")).toString() == rush);
        CHECK(code(porte.call(client, QStringLiteral("medias.info"), {{QStringLiteral("media_id"), QStringLiteral("999")}})) == QStringLiteral("media_introuvable"));
    }

    SECTION("Lot 3 : les séquences, une étape d'annulation chacune")
    {
        const KoraCutPorte::Reply listed = porte.call(client, QStringLiteral("projet.sequences"), {});
        REQUIRE(listed.ok());
        REQUIRE(listed.result.toObject().value(QStringLiteral("sequences")).toArray().size() == 1);
        const int before = undoStack->count();
        const KoraCutPorte::Reply created = porte.call(client, QStringLiteral("projet.sequence"),
                                                       {{QStringLiteral("faire"), QStringLiteral("creer")}, {QStringLiteral("nom"), QStringLiteral("FR 9:16")}, {QStringLiteral("pistes_video"), 1},
                                                        {QStringLiteral("pistes_audio"), 1}, {QStringLiteral("largeur"), 1080}, {QStringLiteral("hauteur"), 1920}});
        INFO(QString::fromUtf8(QJsonDocument(created.error).toJson(QJsonDocument::Compact)).toStdString());
        REQUIRE(created.ok());
        const QJsonObject seq = created.result.toObject();
        CHECK(seq.value(QStringLiteral("nom")).toString() == QStringLiteral("FR 9:16"));
        CHECK(seq.value(QStringLiteral("pistes_video")).toInt() == 1);
        CHECK(seq.value(QStringLiteral("note")).toString().contains(QStringLiteral("un seul profil"))); // le format demandé diffère : on le dit
        CHECK(undoStack->count() == before + 1);
        CHECK(document.getTimelinesUuids().size() == 2);
        const QString uuid = seq.value(QStringLiteral("uuid")).toString();
        // Renommer par le nom, puis retrouver par l'uuid.
        const KoraCutPorte::Reply renamed = porte.call(client, QStringLiteral("projet.sequence"), {{QStringLiteral("faire"), QStringLiteral("renommer")}, {QStringLiteral("sequence"), QStringLiteral("fr 9:16")}, {QStringLiteral("nom"), QStringLiteral("EN 9:16")}});
        REQUIRE(renamed.ok());
        CHECK(renamed.result.toObject().value(QStringLiteral("avant")).toString() == QStringLiteral("FR 9:16"));
        CHECK(undoStack->count() == before + 2);
        auto clip = binModel->getClipByBinID(seq.value(QStringLiteral("media_id")).toString());
        REQUIRE(clip);
        CHECK(clip->clipName() == QStringLiteral("EN 9:16"));
        undoStack->undo();
        CHECK(clip->clipName() == QStringLiteral("FR 9:16"));
        undoStack->redo();
        CHECK(clip->clipName() == QStringLiteral("EN 9:16"));
        CHECK(code(porte.call(client, QStringLiteral("projet.sequence"), {{QStringLiteral("faire"), QStringLiteral("renommer")}, {QStringLiteral("sequence"), QStringLiteral("nulle part")}, {QStringLiteral("nom"), QStringLiteral("x")}})) == QStringLiteral("sequence_introuvable"));
        const KoraCutPorte::Reply activated = porte.call(client, QStringLiteral("projet.sequence"), {{QStringLiteral("faire"), QStringLiteral("activer")}, {QStringLiteral("sequence"), uuid}});
        REQUIRE(activated.ok());
        CHECK(activated.result.toObject().value(QStringLiteral("courante")).toBool());
        // Supprimer demande l'accord ; la dernière séquence reste.
        CHECK(code(porte.call(client, QStringLiteral("projet.sequence"), {{QStringLiteral("faire"), QStringLiteral("supprimer")}, {QStringLiteral("sequence"), uuid}})) == QStringLiteral("confirmation_requise"));
        CHECK(document.getTimelinesUuids().size() == 2);
        porte.answerConfirmation(porte.pendingConfirmations().first(), true);
        CHECK(document.getTimelinesUuids().size() == 1);
        CHECK(code(porte.call(client, QStringLiteral("projet.sequence"), {{QStringLiteral("faire"), QStringLiteral("supprimer")}})) == QStringLiteral("sequence_unique"));
        pCore->projectManager()->testSetActiveTimeline(timeline);
    }

    SECTION("28 septembre 2026 : creer sans nombre de pistes reprend la séquence en cours ; medias.lister rend tout le chutier")
    {
        // projet.sequence passe par l'outil creer_sequence : les pistes de la séquence en cours.
        const int videoTracks = int(timeline->getTracksIds(false).size());
        const int audioTracks = int(timeline->getTracksIds(true).size());
        const KoraCutPorte::Reply created = porte.call(client, QStringLiteral("projet.sequence"), {{QStringLiteral("faire"), QStringLiteral("creer")}, {QStringLiteral("nom"), QStringLiteral("Version 9:16")}});
        INFO(QString::fromUtf8(QJsonDocument(created.error).toJson(QJsonDocument::Compact)).toStdString());
        REQUIRE(created.ok());
        const QJsonObject seq = created.result.toObject();
        CHECK(seq.value(QStringLiteral("pistes_reprises")).toBool());
        CHECK(seq.value(QStringLiteral("nom")).toString() == QStringLiteral("Version 9:16"));
        CHECK(seq.value(QStringLiteral("pistes_video")).toInt() == videoTracks);
        CHECK(seq.value(QStringLiteral("pistes_audio")).toInt() == audioTracks);
        // Le même nom deux fois : refusé, comme par l'outil.
        CHECK(code(porte.call(client, QStringLiteral("projet.sequence"), {{QStringLiteral("faire"), QStringLiteral("creer")}, {QStringLiteral("nom"), QStringLiteral("Version 9:16")}})) == QStringLiteral("existe_deja"));
        pCore->projectManager()->testSetActiveTimeline(timeline);

        // Un chutier de plus de 50 médias : la porte les rend tous (les plans de la V3 d'AFEBABA manquaient).
        for (int i = 0; i < 55; ++i) {
            REQUIRE_FALSE(KdenliveTests::createProducer(pCore->getProjectProfile(), "blue", binModel, 20, true).isEmpty());
        }
        const int all = int(binModel->getAllClipIds().size());
        REQUIRE(all > 55);
        const KoraCutPorte::Reply listed = porte.call(client, QStringLiteral("medias.lister"), {});
        REQUIRE(listed.ok());
        CHECK(listed.result.toObject().value(QStringLiteral("medias")).toArray().size() == all);
        CHECK(listed.result.toObject().value(QStringLiteral("total")).toInt() == all);
        CHECK_FALSE(listed.result.toObject().contains(QStringLiteral("tronque")));
        // Une page : elle dit qu'elle est coupée et où reprendre.
        const KoraCutPorte::Reply page = porte.call(client, QStringLiteral("medias.lister"), {{QStringLiteral("limite"), 10}});
        REQUIRE(page.ok());
        CHECK(page.result.toObject().value(QStringLiteral("medias")).toArray().size() == 10);
        CHECK(page.result.toObject().value(QStringLiteral("tronque")).toBool());
        CHECK(page.result.toObject().value(QStringLiteral("suite")).toInt() == 10);
        const KoraCutPorte::Reply next = porte.call(client, QStringLiteral("medias.lister"), {{QStringLiteral("limite"), 10}, {QStringLiteral("debut"), 10}});
        REQUIRE(next.ok());
        CHECK(next.result.toObject().value(QStringLiteral("medias")).toArray().first().toObject().value(QStringLiteral("id")) !=
              page.result.toObject().value(QStringLiteral("medias")).toArray().first().toObject().value(QStringLiteral("id")));
        // filtre (le nom de la porte) cherche comme recherche (celui de l'outil).
        const KoraCutPorte::Reply filtered = porte.call(client, QStringLiteral("medias.lister"), {{QStringLiteral("filtre"), QStringLiteral("version 9")}});
        REQUIRE(filtered.ok());
        CHECK(filtered.result.toObject().value(QStringLiteral("total")).toInt() == 1);
    }

    SECTION("Lot 3 : un chemin relatif part du projet, un doublon n'ouvre pas de fenêtre, un média se renomme")
    {
        // Le fichier est déjà dans le chutier : la porte répond avec son id, sans passer par la fenêtre
        // « Dupliquer » (qui bloquait la commande, Promo/LISEZ-MOI.md, point 3).
        const size_t clipsBefore = binModel->getAllClipIds().size();
        KoraCutPorte::Reply again;
        bool got = false;
        porte.callAsync(client, QStringLiteral("medias.importer"), {{QStringLiteral("chemins"), QJsonArray{QStringLiteral("Rushs/entretien.mkv")}}}, [&](const KoraCutPorte::Reply &r) {
            again = r;
            got = true;
        });
        REQUIRE(pump([&got]() { return got; }));
        REQUIRE(again.ok());
        const QJsonArray entries = again.result.toArray();
        REQUIRE(entries.size() == 1);
        CHECK(entries.first().toObject().value(QStringLiteral("existait")).toBool());
        CHECK(entries.first().toObject().value(QStringLiteral("media_id")).toString() == rushId);
        CHECK(binModel->getAllClipIds().size() == clipsBefore); // rien d'importé en double
        // Un chemin relatif qui n'existe pas le dit ; « ~ » se comprend.
        got = false;
        porte.callAsync(client, QStringLiteral("medias.importer"), {{QStringLiteral("chemins"), QJsonArray{QStringLiteral("Rushs/absent.mkv"), QStringLiteral("~/absent-koracut.mkv")}}}, [&](const KoraCutPorte::Reply &r) {
            again = r;
            got = true;
        });
        REQUIRE(pump([&got]() { return got; }));
        CHECK(code(again) == QStringLiteral("fichier_introuvable"));
        // Renommer dans le chutier : le fichier ne bouge pas, une étape d'annulation.
        const int before = undoStack->count();
        const KoraCutPorte::Reply renamed = porte.call(client, QStringLiteral("medias.renommer"), {{QStringLiteral("media_id"), rushId}, {QStringLiteral("nom"), QStringLiteral("Entretien Ngor")}});
        REQUIRE(renamed.ok());
        auto clip = binModel->getClipByBinID(rushId);
        CHECK(clip->clipName() == QStringLiteral("Entretien Ngor"));
        CHECK(clip->url() == rush);
        CHECK(undoStack->count() == before + 1);
        undoStack->undo();
        CHECK(clip->clipName() == QStringLiteral("entretien.mkv"));
        CHECK(code(porte.call(client, QStringLiteral("medias.renommer"), {{QStringLiteral("media_id"), QStringLiteral("999")}, {QStringLiteral("nom"), QStringLiteral("x")}})) == QStringLiteral("media_introuvable"));
    }

    SECTION("Lot 3 : les silences d'un média, lus du fichier")
    {
        // Amélie parle à 4,5 s et à 13 s (jeu d'essai de la coupe automatique) ; son micro entend Thomas
        // à -15 dB (0,5 s et 8,5 s). Avec un seuil à -10 dBFS, seule sa voix compte : un blanc de 8 s à 13 s.
        const QString wav = sourcesPath + QStringLiteral("/dataset/koracut-dialogue-amelie.wav");
        const QString wavId = KdenliveTests::createFileProducer(pCore->getProjectProfile(), binModel, wav);
        REQUIRE_FALSE(wavId.isEmpty());
        KoraCutPorte::Reply reply;
        bool got = false;
        porte.callAsync(client, QStringLiteral("medias.analyser"), {{QStringLiteral("media_id"), wavId}, {QStringLiteral("quoi"), QStringLiteral("silences")}, {QStringLiteral("options"), QJsonObject{{QStringLiteral("seuil_db"), -10}, {QStringLiteral("duree_min_ms"), 1000}}}},
                        [&](const KoraCutPorte::Reply &r) {
                            reply = r;
                            got = true;
                        });
        REQUIRE(pump([&got]() { return got; }, 30000));
        INFO(QString::fromUtf8(QJsonDocument(reply.error).toJson(QJsonDocument::Compact)).toStdString());
        REQUIRE(reply.ok());
        const QJsonObject o = reply.result.toObject();
        const int duration = o.value(QStringLiteral("duree")).toObject().value(QStringLiteral("i")).toInt();
        CHECK(duration > 25 * 14);
        CHECK(o.value(QStringLiteral("seuil_db")).toDouble() == -10.);
        CHECK(o.value(QStringLiteral("source")).toString() == QStringLiteral("fichier"));
        const QJsonArray silences = o.value(QStringLiteral("silences")).toArray();
        REQUIRE(!silences.isEmpty());
        bool between = false;
        for (const QJsonValue &v : silences) {
            const int start = v.toObject().value(QStringLiteral("debut")).toObject().value(QStringLiteral("i")).toInt();
            const int end = v.toObject().value(QStringLiteral("fin")).toObject().value(QStringLiteral("i")).toInt();
            CHECK(start >= 0);
            CHECK(end > start);
            CHECK(end <= duration);
            // Un silence qui couvre 9 s à 12 s : personne ne parle dans le micro d'Amélie entre ses deux répliques.
            if (start <= 25 * 9 && end >= 25 * 12) {
                between = true;
            }
        }
        CHECK(between);
        CHECK(code(porte.call(client, QStringLiteral("medias.analyser"), {{QStringLiteral("media_id"), wavId}, {QStringLiteral("quoi"), QStringLiteral("cafe")}})) == QStringLiteral("parametres_invalides"));
    }

    pCore->projectManager()->closeCurrentDocument(false, false);
}
