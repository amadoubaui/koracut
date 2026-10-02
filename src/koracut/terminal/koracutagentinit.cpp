/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutagentinit.h"
#include "koracut/koracutvideotypes.h"
#include "koracut/script/koracutscript.h"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

#include <functional>

namespace {

// Les anciens CLAUDE.md écrits par KoraCut avant le 25 septembre 2026 commençaient par cette ligne.
const char *kOldMarker = "<!-- écrit par KoraCut : ce bloc est refait à chaque ouverture du terminal -->";

QString readText(const QString &path, bool *ok = nullptr)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (ok) {
            *ok = false;
        }
        return QString();
    }
    if (ok) {
        *ok = true;
    }
    return QString::fromUtf8(f.readAll());
}

bool writeText(const QString &path, const QString &text, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = QStringLiteral("ecriture_impossible : %1").arg(path);
        }
        return false;
    }
    f.write(text.toUtf8());
    if (!f.commit()) {
        if (error) {
            *error = QStringLiteral("ecriture_impossible : %1").arg(path);
        }
        return false;
    }
    return true;
}

QString tc(const QJsonValue &v)
{
    const QJsonObject o = v.toObject();
    return o.value(QStringLiteral("tc")).toString();
}

QString relative(const QString &dir, const QString &path)
{
    return QDir(dir).relativeFilePath(path);
}

QString ctl(const QString &reglage)
{
    return reglage.isEmpty() ? QStringLiteral("koracut-ctl") : QStringLiteral("koracut-ctl --reglage %1").arg(reglage);
}

// Le serveur `koracut`, dans la forme commune { command, args, env } (plus « type » et « tools » quand
// l'agent les demande : « type » et « outils » dans le catalogue).
QJsonObject stdioServer(const KoraCutAgentInit::Context &ctx, const QJsonObject &mcp = QJsonObject())
{
    QJsonObject env;
    if (!ctx.porteFile.isEmpty()) {
        env.insert(QStringLiteral("KORACUT_PORTE"), ctx.porteFile);
    }
    if (!ctx.reglage.isEmpty()) {
        env.insert(QStringLiteral("KORACUT_REGLAGE"), ctx.reglage);
    }
    QJsonObject server{{QStringLiteral("command"), ctx.nodePath.isEmpty() ? QStringLiteral("node") : ctx.nodePath},
                       {QStringLiteral("args"), QJsonArray{ctx.mcpScript}},
                       {QStringLiteral("env"), env}};
    if (mcp.contains(QStringLiteral("type"))) {
        server.insert(QStringLiteral("type"), mcp.value(QStringLiteral("type")));
    }
    if (mcp.contains(QStringLiteral("outils"))) {
        server.insert(QStringLiteral("tools"), mcp.value(QStringLiteral("outils")));
    }
    return server;
}

// Un bloc entre « # KORACUT:DEBUT » et « # KORACUT:FIN » dans un fichier à commentaires dièse (TOML,
// crushrc) : remplacé s'il existe, ajouté à la fin sinon ; @p conflict déjà écrit par la personne
// hors du bloc : on ne touche à rien.
bool writeHashBlock(const QString &path, const QString &block, const QString &conflict, QString *error)
{
    const QString content = readText(path);
    const int b = content.indexOf(QLatin1String("# KORACUT:DEBUT"));
    const int e = b >= 0 ? content.indexOf(QLatin1String("# KORACUT:FIN"), b) : -1;
    QString next;
    if (b >= 0 && e > b) {
        int after = content.indexOf(QLatin1Char('\n'), e);
        after = after < 0 ? int(content.size()) : after + 1;
        next = content.left(b) + block + content.mid(after);
    } else if (!conflict.isEmpty() && content.contains(conflict)) {
        if (error) {
            *error = QStringLiteral("deja_configure : %1").arg(path);
        }
        return false;
    } else {
        next = content;
        if (!next.isEmpty() && !next.endsWith(QLatin1Char('\n'))) {
            next += QLatin1Char('\n');
        }
        if (!next.isEmpty()) {
            next += QLatin1Char('\n');
        }
        next += block;
    }
    return next == content || writeText(path, next, error);
}

// Pose @p value à @p keyPath (« mcpServers.koracut ») dans un fichier JSON, sans toucher au reste.
bool mergeJson(const QString &path, const QStringList &keyPath, const QJsonValue &value, const QJsonObject &skeleton, QString *error)
{
    QJsonObject root = skeleton;
    if (QFileInfo::exists(path)) {
        bool ok = false;
        const QString text = readText(path, &ok);
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
        if (!ok || (!text.trimmed().isEmpty() && (parseError.error != QJsonParseError::NoError || !doc.isObject()))) {
            // Un fichier écrit à la main avec des commentaires (JSONC) : on ne le réécrit pas.
            if (error) {
                *error = QStringLiteral("fichier_illisible : %1").arg(path);
            }
            return false;
        }
        if (doc.isObject()) {
            root = doc.object();
        }
    }
    // Descente, remplacement, remontée.
    std::function<QJsonObject(QJsonObject, int)> put = [&](QJsonObject obj, int depth) -> QJsonObject {
        const QString &key = keyPath.at(depth);
        if (depth == keyPath.size() - 1) {
            obj.insert(key, value);
            return obj;
        }
        obj.insert(key, put(obj.value(key).toObject(), depth + 1));
        return obj;
    };
    const QJsonObject next = put(root, 0);
    if (QFileInfo::exists(path) && next == root) {
        return true;
    }
    return writeText(path, QString::fromUtf8(QJsonDocument(next).toJson(QJsonDocument::Indented)), error);
}

QString tomlString(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    out.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + out + QLatin1Char('"');
}

} // namespace

QString KoraCutAgentInit::beginMarker()
{
    return QStringLiteral("<!-- KORACUT:DEBUT · bloc écrit par KoraCut à chaque lancement d'un agent. Écris ton texte à toi en dehors de ce bloc : il est gardé. -->");
}

QString KoraCutAgentInit::endMarker()
{
    return QStringLiteral("<!-- KORACUT:FIN -->");
}

QString KoraCutAgentInit::firstMessage()
{
    return i18nc("@info first message sent to a command line agent launched in the KoraCut terminal; keep koracut-ctl as is",
                 "Read the KoraCut instructions of this folder, look at the project open in KoraCut (koracut-ctl projet info, medias lister, "
                 "timeline etat), then tell me in a few lines what you understand of the video to edit and propose the first step. "
                 "Do not change anything yet.");
}

QStringList KoraCutAgentInit::firstSkills()
{
    return {QStringLiteral("koracut-piloter"), QStringLiteral("koracut-methode-monter-une-video"), QStringLiteral("koracut-retours-cibles"),
            QStringLiteral("koracut-montage-visible"), QStringLiteral("koracut-quand-un-outil-manque")};
}

// Le gabarit du 1er octobre 2026 (document 18, journal) : court, le rôle d'abord, puis l'état du
// projet, comment agir, les règles, les compétences à lire. Le détail du métier est dans les compétences.
QString KoraCutAgentInit::instructionsBlock(const Context &ctx, const QString &agentName)
{
    const QJsonObject &info = ctx.project;
    const QString dir = info.value(QStringLiteral("dossier")).toString();
    const QString projectFile = QFileInfo(info.value(QStringLiteral("chemin")).toString()).fileName();
    const QString name = info.value(QStringLiteral("nom")).toString(projectFile);
    const QJsonObject profile = info.value(QStringLiteral("profil")).toObject();
    const QString typeId = info.value(QStringLiteral("type_video")).toString();
    const KoraCutVideoTypes::VideoType *type = typeId.isEmpty() ? nullptr : KoraCutVideoTypes::byId(typeId);
    const QString ctlCmd = ctl(ctx.reglage);
    const bool isClaude = agentName == QLatin1String("Claude Code");

    QString text;
    QTextStream out(&text);
    out << "# Projet de montage KoraCut : " << (name.isEmpty() ? QStringLiteral("sans nom") : name) << "\n\n";

    out << "## Où tu es, ton rôle\n\n";
    out << "Tu tournes dans le terminal de **KoraCut**, une application de montage vidéo. Ce dossier est celui du projet ouvert dans l'application, "
           "juste au-dessus de toi. Tu n'es pas là pour écrire du code : **tu es le monteur, aux commandes de l'application**. La personne qui t'a lancé";
    if (!agentName.isEmpty()) {
        out << " (dans " << agentName << ")";
    }
    out << " regarde l'écran pendant que tu montes. Tout ce que tu fais passe par la porte de commande de KoraCut, se voit à l'écran et se défait "
           "d'un Annuler. Réponds dans sa langue, le français par défaut, en la tutoyant, en phrases simples et courtes.\n\n";

    out << "## Le projet ouvert\n\n";
    out << "- Nom : " << (name.isEmpty() ? QStringLiteral("sans nom") : name) << "\n";
    out << "- Fichier : `" << (projectFile.isEmpty() ? QStringLiteral("pas encore enregistré") : projectFile) << "`";
    if (info.value(QStringLiteral("modifie")).toBool()) {
        out << " (des changements ne sont pas enregistrés)";
    }
    out << "\n";
    if (!dir.isEmpty()) {
        out << "- Dossier : `" << QDir::toNativeSeparators(dir) << "`\n";
    }
    if (!profile.isEmpty()) {
        out << "- Format : " << profile.value(QStringLiteral("largeur")).toInt() << " × " << profile.value(QStringLiteral("hauteur")).toInt() << " à "
            << profile.value(QStringLiteral("ips")).toDouble() << " images par seconde\n";
    }
    const QJsonArray sequences = info.value(QStringLiteral("sequences")).toArray();
    if (!sequences.isEmpty()) {
        out << "- Séquences :";
        for (const QJsonValue &s : sequences) {
            const QJsonObject o = s.toObject();
            out << " « " << o.value(QStringLiteral("nom")).toString() << " » (" << tc(o.value(QStringLiteral("duree"))) << ")";
        }
        out << "\n";
    }
    if (type) {
        out << "- Type de vidéo : **" << type->name << "**. " << type->description << "\n";
        if (!type->platforms.isEmpty()) {
            out << "- Plateformes : " << type->platforms << "\n";
        }
        if (!type->duration.isEmpty()) {
            out << "- Durée visée : " << type->duration << "\n";
        }
        out << "- Export prévu : " << type->exportLufs << " LUFS";
        if (!type->exportCodec.isEmpty()) {
            out << ", " << type->exportCodec;
        }
        out << "\n";
    } else if (!typeId.isEmpty()) {
        out << "- Type de vidéo : " << info.value(QStringLiteral("type_video_nom")).toString(typeId) << "\n";
    } else {
        out << "- Type de vidéo : pas choisi. Demande-le si le sujet ne le dit pas (publicité, podcast, tutoriel, clip…).\n";
    }
    const QJsonObject folders = info.value(QStringLiteral("dossiers")).toObject();
    for (const QString &key : {QStringLiteral("rushs"), QStringLiteral("exports")}) {
        if (folders.contains(key)) {
            out << "- Dossier " << key << " : `" << relative(dir, folders.value(key).toString()) << "/`\n";
        }
    }
    if (ctx.medias.isEmpty()) {
        out << "- Chutier : vide. Les rushs sont peut-être déjà dans le dossier (`" << ctlCmd << " fichiers lister recursif=true`).\n";
    } else {
        out << "- Chutier (" << ctx.medias.size() << (ctx.medias.size() > 1 ? " médias" : " média") << ") :\n";
        int shown = 0;
        for (const QJsonValue &v : ctx.medias) {
            const QJsonObject m = v.toObject();
            if (++shown > 20) {
                out << "  - … et " << (ctx.medias.size() - 20) << " autres (`" << ctlCmd << " medias lister`)\n";
                break;
            }
            out << "  - `" << m.value(QStringLiteral("id")).toVariant().toString() << "` " << m.value(QStringLiteral("nom")).toString() << " ("
                << m.value(QStringLiteral("type")).toString();
            const QString d = tc(m.value(QStringLiteral("duree")));
            if (!d.isEmpty() && d != QLatin1String("00:00:00:00")) {
                out << ", " << d;
            }
            if (m.value(QStringLiteral("utilise")).toInt() > 0) {
                out << ", déjà dans la timeline";
            }
            out << ")\n";
        }
    }
    const QString scriptJson = info.value(QStringLiteral("proprietes")).toObject().value(KoraCutScript::PropertyName).toString();
    bool scriptOk = false;
    const KoraCutScript script = scriptJson.isEmpty() ? KoraCutScript() : KoraCutScript::fromString(scriptJson, &scriptOk);
    const QString markdown = scriptOk ? script.toMarkdown().trimmed() : QString();
    if (!markdown.isEmpty()) {
        QStringList lines = markdown.split(QLatin1Char('\n'));
        const int total = int(lines.size());
        if (total > 20) {
            lines = lines.mid(0, 20);
        }
        out << "- Script (panneau Script), le début :\n\n";
        for (const QString &l : std::as_const(lines)) {
            out << "> " << l << "\n";
        }
        if (total > 20) {
            out << "> …\n";
        }
        out << "\n  Le script entier : `" << ctlCmd << " timeline outil nom=script_lire` (`sequence=<rang ou nom>` pour une autre séquence).\n";
    } else {
        out << "- Script : aucun pour l'instant. Avant de monter, écris-le avec `script_modele` et `script_ecrire` (transcription d'un rush : `" << ctlCmd
            << " medias analyser media_id=<id> quoi=transcription`).\n";
    }
    out << "\nCet état date du lancement. Relis-le toujours dans l'application avant d'agir.\n";

    out << "\n## Comment agir\n\n";
    out << "La porte de commande est déjà connue de ce terminal (`KORACUT_PORTE`). Deux façons, qui font la même chose :\n\n";
    if (!ctx.mcpScript.isEmpty()) {
        out << "- Les outils MCP `koracut` (`koracut_projet_info`, `koracut_timeline_etat`, `koracut_timeline_outil`, `koracut_medias_importer`…), branchés pour toi.\n";
    }
    out << "- La ligne de commande `" << ctlCmd << " <famille> <methode> clef=valeur` ; `koracut-ctl --aide` liste tout.\n\n";
    out << "```sh\n";
    out << ctlCmd << " projet info\n";
    out << ctlCmd << " timeline etat niveau=resume\n";
    out << ctlCmd << " medias importer chemins='[\"Rushs/01 - Intro.mp4\"]' dossier=Rushs\n";
    out << ctlCmd << " timeline outil nom=ajouter_clip args='{\"media_id\": \"3\", \"piste_id\": 2, \"position\": 0}'\n";
    out << "```\n\n";
    out << "N'écris pas dans le fichier `.koracut` et ne lance pas ffmpeg à la place de l'application : passe par la porte.\n";

    out << "\n## Au démarrage\n\n";
    out << "1. Lis l'état : `projet info`, `medias lister`, `timeline etat niveau=resume`.\n";
    out << "2. Dis en quelques lignes ce que tu comprends de la vidéo à monter et propose ton plan. Attends l'accord avant de toucher au montage.\n";

    out << "\n## Les règles\n\n";
    out << "1. **Montage visible.** Un geste à la fois par la porte, la tête de lecture sur ce que tu fais, la personne voit chaque étape et peut "
           "t'arrêter. Pas de gros script qui fait tout en arrière-plan. Une étape de travail = un tour (`historique tour faire=debut titre='…'`, "
           "puis `faire=fin`) : un seul Annuler la défait.\n";
    out << "2. **Retours ciblés.** Quand la personne demande un changement, tu modifies cela et rien d'autre. Tu ne remontes jamais la vidéo, "
           "tu ne retouches pas ce qui n'a pas été demandé.\n";
    out << "3. **Quand un outil manque, dis-le.** Si KoraCut ne sait pas faire proprement ce qu'il faut, arrête-toi et dis clairement ce qui manque, "
           "plutôt que contourner ou livrer un résultat médiocre.\n";
    out << "4. **L'étalonnage par défaut est clair et vif** : ombres relevées, hautes lumières retenues, couleurs franches, tout se lit "
           "(fiche `koracut-etalonnage-hdr-clair`). Un autre style seulement si la personne le demande.\n";
    out << "5. **Rien sur un visage ni sur un téléphone.** Aucun titre, bandeau, sous-titre, logo ou animation ne recouvre un visage, ni l'écran "
           "d'un téléphone montré à l'image.\n";
    out << "6. Lis `timeline etat` avant d'agir et relis-le après une coupe ou un Annuler : les identifiants changent. Les temps sont en images "
           "ou en timecode `HH:MM:SS:II`, `out` est exclu.\n";
    out << "7. Ne supprime jamais un fichier. `fichiers corbeille` existe, à confirmation. Renommer et déplacer restent dans le dossier du projet.\n";
    out << "8. `confirmation_requise` : la personne doit répondre dans KoraCut, ne relance pas en boucle. `occupe` : réessaie une seconde plus tard.\n";
    out << "9. N'exporte que quand la personne le demande (`export lancer`).\n";

    out << "\n## Les compétences\n\n";
    QStringList known;
    for (const QString &s : ctx.skills) {
        known << s.section(QLatin1Char('|'), 0, 0);
    }
    out << "Le métier est dans les compétences KoraCut";
    if (isClaude) {
        out << ", installées pour toi (" << (ctx.skills.isEmpty() ? QStringLiteral("dossier `~/.claude/skills/`") : QStringLiteral("%1 fiches `koracut-*`").arg(ctx.skills.size()))
            << ")";
    }
    out << ". Lis ces cinq-là d'abord, dans cet ordre, avant ton premier geste :\n\n";
    int rank = 0;
    for (const QString &first : firstSkills()) {
        out << ++rank << ". `" << first << "`";
        for (const QString &s : ctx.skills) {
            const QStringList parts = s.split(QLatin1Char('|'));
            if (parts.value(0) == first && !isClaude && !parts.value(2).isEmpty()) {
                out << " (`" << parts.value(2) << "`)";
            }
        }
        if (!ctx.skills.isEmpty() && !known.contains(first)) {
            out << " (absente de cette machine)";
        }
        out << "\n";
    }
    out << "\nPuis celle du travail en cours : son nom dit son sujet (`koracut-son-*`, `koracut-etalonnage-*`, `koracut-organiser-*`, `koracut-script-*`, "
           "`koracut-sous-titres`, `koracut-animer`, `koracut-export-*`…).";
    if (!isClaude && !ctx.skills.isEmpty()) {
        const QString anyPath = ctx.skills.first().section(QLatin1Char('|'), 2);
        out << " Ce sont des fichiers Markdown, dans `" << QFileInfo(QFileInfo(anyPath).absolutePath()).absolutePath() << "`.";
    }
    out << "\n\nPour tout motion design (titre animé, bandeau, habillage) : écris la composition dans `motion/<nom>/` et pose-la avec "
           "`timeline outil nom=motion_poser`.\n";
    out.flush();
    return text;
}

bool KoraCutAgentInit::writeMarkdownBlock(const QString &file, const QString &block, QString *error)
{
    const QString begin = beginMarker();
    const QString end = endMarker();
    const QString wrapped = begin + QLatin1Char('\n') + block.trimmed() + QLatin1Char('\n') + end + QLatin1Char('\n');
    if (!QFileInfo::exists(file)) {
        return writeText(file, wrapped, error);
    }
    bool ok = false;
    QString content = readText(file, &ok);
    if (!ok) {
        if (error) {
            *error = QStringLiteral("lecture_impossible : %1").arg(file);
        }
        return false;
    }
    QString next;
    const int b = content.indexOf(QLatin1String("<!-- KORACUT:DEBUT"));
    const int e = b >= 0 ? content.indexOf(end, b) : -1;
    if (b >= 0 && e > b) {
        // Le bloc existe : on le remplace, ce qui est autour reste tel quel.
        int after = e + end.size();
        if (after < content.size() && content.at(after) == QLatin1Char('\n')) {
            ++after;
        }
        next = content.left(b) + wrapped + content.mid(after);
    } else if (content.startsWith(QString::fromUtf8(kOldMarker))) {
        // Un ancien fichier entièrement écrit par KoraCut : on le remplace.
        next = wrapped;
    } else {
        // Un fichier à la personne : son texte d'abord, notre bloc à la fin.
        next = content;
        if (!next.isEmpty() && !next.endsWith(QLatin1Char('\n'))) {
            next += QLatin1Char('\n');
        }
        if (!next.isEmpty()) {
            next += QLatin1Char('\n');
        }
        next += wrapped;
    }
    if (next == content) {
        return true;
    }
    return writeText(file, next, error);
}

QString KoraCutAgentInit::writeMcpConfig(const QString &projectDir, const KoraCutAgentsCli::Agent &agent, const Context &ctx, QString *error)
{
    if (ctx.mcpScript.isEmpty() || agent.mcp.isEmpty()) {
        return QString();
    }
    const QString format = agent.mcp.value(QStringLiteral("format")).toString();
    const QString relativeFile = agent.mcp.value(QStringLiteral("fichier")).toString();
    if (relativeFile.isEmpty()) {
        return QString();
    }
    const QString path = QDir(projectDir).absoluteFilePath(relativeFile);
    const QJsonObject server = stdioServer(ctx, agent.mcp);
    bool ok = false;
    if (format == QLatin1String("opencode")) {
        QJsonArray command{server.value(QStringLiteral("command")).toString()};
        for (const QJsonValue &a : server.value(QStringLiteral("args")).toArray()) {
            command.append(a);
        }
        const QJsonObject local{{QStringLiteral("type"), QStringLiteral("local")},
                                {QStringLiteral("command"), command},
                                {QStringLiteral("environment"), server.value(QStringLiteral("env"))},
                                {QStringLiteral("enabled"), true}};
        const QJsonObject skeleton = relativeFile == QLatin1String("opencode.json")
            ? QJsonObject{{QStringLiteral("$schema"), QStringLiteral("https://opencode.ai/config.json")}}
            : QJsonObject();
        ok = mergeJson(path, {QStringLiteral("mcp"), QStringLiteral("koracut")}, local, skeleton, error);
    } else if (format == QLatin1String("crush")) {
        QJsonObject s = server;
        s.insert(QStringLiteral("type"), QStringLiteral("stdio"));
        ok = mergeJson(path, {QStringLiteral("mcp"), QStringLiteral("koracut")}, s,
                       QJsonObject{{QStringLiteral("$schema"), QStringLiteral("https://charm.land/crush.json")}}, error);
    } else if (format == QLatin1String("amp")) {
        ok = mergeJson(path, {QStringLiteral("amp.mcpServers"), QStringLiteral("koracut")}, server, QJsonObject(), error);
    } else if (format == QLatin1String("codex-toml")) {
        // TOML : un bloc entre deux commentaires marqueurs, le reste du fichier gardé.
        QString block;
        QTextStream out(&block);
        out << "# KORACUT:DEBUT (écrit par KoraCut, refait à chaque lancement)\n";
        out << "[mcp_servers.koracut]\n";
        out << "command = " << tomlString(server.value(QStringLiteral("command")).toString()) << "\n";
        out << "args = [" << tomlString(ctx.mcpScript) << "]\n";
        const QJsonObject env = server.value(QStringLiteral("env")).toObject();
        if (!env.isEmpty()) {
            QStringList pairs;
            for (auto it = env.begin(); it != env.end(); ++it) {
                pairs << it.key() + QStringLiteral(" = ") + tomlString(it.value().toString());
            }
            out << "env = { " << pairs.join(QStringLiteral(", ")) << " }\n";
        }
        out << "# KORACUT:FIN\n";
        out.flush();
        ok = writeHashBlock(path, block, QStringLiteral("[mcp_servers.koracut]"), error);
    } else if (format == QLatin1String("crushrc")) {
        // .crushrc est en syntaxe Bash : une commande « mcp add ».
        QString line = QStringLiteral("mcp add koracut --command %1 --args %2")
                           .arg(KoraCutAgentsCli::shellQuote(server.value(QStringLiteral("command")).toString()), KoraCutAgentsCli::shellQuote(ctx.mcpScript));
        const QJsonObject env = server.value(QStringLiteral("env")).toObject();
        for (auto it = env.begin(); it != env.end(); ++it) {
            line += QStringLiteral(" --env %1 %2").arg(it.key(), KoraCutAgentsCli::shellQuote(it.value().toString()));
        }
        const QString block = QStringLiteral("# KORACUT:DEBUT (écrit par KoraCut, refait à chaque lancement)\n") + line + QStringLiteral("\n# KORACUT:FIN\n");
        ok = writeHashBlock(path, block, QStringLiteral("mcp add koracut"), error);
    } else if (format == QLatin1String("vscode")) {
        // .vscode/mcp.json et Copilot : « servers », type stdio.
        QJsonObject s = server;
        s.insert(QStringLiteral("type"), QStringLiteral("stdio"));
        ok = mergeJson(path, {QStringLiteral("servers"), QStringLiteral("koracut")}, s, QJsonObject(), error);
    } else {
        // claude, gemini, qwen, cursor, kilo, copilot… : { "mcpServers": { "koracut": { command, args, env } } }.
        const QString key = agent.mcp.value(QStringLiteral("cle")).toString(QStringLiteral("mcpServers"));
        ok = mergeJson(path, {key, QStringLiteral("koracut")}, server, QJsonObject(), error);
    }
    // Le serveur que KoraCut vient d'écrire est approuvé d'avance quand l'agent le permet (Claude Code :
    // « enabledMcpjsonServers » dans .claude/settings.local.json), les autres serveurs restent à la personne.
    const QJsonObject approval = agent.mcp.value(QStringLiteral("approbation")).toObject();
    if (ok && !approval.isEmpty()) {
        const QString approvalPath = QDir(projectDir).absoluteFilePath(approval.value(QStringLiteral("fichier")).toString());
        const QString key = approval.value(QStringLiteral("cle")).toString();
        QJsonObject root;
        bool readable = true;
        if (QFileInfo::exists(approvalPath)) {
            const QJsonDocument doc = QJsonDocument::fromJson(readText(approvalPath).toUtf8());
            readable = doc.isObject();
            root = doc.object();
        }
        QJsonArray list = root.value(key).toArray();
        if (readable && !key.isEmpty() && !list.contains(QStringLiteral("koracut"))) {
            list.append(QStringLiteral("koracut"));
            root.insert(key, list);
            writeText(approvalPath, QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)), nullptr);
        }
    }
    return ok ? path : QString();
}

QStringList KoraCutAgentInit::mcpLaunchArguments(const KoraCutAgentsCli::Agent &agent, const Context &ctx)
{
    QStringList out;
    if (ctx.mcpScript.isEmpty()) {
        return out;
    }
    const QJsonArray line = agent.mcp.value(QStringLiteral("ligne")).toArray();
    if (line.isEmpty()) {
        return out;
    }
    // Les valeurs sont posées telles quelles : chaque argument est protégé plus tard, à l'écriture de la ligne.
    const QJsonObject server = stdioServer(ctx, agent.mcp);
    const QJsonObject servers{{QStringLiteral("koracut"), server}};
    const QString json = QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("mcpServers"), servers}}).toJson(QJsonDocument::Compact));
    const QString jsonServers = QString::fromUtf8(QJsonDocument(servers).toJson(QJsonDocument::Compact));
    for (const QJsonValue &v : line) {
        QString arg = v.toString();
        arg.replace(QStringLiteral("{json}"), json);
        arg.replace(QStringLiteral("{json_serveurs}"), jsonServers);
        arg.replace(QStringLiteral("{command}"), server.value(QStringLiteral("command")).toString());
        arg.replace(QStringLiteral("{args_toml}"), QLatin1Char('[') + tomlString(ctx.mcpScript) + QLatin1Char(']'));
        arg.replace(QStringLiteral("{args}"), ctx.mcpScript);
        arg.replace(QStringLiteral("{wrapper}"), ctx.mcpWrapper.isEmpty() ? server.value(QStringLiteral("command")).toString() + QLatin1Char(' ') + ctx.mcpScript : ctx.mcpWrapper);
        arg.replace(QStringLiteral("{porte}"), ctx.porteFile);
        const QString dir = ctx.project.value(QStringLiteral("dossier")).toString();
        arg.replace(QStringLiteral("{fichier}"), QDir(dir).absoluteFilePath(agent.mcp.value(QStringLiteral("fichier")).toString()));
        out << arg;
    }
    return out;
}

QStringList KoraCutAgentInit::writeForAgent(const QString &projectDir, const KoraCutAgentsCli::Agent &agent, const Context &ctx, QStringList *errors)
{
    QStringList written;
    if (projectDir.isEmpty() || !QFileInfo(projectDir).isDir()) {
        if (errors) {
            *errors << QStringLiteral("dossier_absent");
        }
        return written;
    }
    const QString block = instructionsBlock(ctx, agent.name);
    // Seulement les fichiers que l'agent lit : pas de CLAUDE.md pour Codex, pas d'AGENTS.md pour Claude Code.
    QStringList files = agent.instructions;
    if (files.isEmpty()) {
        files << QStringLiteral("AGENTS.md");
    }
    for (QString f : std::as_const(files)) {
        // Un dossier de règles (« .cursor/rules/ ») reçoit un fichier à nous.
        if (f.endsWith(QLatin1Char('/'))) {
            f += QStringLiteral("koracut.md");
        }
        const QString path = QDir(projectDir).absoluteFilePath(f);
        QString error;
        if (writeMarkdownBlock(path, block, &error)) {
            written << path;
        } else if (errors) {
            *errors << error;
        }
    }
    QString error;
    const QString mcp = writeMcpConfig(projectDir, agent, ctx, &error);
    if (!mcp.isEmpty()) {
        written << mcp;
    } else if (!error.isEmpty() && errors) {
        *errors << error;
    }
    return written;
}

QString KoraCutAgentInit::findMcpServer()
{
    const QString env = qEnvironmentVariable("KORACUT_MCP_SERVER");
    if (!env.isEmpty() && QFileInfo::exists(env)) {
        return env;
    }
    const QString shipped = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("agents/mcp/main.js"));
    if (!shipped.isEmpty()) {
        return QFileInfo(shipped).canonicalFilePath();
    }
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {appDir + QStringLiteral("/../Resources/mcp/main.js"), appDir + QStringLiteral("/../share/koracut/mcp/main.js"),
                              appDir + QStringLiteral("/mcp/main.js")};
#ifdef KORACUT_SOURCE_ROOT
    candidates << QStringLiteral(KORACUT_SOURCE_ROOT) + QStringLiteral("/Agents in-app/mcp/dist/main.js");
#endif
    for (const QString &c : std::as_const(candidates)) {
        if (QFileInfo::exists(c)) {
            return QFileInfo(c).canonicalFilePath();
        }
    }
    return QString();
}

QString KoraCutAgentInit::findData(const QString &relative)
{
    const QString path = QStringLiteral("agents/") + relative;
    QString found = QStandardPaths::locate(QStandardPaths::AppDataLocation, path);
    if (found.isEmpty()) {
        found = QStandardPaths::locate(QStandardPaths::AppDataLocation, path, QStandardPaths::LocateDirectory);
    }
#ifdef KORACUT_SOURCE_ROOT
    if (found.isEmpty()) {
        const QString dev = QStringLiteral(KORACUT_SOURCE_ROOT) + QStringLiteral("/Sources/kdenlive/data/") + path;
        if (QFileInfo::exists(dev)) {
            found = dev;
        }
    }
#endif
    return found;
}

QString KoraCutAgentInit::bundledSkillsRoot()
{
    // KORACUT_SKILLS_DIR : un autre dossier pour les essais.
    const QString env = qEnvironmentVariable("KORACUT_SKILLS_DIR");
    if (!env.isEmpty()) {
        return QFileInfo(env).isDir() ? env : QString();
    }
    const QString shipped = QStandardPaths::locate(QStandardPaths::AppDataLocation, QStringLiteral("agents/skills"), QStandardPaths::LocateDirectory);
    if (!shipped.isEmpty()) {
        return shipped;
    }
#ifdef KORACUT_SOURCE_ROOT
    const QString dev = QStringLiteral(KORACUT_SOURCE_ROOT) + QStringLiteral("/Sources/koracut-skills");
    if (QFileInfo(dev).isDir()) {
        return dev;
    }
#endif
    return QString();
}

QStringList KoraCutAgentInit::findSkills()
{
    // Celles de la personne d'abord (Claude Code les lit là), puis celles livrées avec l'application.
    QStringList roots = {QDir::homePath() + QStringLiteral("/.claude/skills")};
    const QString bundled = bundledSkillsRoot();
    if (!bundled.isEmpty()) {
        roots << bundled;
    }
    QStringList out;
    QStringList seen;
    static const QRegularExpression descRe(QStringLiteral("^description:\\s*(.+)$"), QRegularExpression::MultilineOption);
    for (const QString &root : std::as_const(roots)) {
        const QDir dir(root);
        const QStringList entries = dir.entryList({QStringLiteral("koracut-*")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString &name : entries) {
            const QString skill = dir.absoluteFilePath(name + QStringLiteral("/SKILL.md"));
            if (seen.contains(name) || !QFileInfo::exists(skill)) {
                continue;
            }
            seen << name;
            QString description = descRe.match(readText(skill).left(4000)).captured(1).trimmed();
            description.remove(QLatin1Char('"'));
            if (description.size() > 130) {
                description = description.left(127) + QStringLiteral("…");
            }
            description.replace(QLatin1Char('|'), QLatin1Char('/'));
            out << name + QLatin1Char('|') + description + QLatin1Char('|') + QDir::toNativeSeparators(skill);
        }
    }
    return out;
}
