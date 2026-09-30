/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutagentscli.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPixmap>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>

#include <algorithm>

namespace {

const QString kResourceDir = QStringLiteral(":/koracut/agents-cli/");

QStringList strings(const QJsonValue &v)
{
    QStringList out;
    for (const QJsonValue &s : v.toArray()) {
        out << s.toString();
    }
    return out;
}

QString osKey()
{
#if defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#elif defined(Q_OS_WIN)
    return QStringLiteral("windows");
#else
    return QStringLiteral("linux");
#endif
}

QString &catalogueSource()
{
    static QString path;
    return path;
}

QList<KoraCutAgentsCli::Agent> load()
{
    QList<KoraCutAgentsCli::Agent> out;
    // Un fichier posé à côté des réglages l'emporte : on corrige une commande sans recompiler.
    QString path = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("koracut-agents-cli.json"));
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        path = kResourceDir + QStringLiteral("koracut-agents-cli.json");
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return out;
    }
    catalogueSource() = path;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue &v : root.value(QStringLiteral("agents")).toArray()) {
        const QJsonObject o = v.toObject();
        KoraCutAgentsCli::Agent a;
        a.id = o.value(QStringLiteral("id")).toString();
        if (a.id.isEmpty()) {
            continue;
        }
        a.name = o.value(QStringLiteral("nom")).toString(a.id);
        a.publisher = o.value(QStringLiteral("editeur")).toString();
        const QString logo = o.value(QStringLiteral("logo")).toString();
        if (!logo.isEmpty() && QFileInfo::exists(kResourceDir + QStringLiteral("icons/") + logo)) {
            a.logo = kResourceDir + QStringLiteral("icons/") + logo;
        }
        a.logoMono = o.value(QStringLiteral("logo_mono")).toBool();
        a.color = o.value(QStringLiteral("couleur")).toString(QStringLiteral("#0D99FF"));
        a.site = o.value(QStringLiteral("site")).toString();
        a.doc = o.value(QStringLiteral("doc")).toString();
        a.license = o.value(QStringLiteral("licence")).toString();
        a.needs = strings(o.value(QStringLiteral("besoin")));
        a.needText = o.value(QStringLiteral("besoin_texte")).toString();
        a.free = o.value(QStringLiteral("gratuit")).toBool();
        a.executables = strings(o.value(QStringLiteral("executables")));
        const QJsonValue install = o.value(QStringLiteral("installation")).toObject().value(osKey());
        if (install.isObject()) {
            const QJsonObject i = install.toObject();
            a.install.command = i.value(QStringLiteral("commande")).toString();
            a.install.shell = i.value(QStringLiteral("shell")).toString();
            a.install.requirements = strings(i.value(QStringLiteral("requiert")));
        }
        a.launch = strings(o.value(QStringLiteral("lancement")));
        if (a.launch.isEmpty() && !a.executables.isEmpty()) {
            a.launch = QStringList{a.executables.first()};
        }
        a.firstMessage = strings(o.value(QStringLiteral("premier_message")));
        a.firstMessageKeepsSession = o.value(QStringLiteral("premier_message_garde_session")).toBool(true);
        a.instructions = strings(o.value(QStringLiteral("instructions")));
        a.readsAgentsMd = o.value(QStringLiteral("lit_agents_md")).toBool();
        a.mcp = o.value(QStringLiteral("mcp")).toObject();
        a.notes = o.value(QStringLiteral("notes")).toString();
        out << a;
    }
    return out;
}

QString home()
{
    return QDir::homePath();
}

QStringList versionedBins(const QString &pattern, const QString &suffix)
{
    // ~/.nvm/versions/node/v22.1.0/bin : la version la plus récente d'abord.
    QStringList out;
    QDir dir(pattern);
    if (!dir.exists()) {
        return out;
    }
    QStringList versions = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(versions.begin(), versions.end(), [](const QString &a, const QString &b) {
        return QVersionNumber::fromString(a.mid(a.startsWith(QLatin1Char('v')) ? 1 : 0)) > QVersionNumber::fromString(b.mid(b.startsWith(QLatin1Char('v')) ? 1 : 0));
    });
    for (const QString &v : versions) {
        out << dir.absoluteFilePath(v) + suffix;
    }
    return out;
}

bool isDevPath(const QString &p)
{
    // L'environnement de développement (KDE Craft) glisse ses outils dans le PATH : un ffmpeg ou un
    // python de Craft ne marche pas hors de l'application. On ne les donne pas au terminal.
    return p.contains(QLatin1String("/CraftRoot/")) || p.endsWith(QLatin1String("/CraftRoot"));
}

} // namespace

const QList<KoraCutAgentsCli::Agent> &KoraCutAgentsCli::all()
{
    static const QList<Agent> data = load();
    return data;
}

const KoraCutAgentsCli::Agent *KoraCutAgentsCli::byId(const QString &id)
{
    for (const Agent &a : all()) {
        if (a.id == id) {
            return &a;
        }
    }
    return nullptr;
}

QString KoraCutAgentsCli::cataloguePath()
{
    all();
    return catalogueSource();
}

QStringList KoraCutAgentsCli::searchPaths()
{
    QStringList out;
    const QString h = home();
#ifdef Q_OS_WIN
    const QChar sep = QLatin1Char(';');
#else
    const QChar sep = QLatin1Char(':');
#endif
    for (const QString &p : qEnvironmentVariable("PATH").split(sep, Qt::SkipEmptyParts)) {
        if (!isDevPath(p)) {
            out << p;
        }
    }
    const QStringList extras = {h + QStringLiteral("/.local/bin"),
                                h + QStringLiteral("/.npm-global/bin"),
                                h + QStringLiteral("/.bun/bin"),
                                h + QStringLiteral("/.cargo/bin"),
                                h + QStringLiteral("/.opencode/bin"),
                                h + QStringLiteral("/.claude/local"),
                                h + QStringLiteral("/.volta/bin"),
                                h + QStringLiteral("/.deno/bin"),
                                h + QStringLiteral("/.yarn/bin"),
                                h + QStringLiteral("/go/bin"),
                                h + QStringLiteral("/.amp/bin"),
                                h + QStringLiteral("/.factory/bin"),
                                h + QStringLiteral("/Library/pnpm"),
                                h + QStringLiteral("/.local/share/pnpm"),
                                QStringLiteral("/opt/homebrew/bin"),
                                QStringLiteral("/usr/local/bin"),
                                QStringLiteral("/home/linuxbrew/.linuxbrew/bin"),
                                QStringLiteral("/snap/bin")};
    for (const QString &p : extras) {
        if (!out.contains(p)) {
            out << p;
        }
    }
    out << versionedBins(h + QStringLiteral("/.nvm/versions/node"), QStringLiteral("/bin"));
    out << versionedBins(h + QStringLiteral("/.local/share/fnm/node-versions"), QStringLiteral("/installation/bin"));
    out << versionedBins(h + QStringLiteral("/Library/Application Support/fnm/node-versions"), QStringLiteral("/installation/bin"));
#ifdef Q_OS_WIN
    const QString appData = qEnvironmentVariable("APPDATA");
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!appData.isEmpty()) {
        out << appData + QStringLiteral("/npm");
    }
    if (!localAppData.isEmpty()) {
        out << localAppData + QStringLiteral("/Programs/nodejs") << localAppData + QStringLiteral("/Microsoft/WinGet/Links");
    }
    out << QStringLiteral("C:/Program Files/nodejs") << h + QStringLiteral("/.local/bin") << h + QStringLiteral("/.bun/bin");
#endif
    out.removeDuplicates();
    return out;
}

QString KoraCutAgentsCli::findExecutable(const QString &name)
{
    if (name.isEmpty()) {
        return QString();
    }
    const QStringList paths = searchPaths();
    QString found = QStandardPaths::findExecutable(name, paths);
#ifdef Q_OS_WIN
    if (found.isEmpty()) {
        for (const QString &ext : {QStringLiteral(".cmd"), QStringLiteral(".exe"), QStringLiteral(".ps1")}) {
            found = QStandardPaths::findExecutable(name + ext, paths);
            if (!found.isEmpty()) {
                break;
            }
        }
    }
#endif
    return found;
}

QString KoraCutAgentsCli::locate(const Agent &agent)
{
    for (const QString &exe : agent.executables) {
        const QString found = findExecutable(exe);
        if (!found.isEmpty()) {
            return found;
        }
    }
    return QString();
}

QString KoraCutAgentsCli::findNode(int *major)
{
    // Une seule question par session : « node --version » coûte une vingtaine de millisecondes.
    static QString cachedPath;
    static int cachedMajor = -1;
    const QString path = findExecutable(QStringLiteral("node"));
    if (path != cachedPath || cachedMajor < 0) {
        cachedPath = path;
        cachedMajor = 0;
        if (!path.isEmpty()) {
            QProcess p;
            p.start(path, {QStringLiteral("--version")});
            if (p.waitForFinished(3000)) {
                const QString v = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
                cachedMajor = QVersionNumber::fromString(v.mid(v.startsWith(QLatin1Char('v')) ? 1 : 0)).majorVersion();
            } else {
                p.kill();
                p.waitForFinished(500);
            }
        }
    }
    if (major) {
        *major = cachedMajor;
    }
    return cachedPath;
}

QStringList KoraCutAgentsCli::missingRequirements(const Agent &agent)
{
    QStringList missing;
    static const QRegularExpression versionRe(QStringLiteral("^([a-z]+)(?:>=([0-9.]+))?$"));
    for (const QString &req : agent.install.requirements) {
        const QRegularExpressionMatch m = versionRe.match(req.trimmed());
        const QString tool = m.hasMatch() ? m.captured(1) : req.trimmed();
        const QVersionNumber wanted = QVersionNumber::fromString(m.captured(2));
        if (tool == QLatin1String("node")) {
            int major = 0;
            if (findNode(&major).isEmpty() || (!wanted.isNull() && major < wanted.majorVersion())) {
                missing << req;
            }
        } else if (tool == QLatin1String("python")) {
            if (findExecutable(QStringLiteral("python3")).isEmpty() && findExecutable(QStringLiteral("python")).isEmpty()) {
                missing << req;
            }
        } else if (tool == QLatin1String("wsl")) {
#ifdef Q_OS_WIN
            if (findExecutable(QStringLiteral("wsl")).isEmpty()) {
                missing << req;
            }
#endif
        } else if (findExecutable(tool).isEmpty()) {
            missing << req;
        }
    }
    return missing;
}

QString KoraCutAgentsCli::shellQuote(const QString &s)
{
#ifdef Q_OS_WIN
    QString out = s;
    out.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + out + QLatin1Char('"');
#else
    if (!s.isEmpty() && QRegularExpression(QStringLiteral("^[A-Za-z0-9_./:=+,@%-]+$")).match(s).hasMatch()) {
        return s;
    }
    QString out = s;
    out.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + out + QLatin1Char('\'');
#endif
}

QString KoraCutAgentsCli::shellLine(const QStringList &args)
{
    QStringList quoted;
    for (const QString &a : args) {
        quoted << shellQuote(a);
    }
    return quoted.join(QLatin1Char(' '));
}

QStringList KoraCutAgentsCli::launchCommand(const Agent &agent, const QString &firstMessage)
{
    if (!firstMessage.isEmpty() && !agent.firstMessage.isEmpty()) {
        QStringList out;
        for (const QString &a : agent.firstMessage) {
            QString arg = a;
            arg.replace(QStringLiteral("{message}"), firstMessage);
            out << arg;
        }
        return out;
    }
    return agent.launch;
}

QString KoraCutAgentsCli::lastChoice()
{
    return KSharedConfig::openConfig()->group(QStringLiteral("KoraCut")).readEntry("terminalAgent", QString());
}

void KoraCutAgentsCli::setLastChoice(const QString &id)
{
    KConfigGroup group = KSharedConfig::openConfig()->group(QStringLiteral("KoraCut"));
    group.writeEntry("terminalAgent", id);
    group.sync();
}

QIcon KoraCutAgentsCli::icon(const Agent &agent, int size)
{
    const qreal dpr = qApp ? qApp->devicePixelRatio() : 1.0;
    QPixmap pix(QSize(size, size) * dpr);
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    if (!agent.logo.isEmpty()) {
        const QPixmap logo = QIcon(agent.logo).pixmap(QSize(size, size), dpr);
        if (agent.logoMono) {
            // Un logo noir sur fond sombre ne se voit pas : on le teint en clair.
            QPixmap tinted = logo;
            QPainter t(&tinted);
            t.setCompositionMode(QPainter::CompositionMode_SourceIn);
            t.fillRect(tinted.rect(), QColor(0xE6, 0xE6, 0xE6));
            t.end();
            p.drawPixmap(QRectF(0, 0, size, size), tinted, QRectF(tinted.rect()));
        } else {
            p.drawPixmap(QRectF(0, 0, size, size), logo, QRectF(logo.rect()));
        }
    } else {
        // Pas de logo sûr : une pastille de sa couleur avec l'initiale, jamais un logo inventé.
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(agent.color));
        p.drawRoundedRect(QRectF(0, 0, size, size), size * 0.28, size * 0.28);
        QFont f = p.font();
        f.setPixelSize(int(size * 0.55));
        f.setBold(true);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, agent.name.left(1).toUpper());
    }
    p.end();
    return QIcon(pix);
}

QString KoraCutAgentsCli::userShell()
{
#ifdef Q_OS_WIN
    const QString comspec = qEnvironmentVariable("COMSPEC");
    return comspec.isEmpty() ? QStringLiteral("cmd.exe") : comspec;
#else
    QString shell = qEnvironmentVariable("SHELL");
    if (shell.isEmpty() || !QFileInfo(shell).isExecutable()) {
        shell = QFileInfo::exists(QStringLiteral("/bin/zsh")) ? QStringLiteral("/bin/zsh") : QStringLiteral("/bin/bash");
    }
    return shell;
#endif
}

QProcessEnvironment KoraCutAgentsCli::terminalEnvironment(const QString &porteFile, const QString &reglage, const QStringList &extraPath)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QStringList keys = env.keys();
    for (const QString &k : keys) {
        const QString v = env.value(k);
        const bool devValue = k != QLatin1String("PATH") && isDevPath(v + QLatin1Char('/'));
        // Ce que l'application ou l'environnement de développement pose pour elle seule, et ce qu'une
        // session Claude Code parente laisserait (Claude Code refuse de se lancer dans une autre).
        if (devValue || k.startsWith(QLatin1String("DYLD_")) || k.startsWith(QLatin1String("QT_")) || k.startsWith(QLatin1String("QML")) ||
            k.startsWith(QLatin1String("MLT_")) || k == QLatin1String("FREI0R_PATH") || k == QLatin1String("LADSPA_PATH") ||
            k.startsWith(QLatin1String("KORACUT_")) || k == QLatin1String("CLAUDECODE") || k.startsWith(QLatin1String("CLAUDE_CODE_")) ||
            k == QLatin1String("TERM_PROGRAM") || k == QLatin1String("TERM_PROGRAM_VERSION") || k == QLatin1String("TERM_SESSION_ID") ||
            k == QLatin1String("ITERM_SESSION_ID") || k == QLatin1String("VSCODE_INJECTION") || k == QLatin1String("OLDPWD")) {
            env.remove(k);
        }
    }
#ifdef Q_OS_WIN
    const QChar sep = QLatin1Char(';');
#else
    const QChar sep = QLatin1Char(':');
#endif
    QStringList path = extraPath;
    for (const QString &p : searchPaths()) {
        if (!path.contains(p) && QFileInfo(p).isDir()) {
            path << p;
        }
    }
    env.insert(QStringLiteral("PATH"), path.join(sep));
#ifndef Q_OS_WIN
    env.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    env.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    env.insert(QStringLiteral("TERM_PROGRAM"), QStringLiteral("KoraCut"));
    env.insert(QStringLiteral("TERM_PROGRAM_VERSION"), QCoreApplication::applicationVersion());
    const QString lang = env.value(QStringLiteral("LC_ALL"), env.value(QStringLiteral("LANG")));
    if (!lang.contains(QLatin1String("UTF-8"), Qt::CaseInsensitive)) {
        env.insert(QStringLiteral("LANG"), QStringLiteral("en_US.UTF-8"));
        env.remove(QStringLiteral("LC_ALL"));
    }
    if (!env.contains(QStringLiteral("HOME"))) {
        env.insert(QStringLiteral("HOME"), home());
    }
#else
    // ConPTY traduit les séquences : les agents peuvent afficher leurs couleurs comme ailleurs.
    env.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    env.insert(QStringLiteral("TERM_PROGRAM"), QStringLiteral("KoraCut"));
    env.insert(QStringLiteral("TERM_PROGRAM_VERSION"), QCoreApplication::applicationVersion());
#endif
    if (!porteFile.isEmpty()) {
        env.insert(QStringLiteral("KORACUT_PORTE"), porteFile);
    }
    if (!reglage.isEmpty()) {
        env.insert(QStringLiteral("KORACUT_REGLAGE"), reglage);
    }
    return env;
}
