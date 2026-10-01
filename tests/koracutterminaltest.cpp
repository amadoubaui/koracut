/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Le terminal intégré et l'init automatique des agents en ligne de commande (document 18, journal du
// 25 septembre 2026) : les fichiers d'instructions gardent le texte de la personne, la configuration
// MCP garde ses autres serveurs, un agent se trouve dans le PATH élargi, et le PTY fait vraiment
// tourner un programme dont on lit la sortie, couleurs comprises.

#include "catch.hpp"

#include "koracut/terminal/koracutagentinit.h"
#include "koracut/terminal/koracutagentscli.h"
#include "koracut/terminal/koracutpty.h"
#include "koracut/terminal/koracutterminalview.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

namespace {

QString readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

void writeAll(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    REQUIRE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(text.toUtf8());
}

// Laisse tourner la boucle d'événements jusqu'à @p done, deux secondes et demie au plus.
template<typename F> bool waitFor(F done, int ms = 2500)
{
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return done();
}

KoraCutAgentInit::Context sampleContext()
{
    KoraCutAgentInit::Context ctx;
    ctx.project = QJsonObject{{QStringLiteral("nom"), QStringLiteral("Tek Tech teaser")},
                              {QStringLiteral("chemin"), QStringLiteral("/tmp/x/Tek Tech.koracut")},
                              {QStringLiteral("dossier"), QStringLiteral("/tmp/x")},
                              {QStringLiteral("profil"), QJsonObject{{QStringLiteral("largeur"), 1080}, {QStringLiteral("hauteur"), 1920}, {QStringLiteral("ips"), 25}}}};
    ctx.medias = QJsonArray{QJsonObject{{QStringLiteral("id"), QStringLiteral("2")}, {QStringLiteral("nom"), QStringLiteral("TEASER TEK TECH.mov")},
                                        {QStringLiteral("type"), QStringLiteral("video")},
                                        {QStringLiteral("duree"), QJsonObject{{QStringLiteral("i"), 1500}, {QStringLiteral("tc"), QStringLiteral("00:01:00:00")}}}}};
    ctx.reglage = QStringLiteral("koracut-essai");
    ctx.porteFile = QStringLiteral("/tmp/porte-koracut-essai.json");
    ctx.nodePath = QStringLiteral("/usr/local/bin/node");
    ctx.mcpScript = QStringLiteral("/Applications/KoraCut dev/mcp/main.js");
    return ctx;
}

} // namespace

TEST_CASE("Init des agents : le bloc KoraCut ne touche pas au texte de la personne", "[KoraCut]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString file = dir.filePath(QStringLiteral("CLAUDE.md"));
    const KoraCutAgentInit::Context ctx = sampleContext();
    const QString block = KoraCutAgentInit::instructionsBlock(ctx, QStringLiteral("Claude Code"));
    CHECK(block.contains(QStringLiteral("Tek Tech teaser")));
    CHECK(block.contains(QStringLiteral("1080 × 1920")));
    CHECK(block.contains(QStringLiteral("TEASER TEK TECH.mov")));
    CHECK(block.contains(QStringLiteral("koracut-ctl --reglage koracut-essai")));
    CHECK_FALSE(block.contains(QChar(0x2014))); // jamais de tiret long

    SECTION("fichier neuf, puis réécrit sans doublon")
    {
        REQUIRE(KoraCutAgentInit::writeMarkdownBlock(file, block));
        const QString once = readAll(file);
        CHECK(once.startsWith(KoraCutAgentInit::beginMarker()));
        CHECK(once.contains(KoraCutAgentInit::endMarker()));
        REQUIRE(KoraCutAgentInit::writeMarkdownBlock(file, block));
        CHECK(readAll(file) == once);
    }

    SECTION("le texte écrit à la main autour du bloc reste tel quel")
    {
        REQUIRE(KoraCutAgentInit::writeMarkdownBlock(file, QStringLiteral("ancien bloc")));
        const QString mine = QStringLiteral("# Mes notes\n\nLe client veut du rouge.\n\n") + readAll(file) + QStringLiteral("\nÀ la fin : pas de musique.\n");
        writeAll(file, mine);
        REQUIRE(KoraCutAgentInit::writeMarkdownBlock(file, block));
        const QString after = readAll(file);
        CHECK(after.startsWith(QStringLiteral("# Mes notes\n\nLe client veut du rouge.\n\n")));
        CHECK(after.endsWith(QStringLiteral("\nÀ la fin : pas de musique.\n")));
        CHECK_FALSE(after.contains(QStringLiteral("ancien bloc")));
        CHECK(after.count(KoraCutAgentInit::endMarker()) == 1);
    }

    SECTION("un fichier sans marqueurs garde tout, le bloc va à la fin")
    {
        writeAll(file, QStringLiteral("Mon CLAUDE.md à moi."));
        REQUIRE(KoraCutAgentInit::writeMarkdownBlock(file, block));
        const QString after = readAll(file);
        CHECK(after.startsWith(QStringLiteral("Mon CLAUDE.md à moi.\n\n")));
        CHECK(after.contains(KoraCutAgentInit::beginMarker()));
    }

    SECTION("un ancien fichier entièrement écrit par KoraCut est remplacé")
    {
        writeAll(file, QStringLiteral("<!-- écrit par KoraCut : ce bloc est refait à chaque ouverture du terminal -->\n# Projet KoraCut : vieux\n"));
        REQUIRE(KoraCutAgentInit::writeMarkdownBlock(file, block));
        CHECK_FALSE(readAll(file).contains(QStringLiteral("vieux")));
    }
}

TEST_CASE("Init des agents : la configuration MCP garde les autres serveurs", "[KoraCut]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const KoraCutAgentInit::Context ctx = sampleContext();
    KoraCutAgentsCli::Agent claude;
    claude.name = QStringLiteral("Claude Code");
    claude.instructions = {QStringLiteral("CLAUDE.md")};
    claude.mcp = QJsonObject{{QStringLiteral("format"), QStringLiteral("claude")},
                             {QStringLiteral("fichier"), QStringLiteral(".mcp.json")},
                             {QStringLiteral("approbation"),
                              QJsonObject{{QStringLiteral("fichier"), QStringLiteral(".claude/settings.local.json")}, {QStringLiteral("cle"), QStringLiteral("enabledMcpjsonServers")}}}};

    SECTION(".mcp.json : le serveur koracut est ajouté à côté de celui de la personne")
    {
        writeAll(dir.filePath(QStringLiteral(".mcp.json")), QStringLiteral(R"({"mcpServers": {"autre": {"command": "autre-serveur"}}, "x": 1})"));
        const QStringList written = KoraCutAgentInit::writeForAgent(dir.path(), claude, ctx);
        CHECK(written.size() == 2);
        const QJsonObject root = QJsonDocument::fromJson(readAll(dir.filePath(QStringLiteral(".mcp.json"))).toUtf8()).object();
        const QJsonObject servers = root.value(QStringLiteral("mcpServers")).toObject();
        CHECK(servers.value(QStringLiteral("autre")).toObject().value(QStringLiteral("command")).toString() == QStringLiteral("autre-serveur"));
        const QJsonObject koracut = servers.value(QStringLiteral("koracut")).toObject();
        CHECK(koracut.value(QStringLiteral("command")).toString() == QStringLiteral("/usr/local/bin/node"));
        CHECK(koracut.value(QStringLiteral("args")).toArray().first().toString() == ctx.mcpScript);
        CHECK(koracut.value(QStringLiteral("env")).toObject().value(QStringLiteral("KORACUT_PORTE")).toString() == ctx.porteFile);
        CHECK(root.value(QStringLiteral("x")).toInt() == 1);
        CHECK(QFileInfo::exists(dir.filePath(QStringLiteral("CLAUDE.md"))));
        CHECK_FALSE(QFileInfo::exists(dir.filePath(QStringLiteral("AGENTS.md"))));
        // Le serveur écrit par KoraCut est approuvé d'avance, une seule fois même après deux lancements.
        KoraCutAgentInit::writeForAgent(dir.path(), claude, ctx);
        const QJsonArray approved = QJsonDocument::fromJson(readAll(dir.filePath(QStringLiteral(".claude/settings.local.json"))).toUtf8())
                                        .object()
                                        .value(QStringLiteral("enabledMcpjsonServers"))
                                        .toArray();
        CHECK(approved == QJsonArray{QStringLiteral("koracut")});
    }

    SECTION("un fichier illisible (commentaires JSONC) n'est pas réécrit")
    {
        const QString jsonc = QStringLiteral("{\n  // mes serveurs\n  \"mcpServers\": {}\n}\n");
        writeAll(dir.filePath(QStringLiteral(".mcp.json")), jsonc);
        QString error;
        CHECK(KoraCutAgentInit::writeMcpConfig(dir.path(), claude, ctx, &error).isEmpty());
        CHECK(error.startsWith(QStringLiteral("fichier_illisible")));
        CHECK(readAll(dir.filePath(QStringLiteral(".mcp.json"))) == jsonc);
    }

    SECTION("TOML de Codex et crushrc : un bloc entre marqueurs, le reste gardé")
    {
        KoraCutAgentsCli::Agent codex;
        codex.mcp = QJsonObject{{QStringLiteral("format"), QStringLiteral("codex-toml")}, {QStringLiteral("fichier"), QStringLiteral(".codex/config.toml")}};
        writeAll(dir.filePath(QStringLiteral(".codex/config.toml")), QStringLiteral("model = \"gpt\"\n"));
        CHECK_FALSE(KoraCutAgentInit::writeMcpConfig(dir.path(), codex, ctx).isEmpty());
        CHECK_FALSE(KoraCutAgentInit::writeMcpConfig(dir.path(), codex, ctx).isEmpty());
        const QString toml = readAll(dir.filePath(QStringLiteral(".codex/config.toml")));
        CHECK(toml.startsWith(QStringLiteral("model = \"gpt\"\n")));
        CHECK(toml.count(QStringLiteral("[mcp_servers.koracut]")) == 1);
        CHECK(toml.contains(QStringLiteral("args = [\"/Applications/KoraCut dev/mcp/main.js\"]")));

        KoraCutAgentsCli::Agent crush;
        crush.mcp = QJsonObject{{QStringLiteral("format"), QStringLiteral("crushrc")}, {QStringLiteral("fichier"), QStringLiteral(".crushrc")}};
        CHECK_FALSE(KoraCutAgentInit::writeMcpConfig(dir.path(), crush, ctx).isEmpty());
#ifndef Q_OS_WIN // guillemets du shell POSIX ; sous Windows, ceux de cmd.exe
        CHECK(readAll(dir.filePath(QStringLiteral(".crushrc"))).contains(QStringLiteral("mcp add koracut --command /usr/local/bin/node --args '/Applications/KoraCut dev/mcp/main.js'")));
#endif
    }

    SECTION("Codex en ligne de commande : des valeurs TOML, sans guillemets en trop")
    {
        KoraCutAgentsCli::Agent codex;
        codex.mcp = QJsonObject{{QStringLiteral("ligne"), QJsonArray{QStringLiteral("-c"), QStringLiteral("mcp_servers.koracut.args={args_toml}")}}};
        const QStringList args = KoraCutAgentInit::mcpLaunchArguments(codex, ctx);
        REQUIRE(args.size() == 2);
        CHECK(args.at(1) == QStringLiteral("mcp_servers.koracut.args=[\"/Applications/KoraCut dev/mcp/main.js\"]"));
    }
}

TEST_CASE("Agents en ligne de commande : catalogue, PATH élargi, premier message", "[KoraCut]")
{
    // Le catalogue embarqué (data/koracut-agents-cli.json) : Claude Code en tête, chacun a une commande.
    const QList<KoraCutAgentsCli::Agent> &agents = KoraCutAgentsCli::all();
    REQUIRE(agents.size() >= 10);
    CHECK(agents.first().id == QStringLiteral("claude-code"));
    for (const KoraCutAgentsCli::Agent &a : agents) {
        CAPTURE(a.id);
        CHECK_FALSE(a.executables.isEmpty());
        CHECK_FALSE(a.launch.isEmpty());
        CHECK_FALSE(a.name.contains(QChar(0x2014)));
    }
    const KoraCutAgentsCli::Agent *gemini = KoraCutAgentsCli::byId(QStringLiteral("gemini-cli"));
    REQUIRE(gemini);
    CHECK(KoraCutAgentsCli::launchCommand(*gemini, QStringLiteral("Bonjour")) == QStringList{QStringLiteral("gemini"), QStringLiteral("-i"), QStringLiteral("Bonjour")});
#ifdef Q_OS_WIN
    // La suite essaie les guillemets POSIX, un faux « claude » en script sh et un PATH séparé par « : » :
    // sous Windows, ces chemins sont essayés par l'application elle-même (workflow, capture du terminal).
    CHECK(KoraCutAgentsCli::shellLine({QStringLiteral("claude"), QStringLiteral("dis")}) == QStringLiteral("\"claude\" \"dis\""));
    return;
#endif
    CHECK(KoraCutAgentsCli::shellLine({QStringLiteral("claude"), QStringLiteral("dis l'état")}) == QStringLiteral("claude 'dis l'\\''état'"));

    // Un faux « claude » posé dans un dossier du PATH : il est trouvé.
    QTemporaryDir bin;
    REQUIRE(bin.isValid());
    const QString fake = bin.filePath(QStringLiteral("claude"));
    writeAll(fake, QStringLiteral("#!/bin/sh\necho faux\n"));
    QFile::setPermissions(fake, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QByteArray oldPath = qgetenv("PATH");
    qputenv("PATH", QByteArray(QFile::encodeName(bin.path()) + QByteArray(":/usr/bin:/bin")));
    const KoraCutAgentsCli::Agent *claude = KoraCutAgentsCli::byId(QStringLiteral("claude-code"));
    REQUIRE(claude);
    CHECK(QFileInfo(KoraCutAgentsCli::locate(*claude)).canonicalFilePath() == QFileInfo(fake).canonicalFilePath());
    // L'environnement du terminal : la porte, TERM, et rien de ce qu'une session Claude Code parente laisserait.
    qputenv("CLAUDECODE", "1");
    const QProcessEnvironment env = KoraCutAgentsCli::terminalEnvironment(QStringLiteral("/tmp/porte.json"), QStringLiteral("essai"), {QStringLiteral("/tmp/lien")});
    qunsetenv("CLAUDECODE");
    qputenv("PATH", oldPath);
    CHECK(env.value(QStringLiteral("KORACUT_PORTE")) == QStringLiteral("/tmp/porte.json"));
    CHECK(env.value(QStringLiteral("PATH")).startsWith(QStringLiteral("/tmp/lien:")));
    CHECK_FALSE(env.contains(QStringLiteral("CLAUDECODE")));
#ifndef Q_OS_WIN
    CHECK(env.value(QStringLiteral("TERM")) == QStringLiteral("xterm-256color"));
#endif
}

#ifndef Q_OS_WIN
TEST_CASE("Terminal : le PTY lance un programme et on lit sa sortie", "[KoraCut]")
{
    SECTION("echo par le PTY brut")
    {
        KoraCutPty pty;
        QByteArray out;
        int code = -2;
        QObject::connect(&pty, &KoraCutPty::dataReceived, [&out](const QByteArray &d) { out += d; });
        QObject::connect(&pty, &KoraCutPty::finished, [&code](int c) { code = c; });
        REQUIRE(pty.start(QStringLiteral("/bin/echo"), {QStringLiteral("bonjour koracut")}, QDir::tempPath(), QProcessEnvironment::systemEnvironment(), 80, 24));
        CHECK(waitFor([&code]() { return code != -2; }));
        CHECK(code == 0);
        CHECK(out.contains("bonjour koracut"));
    }

    SECTION("l'écran du terminal : couleurs lues, taille transmise, texte tapé")
    {
        KoraCutTerminalView view;
        view.resize(640, 320);
        view.show();
        REQUIRE(view.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("printf '\\033[31mrouge\\033[0m '; stty size; read l; echo \"lu:$l\"")},
                           QDir::tempPath(), QProcessEnvironment::systemEnvironment()));
        // stty size répond « lignes colonnes » : celles de la vue, donc la taille est bien passée au PTY.
        CHECK(waitFor([&view]() { return view.screenLines().value(0).contains(QStringLiteral("%1 %2").arg(view.rows()).arg(view.columns())); }));
        const QString first = view.screenLines().value(0);
        const std::string screenDump = view.screenLines().join(QLatin1Char('|')).toStdString();
        CAPTURE(screenDump, view.rows(), view.columns());
        CHECK(first.startsWith(QStringLiteral("rouge ")));
        CHECK(first.contains(QStringLiteral("%1 %2").arg(view.rows()).arg(view.columns())));
        view.sendText(QStringLiteral("ok\r"));
        CHECK(waitFor([&view]() { return view.allText().contains(QStringLiteral("lu:ok")); }));
        CHECK(waitFor([&view]() { return !view.isRunning(); }));
    }

    SECTION("écran alterné : une application plein écran rend l'écran d'avant en partant")
    {
        KoraCutTerminalView view;
        view.resize(640, 320);
        REQUIRE(view.start(QStringLiteral("/bin/sh"),
                           {QStringLiteral("-c"), QStringLiteral("printf 'avant\\n'; printf '\\033[?1049h\\033[Hplein ecran'; sleep 0.3; printf '\\033[?1049l'")},
                           QDir::tempPath(), QProcessEnvironment::systemEnvironment()));
        CHECK(waitFor([&view]() { return view.screenLines().join(QLatin1Char('\n')).contains(QStringLiteral("plein ecran")); }));
        CHECK(waitFor([&view]() { return !view.isRunning(); }));
        const QString screen = view.screenLines().join(QLatin1Char('\n'));
        CHECK(screen.contains(QStringLiteral("avant")));
        CHECK_FALSE(screen.contains(QStringLiteral("plein ecran")));
    }
}
#endif
