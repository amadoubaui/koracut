/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutaisecrets.h"

#include <KLocalizedString>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#if defined(KORACUT_KEYCHAIN) && KORACUT_KEYCHAIN
#include <QEventLoop>
#include <qt6keychain/keychain.h>
#define KORACUT_HAS_KEYCHAIN 1
#else
#define KORACUT_HAS_KEYCHAIN 0
#endif

namespace {

QString &testDirectory()
{
    static QString dir;
    return dir;
}

QString filePath()
{
    QString dir = testDirectory();
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("koracut-cles-ia.json"));
}

QJsonObject readFile()
{
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool writeFile(const QJsonObject &all, QString *error)
{
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    // Droits posés avant d'écrire la moindre clé : lecture et écriture pour le propriétaire seul.
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(all).toJson(QJsonDocument::Compact));
    if (!file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    QFile::setPermissions(filePath(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

#if KORACUT_HAS_KEYCHAIN
// QtKeychain travaille en tâches asynchrones ; une clé se lit en quelques millisecondes, on attend.
void runJob(QKeychain::Job &job)
{
    job.setAutoDelete(false);
    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec(QEventLoop::ExcludeUserInputEvents);
}
#endif

} // namespace

QString KoraCutAiSecrets::serviceName()
{
    return QStringLiteral("pro.pixework.koracut.connexions-ia");
}

KoraCutAiSecrets::Backend KoraCutAiSecrets::backend()
{
#if KORACUT_HAS_KEYCHAIN
    return testDirectory().isEmpty() ? Backend::Keychain : Backend::File;
#else
    return Backend::File;
#endif
}

QString KoraCutAiSecrets::backendLabel()
{
    return backend() == Backend::Keychain ? i18nc("@info where API keys are stored", "the system keychain")
                                          : i18nc("@info where API keys are stored", "a file only you can read");
}

void KoraCutAiSecrets::useFileForTests(const QString &directory)
{
    testDirectory() = directory;
}

bool KoraCutAiSecrets::write(const QString &id, const QString &secret, QString *error)
{
    if (id.isEmpty()) {
        return false;
    }
#if KORACUT_HAS_KEYCHAIN
    if (backend() == Backend::Keychain) {
        QKeychain::WritePasswordJob job(serviceName());
        job.setKey(id);
        job.setTextData(secret);
        runJob(job);
        if (job.error() == QKeychain::NoError) {
            // Une ancienne copie dans le fichier de repli n'a plus lieu d'être.
            QJsonObject all = readFile();
            if (all.contains(id)) {
                all.remove(id);
                writeFile(all, nullptr);
            }
            return true;
        }
        if (error) {
            *error = job.errorString();
        }
        // Trousseau fermé ou refusé : repli sur le fichier protégé plutôt que perdre la clé.
    }
#endif
    QJsonObject all = readFile();
    all.insert(id, secret);
    return writeFile(all, error);
}

QString KoraCutAiSecrets::read(const QString &id, bool *found, QString *error)
{
    if (found) {
        *found = false;
    }
#if KORACUT_HAS_KEYCHAIN
    if (backend() == Backend::Keychain) {
        QKeychain::ReadPasswordJob job(serviceName());
        job.setKey(id);
        runJob(job);
        if (job.error() == QKeychain::NoError) {
            if (found) {
                *found = true;
            }
            return job.textData();
        }
        if (job.error() != QKeychain::EntryNotFound && error) {
            *error = job.errorString();
        }
    }
#endif
    const QJsonObject all = readFile();
    if (all.contains(id)) {
        if (found) {
            *found = true;
        }
        return all.value(id).toString();
    }
    return {};
}

bool KoraCutAiSecrets::remove(const QString &id)
{
    bool ok = true;
#if KORACUT_HAS_KEYCHAIN
    if (backend() == Backend::Keychain) {
        QKeychain::DeletePasswordJob job(serviceName());
        job.setKey(id);
        runJob(job);
        ok = job.error() == QKeychain::NoError || job.error() == QKeychain::EntryNotFound;
    }
#endif
    QJsonObject all = readFile();
    if (all.contains(id)) {
        all.remove(id);
        ok = writeFile(all, nullptr) && ok;
    }
    return ok;
}
