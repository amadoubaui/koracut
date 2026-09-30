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
#ifdef Q_OS_WIN
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
    out << "  koracut-ctl --aide [famille]\n\n";
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
    if (!exchange(socket, open, reply, false, 5000) || reply.contains(QStringLiteral("error"))) {
        printJson(stderr, reply.contains(QStringLiteral("error")) ? reply : QJsonObject{{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("jeton_refuse")}, {QStringLiteral("message"), QStringLiteral("KoraCut a refusé l'ouverture.")}}}}, true);
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
        method = positional.takeFirst() + QLatin1Char('.') + positional.takeFirst();
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
            fprintf(stderr, "%s\n", QJsonDocument(QJsonObject{{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), QStringLiteral("porte_fermee")}, {QStringLiteral("message"), QStringLiteral("KoraCut n'a pas répondu.")}}}}).toJson(QJsonDocument::Compact).constData());
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
