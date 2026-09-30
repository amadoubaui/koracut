/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "jobs/abstracttask.h"

#include <QList>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <functional>

class QProcess;

/** @brief Où sont Node et la CLI HyperFrames, et comment les lancer.
 *
 * Document 19, § 4 : KoraCut n'embarque ni Node ni Chrome. Il trouve Node sur la machine et installe
 * `hyperframes` (version figée) dans son dossier de données ; Chrome est téléchargé par HyperFrames
 * au premier rendu. Sans Node, le panneau Motion l'explique, rien d'autre ne casse.
 */
namespace KoraCutMotionHost {

enum class State { Ready, NotInstalled, NoNode };

/** La version de HyperFrames que KoraCut installe et attend. */
QString versionWanted();
/** Le dossier de données de HyperFrames : `<données de KoraCut>/hyperframes`. */
QString dataDir();
/** Le binaire node, vide s'il est introuvable (chemin, puis les emplacements habituels). */
QString nodePath();
/** Le script npm à côté de node, vide s'il est introuvable. */
QString npmPath();
/** Le script de la CLI (`node_modules/hyperframes/bin/hyperframes.mjs`), vide si non installé. */
QString cliScript();
/** La version installée, lue dans package.json, vide si non installé. */
QString installedVersion();
State state();
/** L'environnement des processus : PATH avec node et ffmpeg, pas de vérification de mise à jour. */
QProcessEnvironment environment();
/** Les arguments complets pour `node <cli> args...`. */
QStringList arguments(const QStringList &cliArgs);
/** Vrai sur une machine modeste (document 6) : `--low-memory-mode`. */
bool lowMemory();
/** Un port TCP libre pour Studio. */
int freePort();

/** @brief Lance la CLI et attend la fin (lint, snapshot : quelques secondes). @p output reçoit la sortie
 * mêlée ; @return faux si la CLI manque, si le temps est dépassé ou si le code de sortie n'est pas 0. */
bool runCli(const QStringList &cliArgs, const QString &workingDir, int timeoutMs, QString *output, int *exitCode = nullptr);

} // namespace KoraCutMotionHost

/** @brief L'installation de HyperFrames à la demande (`npm install hyperframes@<version> --prefix <données>`). */
class KoraCutMotionInstaller : public QObject
{
    Q_OBJECT
public:
    explicit KoraCutMotionInstaller(QObject *parent = nullptr);
    /** @brief Installe la version figée, ou la dernière publiée avec @p latest (« Mettre à jour HyperFrames »). */
    void start(bool latest = false);
    bool isRunning() const;

Q_SIGNALS:
    void progress(const QString &line);
    void finished(bool ok, const QString &message);

private:
    QProcess *m_process{nullptr};
};

/** @brief Studio (`hyperframes preview`) sur le dossier d'un projet Motion, dans le navigateur.
 * Le serveur reste en arrière-plan (`--background`) ; un second appel sur le même dossier le
 * retrouve et rouvre la page. */
class KoraCutMotionStudio : public QObject
{
    Q_OBJECT
public:
    explicit KoraCutMotionStudio(QObject *parent = nullptr);
    void open(const QString &projectDir);
    /** @brief Arrête les aperçus lancés par cette session (à la fermeture). */
    void stopAll();

Q_SIGNALS:
    void opened(const QString &url);
    void failed(const QString &message);

private:
    QStringList m_started;
};

/** @brief Une demande de rendu d'un élément (ou d'une composition entière). */
struct KoraCutMotionRenderRequest
{
    QString project;      ///< nom du projet Motion
    QString projectDir;   ///< son dossier (avec index.html)
    QString elementId;
    QString hash;         ///< empreinte : le nom du fichier en dépend
    QString envelopeHtml; ///< vide : rendre index.html tel quel ; sinon la composition d'enveloppe
    QString output;       ///< le fichier attendu dans le cache (.mov, ou .mp4 pour un plan opaque)
    QString format = QStringLiteral("mov"); ///< mov (ProRes 4444, transparent) ou mp4 (H.264, plan opaque)
    double fps = 25.;
    bool ok() const { return !project.isEmpty() && !elementId.isEmpty() && !output.isEmpty(); }
};

/** @brief Le rendu d'une enveloppe, en tâche de fond du TaskManager (comme les proxys).
 *
 * `hyperframes render <dossier> --composition <enveloppe> --format mov --fps <projet> --quality looks
 * [--low-memory-mode] --output <fichier>`. ProRes 4444 avec alpha, lisible par MLT. Le journal de la
 * CLI est gardé pour le panneau.
 */
class KoraCutMotionRenderTask : public AbstractTask
{
    Q_OBJECT
public:
    KoraCutMotionRenderTask(const KoraCutMotionRenderRequest &request, QObject *receiver);
    const KoraCutMotionRenderRequest &request() const { return m_request; }

protected:
    void run() override;

private:
    KoraCutMotionRenderRequest m_request;
    QString m_log;
};

/** @brief La file des rendus Motion : jamais deux fois le même (même empreinte), le journal du dernier. */
class KoraCutMotionRenderer : public QObject
{
    Q_OBJECT
public:
    explicit KoraCutMotionRenderer(QObject *parent = nullptr);

    /** @brief Demande un rendu. Si le fichier existe déjà, rendered() part tout de suite ; si le même
     * rendu est en cours, rien ne se passe. */
    void request(const KoraCutMotionRenderRequest &request);
    /** @brief Pour les essais : un rendu simulé, sans Node. La fonction écrit le fichier de sortie
     * elle-même et rend vrai ; rendered() ou failed() part au prochain tour de boucle. */
    using Simulator = std::function<bool(const KoraCutMotionRenderRequest &)>;
    static void setSimulator(Simulator simulator);
    static bool hasSimulator();
    /** @brief Attend la fin des rendus en cours (boucle d'événements locale), au plus @p timeoutMs. */
    bool waitForIdle(int timeoutMs);
    bool isPending(const QString &project, const QString &elementId) const;
    int pendingCount() const { return m_pending.size(); }
    QString log() const { return m_log; }
    QString lastError() const { return m_lastError; }

    /** @brief Appelé depuis le fil de la tâche, à la fin du rendu. */
    Q_INVOKABLE void taskFinished(const QString &project, const QString &elementId, const QString &hash, const QString &output, bool ok,
                                  const QString &log);
    Q_INVOKABLE void taskProgress(const QString &project, const QString &elementId, int percent);

Q_SIGNALS:
    void rendered(const QString &project, const QString &elementId, const QString &hash, const QString &path);
    void failed(const QString &project, const QString &elementId, const QString &message);
    void logChanged();
    void pendingChanged();
    void progress(const QString &project, const QString &elementId, int percent);

private:
    struct Pending
    {
        QString project;
        QString elementId;
        QString hash;
    };
    QList<Pending> m_pending;
    QString m_log;
    QString m_lastError;
};
