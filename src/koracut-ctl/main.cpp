/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

/* koracut-ctl : parle à la porte de commande de KoraCut depuis un terminal (document 18, PORTE.md § 6).
 *
 *   koracut-ctl [--reglage <nom>] [--json] <famille> <methode> [clef=valeur …] [--params '<json>']
 *   koracut-ctl actions lister filtre=render
 *   koracut-ctl timeline outil nom=couper_clip args='{"clip_id": 12, "position": "00:00:04:00"}'
 *   koracut-ctl evenements abonner types='["tete"]' --suivre
 *   koracut-ctl --aide [famille]
 *   koracut-ctl --rapide timeline outil …   (sans le montage visible : ni mise en scène ni pause)
 *   koracut-ctl mcp                         (serveur MCP en stdio : chaque méthode de la porte est un outil)
 *
 * Sortie : le result en JSON sur la sortie standard (joli, ou compact avec --json), l'erreur en JSON
 * sur la sortie d'erreur. Code de retour 0 si ok, 1 sinon, 2 si aucune KoraCut n'est ouverte.
 * La porte se trouve par KORACUT_PORTE (le chemin de porte.json), sinon dans le dossier de
 * configuration de KoraCut : le fichier du réglage demandé, sinon le plus récent dont le pid vit.
 */

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QThread>
#include <QTextStream>

#include <cstdio>
#include <iostream>
#include <string>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <csignal>
#endif

namespace {

const char *kVersion = "1.0";

bool processAlive(qint64 pid)
{
    if (pid <= 0) {
        return false;
    }
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
    return ::kill(pid_t(pid), 0) == 0;
#endif
}

QString configDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/koracut");
}

// Le porte.json à utiliser : KORACUT_PORTE, sinon celui du réglage, sinon le plus récent vivant.
QJsonObject findPorte(const QString &reglage, QString &why)
{
    QStringList candidates;
    const QString env = qEnvironmentVariable("KORACUT_PORTE");
    if (!env.isEmpty()) {
        candidates << env;
    } else {
        const QDir dir(configDir());
        if (!reglage.isEmpty()) {
            candidates << dir.absoluteFilePath(reglage == QLatin1String("koracutrc") ? QStringLiteral("porte.json") : QStringLiteral("porte-%1.json").arg(reglage));
        } else {
            const QFileInfoList files = dir.entryInfoList({QStringLiteral("porte*.json")}, QDir::Files, QDir::Time);
            for (const QFileInfo &f : files) {
                candidates << f.absoluteFilePath();
            }
        }
    }
    for (const QString &path : candidates) {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        if (!processAlive(qint64(o.value(QStringLiteral("pid")).toDouble()))) {
            continue;
        }
        if (!o.value(QStringLiteral("ouverte")).toBool(false)) {
            why = QStringLiteral("La porte de KoraCut (%1) est fermée : coche « Autoriser les programmes extérieurs à piloter KoraCut » dans les Préférences.")
                      .arg(o.value(QStringLiteral("reglage")).toString());
            continue;
        }
        return o;
    }
    if (why.isEmpty()) {
        why = env.isEmpty() ? QStringLiteral("Aucune KoraCut ouverte (pas de porte.json vivant dans %1).").arg(configDir())
                            : QStringLiteral("KORACUT_PORTE pointe vers %1, qui n'est pas une porte vivante.").arg(env);
    }
    return {};
}

QJsonValue parseValue(const QString &value)
{
    const QJsonDocument doc = QJsonDocument::fromJson((QStringLiteral("[") + value + QStringLiteral("]")).toUtf8());
    if (doc.isArray() && doc.array().size() == 1) {
        return doc.array().first();
    }
    return value;
}

void printJson(FILE *stream, const QJsonValue &v, bool compact)
{
    const QJsonDocument::JsonFormat format = compact ? QJsonDocument::Compact : QJsonDocument::Indented;
    QByteArray bytes;
    if (v.isObject()) {
        bytes = QJsonDocument(v.toObject()).toJson(format);
    } else if (v.isArray()) {
        bytes = QJsonDocument(v.toArray()).toJson(format);
    } else {
        bytes = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
        bytes = bytes.mid(1, bytes.size() - 2);
    }
    fputs(bytes.constData(), stream);
    if (!bytes.endsWith('\n')) {
        fputc('\n', stream);
    }
    fflush(stream);
}

// KoraCut a pris la connexion mais ne répond pas à porte.ouvrir.
QJsonObject silentAppError()
{
    return QJsonObject{{QStringLiteral("code"), QStringLiteral("sans_reponse")},
                       {QStringLiteral("message"), QStringLiteral("KoraCut est ouverte mais ne répond pas (rien en 5 secondes) : l'application est figée ou tenue par un long calcul.")},
                       {QStringLiteral("conseil"), QStringLiteral("Réessaie dans un instant. Si elle ne répond toujours pas, dis-le à la personne : c'est à elle de décider de la fermer.")}};
}

// Une commande partie sans réponse : la connexion est tombée (KoraCut a quitté ou s'est arrêtée net), ou
// elle tient toujours et l'application n'a rien dit de tout le délai (figée, ou un calcul très long).
QJsonObject noAnswerError(const QLocalSocket &socket, int seconds)
{
    if (socket.state() != QLocalSocket::ConnectedState) {
        return QJsonObject{{QStringLiteral("code"), QStringLiteral("porte_fermee")},
                           {QStringLiteral("message"), QStringLiteral("La connexion à KoraCut s'est fermée avant la réponse : l'application a quitté ou s'est arrêtée net.")},
                           {QStringLiteral("conseil"), QStringLiteral("Vérifie qu'elle tourne encore (app ping). La commande a pu être faite ou non : relis l'état avant de la rejouer.")}};
    }
    return QJsonObject{{QStringLiteral("code"), QStringLiteral("sans_reponse")},
                       {QStringLiteral("message"), QStringLiteral("KoraCut n'a pas répondu en %1 secondes : la commande est peut-être encore en cours, ou l'application est figée.").arg(seconds)},
                       {QStringLiteral("conseil"), QStringLiteral("Ne rejoue pas la commande tout de suite : essaie app ping, puis relis l'état pour voir si elle a été faite.")}};
}

// Une requête, une réponse : la première ligne dont l'id correspond ; les événements passent en sortie.
bool exchange(QLocalSocket &socket, const QJsonObject &request, QJsonObject &response, bool printEvents, int timeoutMs)
{
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
    QByteArray buffer;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (!socket.bytesAvailable() && !socket.waitForReadyRead(200)) {
            if (socket.state() != QLocalSocket::ConnectedState) {
                return false;
            }
            continue;
        }
        buffer += socket.readAll();
        int nl = buffer.indexOf('\n');
        while (nl >= 0) {
            const QByteArray line = buffer.left(nl);
            buffer.remove(0, nl + 1);
            const QJsonObject msg = QJsonDocument::fromJson(line).object();
            if (msg.contains(QStringLiteral("id")) && msg.value(QStringLiteral("id")) == request.value(QStringLiteral("id"))) {
                response = msg;
                return true;
            }
            if (printEvents && msg.value(QStringLiteral("method")).toString() == QLatin1String("evenement")) {
                printJson(stdout, msg.value(QStringLiteral("params")), true);
            }
            nl = buffer.indexOf('\n');
        }
    }
    return false;
}

void printHelp(const QJsonArray &methods, const QString &family)
{
    QTextStream out(stdout);
    out << "koracut-ctl " << kVersion << " : la porte de commande de KoraCut\n\n";
    out << "  koracut-ctl [--reglage <nom>] [--json] [--suivre] [--rapide] <famille> <methode> [clef=valeur …] [--params '<json>']\n";
    out << "  koracut-ctl --aide [famille]\n";
    out << "  koracut-ctl mcp        le serveur MCP de KoraCut (stdio), pour brancher un agent : command « koracut-ctl », args [« mcp »]\n\n";
    out << "Une valeur clef=valeur qui se lit en JSON (nombre, true, [liste], {objet}) le reste ; sinon c'est une chaîne.\n";
    out << "Les temps se donnent en images ou en timecode HH:MM:SS:II. Code de retour : 0 ok, 1 erreur, 2 pas de KoraCut.\n\n";
    QString current;
    for (const QJsonValue &v : methods) {
        const QJsonObject m = v.toObject();
        const QString name = m.value(QStringLiteral("nom")).toString();
        const QString fam = name.section(QLatin1Char('.'), 0, 0);
        if (!family.isEmpty() && fam != family) {
            continue;
        }
        if (fam != current) {
            current = fam;
            out << "\n" << fam << "\n";
        }
        out << "  " << name.section(QLatin1Char('.'), 1).leftJustified(16) << (m.value(QStringLiteral("confirmation")).toBool() ? "(à confirmation) " : "")
            << m.value(QStringLiteral("resume")).toString() << "\n";
        for (const QJsonValue &pv : m.value(QStringLiteral("params")).toArray()) {
            const QJsonObject p = pv.toObject();
            out << "      " << p.value(QStringLiteral("nom")).toString() << (p.value(QStringLiteral("requis")).toBool() ? "*" : "") << " (" << p.value(QStringLiteral("type")).toString()
                << ") : " << p.value(QStringLiteral("description")).toString() << "\n";
        }
    }
    out << "\nLa compétence koracut-piloter (Claude Code) explique les gestes courants.\n";
    out.flush();
}

// ---- koracut-ctl mcp : le serveur MCP de KoraCut, sans Node (5 octobre 2026, document 18).
// Le protocole MCP en stdio est du JSON-RPC, une ligne par message. Chaque méthode de la porte devient
// un outil `koracut_<famille>_<methode>` (les mêmes noms que l'ancien serveur TypeScript), l'état du
// projet des ressources, les compétences koracut-* des prompts. Un agent lancé dans un bac à sable
// (Codex) ne peut pas ouvrir la prise lui-même : son serveur MCP, lui, tourne dehors.

const char *kMcpRule =
    "Tu montes DANS KoraCut, l'application de montage ouverte sur cet ordinateur. Chaque geste passe par ces outils koracut_* "
    "(ou par la commande koracut-ctl). Tu n'utilises jamais ffmpeg, melt, un script Python ou un autre logiciel pour fabriquer la vidéo, "
    "et tu n'écris jamais une vidéo toi-même dans un dossier. Le résultat est la timeline du projet ouvert ; l'export se fait par "
    "koracut_export_lancer, quand la personne le demande. Si un outil manque ou échoue, arrête-toi et dis-le.";

class McpServer
{
public:
    explicit McpServer(const QString &reglage)
        : m_reglage(reglage)
    {
    }

    int run()
    {
#ifdef Q_OS_WIN
        // Pas de traduction des fins de ligne : le canal MCP est en octets.
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        std::string line;
        while (std::getline(std::cin, line)) {
            const QByteArray bytes = QByteArray::fromStdString(line).trimmed();
            if (bytes.isEmpty()) {
                continue;
            }
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
            if (!doc.isObject()) {
                reply(QJsonValue(), QJsonValue(), rpcError(-32700, QStringLiteral("JSON illisible")));
                continue;
            }
            handle(doc.object());
        }
        return 0;
    }

private:
    static QJsonObject rpcError(int code, const QString &message)
    {
        return QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
    }

    void reply(const QJsonValue &id, const QJsonValue &result, const QJsonObject &error = QJsonObject())
    {
        QJsonObject msg{{QStringLiteral("jsonrpc"), QStringLiteral("2.0")}, {QStringLiteral("id"), id}};
        if (error.isEmpty()) {
            msg.insert(QStringLiteral("result"), result);
        } else {
            msg.insert(QStringLiteral("error"), error);
        }
        const QByteArray bytes = QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n';
        fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
        fflush(stdout);
    }

    static void log(const QString &message)
    {
        // La sortie standard est le canal MCP : ce qui est pour un humain va sur la sortie d'erreur.
        fprintf(stderr, "koracut-mcp: %s\n", message.toUtf8().constData());
    }

    // La porte : ouverte au premier besoin, rouverte si KoraCut a redémarré.
    bool ensureOpen(QString &why)
    {
        if (m_socket.state() == QLocalSocket::ConnectedState) {
            return true;
        }
        m_socket.abort();
        const QJsonObject porte = findPorte(m_reglage, why);
        if (porte.isEmpty()) {
            return false;
        }
        m_socket.connectToServer(porte.value(QStringLiteral("prise")).toString());
        if (!m_socket.waitForConnected(3000)) {
            why = QStringLiteral("La prise %1 ne répond pas : %2").arg(porte.value(QStringLiteral("prise")).toString(), m_socket.errorString());
            return false;
        }
        QJsonObject response;
        const QJsonObject open{{QStringLiteral("id"), ++m_id},
                               {QStringLiteral("method"), QStringLiteral("porte.ouvrir")},
                               {QStringLiteral("params"),
                                QJsonObject{{QStringLiteral("jeton"), porte.value(QStringLiteral("jeton")).toString()}, {QStringLiteral("client"), QStringLiteral("koracut-mcp %1").arg(QString::fromLatin1(kVersion))}}}};
        const bool answered = exchange(m_socket, open, response, false, 5000);
        if (!answered || response.contains(QStringLiteral("error"))) {
            why = answered ? QStringLiteral("KoraCut a refusé l'ouverture de la porte.") : silentAppError().value(QStringLiteral("message")).toString();
            m_socket.abort();
            return false;
        }
        return true;
    }

    // Un appel à la porte ; « occupe » est réessayé une minute, une prise tombée est rouverte une fois.
    bool call(const QString &method, const QJsonObject &params, QJsonValue &result, QJsonObject &error)
    {
        QElapsedTimer busySince;
        busySince.start();
        for (int attempt = 0; attempt < 2; ++attempt) {
            QString why;
            if (!ensureOpen(why)) {
                error = QJsonObject{{QStringLiteral("code"), QStringLiteral("porte_fermee")},
                                    {QStringLiteral("message"), why},
                                    {QStringLiteral("conseil"), QStringLiteral("KoraCut doit être ouverte sur cet ordinateur. Dis-le à la personne et arrête-toi : ne monte pas la vidéo par un autre moyen.")}};
                return false;
            }
            const QJsonObject request{{QStringLiteral("id"), ++m_id}, {QStringLiteral("method"), method}, {QStringLiteral("params"), params}};
            QJsonObject response;
            bool answered = false;
            QElapsedTimer sentAt;
            sentAt.start();
            while (true) {
                answered = exchange(m_socket, request, response, false, 120000);
                const bool busy = answered && response.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QLatin1String("occupe");
                if (!busy || busySince.elapsed() > 60000) {
                    break;
                }
                QThread::msleep(500);
            }
            if (!answered) {
                // On ne renvoie la commande que si la prise était déjà morte au moment d'écrire (une KoraCut
                // relancée entre deux appels) : elle n'a alors pas pu partir. Après une attente, ou une
                // connexion tombée en route, la commande a pu être exécutée : la rejouer la ferait deux fois.
                const bool neverSent = m_socket.state() != QLocalSocket::ConnectedState && sentAt.elapsed() < 1000;
                if (neverSent && attempt == 0) {
                    m_socket.abort();
                    continue;
                }
                error = noAnswerError(m_socket, 120);
                m_socket.abort();
                return false;
            }
            if (response.contains(QStringLiteral("error"))) {
                error = response.value(QStringLiteral("error")).toObject();
                return false;
            }
            result = response.value(QStringLiteral("result"));
            return true;
        }
        error = QJsonObject{{QStringLiteral("code"), QStringLiteral("porte_fermee")}, {QStringLiteral("message"), QStringLiteral("KoraCut n'a pas répondu.")}};
        return false;
    }

    QString cachePath() const
    {
        return configDir() + QStringLiteral("/catalogue-mcp.json");
    }

    // Le catalogue : celui de KoraCut si elle répond (gardé en copie), sinon la dernière copie.
    QJsonArray catalogue()
    {
        QJsonValue result;
        QJsonObject error;
        if (call(QStringLiteral("porte.methodes"), {}, result, error) && result.isArray() && !result.toArray().isEmpty()) {
            m_methods = result.toArray();
            QFile f(cachePath());
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                f.write(QJsonDocument(m_methods).toJson(QJsonDocument::Compact));
            }
        } else if (m_methods.isEmpty()) {
            QFile f(cachePath());
            if (f.open(QIODevice::ReadOnly)) {
                m_methods = QJsonDocument::fromJson(f.readAll()).array();
            }
            log(QStringLiteral("KoraCut ne répond pas (%1) : %2 outils de la dernière copie.").arg(error.value(QStringLiteral("message")).toString()).arg(m_methods.size()));
        }
        return m_methods;
    }

    static QString toolName(const QString &method)
    {
        QString name = method;
        const int dot = name.indexOf(QLatin1Char('.'));
        if (dot >= 0) {
            name[dot] = QLatin1Char('_');
        }
        return QStringLiteral("koracut_") + name;
    }

    QJsonArray tools()
    {
        QJsonArray out;
        const QJsonArray methods = catalogue();
        for (const QJsonValue &v : methods) {
            const QJsonObject m = v.toObject();
            const QString method = m.value(QStringLiteral("nom")).toString();
            if (method.isEmpty() || method.startsWith(QLatin1String("porte."))) {
                continue;
            }
            QJsonObject properties;
            QJsonArray required;
            for (const QJsonValue &pv : m.value(QStringLiteral("params")).toArray()) {
                const QJsonObject p = pv.toObject();
                const QString name = p.value(QStringLiteral("nom")).toString();
                QJsonObject property{{QStringLiteral("description"), p.value(QStringLiteral("description")).toString()}};
                const QString type = p.value(QStringLiteral("type")).toString();
                static const QStringList known{QStringLiteral("string"), QStringLiteral("number"), QStringLiteral("integer"), QStringLiteral("boolean"),
                                               QStringLiteral("array"), QStringLiteral("object")};
                if (known.contains(type)) {
                    property.insert(QStringLiteral("type"), type);
                }
                properties.insert(name, property);
                if (p.value(QStringLiteral("requis")).toBool()) {
                    required.append(name);
                }
            }
            QJsonObject schema{{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties}};
            if (!required.isEmpty()) {
                schema.insert(QStringLiteral("required"), required);
            }
            QString description = m.value(QStringLiteral("resume")).toString();
            if (m.value(QStringLiteral("confirmation")).toBool()) {
                description += QStringLiteral(" À confirmation : la personne répond dans la Console de KoraCut ; en attendant, la réponse est confirmation_requise.");
            }
            out.append(QJsonObject{{QStringLiteral("name"), toolName(method)}, {QStringLiteral("title"), method}, {QStringLiteral("description"), description}, {QStringLiteral("inputSchema"), schema}});
        }
        return out;
    }

    // Les compétences koracut-* : KORACUT_SKILLS, celles livrées à côté de l'exécutable, celles de la personne.
    static QString skillsDir()
    {
        const QString appDir = QCoreApplication::applicationDirPath();
        const QStringList candidates{qEnvironmentVariable("KORACUT_SKILLS"), appDir + QStringLiteral("/../Resources/agents/skills"), appDir + QStringLiteral("/../share/koracut/agents/skills"),
                                     appDir + QStringLiteral("/data/agents/skills"), QDir::homePath() + QStringLiteral("/.claude/skills")};
        for (const QString &c : candidates) {
            if (!c.isEmpty() && !QDir(c).entryList({QStringLiteral("koracut-*")}, QDir::Dirs).isEmpty()) {
                return QDir(c).canonicalPath();
            }
        }
        return QString();
    }

    static QJsonObject textContent(const QString &text)
    {
        return QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), text}};
    }

    static QString pretty(const QJsonValue &v)
    {
        if (v.isString()) {
            return v.toString();
        }
        if (v.isObject()) {
            return QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Indented));
        }
        if (v.isArray()) {
            return QString::fromUtf8(QJsonDocument(v.toArray()).toJson(QJsonDocument::Indented));
        }
        const QByteArray bytes = QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact);
        return QString::fromUtf8(bytes.mid(1, bytes.size() - 2));
    }

    struct Resource
    {
        const char *name;
        const char *description;
        const char *method;
        const char *level;
    };
    static const QList<Resource> &resources()
    {
        static const QList<Resource> list{{"projet", "Le projet ouvert : chemin, profil, type de vidéo, séquences, dossiers.", "projet.info", nullptr},
                                          {"timeline", "L'état de la séquence : pistes, clips, sélection, tête de lecture.", "timeline.etat", "normal"},
                                          {"medias", "Les médias du chutier, avec leurs ids.", "medias.lister", nullptr},
                                          {"actions", "Toutes les actions nommées de KoraCut et leur état.", "actions.lister", nullptr}};
        return list;
    }

    void handle(const QJsonObject &msg)
    {
        const QString method = msg.value(QStringLiteral("method")).toString();
        const QJsonValue id = msg.value(QStringLiteral("id"));
        const QJsonObject params = msg.value(QStringLiteral("params")).toObject();
        if (!msg.contains(QStringLiteral("id"))) {
            return; // une notification (notifications/initialized, cancelled) : rien à répondre
        }
        if (method == QLatin1String("initialize")) {
            static const QStringList versions{QStringLiteral("2025-06-18"), QStringLiteral("2025-03-26"), QStringLiteral("2024-11-05")};
            const QString asked = params.value(QStringLiteral("protocolVersion")).toString();
            reply(id, QJsonObject{{QStringLiteral("protocolVersion"), versions.contains(asked) ? asked : versions.first()},
                                  {QStringLiteral("capabilities"),
                                   QJsonObject{{QStringLiteral("tools"), QJsonObject()}, {QStringLiteral("resources"), QJsonObject()}, {QStringLiteral("prompts"), QJsonObject()}}},
                                  {QStringLiteral("serverInfo"), QJsonObject{{QStringLiteral("name"), QStringLiteral("koracut")}, {QStringLiteral("version"), QString::fromLatin1(kVersion)}}},
                                  {QStringLiteral("instructions"), QString::fromUtf8(kMcpRule)}});
        } else if (method == QLatin1String("ping")) {
            reply(id, QJsonObject());
        } else if (method == QLatin1String("tools/list")) {
            reply(id, QJsonObject{{QStringLiteral("tools"), tools()}});
        } else if (method == QLatin1String("tools/call")) {
            const QString name = params.value(QStringLiteral("name")).toString();
            if (m_methods.isEmpty()) {
                catalogue();
            }
            QString porteMethod;
            for (const QJsonValue &v : std::as_const(m_methods)) {
                const QString candidate = v.toObject().value(QStringLiteral("nom")).toString();
                if (toolName(candidate) == name) {
                    porteMethod = candidate;
                    break;
                }
            }
            if (porteMethod.isEmpty()) {
                reply(id, QJsonValue(), rpcError(-32602, QStringLiteral("Outil inconnu : %1").arg(name)));
                return;
            }
            QJsonValue result;
            QJsonObject error;
            if (call(porteMethod, params.value(QStringLiteral("arguments")).toObject(), result, error)) {
                reply(id, QJsonObject{{QStringLiteral("content"), QJsonArray{textContent(pretty(result))}}});
            } else {
                reply(id, QJsonObject{{QStringLiteral("content"), QJsonArray{textContent(QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("error"), error}}).toJson(QJsonDocument::Compact)))}},
                                      {QStringLiteral("isError"), true}});
            }
        } else if (method == QLatin1String("resources/list")) {
            QJsonArray list;
            for (const Resource &r : resources()) {
                list.append(QJsonObject{{QStringLiteral("uri"), QString(QStringLiteral("koracut://") + QString::fromLatin1(r.name))},
                                        {QStringLiteral("name"), QString::fromLatin1(r.name)},
                                        {QStringLiteral("title"), QString(QStringLiteral("KoraCut : ") + QString::fromLatin1(r.name))},
                                        {QStringLiteral("description"), QString::fromUtf8(r.description)},
                                        {QStringLiteral("mimeType"), QStringLiteral("application/json")}});
            }
            reply(id, QJsonObject{{QStringLiteral("resources"), list}});
        } else if (method == QLatin1String("resources/templates/list")) {
            reply(id, QJsonObject{{QStringLiteral("resourceTemplates"), QJsonArray()}});
        } else if (method == QLatin1String("resources/read")) {
            const QString uri = params.value(QStringLiteral("uri")).toString();
            for (const Resource &r : resources()) {
                if (uri == QStringLiteral("koracut://") + QString::fromLatin1(r.name)) {
                    QJsonValue result;
                    QJsonObject error;
                    QJsonObject callParams;
                    if (r.level) {
                        callParams.insert(QStringLiteral("niveau"), QString::fromLatin1(r.level));
                    }
                    const bool ok = call(QString::fromLatin1(r.method), callParams, result, error);
                    const QString text = ok ? pretty(result) : QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("error"), error}}).toJson(QJsonDocument::Compact));
                    reply(id, QJsonObject{{QStringLiteral("contents"),
                                           QJsonArray{QJsonObject{{QStringLiteral("uri"), uri}, {QStringLiteral("mimeType"), QStringLiteral("application/json")}, {QStringLiteral("text"), text}}}}});
                    return;
                }
            }
            reply(id, QJsonValue(), rpcError(-32002, QStringLiteral("Ressource inconnue : %1").arg(uri)));
        } else if (method == QLatin1String("prompts/list")) {
            QJsonArray list;
            const QString dir = skillsDir();
            if (!dir.isEmpty()) {
                const QStringList names = QDir(dir).entryList({QStringLiteral("koracut-*")}, QDir::Dirs, QDir::Name);
                for (const QString &name : names) {
                    QFile f(dir + QLatin1Char('/') + name + QStringLiteral("/SKILL.md"));
                    if (!f.open(QIODevice::ReadOnly)) {
                        continue;
                    }
                    QString description = name;
                    const QStringList head = QString::fromUtf8(f.read(4000)).split(QLatin1Char('\n'));
                    for (const QString &l : head) {
                        if (l.startsWith(QLatin1String("description:"))) {
                            description = l.mid(12).trimmed();
                            break;
                        }
                    }
                    list.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("title"), name}, {QStringLiteral("description"), description}});
                }
            }
            reply(id, QJsonObject{{QStringLiteral("prompts"), list}});
        } else if (method == QLatin1String("prompts/get")) {
            const QString name = params.value(QStringLiteral("name")).toString();
            const QString dir = skillsDir();
            QFile f(dir + QLatin1Char('/') + name + QStringLiteral("/SKILL.md"));
            if (dir.isEmpty() || !name.startsWith(QLatin1String("koracut-")) || name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) || !f.open(QIODevice::ReadOnly)) {
                reply(id, QJsonValue(), rpcError(-32602, QStringLiteral("Compétence inconnue : %1").arg(name)));
                return;
            }
            QString body = QString::fromUtf8(f.readAll());
            if (body.startsWith(QLatin1String("---\n"))) {
                const int end = body.indexOf(QLatin1String("\n---\n"), 4);
                if (end > 0) {
                    body = body.mid(end + 5);
                }
            }
            reply(id, QJsonObject{{QStringLiteral("messages"), QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("content"), textContent(body)}}}}});
        } else {
            reply(id, QJsonValue(), rpcError(-32601, QStringLiteral("Méthode inconnue : %1").arg(method)));
        }
    }

    QString m_reglage;
    QLocalSocket m_socket;
    QJsonArray m_methods;
    int m_id{0};
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("koracut-ctl"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(kVersion));

    QStringList args = QCoreApplication::arguments();
    args.removeFirst();
    QString reglage;
    QString paramsJson;
    bool compact = false;
    bool follow = false;
    bool help = false;
    bool fast = false;
    QStringList positional;
    for (int i = 0; i < args.size(); ++i) {
        const QString a = args.at(i);
        if (a == QLatin1String("--reglage") && i + 1 < args.size()) {
            reglage = args.at(++i);
        } else if (a.startsWith(QLatin1String("--reglage="))) {
            reglage = a.mid(10);
        } else if (a == QLatin1String("--params") && i + 1 < args.size()) {
            paramsJson = args.at(++i);
        } else if (a.startsWith(QLatin1String("--params="))) {
            paramsJson = a.mid(9);
        } else if (a == QLatin1String("--json")) {
            compact = true;
        } else if (a == QLatin1String("--suivre")) {
            follow = true;
        } else if (a == QLatin1String("--rapide")) {
            // Sans le montage visible : pas de mise en scène ni de pause (visible=false).
            fast = true;
        } else if (a == QLatin1String("--aide") || a == QLatin1String("--help") || a == QLatin1String("-h")) {
            help = true;
        } else if (a == QLatin1String("--version")) {
            printf("koracut-ctl %s\n", kVersion);
            return 0;
        } else {
            positional << a;
        }
    }

    // « koracut-ctl mcp » : le serveur MCP en stdio ; il trouve la porte tout seul et la rouvre au besoin.
    if (!help && positional.size() == 1 && positional.first() == QLatin1String("mcp")) {
        if (reglage.isEmpty()) {
            reglage = qEnvironmentVariable("KORACUT_REGLAGE");
        }
        return McpServer(reglage).run();
    }

    QString why;
    const QJsonObject porte = findPorte(reglage, why);
    if (porte.isEmpty()) {
        fprintf(stderr, "%s\n", QJsonDocument(QJsonObject{{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("porte_fermee")}, {QStringLiteral("message"), why}}}}).toJson(QJsonDocument::Compact).constData());
        if (help) {
            printHelp({}, QString());
        }
        return 2;
    }

    QLocalSocket socket;
    socket.connectToServer(porte.value(QStringLiteral("prise")).toString());
    if (!socket.waitForConnected(3000)) {
        fprintf(stderr, "%s\n", QJsonDocument(QJsonObject{{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("porte_fermee")}, {QStringLiteral("message"), QStringLiteral("La prise %1 ne répond pas : %2").arg(porte.value(QStringLiteral("prise")).toString(), socket.errorString())}}}}).toJson(QJsonDocument::Compact).constData());
        return 2;
    }
    QJsonObject reply;
    const QJsonObject open{{QStringLiteral("id"), 0},
                           {QStringLiteral("method"), QStringLiteral("porte.ouvrir")},
                           {QStringLiteral("params"), QJsonObject{{QStringLiteral("jeton"), porte.value(QStringLiteral("jeton")).toString()}, {QStringLiteral("client"), QStringLiteral("koracut-ctl %1").arg(QString::fromLatin1(kVersion))}}}};
    if (!exchange(socket, open, reply, false, 5000)) {
        // La prise s'est ouverte (c'est le système qui l'accepte) mais KoraCut ne dit rien : ce n'est pas un refus du jeton,
        // c'est une application qui ne tourne plus sa boucle (figée, ou tenue par un long calcul).
        printJson(stderr, QJsonObject{{QStringLiteral("error"), silentAppError()}}, true);
        return 2;
    }
    if (reply.contains(QStringLiteral("error"))) {
        printJson(stderr, reply, true);
        return 2;
    }

    if (help || positional.isEmpty()) {
        const QJsonObject req{{QStringLiteral("id"), 1}, {QStringLiteral("method"), QStringLiteral("porte.methodes")}, {QStringLiteral("params"), QJsonObject()}};
        if (exchange(socket, req, reply, false, 5000)) {
            printHelp(reply.value(QStringLiteral("result")).toArray(), positional.isEmpty() ? QString() : positional.first());
        }
        return help ? 0 : 1;
    }

    // La méthode : « famille methode » ou « famille.methode ».
    QString method;
    if (positional.first().contains(QLatin1Char('.'))) {
        method = positional.takeFirst();
    } else {
        if (positional.size() < 2) {
            fprintf(stderr, "Écris : koracut-ctl <famille> <methode> [clef=valeur …]. koracut-ctl --aide liste tout.\n");
            return 1;
        }
        // Deux instructions : l'ordre des deux takeFirst() dans une seule expression n'est pas garanti.
        const QString family = positional.takeFirst();
        method = family + QLatin1Char('.') + positional.takeFirst();
    }
    QJsonObject params;
    if (!paramsJson.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(paramsJson.toUtf8());
        if (!doc.isObject()) {
            fprintf(stderr, "--params n'est pas un objet JSON.\n");
            return 1;
        }
        params = doc.object();
    }
    for (const QString &kv : positional) {
        const int eq = kv.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            fprintf(stderr, "« %s » n'est pas de la forme clef=valeur.\n", kv.toUtf8().constData());
            return 1;
        }
        params.insert(kv.left(eq), parseValue(kv.mid(eq + 1)));
    }
    if (fast && !params.contains(QStringLiteral("visible"))) {
        params.insert(QStringLiteral("visible"), false);
    }

    const QJsonObject req{{QStringLiteral("id"), 2}, {QStringLiteral("method"), method}, {QStringLiteral("params"), params}};
    // Les méthodes qui attendent (import, transcription) peuvent prendre du temps : deux minutes.
    // « occupe » : KoraCut charge un projet ou exécute une autre commande. On réessaie toutes les
    // demi-secondes pendant une minute au plus, sans que l'appelant ait à s'en soucier.
    QElapsedTimer busySince;
    busySince.start();
    while (true) {
        if (!exchange(socket, req, reply, follow, 120000)) {
            fprintf(stderr, "%s\n", QJsonDocument(QJsonObject{{QStringLiteral("error"), noAnswerError(socket, 120)}}).toJson(QJsonDocument::Compact).constData());
            return 2;
        }
        const bool busy = reply.value(QStringLiteral("error")).toObject().value(QStringLiteral("code")).toString() == QLatin1String("occupe");
        if (!busy || busySince.elapsed() > 60000) {
            break;
        }
        QThread::msleep(500);
    }
    if (reply.contains(QStringLiteral("error"))) {
        printJson(stderr, QJsonObject{{QStringLiteral("error"), reply.value(QStringLiteral("error"))}}, compact);
        return 1;
    }
    printJson(stdout, reply.value(QStringLiteral("result")), compact);
    if (follow) {
        // Imprime les événements jusqu'à Ctrl+C ou la fermeture de KoraCut.
        QByteArray buffer;
        while (socket.state() == QLocalSocket::ConnectedState) {
            if (!socket.waitForReadyRead(1000)) {
                continue;
            }
            buffer += socket.readAll();
            int nl = buffer.indexOf('\n');
            while (nl >= 0) {
                const QByteArray line = buffer.left(nl);
                buffer.remove(0, nl + 1);
                const QJsonObject msg = QJsonDocument::fromJson(line).object();
                if (msg.value(QStringLiteral("method")).toString() == QLatin1String("evenement")) {
                    printJson(stdout, msg.value(QStringLiteral("params")), true);
                }
                nl = buffer.indexOf('\n');
            }
        }
    }
    return 0;
}
