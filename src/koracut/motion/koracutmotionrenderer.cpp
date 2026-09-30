/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutmotionrenderer.h"
#include "koracutmotionsync.h"

#include "core.h"
#include "jobs/taskmanager.h"
#include "kdenlivesettings.h"
#include "utils/machineprofile.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QHostAddress>
#include <QTcpServer>

namespace KoraCutMotionHost {

QString versionWanted()
{
    return QStringLiteral("0.8.67");
}

QString dataDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/hyperframes");
}

namespace {

QStringList usualBinDirs()
{
#ifdef Q_OS_WIN
    // Node sous Windows : l'installeur officiel (Program Files\nodejs), winget, Volta, nvm-windows.
    QStringList dirs = {QStringLiteral("C:/Program Files/nodejs"), qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Programs/nodejs"),
                        qEnvironmentVariable("APPDATA") + QStringLiteral("/npm"), qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Volta/bin"),
                        qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Microsoft/WinGet/Links")};
    if (!qEnvironmentVariable("NVM_SYMLINK").isEmpty()) {
        dirs.prepend(qEnvironmentVariable("NVM_SYMLINK"));
    }
#else
    QStringList dirs = {QStringLiteral("/usr/local/bin"), QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/bin"),
                        QDir::homePath() + QStringLiteral("/.volta/bin"), QDir::homePath() + QStringLiteral("/.local/bin")};
    // nvm et fnm : la dernière version installée.
    for (const QString &base : {QDir::homePath() + QStringLiteral("/.nvm/versions/node"), QDir::homePath() + QStringLiteral("/.fnm/node-versions")}) {
        const QDir d(base);
        const QStringList versions = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::Reversed);
        for (const QString &v : versions) {
            dirs << d.absoluteFilePath(v) + QStringLiteral("/bin");
            dirs << d.absoluteFilePath(v) + QStringLiteral("/installation/bin");
        }
    }
#endif
    return dirs;
}

QString findTool(const QString &name)
{
    const QString custom = KdenliveSettings::koracut_nodepath();
    if (!custom.isEmpty()) {
        // À côté du Node choisi dans les réglages (npx, et sous Windows npx.cmd).
        const QString candidate = QStandardPaths::findExecutable(name, {QFileInfo(custom).absolutePath()});
        if (!candidate.isEmpty()) {
            return candidate;
        }
    }
    QString found = QStandardPaths::findExecutable(name);
    if (!found.isEmpty()) {
        return found;
    }
    found = QStandardPaths::findExecutable(name, usualBinDirs());
    return found;
}

} // namespace

QString nodePath()
{
    const QString custom = KdenliveSettings::koracut_nodepath();
    if (!custom.isEmpty() && QFileInfo(custom).isExecutable()) {
        return custom;
    }
    return findTool(QStringLiteral("node"));
}

QString npmPath()
{
    return findTool(QStringLiteral("npm"));
}

QString cliScript()
{
    const QString script = dataDir() + QStringLiteral("/node_modules/hyperframes/bin/hyperframes.mjs");
    return QFile::exists(script) ? script : QString();
}

QString installedVersion()
{
    QFile pkg(dataDir() + QStringLiteral("/node_modules/hyperframes/package.json"));
    if (!pkg.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QJsonDocument::fromJson(pkg.readAll()).object().value(QStringLiteral("version")).toString();
}

State state()
{
    if (nodePath().isEmpty()) {
        return State::NoNode;
    }
    if (cliScript().isEmpty()) {
        return State::NotInstalled;
    }
    return State::Ready;
}

QProcessEnvironment environment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QStringList path = env.value(QStringLiteral("PATH")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    const QString node = nodePath();
    if (!node.isEmpty()) {
        path.prepend(QFileInfo(node).absolutePath());
    }
    const QString ffmpeg = KdenliveSettings::ffmpegpath();
    if (!ffmpeg.isEmpty()) {
        path.append(QFileInfo(ffmpeg).absolutePath());
    }
    for (const QString &d : usualBinDirs()) {
        if (!path.contains(d)) {
            path.append(d);
        }
    }
    env.insert(QStringLiteral("PATH"), path.join(QLatin1Char(':')));
    env.insert(QStringLiteral("HYPERFRAMES_NO_UPDATE_CHECK"), QStringLiteral("1"));
    env.insert(QStringLiteral("HYPERFRAMES_TELEMETRY"), QStringLiteral("0"));
    env.insert(QStringLiteral("NO_COLOR"), QStringLiteral("1"));
    env.insert(QStringLiteral("CI"), QStringLiteral("1"));
    return env;
}

QStringList arguments(const QStringList &cliArgs)
{
    return QStringList{cliScript()} + cliArgs;
}

bool lowMemory()
{
    return MachineProfile::effectiveLevel() == MachineProfile::Level::Modest;
}

int freePort()
{
    QTcpServer server;
    if (server.listen(QHostAddress::LocalHost, 0)) {
        const int port = server.serverPort();
        server.close();
        return port;
    }
    return 4310;
}

bool runCli(const QStringList &cliArgs, const QString &workingDir, int timeoutMs, QString *output, int *exitCode)
{
    if (exitCode) {
        *exitCode = -1;
    }
    if (state() != State::Ready) {
        if (output) {
            *output = QStringLiteral("HyperFrames n'est pas installé.");
        }
        return false;
    }
    QProcess process;
    process.setProcessEnvironment(environment());
    process.setWorkingDirectory(workingDir);
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(nodePath(), arguments(cliArgs));
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        if (output) {
            *output = QStringLiteral("hyperframes %1 : temps dépassé (%2 s).").arg(cliArgs.value(0)).arg(timeoutMs / 1000);
        }
        return false;
    }
    if (output) {
        *output = QString::fromUtf8(process.readAllStandardOutput());
    }
    if (exitCode) {
        *exitCode = process.exitCode();
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

} // namespace KoraCutMotionHost

// ---------------------------------------------------------------------------------------------

KoraCutMotionInstaller::KoraCutMotionInstaller(QObject *parent)
    : QObject(parent)
{
}

bool KoraCutMotionInstaller::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void KoraCutMotionInstaller::start(bool latest)
{
    if (isRunning()) {
        return;
    }
    const QString npm = KoraCutMotionHost::npmPath();
    if (npm.isEmpty()) {
        Q_EMIT finished(false, QStringLiteral("npm introuvable"));
        return;
    }
    const QString dir = KoraCutMotionHost::dataDir();
    QDir().mkpath(dir);
    m_process = new QProcess(this);
    m_process->setProcessEnvironment(KoraCutMotionHost::environment());
    m_process->setWorkingDirectory(dir);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        const QStringList lines = QString::fromUtf8(m_process->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &l : lines) {
            Q_EMIT progress(l.trimmed());
        }
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0 && !KoraCutMotionHost::cliScript().isEmpty();
        Q_EMIT finished(ok, ok ? KoraCutMotionHost::installedVersion() : QStringLiteral("npm a rendu le code %1").arg(code));
        m_process->deleteLater();
        m_process = nullptr;
    });
    const QString cache = QDir::tempPath() + QStringLiteral("/npm-cache-hf");
    // Le node de la machine lance le script npm (npm est un script JavaScript).
    m_process->start(KoraCutMotionHost::nodePath(),
                     {npm, QStringLiteral("install"), QStringLiteral("hyperframes@") + (latest ? QStringLiteral("latest") : KoraCutMotionHost::versionWanted()),
                      QStringLiteral("--prefix"), dir,
                      QStringLiteral("--cache"), cache, QStringLiteral("--no-fund"), QStringLiteral("--no-audit"), QStringLiteral("--loglevel"),
                      QStringLiteral("info")});
}

// ---------------------------------------------------------------------------------------------

KoraCutMotionStudio::KoraCutMotionStudio(QObject *parent)
    : QObject(parent)
{
}

void KoraCutMotionStudio::open(const QString &projectDir)
{
    if (KoraCutMotionHost::state() != KoraCutMotionHost::State::Ready) {
        Q_EMIT failed(QStringLiteral("HyperFrames n'est pas installé."));
        return;
    }
    auto *process = new QProcess(this);
    process->setProcessEnvironment(KoraCutMotionHost::environment());
    process->setWorkingDirectory(projectDir);
    connect(process, &QProcess::finished, this, [this, process, projectDir](int, QProcess::ExitStatus) {
        const QByteArray out = process->readAllStandardOutput();
        process->deleteLater();
        // La CLI peut écrire une ligne d'avertissement avant le JSON : on cherche l'accolade.
        const int brace = out.indexOf('{');
        const QJsonObject result = brace < 0 ? QJsonObject() : QJsonDocument::fromJson(out.mid(brace)).object().value(QStringLiteral("result")).toObject();
        const QString url = result.value(QStringLiteral("studioUrl")).toString();
        if (url.isEmpty()) {
            Q_EMIT failed(QString::fromUtf8(process->readAllStandardError() + out).trimmed());
            return;
        }
        if (!m_started.contains(projectDir)) {
            m_started.append(projectDir);
        }
        Q_EMIT opened(url);
    });
    process->start(KoraCutMotionHost::nodePath(),
                   KoraCutMotionHost::arguments({QStringLiteral("preview"), projectDir, QStringLiteral("--port"), QString::number(KoraCutMotionHost::freePort()),
                                                 QStringLiteral("--no-open"), QStringLiteral("--background"), QStringLiteral("--json")}));
}

void KoraCutMotionStudio::stopAll()
{
    for (const QString &dir : std::as_const(m_started)) {
        QProcess::startDetached(KoraCutMotionHost::nodePath(), KoraCutMotionHost::arguments({QStringLiteral("preview"), dir, QStringLiteral("--stop")}), dir);
    }
    m_started.clear();
}

// ---------------------------------------------------------------------------------------------

namespace {
int nextTaskOwner()
{
    // Les identifiants du chutier sont positifs : les rendus Motion vivent en négatif.
    static int counter = -100000;
    return --counter;
}
} // namespace

KoraCutMotionRenderTask::KoraCutMotionRenderTask(const KoraCutMotionRenderRequest &request, QObject *receiver)
    : AbstractTask(ObjectId(KdenliveObjectType::NoItem, nextTaskOwner(), QUuid()), AbstractTask::MOTIONJOB, receiver)
    , m_request(request)
{
    m_description = QStringLiteral("Motion : %1 / %2").arg(request.project, request.elementId);
}

void KoraCutMotionRenderTask::run()
{
    AbstractTaskDone whenFinished(m_owner.itemId, this);
    if (m_isCanceled || pCore->taskManager.isBlocked()) {
        return;
    }
    QMutexLocker lock(&m_runMutex);
    m_running = true;
    m_progress = 0;
    const KoraCutMotionRenderRequest &r = m_request;
    auto report = [this, &r](bool ok) {
        QMetaObject::invokeMethod(m_object, "taskFinished", Qt::QueuedConnection, Q_ARG(QString, r.project), Q_ARG(QString, r.elementId), Q_ARG(QString, r.hash),
                                  Q_ARG(QString, r.output), Q_ARG(bool, ok), Q_ARG(QString, m_log));
    };
    if (KoraCutMotionHost::state() != KoraCutMotionHost::State::Ready) {
        m_log = QStringLiteral("HyperFrames n'est pas installé.");
        report(false);
        return;
    }
    QDir().mkpath(QFileInfo(r.output).absolutePath());
    // L'enveloppe vit à côté de index.html : les chemins relatifs (assets/, shared/) restent bons.
    QString composition = QStringLiteral("index.html");
    QString envelopePath;
    if (!r.envelopeHtml.isEmpty()) {
        composition = QStringLiteral(".koracut-%1.html").arg(r.elementId);
        envelopePath = r.projectDir + QLatin1Char('/') + composition;
        QFile env(envelopePath);
        if (!env.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            m_log = QStringLiteral("L'enveloppe %1 n'a pas pu être écrite.").arg(envelopePath);
            report(false);
            return;
        }
        env.write(r.envelopeHtml.toUtf8());
        env.close();
    }
    const QString tmpOutput = r.output + QStringLiteral(".partiel.") + r.format;
    QFile::remove(tmpOutput);
    QStringList args = {QStringLiteral("render"), r.projectDir, QStringLiteral("--composition"), composition, QStringLiteral("--format"), r.format,
                        QStringLiteral("--fps"), KoraCutMotion::secondsText(r.fps), QStringLiteral("--quality"), QStringLiteral("looks"),
                        QStringLiteral("--output"), tmpOutput, QStringLiteral("--quiet")};
    if (KoraCutMotionHost::lowMemory()) {
        args << QStringLiteral("--low-memory-mode");
    }
    QProcess process;
    process.setProcessEnvironment(KoraCutMotionHost::environment());
    process.setWorkingDirectory(r.projectDir);
    process.setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(this, &KoraCutMotionRenderTask::jobCanceled, &process, &QProcess::kill, Qt::DirectConnection);
    static const QRegularExpression fraction(QStringLiteral("(\\d+)\\s*/\\s*(\\d+)"));
    static const QRegularExpression percent(QStringLiteral("(\\d+)\\s*%"));
    QObject::connect(&process, &QProcess::readyReadStandardOutput, &process, [this, &process, &r]() {
        const QString chunk = QString::fromUtf8(process.readAllStandardOutput());
        m_log += chunk;
        int value = -1;
        const QRegularExpressionMatch f = fraction.match(chunk);
        if (f.hasMatch() && f.captured(2).toInt() > 0) {
            value = 100 * f.captured(1).toInt() / f.captured(2).toInt();
        } else {
            const QRegularExpressionMatch p = percent.match(chunk);
            if (p.hasMatch()) {
                value = p.captured(1).toInt();
            }
        }
        if (value >= 0 && value != m_progress) {
            m_progress = value;
            QMetaObject::invokeMethod(m_object, "taskProgress", Qt::QueuedConnection, Q_ARG(QString, r.project), Q_ARG(QString, r.elementId), Q_ARG(int, value));
        }
    });
    m_log = QStringLiteral("$ hyperframes %1\n").arg(args.join(QLatin1Char(' ')));
    process.start(KoraCutMotionHost::nodePath(), KoraCutMotionHost::arguments(args));
    process.waitForFinished(-1);
    m_log += QString::fromUtf8(process.readAllStandardOutput());
    bool ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 && QFileInfo(tmpOutput).size() > 0 && !m_isCanceled;
    if (ok) {
        QFile::remove(r.output);
        ok = QFile::rename(tmpOutput, r.output);
    } else {
        QFile::remove(tmpOutput);
        if (!m_isCanceled) {
            m_log += QStringLiteral("\nLe rendu a échoué (code %1).").arg(process.exitCode());
        }
    }
    if (!envelopePath.isEmpty()) {
        QFile::remove(envelopePath);
    }
    m_progress = 100;
    if (!m_isCanceled) {
        report(ok);
    }
}

// ---------------------------------------------------------------------------------------------

namespace {
KoraCutMotionRenderer::Simulator &simulator()
{
    static KoraCutMotionRenderer::Simulator s;
    return s;
}
} // namespace

KoraCutMotionRenderer::KoraCutMotionRenderer(QObject *parent)
    : QObject(parent)
{
}

void KoraCutMotionRenderer::setSimulator(Simulator s)
{
    simulator() = std::move(s);
}

bool KoraCutMotionRenderer::hasSimulator()
{
    return bool(simulator());
}

bool KoraCutMotionRenderer::waitForIdle(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!m_pending.isEmpty() && timer.elapsed() < timeoutMs) {
        QEventLoop loop;
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
        connect(this, &KoraCutMotionRenderer::pendingChanged, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return m_pending.isEmpty();
}

bool KoraCutMotionRenderer::isPending(const QString &project, const QString &elementId) const
{
    for (const Pending &p : m_pending) {
        if (p.project == project && p.elementId == elementId) {
            return true;
        }
    }
    return false;
}

void KoraCutMotionRenderer::request(const KoraCutMotionRenderRequest &request)
{
    if (!request.ok()) {
        return;
    }
    if (QFileInfo(request.output).size() > 0) {
        // Déjà en cache : la réponse arrive au prochain tour de boucle, jamais au milieu de l'appelant
        // (un rendu qui reviendrait pendant applyFromFile rechargerait un clip en pleine mise à jour).
        QTimer::singleShot(0, this, [this, request]() { Q_EMIT rendered(request.project, request.elementId, request.hash, request.output); });
        return;
    }
    for (const Pending &p : m_pending) {
        if (p.project == request.project && p.elementId == request.elementId && p.hash == request.hash) {
            return; // déjà en cours
        }
    }
    m_pending.append({request.project, request.elementId, request.hash});
    Q_EMIT pendingChanged();
    if (simulator()) {
        // Les essais : pas de Node, le simulateur écrit le fichier (ou pas) et la réponse arrive
        // au prochain tour de boucle, comme une vraie tâche.
        const bool ok = simulator()(request) && QFileInfo(request.output).size() > 0;
        QTimer::singleShot(0, this, [this, request, ok]() {
            taskFinished(request.project, request.elementId, request.hash, request.output, ok, ok ? QStringLiteral("rendu simulé") : QStringLiteral("rendu simulé : échec"));
        });
        return;
    }
    auto *task = new KoraCutMotionRenderTask(request, this);
    pCore->taskManager.startTask(task->ownerId().itemId, task);
}

void KoraCutMotionRenderer::taskFinished(const QString &project, const QString &elementId, const QString &hash, const QString &output, bool ok, const QString &log)
{
    for (int i = 0; i < m_pending.size(); ++i) {
        if (m_pending.at(i).project == project && m_pending.at(i).elementId == elementId && m_pending.at(i).hash == hash) {
            m_pending.removeAt(i);
            break;
        }
    }
    m_log = log;
    Q_EMIT logChanged();
    Q_EMIT pendingChanged();
    if (ok) {
        m_lastError.clear();
        Q_EMIT rendered(project, elementId, hash, output);
    } else {
        m_lastError = log.section(QLatin1Char('\n'), -3).trimmed();
        Q_EMIT failed(project, elementId, m_lastError);
    }
}

void KoraCutMotionRenderer::taskProgress(const QString &project, const QString &elementId, int percent)
{
    Q_EMIT progress(project, elementId, percent);
}
