/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutporte.h"

#include "doc/docundostack.hpp"
#include "kdenlive_debug.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QEvent>
#include <QLineEdit>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QProgressDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#endif

// ---------------------------------------------------------------------------------------------
// La commande de tour

KoraCutPorteTurnCommand::KoraCutPorteTurnCommand(std::vector<QUndoCommand *> steps, const QString &text)
    : QUndoCommand(text)
    , m_steps(std::move(steps))
{
}

KoraCutPorteTurnCommand::~KoraCutPorteTurnCommand()
{
    qDeleteAll(m_steps);
}

void KoraCutPorteTurnCommand::undo()
{
    for (auto it = m_steps.rbegin(); it != m_steps.rend(); ++it) {
        (*it)->undo();
    }
    m_undone = true;
}

void KoraCutPorteTurnCommand::redo()
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
// Les structures internes

struct KoraCutPorte::Method
{
    QString name;
    QString summary;
    QList<Param> params;
    Handler handler;
    AsyncHandler asyncHandler;
    bool confirmation{false};
};

struct KoraCutPorte::Turn
{
    QString title;
    std::vector<QUndoCommand *> commands;
};

/** Ce qui survit à la connexion : le tour ouvert et la confiance, par nom de client. */
struct KoraCutPorte::ClientState
{
    bool trusted{false};
    std::unique_ptr<Turn> turn;
};

struct KoraCutPorte::Client
{
    quint64 id{0};
    QLocalSocket *socket{nullptr};
    QString name;
    bool authenticated{false};
    QSet<QString> subscriptions;
    QByteArray buffer;
    std::shared_ptr<ClientState> state;
};

struct KoraCutPorte::Demand
{
    QString id;
    quint64 clientId{0}; ///< la connexion qui a demandé (pour l'événement de réponse)
    QString clientName;  ///< le nom : c'est lui qui a le droit de rejouer l'appel
    QString method;
    QJsonObject params;
    QString what;
    bool always{false};
    bool answered{false};
    bool accepted{false};
    bool hasResult{false};
    Reply result;
};

namespace {

bool processAlive(qint64 pid)
{
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h) {
        return false;
    }
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
#else
    return ::kill(pid_t(pid), 0) == 0 || errno == EPERM;
#endif
}

const QStringList kEventTypes = {QStringLiteral("tete"),     QStringLiteral("selection"), QStringLiteral("projet"),
                                 QStringLiteral("timeline"), QStringLiteral("taches"),    QStringLiteral("fenetre"),
                                 QStringLiteral("journal"),  QStringLiteral("confirmation")};

int positionOf(const QJsonValue &v)
{
    if (v.isDouble()) {
        return v.toInt();
    }
    if (v.isObject()) {
        const QJsonObject o = v.toObject();
        if (o.contains(QStringLiteral("i"))) {
            return o.value(QStringLiteral("i")).toInt();
        }
    }
    return -1;
}

// Le temps concerné par un appel : « position » dans les paramètres, sinon dans le résultat.
int positionOfCall(const QJsonObject &params, const QJsonValue &result)
{
    int p = positionOf(params.value(QStringLiteral("position")));
    if (p < 0 && params.contains(QStringLiteral("args"))) {
        p = positionOf(params.value(QStringLiteral("args")).toObject().value(QStringLiteral("position")));
    }
    if (p < 0 && result.isObject()) {
        p = positionOf(result.toObject().value(QStringLiteral("position")));
    }
    return p;
}

QString resultSummary(const KoraCutPorte::Reply &reply)
{
    if (!reply.ok()) {
        return reply.error.value(QStringLiteral("message")).toString();
    }
    if (reply.result.isArray()) {
        return QStringLiteral("%1").arg(reply.result.toArray().size());
    }
    if (reply.result.isObject()) {
        const QJsonObject o = reply.result.toObject();
        for (const QString &key : {QStringLiteral("chemin"), QStringLiteral("courant"), QStringLiteral("texte"), QStringLiteral("nom")}) {
            if (o.contains(key) && o.value(key).isString()) {
                return o.value(key).toString();
            }
        }
        return QString();
    }
    if (reply.result.isString()) {
        return reply.result.toString();
    }
    return QString();
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Reply

KoraCutPorte::Reply KoraCutPorte::Reply::value(const QJsonValue &v)
{
    Reply r;
    r.result = v.isUndefined() ? QJsonValue(QJsonObject{{QStringLiteral("ok"), true}}) : v;
    return r;
}

KoraCutPorte::Reply KoraCutPorte::Reply::fail(const QString &code, const QString &message, const QString &advice, const QJsonObject &extra)
{
    Reply r;
    r.error = QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
    if (!advice.isEmpty()) {
        r.error.insert(QStringLiteral("conseil"), advice);
    }
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        r.error.insert(it.key(), it.value());
    }
    return r;
}

KoraCutPorte::Reply KoraCutPorte::Reply::fromTool(const QJsonObject &toolResult)
{
    if (toolResult.contains(QStringLiteral("ok")) && !toolResult.value(QStringLiteral("ok")).toBool(true)) {
        const QJsonObject e = toolResult.value(QStringLiteral("erreur")).toObject();
        return fail(e.value(QStringLiteral("code")).toString(QStringLiteral("erreur")), e.value(QStringLiteral("message")).toString(),
                    e.value(QStringLiteral("conseil")).toString());
    }
    return value(toolResult);
}

QJsonObject KoraCutPorte::Reply::toJson() const
{
    return ok() ? QJsonObject{{QStringLiteral("result"), result}} : QJsonObject{{QStringLiteral("error"), error}};
}

// ---------------------------------------------------------------------------------------------

KoraCutPorte::KoraCutPorte(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<KoraCutPorte::JournalEntry>("KoraCutPorte::JournalEntry");
    m_inner = std::make_unique<Client>();
    m_inner->id = 0;
    m_inner->name = QStringLiteral("Console");
    m_inner->authenticated = true;
    m_inner->state = stateFor(m_inner.get());
}

KoraCutPorte::~KoraCutPorte()
{
    close();
    for (auto &st : m_states) {
        if (st->turn) {
            qDeleteAll(st->turn->commands);
        }
    }
}

std::shared_ptr<KoraCutPorte::ClientState> KoraCutPorte::stateFor(Client *client)
{
    if (!client) {
        return nullptr;
    }
    const QString key = client->name.isEmpty() ? QStringLiteral("client %1").arg(client->id) : client->name;
    auto st = m_states.value(key);
    if (!st) {
        st = std::make_shared<ClientState>();
        m_states.insert(key, st);
    }
    client->state = st;
    return st;
}

// ---- Le registre

void KoraCutPorte::registerMethod(const QString &name, const QString &summary, const QList<Param> &params, Handler handler, bool confirmation)
{
    auto m = std::make_shared<Method>();
    m->name = name;
    m->summary = summary;
    m->params = params;
    m->handler = std::move(handler);
    m->confirmation = confirmation;
    if (!m_methods.contains(name)) {
        m_order << name;
    }
    m_methods.insert(name, m);
}

void KoraCutPorte::registerAsyncMethod(const QString &name, const QString &summary, const QList<Param> &params, AsyncHandler handler, bool confirmation)
{
    auto m = std::make_shared<Method>();
    m->name = name;
    m->summary = summary;
    m->params = params;
    m->asyncHandler = std::move(handler);
    m->confirmation = confirmation;
    if (!m_methods.contains(name)) {
        m_order << name;
    }
    m_methods.insert(name, m);
}

bool KoraCutPorte::hasMethod(const QString &name) const
{
    return m_methods.contains(name);
}

QStringList KoraCutPorte::methodNames() const
{
    return m_order;
}

QJsonArray KoraCutPorte::catalogue() const
{
    QJsonArray list;
    for (const QString &name : m_order) {
        const auto m = m_methods.value(name);
        QJsonArray params;
        for (const Param &p : m->params) {
            params.append(QJsonObject{{QStringLiteral("nom"), p.name},
                                      {QStringLiteral("type"), p.type},
                                      {QStringLiteral("description"), p.description},
                                      {QStringLiteral("requis"), p.required}});
        }
        list.append(QJsonObject{{QStringLiteral("nom"), name},
                                {QStringLiteral("resume"), m->summary},
                                {QStringLiteral("confirmation"), m->confirmation},
                                {QStringLiteral("params"), params}});
    }
    return list;
}

// ---- La prise

QString KoraCutPorte::defaultConfigDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/koracut");
}

bool KoraCutPorte::listen(const QString &reglage, const QString &configDir)
{
    close();
    m_reglage = reglage.isEmpty() ? QStringLiteral("koracutrc") : reglage;
    m_configDir = configDir.isEmpty() ? defaultConfigDir() : configDir;
    QDir().mkpath(m_configDir);
    m_porteFile = m_configDir + (m_reglage == QLatin1String("koracutrc") ? QStringLiteral("/porte.json") : QStringLiteral("/porte-%1.json").arg(m_reglage));
    sweepStaleFiles();
    if (m_token.isEmpty()) {
        m_token = QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-')) +
                  QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-')).left(16);
    }
    if (!m_enabled) {
        writePorteFile();
        return false;
    }
    const QString name = QStringLiteral("koracut-%1").arg(m_reglage);
    // Une autre KoraCut vivante sur le même réglage tient déjà la porte : on ne lui vole pas sa prise
    // (removeServer l'effaçait, et les commandes partaient vers une fenêtre qui ne les recevait plus).
    // Son porte.json reste le bon ; celle-ci reste sans porte.
    {
        QLocalSocket probe;
        probe.connectToServer(name);
        if (probe.waitForConnected(300)) {
            probe.abort();
            qCWarning(KDENLIVE_LOG) << "KoraCut: une autre KoraCut ouverte sur le réglage" << m_reglage << "tient déjà la porte, celle-ci reste fermée.";
            m_porteFile.clear();
            return false;
        }
    }
    QLocalServer::removeServer(name);
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(name)) {
        qCWarning(KDENLIVE_LOG) << "KoraCut: la porte ne s'ouvre pas :" << m_server->errorString();
        delete m_server;
        m_server = nullptr;
        writePorteFile();
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, &KoraCutPorte::onNewConnection);
    writePorteFile();
    Q_EMIT listeningChanged(true);
    return true;
}

void KoraCutPorte::close()
{
    const bool was = m_server != nullptr;
    for (auto &c : m_clients) {
        if (c->socket) {
            c->socket->disconnect(this);
            c->socket->abort();
            c->socket->deleteLater();
            c->socket = nullptr;
        }
    }
    m_clients.clear();
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
    removePorteFile();
    if (was) {
        Q_EMIT listeningChanged(false);
    }
}

bool KoraCutPorte::isListening() const
{
    return m_server && m_server->isListening();
}

QString KoraCutPorte::socketPath() const
{
    return m_server ? m_server->fullServerName() : QString();
}

QString KoraCutPorte::token() const
{
    return m_token;
}

void KoraCutPorte::setEnabled(bool enabled)
{
    if (m_enabled == enabled) {
        return;
    }
    m_enabled = enabled;
    if (m_reglage.isEmpty()) {
        return;
    }
    if (enabled) {
        listen(m_reglage, m_configDir);
    } else {
        close();
        writePorteFile();
    }
}

int KoraCutPorte::clientCount() const
{
    return int(m_clients.size());
}

void KoraCutPorte::writePorteFile()
{
    if (m_porteFile.isEmpty()) {
        return;
    }
    QJsonObject o{{QStringLiteral("version"), 1},
                  {QStringLiteral("application"), QStringLiteral("KoraCut %1").arg(QCoreApplication::applicationVersion())},
                  {QStringLiteral("reglage"), m_reglage},
                  {QStringLiteral("ouverte"), isListening()},
                  {QStringLiteral("pid"), double(QCoreApplication::applicationPid())},
                  {QStringLiteral("depuis"), QDateTime::currentDateTime().toString(Qt::ISODate)}};
    if (isListening()) {
        o.insert(QStringLiteral("prise"), socketPath());
        o.insert(QStringLiteral("jeton"), m_token);
    }
    QFile f(m_porteFile);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(KDENLIVE_LOG) << "KoraCut: porte.json ne s'écrit pas dans" << m_porteFile;
        return;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.close();
    f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

void KoraCutPorte::sweepStaleFiles()
{
    // Une KoraCut tuée sans passer par ses destructeurs laisse son porte.json : le pid mort le dit.
    const QFileInfoList files = QDir(m_configDir).entryInfoList({QStringLiteral("porte*.json")}, QDir::Files);
    for (const QFileInfo &info : files) {
        QFile f(info.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        const qint64 pid = qint64(QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("pid")).toDouble());
        f.close();
        if (pid > 0 && pid != QCoreApplication::applicationPid() && !processAlive(pid)) {
            QFile::remove(info.absoluteFilePath());
        }
    }
}

void KoraCutPorte::removePorteFile()
{
    if (!m_porteFile.isEmpty()) {
        QFile::remove(m_porteFile);
    }
}

// ---- Les clients

KoraCutPorte::Client *KoraCutPorte::clientById(quint64 id) const
{
    if (id == 0) {
        return m_inner.get();
    }
    for (const auto &c : m_clients) {
        if (c->id == id) {
            return c.get();
        }
    }
    return nullptr;
}

void KoraCutPorte::onNewConnection()
{
    while (m_server && m_server->hasPendingConnections()) {
        QLocalSocket *socket = m_server->nextPendingConnection();
        if (!socket) {
            break;
        }
        auto client = std::make_unique<Client>();
        client->id = m_nextClient++;
        client->socket = socket;
        const quint64 id = client->id;
        m_clients.push_back(std::move(client));
        // Les signaux retrouvent le client par son id, jamais par un pointeur : une notification de
        // lecture peut encore arriver entre l'effacement du client et la destruction de la prise
        // (deleteLater), et elle lisait alors un client détruit (plantage du 24 septembre 2026, 14 h 56).
        connect(socket, &QLocalSocket::readyRead, this, [this, id]() {
            if (Client *c = clientById(id)) {
                onReadyRead(c);
            }
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, id]() {
            if (Client *c = clientById(id)) {
                onDisconnected(c);
            }
        });
    }
}

void KoraCutPorte::onReadyRead(Client *client)
{
    if (!client->socket) {
        return;
    }
    client->buffer += client->socket->readAll();
    // Une ligne par message ; ce qui reste attend la suite. Le client peut disparaître pendant le
    // traitement (jeton refusé, ou koracut-ctl qui raccroche pendant qu'une fenêtre bloque) : on ne
    // garde que son id, jamais le pointeur.
    const quint64 id = client->id;
    int nl = client->buffer.indexOf('\n');
    while (nl >= 0) {
        const QByteArray line = client->buffer.left(nl).trimmed();
        client->buffer.remove(0, nl + 1);
        if (!line.isEmpty()) {
            handleLine(client, line);
        }
        client = clientById(id);
        if (!client || !client->socket) {
            return;
        }
        nl = client->buffer.indexOf('\n');
    }
    if (client->buffer.size() > 64 * 1024 * 1024) {
        client->buffer.clear();
    }
}

void KoraCutPorte::onDisconnected(Client *client)
{
    const QString name = client->name;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it->get() == client) {
            // Un tour ouvert reste ouvert : koracut-ctl ferme la connexion après chaque commande et
            // revient sous le même nom pour dire faire=fin.
            if (client->socket) {
                client->socket->disconnect(this);
                retireSocket(client->socket);
                client->socket = nullptr;
            }
            m_clients.erase(it);
            break;
        }
    }
    if (!name.isEmpty()) {
        Q_EMIT clientClosed(name);
    }
}

// Une commande qui a ouvert une fenêtre modale tourne encore, plus haut dans la pile, DANS le signal
// de lecture de la prise de son client. Ce client a reçu `fenetre_ouverte` et raccroche : détruire sa
// prise tout de suite (deleteLater est servi par la boucle de la fenêtre) la retirait sous les pieds
// de Qt, et l'application plantait à la fermeture de la fenêtre (1er octobre 2026). Tant qu'une
// commande tourne, la prise attend.
void KoraCutPorte::retireSocket(QLocalSocket *socket)
{
    if (!socket) {
        return;
    }
    if (m_running > 0) {
        m_retiredSockets << socket;
    } else {
        socket->deleteLater();
    }
}

void KoraCutPorte::releaseRetiredSockets()
{
    if (m_running > 0) {
        return;
    }
    for (QLocalSocket *socket : std::as_const(m_retiredSockets)) {
        socket->deleteLater();
    }
    m_retiredSockets.clear();
}

void KoraCutPorte::send(Client *client, const QJsonObject &message)
{
    if (!client || !client->socket || client->socket->state() != QLocalSocket::ConnectedState) {
        return;
    }
    // L'écriture peut constater tout de suite que l'autre bout a raccroché : « disconnected » part
    // alors pendant write(), onDisconnected efface le client, et client->socket ne se relit plus.
    const QPointer<QLocalSocket> socket = client->socket;
    socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
    if (socket) {
        socket->flush();
    }
}

void KoraCutPorte::handleLine(Client *client, const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (!doc.isObject()) {
        send(client, QJsonObject{{QStringLiteral("id"), QJsonValue()},
                                 {QStringLiteral("error"), Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("La ligne n'est pas un objet JSON : %1").arg(parseError.errorString())).error}});
        return;
    }
    const QJsonObject req = doc.object();
    const QJsonValue id = req.value(QStringLiteral("id"));
    const QString method = req.value(QStringLiteral("method")).toString();
    const QJsonValue params = req.value(QStringLiteral("params"));
    if (!params.isUndefined() && !params.isNull() && !params.isObject()) {
        send(client, QJsonObject{{QStringLiteral("id"), id},
                                 {QStringLiteral("error"), Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Les paramètres sont toujours un objet (nommés, jamais positionnels).")).error}});
        return;
    }
    dispatch(client, id, method, params.toObject());
}

void KoraCutPorte::dispatch(Client *client, const QJsonValue &id, const QString &method, const QJsonObject &params)
{
    // La réponse retrouve le client par son id : s'il a raccroché entre-temps (fenêtre modale qui a
    // fait attendre koracut-ctl au-delà de ses deux minutes), rien n'est écrit sur un objet détruit.
    const quint64 clientId = client->id;
    const auto respond = [this, clientId, id](const Reply &reply) {
        QJsonObject msg = reply.toJson();
        msg.insert(QStringLiteral("id"), id);
        send(clientById(clientId), msg);
    };
    if (!client->authenticated) {
        if (method != QLatin1String("porte.ouvrir") || params.value(QStringLiteral("jeton")).toString() != m_token || m_token.isEmpty()) {
            respond(Reply::fail(QStringLiteral("jeton_refuse"), QStringLiteral("Le premier message doit être porte.ouvrir avec le jeton de porte.json."),
                                QStringLiteral("Lis porte.json dans le dossier de configuration de KoraCut, ou la variable KORACUT_PORTE.")));
            // respond() a pu voir l'autre bout raccrocher et effacer le client : on le cherche de nouveau.
            if (Client *c = clientById(clientId); c && c->socket) {
                c->socket->disconnectFromServer();
            }
            return;
        }
        client->authenticated = true;
        client->name = params.value(QStringLiteral("client")).toString().trimmed();
        if (client->name.isEmpty()) {
            client->name = QStringLiteral("client %1").arg(client->id);
        }
        stateFor(client);
        Q_EMIT clientOpened(client->name);
    }
    if (method == QLatin1String("porte.ouvrir")) {
        QJsonArray names;
        for (const QString &n : m_order) {
            names.append(n);
        }
        const QString project = m_projectProvider ? m_projectProvider() : QString();
        respond(Reply::value(QJsonObject{{QStringLiteral("application"), QStringLiteral("KoraCut")},
                                         {QStringLiteral("version"), QCoreApplication::applicationVersion()},
                                         {QStringLiteral("protocole"), 1},
                                         {QStringLiteral("reglage"), m_reglage},
                                         {QStringLiteral("projet"), project.isEmpty() ? QJsonValue() : QJsonValue(project)},
                                         {QStringLiteral("methodes"), names}}));
        return;
    }
    if (method == QLatin1String("porte.methodes")) {
        respond(Reply::value(catalogue()));
        return;
    }
    const auto m = m_methods.value(method);
    if (!m) {
        respond(Reply::fail(QStringLiteral("methode_inconnue"), QStringLiteral("Aucune méthode ne s'appelle « %1 ».").arg(method),
                            QStringLiteral("porte.methodes liste ce qui existe.")));
        return;
    }
    QStringList missing;
    for (const Param &p : m->params) {
        if (p.required && !params.contains(p.name)) {
            missing << p.name;
        }
    }
    if (!missing.isEmpty()) {
        respond(Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Il manque : %1.").arg(missing.join(QStringLiteral(", ")))));
        return;
    }
    Call call;
    call.client = client->name;
    call.clientId = client->id;
    call.method = method;
    call.params = params;
    execute(client, call, respond);
}

void KoraCutPorte::execute(Client *client, const Call &call, const Done &done)
{
    const auto m = m_methods.value(call.method);
    if (!m) {
        done(Reply::fail(QStringLiteral("methode_inconnue"), QStringLiteral("Aucune méthode ne s'appelle « %1 ».").arg(call.method)));
        return;
    }
    // Un appel rejoué avec une demande déjà exécutée : la même réponse, sans refaire.
    const QString given = call.params.value(QStringLiteral("confirmation")).toString();
    if (!given.isEmpty()) {
        const auto d = m_demands.value(given);
        if (d && d->hasResult && d->clientName == call.client) {
            done(d->result);
            return;
        }
    }
    // Une commande qui arrive pendant qu'une autre s'exécute (un chargement de projet fait tourner la
    // boucle d'événements) ou pendant qu'un projet se charge verrait un document à moitié construit,
    // ou pas de document du tout (plantages du 24 septembre 2026, 14 h 56 et 21 h 46 : la pile
    // d'annulation lue sur un document nul). On répond « occupe » avant de toucher à quoi que ce soit ;
    // koracut-ctl et le serveur MCP réessaient seuls. app.*, porte.* et ui.* restent libres, et sans projet
    // on peut toujours en ouvrir ou en créer un.
    {
        // ui.* aussi : c'est par là qu'on lit et qu'on ferme une fenêtre qui attend une réponse.
        const bool alwaysFree = call.method.startsWith(QLatin1String("app.")) || call.method.startsWith(QLatin1String("porte.")) ||
                                call.method.startsWith(QLatin1String("ui.")) || call.method.startsWith(QLatin1String("theme.")) ||
                                call.method.startsWith(QLatin1String("reglages."));
        const bool opensProject = call.method == QLatin1String("projet.ouvrir") || call.method == QLatin1String("projet.nouveau");
        if (!alwaysFree && (m_running > 0 || (!opensProject && m_busyProvider && m_busyProvider()))) {
            const Reply busy = Reply::fail(QStringLiteral("occupe"), QStringLiteral("KoraCut est en train de charger un projet ou d'exécuter une autre commande."),
                                           QStringLiteral("Réessaie dans une seconde (app.ping répond toujours)."));
            logCall(call, busy, 0);
            done(busy);
            return;
        }
    }
    const quint64 clientId = client ? client->id : 0;
    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    auto finished = std::make_shared<bool>(false);
    auto stack = undoStack();
    // Pendant un tour, les commandes d'annulation sont mises de côté pour la commande de tour.
    const bool collecting = client && client->state && client->state->turn && stack && !stack->isCollecting();
    if (collecting) {
        stack->beginCollecting();
    }
    const Done finish = [this, call, done, timer, finished, clientId](const Reply &reply) {
        if (*finished) {
            return;
        }
        *finished = true;
        logCall(call, reply, timer->elapsed());
        done(reply);
        Q_UNUSED(clientId)
    };
    // Aucune commande ne doit ouvrir une fenêtre qui bloque (lot 3). Deux filets :
    // 1. pendant la commande, l'application porte une propriété que le code de Kdenlive consulte
    //    avant d'ouvrir une fenêtre à choix (création des pistes audio manquantes) : il prend alors
    //    la réponse par défaut ;
    // 2. si une fenêtre modale s'ouvre quand même, son exec() fait tourner la boucle : ce minuteur à
    //    0 ms s'y déclenche, répond `fenetre_ouverte` avec la description de la fenêtre, et le
    //    client rend la main tout de suite. La vraie réponse, quand la fenêtre se ferme, est perdue
    //    (le client relit l'état) ; ce que la commande a fait reste annulable.
    QWidget *modalBefore = qApp ? QApplication::activeModalWidget() : nullptr;
    const QVariant noDialogBefore = qApp ? qApp->property(noDialogProperty()) : QVariant();
    if (qApp) {
        qApp->setProperty(noDialogProperty(), true);
        // Regardé tout de suite, puis toutes les 300 ms tant que la commande n'a pas répondu : une
        // fenêtre peut s'ouvrir tard (« Problèmes du clip » à la fin d'un chargement de projet, vu le
        // 24 septembre 2026 : projet.ouvrir restait bloqué plus de deux minutes). Une barre de
        // progression n'est pas une question : on la laisse travailler.
        auto watch = new QTimer(this);
        watch->setInterval(300);
        const auto check = [finish, finished, modalBefore, watch]() {
            if (*finished) {
                watch->stop();
                watch->deleteLater();
                return;
            }
            QWidget *modal = QApplication::activeModalWidget();
            if (!modal || modal == modalBefore || !modal->isVisible() || qobject_cast<QProgressDialog *>(modal)) {
                return;
            }
            watch->stop();
            watch->deleteLater();
            const QJsonObject window = describeWindow(modal);
            finish(Reply::fail(QStringLiteral("fenetre_ouverte"),
                               QStringLiteral("KoraCut a ouvert la fenêtre « %1 » et attend une réponse.").arg(window.value(QStringLiteral("titre")).toString()),
                               QStringLiteral("Lis fenetre (boutons, champs), réponds par ui.fenetre faire='bouton:<libellé>', puis relis l'état."),
                               QJsonObject{{QStringLiteral("fenetre"), window}}));
        };
        connect(watch, &QTimer::timeout, this, check);
        QTimer::singleShot(0, this, check);
        watch->start();
    }
    ++m_running;
    if (m->handler) {
        finish(m->handler(call));
    } else if (m->asyncHandler) {
        m->asyncHandler(call, finish);
    }
    --m_running;
    releaseRetiredSockets();
    if (qApp) {
        qApp->setProperty(noDialogProperty(), noDialogBefore);
    }
    if (collecting) {
        std::vector<QUndoCommand *> commands = stack->endCollecting();
        if (Client *c = clientById(clientId); c && c->state && c->state->turn) {
            c->state->turn->commands.insert(c->state->turn->commands.end(), commands.begin(), commands.end());
        } else {
            // Le tour s'est fermé pendant l'appel (historique.tour fin) : rien à garder de plus.
            qDeleteAll(commands);
        }
    }
}

void KoraCutPorte::logCall(const Call &call, const Reply &reply, qint64 ms)
{
    JournalEntry e;
    e.time = QDateTime::currentDateTime();
    e.client = call.client;
    e.method = call.method;
    e.params = call.params;
    e.ok = reply.ok();
    e.code = reply.ok() ? QString() : reply.error.value(QStringLiteral("code")).toString();
    e.summary = resultSummary(reply);
    e.ms = ms;
    e.position = positionOfCall(call.params, reply.result);
    Q_EMIT journal(e);
    QJsonObject params = call.params;
    params.remove(QStringLiteral("confirmation"));
    publish(QStringLiteral("journal"),
            QJsonObject{{QStringLiteral("client"), e.client},
                        {QStringLiteral("methode"), e.method},
                        {QStringLiteral("params"), params},
                        {QStringLiteral("ok"), e.ok},
                        {QStringLiteral("code"), e.code},
                        {QStringLiteral("duree_ms"), double(ms)}},
            call.clientId);
}

// ---- Appels de l'intérieur

KoraCutPorte::Reply KoraCutPorte::call(const QString &client, const QString &method, const QJsonObject &params)
{
    Reply out = Reply::fail(QStringLiteral("en_cours"), QStringLiteral("La méthode %1 répond plus tard : passe par callAsync.").arg(method));
    bool got = false;
    m_inner->name = client.isEmpty() ? QStringLiteral("Console") : client;
    stateFor(m_inner.get());
    if (!hasMethod(method)) {
        return Reply::fail(QStringLiteral("methode_inconnue"), QStringLiteral("Aucune méthode ne s'appelle « %1 ».").arg(method),
                           QStringLiteral("porte.methodes liste ce qui existe."));
    }
    QStringList missing;
    for (const Param &p : m_methods.value(method)->params) {
        if (p.required && !params.contains(p.name)) {
            missing << p.name;
        }
    }
    if (!missing.isEmpty()) {
        return Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Il manque : %1.").arg(missing.join(QStringLiteral(", "))));
    }
    Call c;
    c.client = m_inner->name;
    c.clientId = 0;
    c.method = method;
    c.params = params;
    execute(m_inner.get(), c, [&out, &got](const Reply &r) {
        out = r;
        got = true;
    });
    Q_UNUSED(got)
    return out;
}

void KoraCutPorte::callAsync(const QString &client, const QString &method, const QJsonObject &params, const Done &done)
{
    m_inner->name = client.isEmpty() ? QStringLiteral("Console") : client;
    stateFor(m_inner.get());
    if (method == QLatin1String("porte.methodes")) {
        done(Reply::value(catalogue()));
        return;
    }
    if (!hasMethod(method)) {
        done(Reply::fail(QStringLiteral("methode_inconnue"), QStringLiteral("Aucune méthode ne s'appelle « %1 ».").arg(method),
                         QStringLiteral("porte.methodes liste ce qui existe.")));
        return;
    }
    QStringList missing;
    for (const Param &p : m_methods.value(method)->params) {
        if (p.required && !params.contains(p.name)) {
            missing << p.name;
        }
    }
    if (!missing.isEmpty()) {
        done(Reply::fail(QStringLiteral("parametres_invalides"), QStringLiteral("Il manque : %1.").arg(missing.join(QStringLiteral(", ")))));
        return;
    }
    Call c;
    c.client = m_inner->name;
    c.clientId = 0;
    c.method = method;
    c.params = params;
    execute(m_inner.get(), c, done);
}

bool KoraCutPorte::parseCommandLine(const QString &line, QString &method, QJsonObject &params, QString &error)
{
    const QString text = line.trimmed();
    method.clear();
    params = QJsonObject();
    if (text.isEmpty()) {
        error = QStringLiteral("Rien à envoyer.");
        return false;
    }
    if (text.startsWith(QLatin1Char('{'))) {
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &pe);
        if (!doc.isObject()) {
            error = pe.errorString();
            return false;
        }
        const QJsonObject o = doc.object();
        method = o.value(QStringLiteral("method")).toString(o.value(QStringLiteral("methode")).toString());
        params = o.value(QStringLiteral("params")).toObject();
        if (method.isEmpty()) {
            error = QStringLiteral("Il manque « method ».");
            return false;
        }
        return true;
    }
    // Découpe en mots. Un guillemet (simple ou double) n'ouvre une citation qu'au début d'un mot ou
    // juste après « = » : args='{"a": 1}' et args={"a":[1]} marchent tous les deux.
    QStringList words;
    QString current;
    QChar quote;
    bool inWord = false;
    QChar previous;
    for (const QChar ch : text) {
        if (!quote.isNull()) {
            if (ch == quote) {
                quote = QChar();
            } else {
                current += ch;
            }
            previous = ch;
            continue;
        }
        const bool opensQuote = (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) && (!inWord || previous == QLatin1Char('='));
        if (opensQuote) {
            quote = ch;
            inWord = true;
            previous = ch;
            continue;
        }
        if (ch.isSpace()) {
            if (inWord) {
                words << current;
                current.clear();
                inWord = false;
            }
            previous = ch;
            continue;
        }
        current += ch;
        inWord = true;
        previous = ch;
    }
    if (inWord) {
        words << current;
    }
    if (words.isEmpty()) {
        error = QStringLiteral("Rien à envoyer.");
        return false;
    }
    if (words.first().contains(QLatin1Char('.'))) {
        method = words.takeFirst();
    } else {
        if (words.size() < 2 || words.at(1).contains(QLatin1Char('='))) {
            error = QStringLiteral("Écris « famille methode » ou « famille.methode », puis clef=valeur.");
            return false;
        }
        // Deux instructions : dans une seule expression, l'ordre des deux takeFirst() n'est pas garanti
        // par le C++ (le compilateur Windows rendait « etat.timeline », vu le 1er octobre 2026).
        const QString family = words.takeFirst();
        method = family + QLatin1Char('.') + words.takeFirst();
    }
    for (const QString &w : words) {
        const int eq = w.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            error = QStringLiteral("« %1 » n'est pas de la forme clef=valeur.").arg(w);
            return false;
        }
        const QString key = w.left(eq);
        const QString value = w.mid(eq + 1);
        // Une valeur qui se lit en JSON (nombre, booléen, tableau, objet) le reste ; sinon une chaîne.
        const QJsonDocument doc = QJsonDocument::fromJson((QStringLiteral("[") + value + QStringLiteral("]")).toUtf8());
        if (doc.isArray() && doc.array().size() == 1) {
            params.insert(key, doc.array().first());
        } else {
            params.insert(key, value);
        }
    }
    return true;
}

// ---- La confirmation

bool KoraCutPorte::needsConfirmation(const Call &call, const QString &what, bool always, Reply &gate)
{
    const QString given = call.params.value(QStringLiteral("confirmation")).toString();
    if (!given.isEmpty()) {
        const auto d = m_demands.value(given);
        if (d && d->clientName == call.client) {
            if (d->answered && d->accepted) {
                return false;
            }
            if (d->answered) {
                gate = Reply::fail(QStringLiteral("confirmation_refusee"), QStringLiteral("La personne a refusé : %1").arg(d->what),
                                   QStringLiteral("N'insiste pas, explique-lui ce que tu voulais faire."));
                return true;
            }
            // Pas encore répondu : la même demande reste ouverte, on n'en crée pas une deuxième.
            gate = Reply::fail(QStringLiteral("confirmation_requise"), QStringLiteral("%1 attend toujours l'accord de la personne : %2").arg(call.method, d->what),
                               QStringLiteral("Attends la réponse dans la Console (événement « confirmation »). Ne relance pas en boucle."),
                               QJsonObject{{QStringLiteral("demande_id"), d->id}});
            return true;
        }
    }
    Client *c = clientById(call.clientId);
    if (c && c->state && c->state->trusted && !always) {
        return false;
    }
    // La personne qui lance KoraCut peut accorder la confiance d'avance, pour un travail sans personne devant l'écran :
    // KORACUT_PORTE_CONFIANCE=koracut-ctl,claude (débuts de noms de clients, séparés par des virgules ; « * » : tous).
    // Comme la case de la Console : quitter et écraser un fichier demandent toujours.
    if (!always) {
        const QStringList trustedNames = qEnvironmentVariable("KORACUT_PORTE_CONFIANCE").split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &name : trustedNames) {
            const QString n = name.trimmed();
            if (n == QLatin1String("*") || (!n.isEmpty() && call.client.startsWith(n, Qt::CaseInsensitive))) {
                return false;
            }
        }
    }
    auto d = std::make_shared<Demand>();
    d->id = QStringLiteral("d%1").arg(m_nextDemand++);
    d->clientId = call.clientId;
    d->clientName = call.client;
    d->method = call.method;
    d->params = call.params;
    d->params.remove(QStringLiteral("confirmation"));
    d->what = what;
    d->always = always;
    m_demands.insert(d->id, d);
    Q_EMIT confirmationRequested(d->id, d->clientName, d->method, d->params, what);
    gate = Reply::fail(QStringLiteral("confirmation_requise"), QStringLiteral("%1 attend l'accord de la personne : %2").arg(call.method, what),
                       QStringLiteral("Attends la réponse dans la Console (événement « confirmation »), ou rappelle la même méthode avec confirmation=%1 une fois "
                                      "l'accord donné. Ne relance pas en boucle.")
                           .arg(d->id),
                       QJsonObject{{QStringLiteral("demande_id"), d->id}});
    return true;
}

void KoraCutPorte::answerConfirmation(const QString &demandeId, bool accepted, bool trustClient)
{
    auto d = m_demands.value(demandeId);
    if (!d || d->answered) {
        return;
    }
    d->answered = true;
    d->accepted = accepted;
    // La connexion qui a demandé est peut-être fermée (koracut-ctl) : le nom suffit pour la confiance,
    // et l'appel se rejoue sur le client de l'intérieur si personne n'écoute plus.
    Client *c = clientById(d->clientId);
    if (trustClient) {
        if (auto st = m_states.value(d->clientName)) {
            st->trusted = true;
        }
    }
    Q_EMIT confirmationAnswered(demandeId, accepted);
    if (!accepted) {
        d->hasResult = true;
        d->result = Reply::fail(QStringLiteral("confirmation_refusee"), QStringLiteral("La personne a refusé : %1").arg(d->what),
                                QStringLiteral("N'insiste pas, explique-lui ce que tu voulais faire."));
        if (c) {
            send(c, QJsonObject{{QStringLiteral("method"), QStringLiteral("evenement")},
                                {QStringLiteral("params"), QJsonObject{{QStringLiteral("type"), QStringLiteral("confirmation")},
                                                                       {QStringLiteral("demande_id"), demandeId},
                                                                       {QStringLiteral("accord"), false},
                                                                       {QStringLiteral("methode"), d->method},
                                                                       {QStringLiteral("error"), d->result.error}}}});
        }
        return;
    }
    if (!c) {
        // La connexion est fermée (koracut-ctl) : on exécute quand même, sous le même nom ; le client
        // relit la réponse en rappelant la méthode avec confirmation=<demande_id>.
        c = m_inner.get();
    }
    Call call;
    call.client = d->clientName;
    call.clientId = c->id;
    call.method = d->method;
    call.params = d->params;
    call.params.insert(QStringLiteral("confirmation"), demandeId);
    const quint64 clientId = d->clientId;
    execute(c, call, [this, d, demandeId, clientId](const Reply &reply) {
        d->hasResult = true;
        d->result = reply;
        if (Client *cl = clientById(clientId)) {
            QJsonObject params{{QStringLiteral("type"), QStringLiteral("confirmation")},
                               {QStringLiteral("demande_id"), demandeId},
                               {QStringLiteral("accord"), true},
                               {QStringLiteral("methode"), d->method}};
            if (reply.ok()) {
                params.insert(QStringLiteral("result"), reply.result);
            } else {
                params.insert(QStringLiteral("error"), reply.error);
            }
            send(cl, QJsonObject{{QStringLiteral("method"), QStringLiteral("evenement")}, {QStringLiteral("params"), params}});
        }
    });
}

QStringList KoraCutPorte::pendingConfirmations() const
{
    QStringList ids;
    for (auto it = m_demands.begin(); it != m_demands.end(); ++it) {
        if (!it.value()->answered) {
            ids << it.key();
        }
    }
    return ids;
}

// ---- Le tour

bool KoraCutPorte::beginTurn(quint64 clientId, const QString &client, const QString &title, QString &error)
{
    Client *c = clientById(clientId);
    if (!c) {
        error = QStringLiteral("Client inconnu.");
        return false;
    }
    if (c->name.isEmpty()) {
        c->name = client;
    }
    auto st = stateFor(c);
    if (st->turn) {
        error = QStringLiteral("Un tour est déjà ouvert (« %1 ») : ferme-le avec faire=fin ou faire=abandon.").arg(st->turn->title);
        return false;
    }
    st->turn = std::make_unique<Turn>();
    st->turn->title = title.simplified().isEmpty() ? QStringLiteral("tour") : title.simplified();
    if (st->turn->title.size() > 60) {
        st->turn->title = st->turn->title.left(59) + QChar(0x2026);
    }
    Q_EMIT turnsChanged();
    return true;
}

int KoraCutPorte::endTurn(quint64 clientId, QString &error, bool abandon)
{
    Client *c = clientById(clientId);
    if (!c || !c->state || !c->state->turn) {
        error = QStringLiteral("Aucun tour ouvert : commence par faire=debut.");
        return -1;
    }
    std::unique_ptr<Turn> turn = std::move(c->state->turn);
    Q_EMIT turnsChanged();
    const int actions = int(turn->commands.size());
    if (actions == 0) {
        return 0;
    }
    auto stack = undoStack();
    if (!stack) {
        qDeleteAll(turn->commands);
        return actions;
    }
    const bool wasCollecting = stack->isCollecting();
    std::vector<QUndoCommand *> collected;
    if (wasCollecting) {
        // Un tour d'agent est ouvert par-dessus : on ferme le nôtre sans le mélanger au sien.
        collected = stack->endCollecting();
    }
    stack->push(new KoraCutPorteTurnCommand(std::move(turn->commands), QStringLiteral("%1 : %2").arg(c->name, turn->title)));
    if (abandon) {
        stack->undo();
    }
    if (wasCollecting) {
        stack->beginCollecting();
        // Ce que l'agent avait déjà mis de côté n'est pas perdu : il retourne dans sa collecte.
        for (QUndoCommand *cmd : collected) {
            stack->push(cmd);
        }
    }
    return actions;
}

void KoraCutPorte::runAsClient(const QString &client, const std::function<void()> &fn)
{
    const auto st = m_states.value(client);
    auto stack = undoStack();
    const bool collecting = st && st->turn && stack && !stack->isCollecting();
    if (collecting) {
        stack->beginCollecting();
    }
    const QVariant noDialogBefore = qApp ? qApp->property(noDialogProperty()) : QVariant();
    if (qApp) {
        qApp->setProperty(noDialogProperty(), true);
    }
    ++m_running;
    fn();
    --m_running;
    releaseRetiredSockets();
    if (qApp) {
        qApp->setProperty(noDialogProperty(), noDialogBefore);
    }
    if (collecting) {
        std::vector<QUndoCommand *> commands = stack->endCollecting();
        // Le tour a pu se fermer pendant la pause : ce qui a été fait reste alors une étape à part.
        if (auto now = m_states.value(client); now && now->turn) {
            now->turn->commands.insert(now->turn->commands.end(), commands.begin(), commands.end());
        } else {
            for (QUndoCommand *cmd : commands) {
                stack->push(cmd);
            }
        }
    }
}

bool KoraCutPorte::turnOpen(quint64 clientId) const
{
    Client *c = clientById(clientId);
    return c && c->state && c->state->turn;
}

QStringList KoraCutPorte::openTurns() const
{
    QStringList turns;
    for (auto it = m_states.cbegin(); it != m_states.cend(); ++it) {
        if (it.value() && it.value()->turn) {
            turns << QStringLiteral("%1 : %2").arg(it.key(), it.value()->turn->title);
        }
    }
    turns.sort();
    return turns;
}

std::shared_ptr<DocUndoStack> KoraCutPorte::undoStack() const
{
    return m_undoStackProvider ? m_undoStackProvider() : nullptr;
}

void KoraCutPorte::setUndoStackProvider(std::function<std::shared_ptr<DocUndoStack>()> provider)
{
    m_undoStackProvider = std::move(provider);
}

void KoraCutPorte::setBusyProvider(std::function<bool()> provider)
{
    m_busyProvider = std::move(provider);
}

void KoraCutPorte::setProjectProvider(std::function<QString()> provider)
{
    m_projectProvider = std::move(provider);
}

// ---- Les fenêtres et la sélection (lot 3)

const char *KoraCutPorte::noDialogProperty()
{
    return "koracut_sans_fenetre";
}

QJsonObject KoraCutPorte::describeWindow(QWidget *w)
{
    const auto clean = [](QString text) { return text.remove(QLatin1Char('&')); };
    // Le nom d'un champ : son nom stable pour les IA (propriété koracutId, fenêtre Préférences) s'il en a un.
    const auto nameOf = [](QWidget *f) -> QString {
        const QString id = f->property("koracutId").toString();
        return id.isEmpty() ? f->objectName() : id;
    };
    const auto field = [&nameOf](QWidget *f, const QJsonValue &value) {
        QJsonObject o{{QStringLiteral("nom"), nameOf(f)}, {QStringLiteral("valeur"), value}};
        if (!f->accessibleName().isEmpty()) {
            o.insert(QStringLiteral("libelle"), f->accessibleName());
        }
        return o;
    };
    QJsonArray buttons;
    for (QAbstractButton *b : w->findChildren<QAbstractButton *>()) {
        if (b->isVisible() && !b->text().isEmpty() && !qobject_cast<QCheckBox *>(b)) {
            buttons.append(clean(b->text()));
        }
    }
    QJsonArray fields;
    for (QLineEdit *e : w->findChildren<QLineEdit *>()) {
        // Les champs internes de Qt (qt_spinbox_lineedit…) ne sont pas des champs de la fenêtre.
        if (e->isVisible() && !nameOf(e).isEmpty() && !e->objectName().startsWith(QLatin1String("qt_"))) {
            fields.append(field(e, e->text()));
        }
    }
    for (QAbstractSpinBox *s : w->findChildren<QAbstractSpinBox *>()) {
        if (s->isVisible() && !nameOf(s).isEmpty()) {
            if (auto *i = qobject_cast<QSpinBox *>(s)) {
                fields.append(field(s, i->value()));
            } else if (auto *d = qobject_cast<QDoubleSpinBox *>(s)) {
                fields.append(field(s, d->value()));
            }
        }
    }
    for (QComboBox *c : w->findChildren<QComboBox *>()) {
        if (c->isVisible() && !nameOf(c).isEmpty()) {
            fields.append(field(c, c->currentText()));
        }
    }
    for (QCheckBox *c : w->findChildren<QCheckBox *>()) {
        if (c->isVisible()) {
            const QString name = nameOf(c).isEmpty() ? clean(c->text()) : nameOf(c);
            QJsonObject o = field(c, c->isChecked());
            o.insert(QStringLiteral("nom"), name);
            fields.append(o);
        }
    }
    for (QAbstractSlider *s : w->findChildren<QAbstractSlider *>()) {
        if (s->isVisible() && !nameOf(s).isEmpty() && !qobject_cast<QScrollBar *>(s)) {
            fields.append(field(s, s->value()));
        }
    }
    QJsonArray tabs;
    QTabWidget *t = nullptr;
    // Les onglets de la partie visible (une fenêtre à sections en garde d'autres, cachés).
    for (QTabWidget *candidate : w->findChildren<QTabWidget *>()) {
        if (candidate->isVisible()) {
            t = candidate;
            break;
        }
    }
    if (t) {
        for (int i = 0; i < t->count(); ++i) {
            tabs.append(clean(t->tabText(i)));
        }
    }
    QJsonObject o{{QStringLiteral("titre"), w->windowTitle()},
                  {QStringLiteral("classe"), QString::fromLatin1(w->metaObject()->className())},
                  {QStringLiteral("modale"), w->isModal()},
                  {QStringLiteral("boutons"), buttons},
                  {QStringLiteral("champs"), fields}};
    if (!tabs.isEmpty()) {
        o.insert(QStringLiteral("onglets"), tabs);
    }
    // Une fenêtre à sections (Préférences) : ses sections, celle qui est affichée, la recherche en cours.
    // ui.fenetre faire=section:<id> y va, faire=recherche:<texte> filtre les réglages.
    const QVariant sections = w->property("koracutSections");
    if (sections.isValid()) {
        o.insert(QStringLiteral("sections"), QJsonArray::fromStringList(sections.toStringList()));
        o.insert(QStringLiteral("section"), w->property("koracutSection").toString());
        o.insert(QStringLiteral("recherche"), w->property("koracutSearch").toString());
    }
    return o;
}

void KoraCutPorte::watchWindows()
{
    if (m_watchingWindows || !qApp) {
        return;
    }
    m_watchingWindows = true;
    qApp->installEventFilter(this);
}

bool KoraCutPorte::eventFilter(QObject *watched, QEvent *event)
{
    // Une fenêtre modale qui s'ouvre ou se ferme : l'événement `fenetre` aux abonnés. Les boutons ne
    // sont posés qu'après le Show, d'où la description différée d'un tour de boucle.
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
        auto *w = qobject_cast<QWidget *>(watched);
        if (w && w->isWindow() && w->isModal()) {
            if (event->type() == QEvent::Show) {
                QPointer<QWidget> guard(w);
                QTimer::singleShot(0, this, [this, guard]() {
                    if (guard && guard->isVisible()) {
                        QJsonObject o = describeWindow(guard);
                        o.insert(QStringLiteral("etat"), QStringLiteral("ouverte"));
                        publish(QStringLiteral("fenetre"), o);
                    }
                });
            } else {
                publish(QStringLiteral("fenetre"), QJsonObject{{QStringLiteral("etat"), QStringLiteral("fermee")}, {QStringLiteral("titre"), w->windowTitle()}});
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void KoraCutPorte::watchTimeline(const std::shared_ptr<TimelineItemModel> &timeline)
{
    if (m_watchedTimeline.lock() == timeline) {
        return;
    }
    if (m_selectionConnection) {
        disconnect(m_selectionConnection);
    }
    m_watchedTimeline = timeline;
    if (!timeline) {
        return;
    }
    std::weak_ptr<TimelineItemModel> weak = timeline;
    m_selectionConnection = connect(timeline.get(), &TimelineModel::selectionChanged, this, [this, weak]() {
        auto tl = weak.lock();
        if (!tl) {
            return;
        }
        QJsonArray ids;
        for (int id : tl->getCurrentSelection()) {
            ids.append(id);
        }
        publish(QStringLiteral("selection"), QJsonObject{{QStringLiteral("clip_ids"), ids}, {QStringLiteral("sequence"), tl->uuid().toString(QUuid::WithoutBraces)}});
    });
}

// ---- Les événements

const QStringList &KoraCutPorte::eventTypes()
{
    return kEventTypes;
}

void KoraCutPorte::publish(const QString &type, const QJsonObject &params, quint64 exceptClient)
{
    QJsonObject p = params;
    p.insert(QStringLiteral("type"), type);
    const QJsonObject msg{{QStringLiteral("method"), QStringLiteral("evenement")}, {QStringLiteral("params"), p}};
    for (const auto &c : m_clients) {
        if (c->id != exceptClient && c->authenticated && c->subscriptions.contains(type)) {
            send(c.get(), msg);
        }
    }
}

bool KoraCutPorte::subscribe(quint64 clientId, const QStringList &types, QStringList &accepted)
{
    Client *c = clientById(clientId);
    if (!c) {
        return false;
    }
    for (const QString &t : types) {
        if (kEventTypes.contains(t)) {
            c->subscriptions.insert(t);
        }
    }
    accepted = c->subscriptions.values();
    accepted.sort();
    return true;
}

bool KoraCutPorte::unsubscribe(quint64 clientId, const QStringList &types)
{
    Client *c = clientById(clientId);
    if (!c) {
        return false;
    }
    for (const QString &t : types) {
        c->subscriptions.remove(t);
    }
    return true;
}
