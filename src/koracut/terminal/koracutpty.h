/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcessEnvironment>
#include <QStringList>

#ifdef Q_OS_WIN
#include <thread>
#endif

class QSocketNotifier;
class QTimer;

/** @brief Un processus dans un pseudo-terminal (PTY) : ce que le terminal intégré fait tourner.
 *
 * macOS et Linux : posix_openpt, fork, setsid, le côté esclave en entrée, sortie et erreur,
 * puis execve. Le côté maître est lu par un QSocketNotifier : aucun fil, aucune minuterie tant
 * que rien n'arrive. Windows (depuis le 30 septembre 2026) : ConPTY, la console virtuelle de
 * Windows 10 1809 et après (CreatePseudoConsole, cherchée à l'exécution) ; la sortie est lue par
 * un fil à part (ReadFile bloque), la fin du processus par un QWinEventNotifier. Sans ConPTY,
 * isSupported() rend false et le panneau propose le terminal du système.
 */
class KoraCutPty : public QObject
{
    Q_OBJECT
public:
    explicit KoraCutPty(QObject *parent = nullptr);
    ~KoraCutPty() override;

    /** @brief true si ce système sait faire un PTY ici. */
    static bool isSupported();

    /** @brief Lance @p program avec @p args dans @p workDir, fenêtre de @p columns × @p rows cases.
     *  Rend false avec @p error rempli si rien n'a pu démarrer. */
    bool start(const QString &program, const QStringList &args, const QString &workDir, const QProcessEnvironment &env, int columns, int rows,
               QString *error = nullptr);
    /** @brief Envoie des octets au processus (ce qu'on tape). */
    void write(const QByteArray &data);
    /** @brief Nouvelle taille en cases : le processus reçoit SIGWINCH. */
    void resize(int columns, int rows);
    /** @brief Ferme : SIGHUP au groupe, puis SIGKILL si rien ne bouge après deux secondes. */
    void terminate();

    bool isRunning() const;
    /** @brief Un programme tourne au premier plan (autre que le shell) : fermer l'onglet le couperait. */
    bool hasForegroundJob() const;
    qint64 pid() const;
    int exitCode() const;

Q_SIGNALS:
    void dataReceived(const QByteArray &data);
    void finished(int exitCode);

private:
    void onReadable();
    void onWritable();
    void closeMaster();
    void reap(bool wait);

#ifdef Q_OS_WIN
    void onProcessExited();
    void finishIfDone();
    void closeConsole();
    void *m_console{nullptr};  ///< HPCON
    void *m_inWrite{nullptr};  ///< ce qu'on tape, vers la console
    void *m_outRead{nullptr};  ///< ce que la console affiche
    void *m_process{nullptr};  ///< HANDLE du processus
    std::thread m_reader;
    class QWinEventNotifier *m_exitNotifier{nullptr};
    bool m_processExited{false};
    bool m_readerDone{false};
#endif
    int m_master{-1};
    qint64 m_pid{-1};
    int m_exitCode{-1};
    bool m_running{false};
    QByteArray m_pending;
    QSocketNotifier *m_readNotifier{nullptr};
    QSocketNotifier *m_writeNotifier{nullptr};
    QTimer *m_reaper{nullptr};
};
