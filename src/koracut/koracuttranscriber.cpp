/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracuttranscriber.h"
#include "koracuttimelineaudio.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "kdenlivesettings.h"
#include "pythoninterfaces/speechtotextwhisper.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <limits>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

KoraCutTranscriber::KoraCutTranscriber(QObject *parent)
    : QObject(parent)
{
}

KoraCutTranscriber::~KoraCutTranscriber()
{
    cancel();
}

QString KoraCutTranscriber::chooseModel()
{
    // Le dossier des réglages de la reconnaissance vocale d'abord, puis ~/.cache/whisper, celui
    // qu'utilise Whisper lui-même. Les deux diffèrent dès que XDG_CACHE_HOME est posé (c'est le cas
    // dans l'environnement de développement Craft, et chez certains utilisateurs Linux).
    SpeechToTextWhisper stt;
    QStringList folders = {stt.modelFolder(), QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.cache/whisper")};
    QStringList files;
    for (const QString &path : std::as_const(folders)) {
        QDir folder(path);
        if (path.isEmpty() || !folder.exists()) {
            continue;
        }
        const QStringList found = folder.entryList({QStringLiteral("*.pt")}, QDir::Files, QDir::Name);
        for (const QString &f : found) {
            files << folder.absoluteFilePath(f);
        }
    }
    // Préférence : small, puis base, puis medium, puis le premier trouvé.
    for (const QString &preferred : {QStringLiteral("small.pt"), QStringLiteral("base.pt"), QStringLiteral("medium.pt")}) {
        for (const QString &f : std::as_const(files)) {
            if (QFileInfo(f).fileName() == preferred) {
                return f;
            }
        }
    }
    return files.isEmpty() ? QString() : files.first();
}

void KoraCutTranscriber::downloadModel()
{
    // Le modèle « base » : le plus léger qui reconnaît bien le français, adapté aux machines modestes.
    // La somme est celle que l'URL d'OpenAI contient ; Whisper la vérifie aussi au chargement.
    static const QString sha = QStringLiteral("ed3a0b6b1c0edf879ad9b11b1af5a0e6ab5db9205f891f668f8b0e6c6326e34e");
    const QUrl url(QStringLiteral("https://openaipublic.azureedge.net/main/whisper/models/%1/base.pt").arg(sha));
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + QStringLiteral("/.cache/whisper");
    QDir().mkpath(folder);
    auto *network = new QNetworkAccessManager(this);
    QNetworkReply *reply = network->get(QNetworkRequest(url));
    auto *file = new QSaveFile(folder + QStringLiteral("/base.pt"), reply);
    if (!file->open(QIODevice::WriteOnly)) {
        reply->abort();
        Q_EMIT downloadFinished(i18nc("@info", "Could not write to %1.", folder));
        return;
    }
    auto *hash = new QCryptographicHash(QCryptographicHash::Sha256);
    connect(reply, &QNetworkReply::downloadProgress, this, &KoraCutTranscriber::downloadProgress);
    connect(reply, &QNetworkReply::readyRead, this, [reply, file, hash]() {
        const QByteArray chunk = reply->readAll();
        hash->addData(chunk);
        file->write(chunk);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, hash, network]() {
        const QByteArray rest = reply->readAll();
        hash->addData(rest);
        file->write(rest);
        QString error;
        if (reply->error() != QNetworkReply::NoError) {
            error = i18nc("@info", "The download failed: %1", reply->errorString());
            file->cancelWriting();
        } else if (QString::fromLatin1(hash->result().toHex()) != sha) {
            error = i18nc("@info", "The downloaded file is damaged (wrong checksum). Try again.");
            file->cancelWriting();
        } else if (!file->commit()) {
            error = i18nc("@info", "Could not save the model.");
        }
        delete hash;
        reply->deleteLater();
        network->deleteLater();
        Q_EMIT downloadFinished(error);
    });
}

QString KoraCutTranscriber::modelName(const QString &modelPath)
{
    return QFileInfo(modelPath).completeBaseName();
}

bool KoraCutTranscriber::isRunning() const
{
    return m_process != nullptr;
}

void KoraCutTranscriber::cancel()
{
    m_pending.clear();
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
        m_process->deleteLater();
        m_process = nullptr;
    }
}

QString KoraCutTranscriber::cachePath(const QString &file) const
{
    return cacheFileFor(file, m_language, m_model);
}

QString KoraCutTranscriber::cacheFileFor(const QString &file, const QString &language, const QString &modelPath)
{
    // Le même fichier, à la même date, avec le même modèle et la même langue : même transcription.
    const QFileInfo info(file);
    const QByteArray key = QStringLiteral("%1|%2|%3|%4|%5")
                               .arg(info.absoluteFilePath())
                               .arg(info.size())
                               .arg(info.lastModified().toMSecsSinceEpoch())
                               .arg(modelName(modelPath.isEmpty() ? chooseModel() : modelPath), language)
                               .toUtf8();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/mots");
    QDir().mkpath(dir);
    return dir + QLatin1Char('/') + QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex()) + QStringLiteral(".json");
}

void KoraCutTranscriber::start(const std::shared_ptr<TimelineItemModel> &timeline, const QList<int> &tracks, const QString &language)
{
    cancel();
    m_words.clear();
    m_jobs.clear();
    m_done = 0;
    m_language = language;
    m_fps = pCore->getCurrentFps();
    m_model = chooseModel();
    if (m_model.isEmpty()) {
        Q_EMIT failed(i18nc("@info", "No speech recognition model is installed. Install one in Settings > Speech Recognition (“small” or “base”)."));
        return;
    }
    SpeechToTextWhisper stt;
    m_python = stt.venvPythonExecs().python;
    m_script = stt.getScript(QStringLiteral("whisper/koracut_mots.py"));
    if (m_python.isEmpty() || m_script.isEmpty()) {
        Q_EMIT failed(i18nc("@info", "Speech recognition is not installed. Install it in Settings > Speech Recognition."));
        return;
    }
    for (int tid : tracks) {
        for (int cid : KoraCutTimelineAudio::clipsOnTrack(timeline, tid)) {
            auto clip = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(cid));
            if (!clip || clip->url().isEmpty() || !QFileInfo::exists(clip->url())) {
                continue;
            }
            m_jobs << Job{clip->url(), timeline->getClipPosition(cid), timeline->getClipIn(cid), timeline->getClipPlaytime(cid)};
        }
    }
    if (m_jobs.isEmpty()) {
        Q_EMIT failed(i18nc("@info", "No audio file to transcribe on these tracks."));
        return;
    }
    m_pending = m_jobs;
    next();
}

void KoraCutTranscriber::startFile(const QString &file, const QString &language)
{
    cancel();
    m_words.clear();
    m_jobs.clear();
    m_done = 0;
    m_language = language;
    m_fps = pCore->getCurrentFps();
    m_model = chooseModel();
    if (m_model.isEmpty()) {
        Q_EMIT failed(i18nc("@info", "No speech recognition model is installed. Install one in Settings > Speech Recognition (“small” or “base”)."));
        return;
    }
    SpeechToTextWhisper stt;
    m_python = stt.venvPythonExecs().python;
    m_script = stt.getScript(QStringLiteral("whisper/koracut_mots.py"));
    if (m_python.isEmpty() || m_script.isEmpty()) {
        Q_EMIT failed(i18nc("@info", "Speech recognition is not installed. Install it in Settings > Speech Recognition."));
        return;
    }
    if (file.isEmpty() || !QFileInfo::exists(file)) {
        Q_EMIT failed(i18nc("@info", "No audio file to transcribe on these tracks."));
        return;
    }
    // Position 0, entrée 0, longueur « sans fin » : les temps du fichier sortent tels quels.
    m_jobs << Job{file, 0, 0, std::numeric_limits<int>::max() / 2};
    m_pending = m_jobs;
    next();
}

void KoraCutTranscriber::addWords(const Job &job, const QByteArray &json)
{
    // Temps du fichier -> temps de la timeline, et seulement ce que le clip montre.
    const QList<KoraCutAutoCut::Word> words = KoraCutAutoCut::parseWords(json);
    const double clipStart = job.in / m_fps;
    const double clipEnd = (job.in + job.length) / m_fps;
    const double offset = (job.position - job.in) / m_fps;
    for (KoraCutAutoCut::Word w : words) {
        if (w.end <= clipStart || w.start >= clipEnd) {
            continue;
        }
        w.start = qMax(w.start, clipStart) + offset;
        w.end = qMin(w.end, clipEnd) + offset;
        m_words << w;
    }
}

void KoraCutTranscriber::next()
{
    // D'abord tout ce qui est déjà en cache : ça ne coûte rien.
    while (!m_pending.isEmpty()) {
        const Job job = m_pending.first();
        QFile cached(cachePath(job.file));
        if (!cached.open(QIODevice::ReadOnly)) {
            break;
        }
        addWords(job, cached.readAll());
        m_pending.removeFirst();
        ++m_done;
    }
    if (m_pending.isEmpty()) {
        // Plusieurs micros entendent la même voix : un seul exemplaire de chaque mot.
        m_words = KoraCutAutoCut::mergeTranscriptions(m_words);
        Q_EMIT finished(m_words);
        return;
    }
    const Job job = m_pending.first();
    Q_EMIT progress(i18nc("@info", "Transcribing %1 (%2 of %3, model %4)…", QFileInfo(job.file).fileName(), m_done + 1, m_jobs.size(),
                          modelName(m_model)));
    m_output.clear();
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() { m_output += m_process->readAllStandardOutput(); });
    connect(m_process, &QProcess::finished, this, [this, job](int code, QProcess::ExitStatus status) {
        m_output += m_process->readAllStandardOutput();
        const QByteArray errors = m_process->readAllStandardError();
        m_process->deleteLater();
        m_process = nullptr;
        QByteArray json;
        const QList<QByteArray> lines = m_output.split('\n');
        for (const QByteArray &line : lines) {
            if (line.startsWith("KORACUT-MOTS ")) {
                json = line.mid(13);
            }
        }
        if (status != QProcess::NormalExit || code != 0 || json.isEmpty()) {
            qWarning() << "KoraCut: transcription échouée" << job.file << code << errors.right(2000);
            Q_EMIT failed(i18nc("@info", "Transcribing %1 failed. The details are in the application log.", QFileInfo(job.file).fileName()));
            m_pending.clear();
            return;
        }
        QFile cache(cachePath(job.file));
        if (cache.open(QIODevice::WriteOnly)) {
            cache.write(json);
        }
        addWords(job, json);
        m_pending.removeFirst();
        ++m_done;
        next();
    });
    QStringList args = {m_script, QStringLiteral("--src=%1").arg(job.file), QStringLiteral("--model=%1").arg(m_model),
                        QStringLiteral("--language=%1").arg(m_language)};
    if (!KdenliveSettings::whisperDevice().isEmpty()) {
        args << QStringLiteral("--device=%1").arg(KdenliveSettings::whisperDevice());
    }
    if (!KdenliveSettings::ffmpegpath().isEmpty()) {
        args << QStringLiteral("--ffmpeg_path=%1").arg(QFileInfo(KdenliveSettings::ffmpegpath()).absolutePath());
    }
    m_process->start(m_python, args);
}
