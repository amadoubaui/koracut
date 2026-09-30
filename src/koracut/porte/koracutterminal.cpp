/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutterminal.h"
#include "koracut/terminal/koracutagentinit.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

namespace {

QString shellQuote(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + out + QLatin1Char('\'');
}

QStringList extraPaths()
{
    return {QDir::homePath() + QStringLiteral("/.local/bin"), QStringLiteral("/usr/local/bin"), QStringLiteral("/opt/homebrew/bin"),
            QDir::homePath() + QStringLiteral("/.npm-global/bin")};
}

} // namespace

QString KoraCutTerminal::findClaude()
{
    QString found = QStandardPaths::findExecutable(QStringLiteral("claude"));
    if (found.isEmpty()) {
        found = QStandardPaths::findExecutable(QStringLiteral("claude"), extraPaths());
    }
#ifdef Q_OS_WIN
    if (found.isEmpty()) {
        found = QStandardPaths::findExecutable(QStringLiteral("claude.cmd"));
    }
#endif
    return found;
}

QString KoraCutTerminal::defaultCommand()
{
    const QString configured = KSharedConfig::openConfig()->group(QStringLiteral("KoraCut")).readEntry("terminalCommande", QString()).trimmed();
    if (!configured.isEmpty()) {
        return configured == QLatin1String("-") ? QString() : configured;
    }
    return findClaude().isEmpty() ? QString() : QStringLiteral("claude");
}

QString KoraCutTerminal::ctlDirectory()
{
    const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("koracut-ctl.exe");
#else
    const QString name = QStringLiteral("koracut-ctl");
#endif
    const QStringList candidates = {appDir, appDir + QStringLiteral("/../../../bin"), appDir + QStringLiteral("/../bin"), appDir + QStringLiteral("/../Resources/bin")};
    for (const QString &dir : candidates) {
        if (QFileInfo::exists(dir + QLatin1Char('/') + name)) {
            return QDir(dir).canonicalPath();
        }
    }
    const QString inPath = QStandardPaths::findExecutable(QStringLiteral("koracut-ctl"));
    return inPath.isEmpty() ? QString() : QFileInfo(inPath).absolutePath();
}

QString KoraCutTerminal::writeProjectNotes(const QString &projectDir, const QJsonObject &info)
{
    // Depuis le 25 septembre 2026, le même bloc que le terminal intégré (koracut/terminal/koracutagentinit) :
    // entre deux marqueurs, le texte de la personne autour est gardé.
    if (projectDir.isEmpty() || !QFileInfo(projectDir).isDir()) {
        return QString();
    }
    KoraCutAgentInit::Context ctx;
    ctx.project = info;
    ctx.skills = KoraCutAgentInit::findSkills();
    const QString target = projectDir + QStringLiteral("/CLAUDE.md");
    return KoraCutAgentInit::writeMarkdownBlock(target, KoraCutAgentInit::instructionsBlock(ctx, QStringLiteral("Claude Code"))) ? target : QString();
}

bool KoraCutTerminal::open(const QString &projectDir, const QString &porteFile, const QString &ctlDir, const QString &command, QString &error)
{
    if (projectDir.isEmpty() || !QFileInfo(projectDir).isDir()) {
        error = QStringLiteral("dossier_absent");
        return false;
    }
    const QString tmp = QDir::tempPath() + QStringLiteral("/koracut-terminal-%1").arg(QCoreApplication::applicationPid());
    QDir().mkpath(tmp);
#if defined(Q_OS_WIN)
    const QString script = tmp + QStringLiteral("/koracut.cmd");
    QFile f(script);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = QStringLiteral("ecriture_impossible");
        return false;
    }
    QTextStream out(&f);
    out << "@echo off\r\n";
    out << "cd /d \"" << QDir::toNativeSeparators(projectDir) << "\"\r\n";
    out << "set KORACUT_PORTE=" << QDir::toNativeSeparators(porteFile) << "\r\n";
    if (!ctlDir.isEmpty()) {
        out << "set PATH=" << QDir::toNativeSeparators(ctlDir) << ";%PATH%\r\n";
    }
    out << "echo KoraCut : porte ouverte, koracut-ctl --aide pour la liste des commandes.\r\n";
    if (!command.isEmpty()) {
        out << command << "\r\n";
    }
    f.close();
    if (!QStandardPaths::findExecutable(QStringLiteral("wt")).isEmpty()) {
        return QProcess::startDetached(QStringLiteral("wt"), {QStringLiteral("-d"), projectDir, QStringLiteral("cmd"), QStringLiteral("/k"), script});
    }
    return QProcess::startDetached(QStringLiteral("cmd"), {QStringLiteral("/c"), QStringLiteral("start"), QStringLiteral("KoraCut"), QStringLiteral("cmd"), QStringLiteral("/k"), script});
#else
    QString shell = qEnvironmentVariable("SHELL");
    if (shell.isEmpty() || !QFileInfo::exists(shell)) {
        shell = QFileInfo::exists(QStringLiteral("/bin/zsh")) ? QStringLiteral("/bin/zsh") : QStringLiteral("/bin/bash");
    }
    const QString script = tmp + QStringLiteral("/koracut.command");
    QFile f(script);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = QStringLiteral("ecriture_impossible");
        return false;
    }
    QTextStream out(&f);
    // Un shell de connexion : le PATH de la personne (Homebrew, ~/.local/bin) est chargé, Claude Code se trouve.
    out << "#!" << shell << " -l\n";
    out << "cd " << shellQuote(projectDir) << "\n";
    out << "export KORACUT_PORTE=" << shellQuote(porteFile) << "\n";
    QStringList paths = extraPaths();
    if (!ctlDir.isEmpty()) {
        paths.prepend(ctlDir);
    }
    out << "export PATH=" << shellQuote(paths.join(QLatin1Char(':'))) << ":\"$PATH\"\n";
    out << "printf 'KoraCut : porte ouverte dans ce dossier. koracut-ctl --aide liste les commandes.\\n'\n";
    if (!command.isEmpty()) {
        out << command << "\n";
    }
    out << "exec " << shellQuote(shell) << " -l\n";
    f.close();
    QFile::setPermissions(script, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
#if defined(Q_OS_MAC)
    return QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-a"), QStringLiteral("Terminal"), script});
#else
    const QString configured = qEnvironmentVariable("TERMINAL");
    QStringList candidates;
    if (!configured.isEmpty()) {
        candidates << configured;
    }
    candidates << QStringLiteral("x-terminal-emulator") << QStringLiteral("konsole") << QStringLiteral("gnome-terminal") << QStringLiteral("xfce4-terminal")
               << QStringLiteral("kitty") << QStringLiteral("alacritty") << QStringLiteral("xterm");
    for (const QString &term : candidates) {
        const QString exe = QStandardPaths::findExecutable(term);
        if (exe.isEmpty()) {
            continue;
        }
        QStringList args;
        if (term.contains(QLatin1String("gnome-terminal"))) {
            args << QStringLiteral("--working-directory=%1").arg(projectDir) << QStringLiteral("--") << script;
        } else {
            args << QStringLiteral("-e") << script;
        }
        if (QProcess::startDetached(exe, args, projectDir)) {
            return true;
        }
    }
    error = QStringLiteral("terminal_introuvable");
    return false;
#endif
#endif
}
