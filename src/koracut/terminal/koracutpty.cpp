/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutpty.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSocketNotifier>
#include <QTimer>

#include <vector>

#ifdef Q_OS_WIN
#include <QWinEventNotifier>
#include <algorithm>
#include <windows.h>
#include <tlhelp32.h>
#else
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#endif

#ifdef Q_OS_WIN
namespace {
// ConPTY est dans kernel32 depuis Windows 10 1809. On le cherche à l'exécution : les en-têtes du SDK
// ne le déclarent qu'avec une cible Windows récente, et un Windows plus vieux doit quand même démarrer.
using CreatePseudoConsoleFn = HRESULT(WINAPI *)(COORD, HANDLE, HANDLE, DWORD, void **);
using ResizePseudoConsoleFn = HRESULT(WINAPI *)(void *, COORD);
using ClosePseudoConsoleFn = void(WINAPI *)(void *);
constexpr DWORD_PTR kAttributePseudoConsole = 0x00020016; // PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE

struct ConPtyApi {
    CreatePseudoConsoleFn create{nullptr};
    ResizePseudoConsoleFn resize{nullptr};
    ClosePseudoConsoleFn close{nullptr};
};

const ConPtyApi &conPty()
{
    static const ConPtyApi api = [] {
        ConPtyApi a;
        if (HMODULE k = GetModuleHandleW(L"kernel32.dll")) {
            a.create = reinterpret_cast<CreatePseudoConsoleFn>(reinterpret_cast<void *>(GetProcAddress(k, "CreatePseudoConsole")));
            a.resize = reinterpret_cast<ResizePseudoConsoleFn>(reinterpret_cast<void *>(GetProcAddress(k, "ResizePseudoConsole")));
            a.close = reinterpret_cast<ClosePseudoConsoleFn>(reinterpret_cast<void *>(GetProcAddress(k, "ClosePseudoConsole")));
        }
        return a;
    }();
    return api;
}

COORD consoleSize(int columns, int rows)
{
    COORD c;
    c.X = SHORT(std::clamp(columns, 2, 32767));
    c.Y = SHORT(std::clamp(rows, 2, 32767));
    return c;
}

// Un argument dans la ligne de commande Windows, selon les règles de CommandLineToArgvW.
QString quoteArgument(const QString &arg)
{
    if (!arg.isEmpty() && !arg.contains(QLatin1Char(' ')) && !arg.contains(QLatin1Char('\t')) && !arg.contains(QLatin1Char('"'))) {
        return arg;
    }
    QString out = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar c : arg) {
        if (c == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (c == QLatin1Char('"')) {
            out += QString(backslashes * 2 + 1, QLatin1Char('\\'));
            out += c;
        } else {
            out += QString(backslashes, QLatin1Char('\\'));
            out += c;
        }
        backslashes = 0;
    }
    out += QString(backslashes * 2, QLatin1Char('\\'));
    out += QLatin1Char('"');
    return out;
}

// Le bloc d'environnement Unicode : « NOM=valeur\0 » triés sans tenir compte de la casse, puis \0.
std::vector<wchar_t> environmentBlock(const QProcessEnvironment &env)
{
    QStringList lines = env.toStringList();
    std::sort(lines.begin(), lines.end(), [](const QString &a, const QString &b) {
        return a.section(QLatin1Char('='), 0, 0).compare(b.section(QLatin1Char('='), 0, 0), Qt::CaseInsensitive) < 0;
    });
    std::vector<wchar_t> block;
    for (const QString &line : std::as_const(lines)) {
        const std::wstring w = line.toStdWString();
        block.insert(block.end(), w.begin(), w.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

void closeHandle(void *&h)
{
    if (h && h != INVALID_HANDLE_VALUE) {
        CloseHandle(static_cast<HANDLE>(h));
    }
    h = nullptr;
}
} // namespace
#endif

KoraCutPty::KoraCutPty(QObject *parent)
    : QObject(parent)
{
}

KoraCutPty::~KoraCutPty()
{
#ifndef Q_OS_WIN
    if (m_running && m_pid > 0) {
        // Le programme au premier plan (Claude Code dans le shell) a son propre groupe : on le prévient aussi.
        const pid_t group = m_master >= 0 ? ::tcgetpgrp(m_master) : -1;
        if (group > 0 && group != pid_t(m_pid)) {
            ::kill(-group, SIGHUP);
        }
        ::kill(-pid_t(m_pid), SIGHUP);
        ::kill(pid_t(m_pid), SIGHUP);
        // Quelques essais courts, jamais d'attente bloquante dans un destructeur.
        for (int i = 0; i < 20; ++i) {
            int status = 0;
            if (::waitpid(pid_t(m_pid), &status, WNOHANG) != 0) {
                m_running = false;
                break;
            }
            if (i == 10) {
                ::kill(-pid_t(m_pid), SIGKILL);
                ::kill(pid_t(m_pid), SIGKILL);
            }
            ::usleep(5000);
        }
    }
#else
    if (m_process && !m_processExited) {
        TerminateProcess(static_cast<HANDLE>(m_process), 1);
    }
    if (m_exitNotifier) {
        m_exitNotifier->setEnabled(false);
    }
    // Fermer la console fait sortir le fil de lecture (fin du tube) ; on l'attend avant de partir.
    closeConsole();
    if (m_reader.joinable()) {
        m_reader.join();
    }
    closeHandle(m_outRead);
    closeHandle(m_process);
#endif
    closeMaster();
}

bool KoraCutPty::isSupported()
{
#ifdef Q_OS_WIN
    const ConPtyApi &api = conPty();
    return api.create && api.resize && api.close;
#else
    return true;
#endif
}

bool KoraCutPty::isRunning() const
{
    return m_running;
}

bool KoraCutPty::hasForegroundJob() const
{
#ifndef Q_OS_WIN
    if (m_master < 0 || !m_running) {
        return false;
    }
    const pid_t group = ::tcgetpgrp(m_master);
    return group > 0 && group != pid_t(m_pid);
#else
    // Un programme lancé depuis le shell (claude, npm…) est un enfant du shell : on les cherche.
    if (!m_running || m_pid <= 0) {
        return false;
    }
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    bool found = false;
    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more && !found; more = Process32NextW(snapshot, &entry)) {
        if (entry.th32ParentProcessID != DWORD(m_pid)) {
            continue;
        }
        const QString name = QString::fromWCharArray(entry.szExeFile);
        found = name.compare(QLatin1String("conhost.exe"), Qt::CaseInsensitive) != 0 && name.compare(QLatin1String("OpenConsole.exe"), Qt::CaseInsensitive) != 0;
    }
    CloseHandle(snapshot);
    return found;
#endif
}

qint64 KoraCutPty::pid() const
{
    return m_pid;
}

int KoraCutPty::exitCode() const
{
    return m_exitCode;
}

bool KoraCutPty::start(const QString &program, const QStringList &args, const QString &workDir, const QProcessEnvironment &env, int columns, int rows,
                       QString *error)
{
#ifdef Q_OS_WIN
    if (m_running) {
        if (error) {
            *error = QStringLiteral("deja_lance");
        }
        return false;
    }
    const ConPtyApi &api = conPty();
    if (!api.create) {
        if (error) {
            *error = QStringLiteral("pty_non_disponible");
        }
        return false;
    }
    // Deux tubes : ce qu'on tape va vers la console, ce qu'elle affiche revient.
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, nullptr, 0) || !CreatePipe(&outRead, &outWrite, nullptr, 0)) {
        for (HANDLE h : {inRead, inWrite, outRead, outWrite}) {
            if (h) {
                CloseHandle(h);
            }
        }
        if (error) {
            *error = QStringLiteral("pty_impossible");
        }
        return false;
    }
    void *console = nullptr;
    const HRESULT hr = api.create(consoleSize(columns, rows), inRead, outWrite, 0, &console);
    // La console garde ses propres copies de ces deux bouts.
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (FAILED(hr)) {
        CloseHandle(inWrite);
        CloseHandle(outRead);
        if (error) {
            *error = QStringLiteral("pty_impossible");
        }
        return false;
    }

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrStore(attrSize);
    auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrStore.data());
    STARTUPINFOEXW startup;
    ZeroMemory(&startup, sizeof(startup));
    startup.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    startup.lpAttributeList = attributes;
    const bool attrOk = InitializeProcThreadAttributeList(attributes, 1, 0, &attrSize)
        && UpdateProcThreadAttribute(attributes, 0, kAttributePseudoConsole, console, sizeof(console), nullptr, nullptr);

    QStringList parts{quoteArgument(QDir::toNativeSeparators(program))};
    for (const QString &a : args) {
        parts << quoteArgument(a);
    }
    std::wstring commandLine = parts.join(QLatin1Char(' ')).toStdWString();
    std::vector<wchar_t> envBlock = environmentBlock(env);
    const std::wstring dir = QDir::toNativeSeparators(workDir.isEmpty() ? QDir::homePath() : workDir).toStdWString();
    PROCESS_INFORMATION info;
    ZeroMemory(&info, sizeof(info));
    const bool created = attrOk
        && CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE, EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, envBlock.data(),
                          dir.c_str(), &startup.StartupInfo, &info);
    if (attrOk) {
        DeleteProcThreadAttributeList(attributes);
    }
    if (!created) {
        api.close(console);
        CloseHandle(inWrite);
        CloseHandle(outRead);
        if (error) {
            *error = QStringLiteral("lancement_impossible_%1").arg(GetLastError());
        }
        return false;
    }
    CloseHandle(info.hThread);
    m_console = console;
    m_inWrite = inWrite;
    m_outRead = outRead;
    m_process = info.hProcess;
    m_pid = qint64(info.dwProcessId);
    m_exitCode = -1;
    m_running = true;
    m_processExited = false;
    m_readerDone = false;

    // ReadFile bloque : un fil à part lit, et remet chaque morceau au fil de l'interface.
    m_reader = std::thread([this, outRead]() {
        char buffer[65536];
        DWORD n = 0;
        while (ReadFile(outRead, buffer, sizeof(buffer), &n, nullptr) && n > 0) {
            QByteArray chunk(buffer, qsizetype(n));
            QMetaObject::invokeMethod(this, [this, chunk]() { Q_EMIT dataReceived(chunk); }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(
            this,
            [this]() {
                m_readerDone = true;
                finishIfDone();
            },
            Qt::QueuedConnection);
    });
    m_exitNotifier = new QWinEventNotifier(static_cast<HANDLE>(m_process), this);
    connect(m_exitNotifier, &QWinEventNotifier::activated, this, &KoraCutPty::onProcessExited);
    return true;
#else
    if (m_running) {
        if (error) {
            *error = QStringLiteral("deja_lance");
        }
        return false;
    }
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0 || ::grantpt(master) != 0 || ::unlockpt(master) != 0) {
        if (master >= 0) {
            ::close(master);
        }
        if (error) {
            *error = QStringLiteral("pty_impossible");
        }
        return false;
    }
    const char *slaveName = ::ptsname(master);
    if (!slaveName) {
        ::close(master);
        if (error) {
            *error = QStringLiteral("pty_impossible");
        }
        return false;
    }
    // Tout ce dont l'enfant a besoin est préparé avant fork : après, seuls des appels sûrs.
    const QByteArray slavePath(slaveName);
    const QByteArray programPath = QFile::encodeName(program);
    const QByteArray dir = QFile::encodeName(workDir.isEmpty() ? QDir::homePath() : workDir);
    std::vector<QByteArray> argStore;
    argStore.push_back(QFile::encodeName(QFileInfo(program).fileName()));
    for (const QString &a : args) {
        argStore.push_back(a.toLocal8Bit());
    }
    std::vector<char *> argv;
    for (QByteArray &a : argStore) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    std::vector<QByteArray> envStore;
    for (const QString &line : env.toStringList()) {
        envStore.push_back(line.toLocal8Bit());
    }
    std::vector<char *> envp;
    for (QByteArray &e : envStore) {
        envp.push_back(e.data());
    }
    envp.push_back(nullptr);
    struct winsize ws;
    std::memset(&ws, 0, sizeof(ws));
    ws.ws_col = static_cast<unsigned short>(qMax(2, columns));
    ws.ws_row = static_cast<unsigned short>(qMax(2, rows));
    long maxFd = ::sysconf(_SC_OPEN_MAX);
    if (maxFd <= 0 || maxFd > 4096) {
        maxFd = 4096;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(master);
        if (error) {
            *error = QStringLiteral("fork_impossible");
        }
        return false;
    }
    if (pid == 0) {
        // L'enfant : une session à lui, le PTY pour terminal de contrôle.
        ::setsid();
        const int slave = ::open(slavePath.constData(), O_RDWR);
        if (slave < 0) {
            ::_exit(127);
        }
#ifdef TIOCSCTTY
        ::ioctl(slave, TIOCSCTTY, 0);
#endif
        ::ioctl(slave, TIOCSWINSZ, &ws);
        ::dup2(slave, 0);
        ::dup2(slave, 1);
        ::dup2(slave, 2);
        for (long fd = 3; fd < maxFd; ++fd) {
            ::close(int(fd));
        }
        // Les signaux que l'application a pu bloquer ou ignorer reviennent à leur défaut.
        sigset_t none;
        sigemptyset(&none);
        ::sigprocmask(SIG_SETMASK, &none, nullptr);
        for (int sig : {SIGPIPE, SIGINT, SIGQUIT, SIGTERM, SIGHUP, SIGCHLD, SIGTSTP, SIGTTIN, SIGTTOU}) {
            ::signal(sig, SIG_DFL);
        }
        if (::chdir(dir.constData()) != 0) {
            ::chdir("/");
        }
        ::execve(programPath.constData(), argv.data(), envp.data());
        ::_exit(127);
    }

    m_master = master;
    m_pid = pid;
    m_exitCode = -1;
    m_running = true;
    ::fcntl(m_master, F_SETFL, ::fcntl(m_master, F_GETFL) | O_NONBLOCK);
    ::fcntl(m_master, F_SETFD, FD_CLOEXEC);
    m_readNotifier = new QSocketNotifier(qintptr(m_master), QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, &KoraCutPty::onReadable);
    m_writeNotifier = new QSocketNotifier(qintptr(m_master), QSocketNotifier::Write, this);
    m_writeNotifier->setEnabled(false);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, &KoraCutPty::onWritable);
    return true;
#endif
}

void KoraCutPty::write(const QByteArray &data)
{
#ifndef Q_OS_WIN
    if (m_master < 0 || data.isEmpty()) {
        return;
    }
    m_pending.append(data);
    onWritable();
#else
    // Ce qu'on tape est court : une écriture directe dans le tube de la console suffit.
    if (!m_inWrite || data.isEmpty()) {
        return;
    }
    const char *p = data.constData();
    qsizetype left = data.size();
    while (left > 0) {
        DWORD written = 0;
        if (!WriteFile(static_cast<HANDLE>(m_inWrite), p, DWORD(left), &written, nullptr) || written == 0) {
            break;
        }
        p += written;
        left -= qsizetype(written);
    }
#endif
}

void KoraCutPty::onWritable()
{
#ifndef Q_OS_WIN
    while (m_master >= 0 && !m_pending.isEmpty()) {
        const ssize_t n = ::write(m_master, m_pending.constData(), size_t(m_pending.size()));
        if (n > 0) {
            m_pending.remove(0, qsizetype(n));
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(m_master >= 0 && !m_pending.isEmpty());
    }
#endif
}

void KoraCutPty::onReadable()
{
#ifndef Q_OS_WIN
    if (m_master < 0) {
        return;
    }
    char buffer[65536];
    QByteArray chunk;
    for (int round = 0; round < 8; ++round) {
        const ssize_t n = ::read(m_master, buffer, sizeof(buffer));
        if (n > 0) {
            chunk.append(buffer, qsizetype(n));
            if (size_t(n) < sizeof(buffer)) {
                break;
            }
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        // 0 ou EIO : l'autre côté est fermé, le processus s'en va.
        if (!chunk.isEmpty()) {
            Q_EMIT dataReceived(chunk);
            chunk.clear();
        }
        closeMaster();
        reap(false);
        return;
    }
    if (!chunk.isEmpty()) {
        Q_EMIT dataReceived(chunk);
    }
#endif
}

void KoraCutPty::resize(int columns, int rows)
{
#ifndef Q_OS_WIN
    if (m_master < 0) {
        return;
    }
    struct winsize ws;
    std::memset(&ws, 0, sizeof(ws));
    ws.ws_col = static_cast<unsigned short>(qMax(2, columns));
    ws.ws_row = static_cast<unsigned short>(qMax(2, rows));
    ::ioctl(m_master, TIOCSWINSZ, &ws);
#else
    if (m_console && conPty().resize) {
        conPty().resize(m_console, consoleSize(columns, rows));
    }
#endif
}

void KoraCutPty::terminate()
{
#ifndef Q_OS_WIN
    if (!m_running || m_pid <= 0) {
        return;
    }
    const pid_t group = m_master >= 0 ? ::tcgetpgrp(m_master) : -1;
    if (group > 0 && group != pid_t(m_pid)) {
        ::kill(-group, SIGHUP);
    }
    ::kill(-pid_t(m_pid), SIGHUP);
    ::kill(pid_t(m_pid), SIGHUP);
    QTimer::singleShot(2000, this, [this]() {
        if (m_running && m_pid > 0) {
            ::kill(-pid_t(m_pid), SIGKILL);
            ::kill(pid_t(m_pid), SIGKILL);
        }
    });
    reap(false);
#else
    if (!m_running || !m_process) {
        return;
    }
    // Fermer la console coupe le shell et ce qu'il fait tourner (comme SIGHUP) ; au bout de deux
    // secondes, s'il reste là, on l'arrête.
    closeConsole();
    QTimer::singleShot(2000, this, [this]() {
        if (m_running && m_process && !m_processExited) {
            TerminateProcess(static_cast<HANDLE>(m_process), 1);
        }
    });
#endif
}

#ifdef Q_OS_WIN
void KoraCutPty::closeConsole()
{
    closeHandle(m_inWrite);
    if (m_console) {
        // Le fil de lecture vide la sortie pendant ce temps : ClosePseudoConsole ne reste pas bloqué.
        conPty().close(m_console);
        m_console = nullptr;
    }
}

void KoraCutPty::onProcessExited()
{
    if (m_processExited) {
        return;
    }
    m_processExited = true;
    if (m_exitNotifier) {
        m_exitNotifier->setEnabled(false);
    }
    DWORD code = 0;
    if (m_process && GetExitCodeProcess(static_cast<HANDLE>(m_process), &code)) {
        m_exitCode = int(code);
    }
    // La console reste ouverte après le processus : la fermer termine la lecture (fin du tube).
    closeConsole();
    finishIfDone();
}

void KoraCutPty::finishIfDone()
{
    if (!m_processExited || !m_readerDone || !m_running) {
        return;
    }
    if (m_reader.joinable()) {
        m_reader.join();
    }
    closeHandle(m_outRead);
    closeHandle(m_process);
    m_running = false;
    Q_EMIT finished(m_exitCode);
}
#endif

void KoraCutPty::closeMaster()
{
    if (m_readNotifier) {
        m_readNotifier->setEnabled(false);
        m_readNotifier->deleteLater();
        m_readNotifier = nullptr;
    }
    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(false);
        m_writeNotifier->deleteLater();
        m_writeNotifier = nullptr;
    }
#ifndef Q_OS_WIN
    if (m_master >= 0) {
        ::close(m_master);
    }
#endif
    m_master = -1;
    m_pending.clear();
}

void KoraCutPty::reap(bool wait)
{
    Q_UNUSED(wait)
#ifndef Q_OS_WIN
    if (!m_running || m_pid <= 0) {
        return;
    }
    int status = 0;
    const pid_t r = ::waitpid(pid_t(m_pid), &status, WNOHANG);
    if (r == pid_t(m_pid) || (r < 0 && errno == ECHILD)) {
        m_running = false;
        m_exitCode = (r > 0 && WIFEXITED(status)) ? WEXITSTATUS(status) : (r > 0 && WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
        if (m_reaper) {
            m_reaper->stop();
        }
        closeMaster();
        Q_EMIT finished(m_exitCode);
        return;
    }
    // Pas encore parti : on repasse, sans jamais bloquer le fil de l'interface.
    if (!m_reaper) {
        m_reaper = new QTimer(this);
        m_reaper->setInterval(100);
        connect(m_reaper, &QTimer::timeout, this, [this]() { reap(false); });
    }
    if (!m_reaper->isActive()) {
        m_reaper->start();
    }
#endif
}
