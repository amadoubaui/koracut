/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutexportqueue.h"
#include "koracut/koracuthwdecode.h"

#include "core.h"
#include "dialogs/renderwidget.h"
#include "kdenlivesettings.h"
#include "mainwindow.h"

#include <KLocalizedString>
#include <KNotification>

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace {
double fps()
{
    const double f = pCore ? pCore->getCurrentFps() : 25.;
    return f > 0 ? f : 25.;
}
bool active(const KoraCutExportQueue::JobPtr &j)
{
    return j->state == QLatin1String("en_attente") || j->state == QLatin1String("en_cours");
}
} // namespace

KoraCutExportQueue &KoraCutExportQueue::self()
{
    static auto *instance = new KoraCutExportQueue();
    return *instance;
}

KoraCutExportQueue::KoraCutExportQueue()
    : QObject(nullptr)
{
    m_pid = QCoreApplication::applicationPid() * 1000 + 7;
    m_serverName = QStringLiteral("pro.pixework.koracut-%1").arg(m_pid);
    QLocalServer::removeServer(m_serverName);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    if (m_server.listen(m_serverName)) {
        connect(&m_server, &QLocalServer::newConnection, this, &KoraCutExportQueue::onConnection);
    }
}

QHash<QString, double> &KoraCutExportQueue::speedCache()
{
    static QHash<QString, double> cache;
    return cache;
}

QString KoraCutExportQueue::rendererPath()
{
    QString path = KdenliveSettings::koracutrendererpath();
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        return path;
    }
#ifdef Q_OS_WIN
    const QString name = QStringLiteral("koracut_render.exe");
#else
    const QString name = QStringLiteral("koracut_render");
#endif
    path = QCoreApplication::applicationDirPath() + QLatin1Char('/') + name;
    if (QFileInfo::exists(path)) {
        return path;
    }
    path = QStandardPaths::findExecutable(name, {QFileInfo(KdenliveSettings::meltpath()).canonicalPath(), QCoreApplication::applicationDirPath()});
    if (path.isEmpty()) {
        path = QStandardPaths::findExecutable(name);
    }
    return path;
}

KoraCutExportQueue::JobPtr KoraCutExportQueue::find(const QString &id) const
{
    for (const auto &j : m_jobs) {
        if (j->id == id) {
            return j;
        }
    }
    return nullptr;
}

bool KoraCutExportQueue::holds(const QString &path) const
{
    for (const auto &j : m_jobs) {
        if (j->path == path && active(j)) {
            return true;
        }
    }
    return false;
}

int KoraCutExportQueue::count(const QString &state) const
{
    return int(std::count_if(m_jobs.begin(), m_jobs.end(), [&state](const JobPtr &j) { return j->state == state; }));
}

KoraCutExportQueue::JobPtr KoraCutExportQueue::add(const RenderRequest::RenderJob &render, const QString &presetId, const QString &presetName, int frames, int lufs,
                                                   bool replaces, bool held, const QString &sequence)
{
    auto job = std::make_shared<Job>();
    job->id = QStringLiteral("exp-%1").arg(++m_counter);
    job->path = render.outputFile;
    job->playlist = render.playlistPath;
    job->presetId = presetId;
    job->presetName = presetName;
    job->sequence = sequence;
    job->frames = frames;
    job->lufs = lufs;
    job->replaces = replaces;
    job->held = held;
    job->args = RenderRequest::argsByJob(render, false) << QStringLiteral("--pid") << QString::number(m_pid);
    m_jobs.push_back(job);
    Q_EMIT changed();
    return job;
}

void KoraCutExportQueue::startNext()
{
    for (const auto &j : m_jobs) {
        if (j->state == QLatin1String("en_cours")) {
            return;
        }
    }
    for (const auto &j : m_jobs) {
        if (j->state == QLatin1String("en_attente") && !j->held) {
            start(j);
            return;
        }
    }
}

void KoraCutExportQueue::startAll()
{
    for (const auto &j : m_jobs) {
        j->held = false;
    }
    startNext();
    Q_EMIT changed();
}

bool KoraCutExportQueue::abort(const JobPtr &job)
{
    if (job->state == QLatin1String("en_attente")) {
        job->state = QStringLiteral("annule");
        Q_EMIT changed();
        return true;
    }
    if (job->state != QLatin1String("en_cours")) {
        return false;
    }
    job->abortRequested = true;
    if (job->socket && job->socket->state() == QLocalSocket::ConnectedState) {
        // Le programme de rendu arrête melt proprement et retire le fichier partiel.
        job->socket->write("abort");
        job->socket->flush();
    } else if (job->process) {
        job->process->kill();
    }
    return true;
}

bool KoraCutExportQueue::retry(const JobPtr &job)
{
    if (active(job) || !QFileInfo::exists(job->playlist)) {
        return false;
    }
    // Jamais par-dessus un fichier arrivé entre-temps.
    if (QFileInfo::exists(job->path) && job->state != QLatin1String("termine")) {
        QFile::remove(job->path);
    } else if (QFileInfo::exists(job->path)) {
        return false;
    }
    job->state = QStringLiteral("en_attente");
    job->held = false;
    job->progress = 0;
    job->frame = 0;
    job->abortRequested = false;
    job->errorText.clear();
    startNext();
    Q_EMIT changed();
    return true;
}

bool KoraCutExportQueue::remove(const JobPtr &job)
{
    if (active(job)) {
        return false;
    }
    if (!KdenliveSettings::keepRenderLogFiles() && !job->playlist.isEmpty()) {
        QFile::remove(job->playlist);
    }
    m_jobs.erase(std::remove(m_jobs.begin(), m_jobs.end(), job), m_jobs.end());
    Q_EMIT changed();
    return true;
}

void KoraCutExportQueue::clearFinished()
{
    const auto copy = m_jobs;
    for (const auto &j : copy) {
        if (j->state == QLatin1String("termine")) {
            remove(j);
        }
    }
}

int KoraCutExportQueue::remainingSeconds(const JobPtr &job) const
{
    if (job->state == QLatin1String("en_cours")) {
        const double elapsed = (QDateTime::currentMSecsSinceEpoch() - job->startMs) / 1000.;
        if (job->progress > 0) {
            return int(std::lround(elapsed * (100 - job->progress) / job->progress));
        }
        if (speedCache().contains(job->presetName)) {
            return int(std::lround(job->frames / fps() * speedCache().value(job->presetName)));
        }
        return -1;
    }
    if (job->state == QLatin1String("en_attente")) {
        return speedCache().contains(job->presetName) ? int(std::lround(job->frames / fps() * speedCache().value(job->presetName))) : -1;
    }
    return 0;
}

QJsonObject KoraCutExportQueue::json(const JobPtr &job) const
{
    QJsonObject o{{QStringLiteral("export_id"), job->id},
                  {QStringLiteral("tache_id"), job->path},
                  {QStringLiteral("chemin"), job->path},
                  {QStringLiteral("preset"), job->presetId},
                  {QStringLiteral("preset_kdenlive"), job->presetName},
                  {QStringLiteral("etat"), job->state},
                  {QStringLiteral("progression_pourcent"), job->progress},
                  {QStringLiteral("image_courante"), job->frame}};
    if (job->held && job->state == QLatin1String("en_attente")) {
        o.insert(QStringLiteral("retenu"), true);
    }
    const int remaining = remainingSeconds(job);
    if (remaining >= 0) {
        o.insert(QStringLiteral("temps_restant_secondes"), remaining);
    }
    if (job->state == QLatin1String("termine")) {
        const qint64 bytes = QFileInfo(job->path).size();
        o.insert(QStringLiteral("taille_mo"), std::round(bytes / 1000000. * 100.) / 100.);
        o.insert(QStringLiteral("taille_ko"), int(bytes / 1000));
        o.insert(QStringLiteral("duree_rendu_secondes"), std::round((job->endMs - job->startMs) / 100.) / 10.);
    }
    if (job->state == QLatin1String("echec") && !job->errorText.isEmpty()) {
        o.insert(QStringLiteral("erreur"), job->errorText);
    }
    if (!job->subtitleFile.isEmpty()) {
        o.insert(QStringLiteral("sous_titres_srt"), job->subtitleFile);
    }
    return o;
}

void KoraCutExportQueue::start(const JobPtr &job)
{
    const QString renderer = rendererPath();
    if (renderer.isEmpty()) {
        job->state = QStringLiteral("echec");
        job->errorText = QStringLiteral("koracut_render est introuvable.");
        Q_EMIT changed();
        return;
    }
    auto *process = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // KoraCut : les essais en cours finissent d'abord, puis jamais la puce si un clip ne se décode pas avec elle.
    KoraCutHwDecode::waitForTrials(3000);
    KoraCutHwDecode::applyTo(env);
    process->setProcessEnvironment(env);
    process->setProgram(renderer);
    process->setArguments(job->args);
    job->process = process;
    job->state = QStringLiteral("en_cours");
    job->startMs = QDateTime::currentMSecsSinceEpoch();
    std::weak_ptr<Job> weak = job;
    connect(process, &QProcess::finished, this, [this, weak](int exitCode, QProcess::ExitStatus status) {
        if (auto j = weak.lock()) {
            finished(j, exitCode, int(status));
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, weak](QProcess::ProcessError) {
        if (auto j = weak.lock(); j && j->state == QLatin1String("en_cours") && j->process && j->process->state() == QProcess::NotRunning) {
            finished(j, -1, int(QProcess::CrashExit));
        }
    });
    process->start();
    forwardProgress(job->path, 0, 0);
    Q_EMIT changed();
}

void KoraCutExportQueue::finished(const JobPtr &job, int exitCode, int status)
{
    if (job->state == QLatin1String("en_cours")) {
        // Fin sans message du programme de rendu : le fichier dit si c'est fini.
        job->endMs = QDateTime::currentMSecsSinceEpoch();
        if (job->abortRequested) {
            job->state = QStringLiteral("annule");
        } else if (status == int(QProcess::NormalExit) && exitCode == 0 && QFileInfo(job->path).size() > 0) {
            job->state = QStringLiteral("termine");
            job->progress = 100;
            recordSpeed(job);
        } else {
            job->state = QStringLiteral("echec");
            job->errorText = QStringLiteral("Le rendu s'est arrêté sans message (code %1).").arg(exitCode);
            if (job->process) {
                const QString err = QString::fromLocal8Bit(job->process->readAllStandardError()).trimmed();
                if (!err.isEmpty()) {
                    job->errorText += QLatin1Char(' ') + err.section(QLatin1Char('\n'), -2).trimmed();
                }
            }
        }
        notifyEnd(job);
    }
    if (job->process) {
        job->process->deleteLater();
        job->process = nullptr;
    }
    cleanup(job);
    Q_EMIT changed();
    startNext();
}

void KoraCutExportQueue::cleanup(const JobPtr &job)
{
    // La playlist reste tant qu'on peut vouloir relancer (échec, annulation) ; retirée quand c'est fini.
    if (job->state == QLatin1String("termine") && !KdenliveSettings::keepRenderLogFiles() && !job->playlist.isEmpty()) {
        QFile::remove(job->playlist);
    }
}

void KoraCutExportQueue::recordSpeed(const JobPtr &job)
{
    const double seconds = job->frames / fps();
    if (seconds > 0 && job->endMs > job->startMs) {
        speedCache().insert(job->presetName, (job->endMs - job->startMs) / 1000. / seconds);
    }
}

void KoraCutExportQueue::notifyEnd(const JobPtr &job)
{
    Q_EMIT jobFinished(job->id, job->state);
    const QString file = QFileInfo(job->path).fileName();
    // Sans bureau (plateforme « offscreen » : tests, machine sans écran), pas de notification du système :
    // KNotifications plante sous Windows quand personne ne peut l'afficher (trois tests, 11 octobre 2026).
    const bool desktop = QGuiApplication::platformName() != QLatin1String("offscreen");
    if (job->state == QLatin1String("termine")) {
        // La notification du système (comme Resolve à la fin de la liste d'export), et la barre d'état.
        if (desktop) {
            KNotification::event(QStringLiteral("RenderFinished"), i18nc("@info notification", "Export finished: %1", file), QPixmap());
        }
        if (pCore) {
            pCore->displayMessage(i18nc("@info", "Export finished: %1", file), OperationCompletedMessage, 6000);
        }
        if (auto *w = QApplication::activeWindow(); !w && pCore && pCore->window()) {
            QApplication::alert(pCore->window());
        }
    } else if (job->state == QLatin1String("echec") && desktop) {
        KNotification::event(QStringLiteral("ErrorMessage"), i18nc("@info notification", "Export failed: %1", file), QPixmap());
    }
}

void KoraCutExportQueue::forwardProgress(const QString &path, int progress, int frame)
{
    if (pCore && pCore->window()) {
        if (RenderWidget *rw = pCore->window()->koraCutRenderWidget(false)) {
            rw->setRenderProgress(path, progress, frame);
        }
    }
}

void KoraCutExportQueue::forwardStatus(const QString &path, int status, const QString &error)
{
    if (pCore && pCore->window()) {
        if (RenderWidget *rw = pCore->window()->koraCutRenderWidget(false)) {
            rw->setRenderStatus(path, status, error);
        }
    }
}

void KoraCutExportQueue::onConnection()
{
    QLocalSocket *socket = m_server.nextPendingConnection();
    connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { read(socket); });
    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
}

void KoraCutExportQueue::read(QLocalSocket *socket)
{
    QTextStream text(socket);
    QString block;
    QString line;
    while (text.readLineInto(&line)) {
        block.append(line);
        if (line == QLatin1String("}")) {
            const QJsonObject json = QJsonDocument::fromJson(block.toUtf8()).object();
            handle(json, socket);
            block.clear();
        }
    }
}

KoraCutExportQueue::JobPtr KoraCutExportQueue::running(const QString &path) const
{
    for (const auto &j : m_jobs) {
        if (j->path == path && j->state == QLatin1String("en_cours")) {
            return j;
        }
    }
    return nullptr;
}

void KoraCutExportQueue::handle(const QJsonObject &json, QLocalSocket *socket)
{
    if (json.contains(QStringLiteral("url"))) {
        if (auto j = running(json.value(QStringLiteral("url")).toString())) {
            j->socket = socket;
            if (j->abortRequested) {
                socket->write("abort");
                socket->flush();
            }
        }
    }
    if (json.contains(QStringLiteral("setRenderingProgress"))) {
        const QJsonObject o = json.value(QStringLiteral("setRenderingProgress")).toObject();
        const QString url = o.value(QStringLiteral("url")).toString();
        if (auto j = running(url)) {
            j->progress = std::clamp(o.value(QStringLiteral("progress")).toInt(), 0, 100);
            j->frame = o.value(QStringLiteral("frame")).toInt();
            forwardProgress(url, j->progress, j->frame);
            Q_EMIT changed();
        }
    }
    if (json.contains(QStringLiteral("setRenderingFinished"))) {
        const QJsonObject o = json.value(QStringLiteral("setRenderingFinished")).toObject();
        const QString url = o.value(QStringLiteral("url")).toString();
        const int status = o.value(QStringLiteral("status")).toInt();
        const QString err = o.value(QStringLiteral("error")).toString();
        if (auto j = running(url)) {
            j->endMs = QDateTime::currentMSecsSinceEpoch();
            if (status == -1) {
                j->state = QStringLiteral("termine");
                j->progress = 100;
                recordSpeed(j);
            } else if (status == -3 || j->abortRequested) {
                j->state = QStringLiteral("annule");
            } else {
                j->state = QStringLiteral("echec");
                j->errorText = err.isEmpty() ? QStringLiteral("Le rendu a échoué.") : err;
            }
            j->socket = nullptr;
            forwardStatus(url, status, err);
            notifyEnd(j);
            Q_EMIT changed();
        }
    }
}
