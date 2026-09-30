/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "render/renderrequest.h"

#include <QHash>
#include <QJsonObject>
#include <QLocalServer>
#include <QObject>
#include <QStringList>

#include <memory>
#include <vector>

class QLocalSocket;
class QProcess;

/** @brief La file de rendu de KoraCut (la « Liste d'export » de la page Deliver de Resolve).
 *
 * Un koracut_render à la fois, pour ne pas étouffer une machine modeste ; la progression arrive sur
 * une prise locale à nous (le nom que le programme de rendu déduit du --pid qu'on lui donne). Écrite
 * le 24 septembre 2026 pour les outils d'agent (lot B), sortie de koracutagenttoolsexport.cpp le
 * 28 septembre 2026 pour être partagée avec le module Export (document 23). Les états gardent leurs
 * noms d'origine, que lisent les agents : en_attente, en_cours, termine, echec, annule.
 */
class KoraCutExportQueue : public QObject
{
    Q_OBJECT
public:
    struct Job
    {
        QString id;
        QString path;
        QString playlist;
        QString presetId;   ///< identifiant du modèle ou du préréglage (youtube_1080p, personnalise…)
        QString presetName; ///< nom lisible (« YouTube 1080p », ou le préréglage MLT)
        QString sequence;   ///< nom de la séquence rendue
        QString state = QStringLiteral("en_attente");
        int progress = 0;
        int frame = 0;
        int frames = 0;
        int lufs = 0;
        bool replaces = false;
        bool held = false; ///< ajouté à la file, attend « Tout exporter »
        bool abortRequested = false;
        QString errorText;
        qint64 startMs = 0;
        qint64 endMs = 0;
        QStringList args;
        QString subtitleFile; ///< un .srt écrit à côté du fichier, le cas échéant
        QProcess *process = nullptr;
        QLocalSocket *socket = nullptr;
    };
    using JobPtr = std::shared_ptr<Job>;

    static KoraCutExportQueue &self();

    const std::vector<JobPtr> &jobs() const { return m_jobs; }
    JobPtr find(const QString &id) const;
    /** @brief Vrai si un rendu en attente ou en cours écrit déjà ce fichier. */
    bool holds(const QString &path) const;

    /** @brief Ajoute un rendu préparé par RenderRequest. @p held : attend « Tout exporter ». */
    JobPtr add(const RenderRequest::RenderJob &render, const QString &presetId, const QString &presetName, int frames, int lufs, bool replaces,
               bool held = false, const QString &sequence = QString());
    /** @brief Lance le suivant si rien ne tourne (les rendus retenus attendent). */
    void startNext();
    /** @brief « Tout exporter » : libère les rendus retenus et lance le premier. */
    void startAll();
    /** @brief Arrête un rendu en cours, ou retire un rendu en attente. */
    bool abort(const JobPtr &job);
    /** @brief Relance un rendu échoué ou annulé (même playlist, même fichier). */
    bool retry(const JobPtr &job);
    /** @brief Retire de la liste un rendu fini, échoué ou annulé. */
    bool remove(const JobPtr &job);
    /** @brief Retire tous les rendus finis. */
    void clearFinished();
    int count(const QString &state) const;

    QJsonObject json(const JobPtr &job) const;
    /** @brief Secondes restantes, -1 si inconnues. */
    int remainingSeconds(const JobPtr &job) const;

    /** @brief Secondes de calcul par seconde de film, par préréglage, mesurées pendant la session. */
    static QHash<QString, double> &speedCache();
    /** @brief Le programme de rendu, comme le cherche l'assistant de configuration. */
    static QString rendererPath();

Q_SIGNALS:
    /** @brief Un rendu a bougé (ajout, progression, état). */
    void changed();
    void jobFinished(const QString &id, const QString &state);

private:
    KoraCutExportQueue();
    void start(const JobPtr &job);
    void finished(const JobPtr &job, int exitCode, int status);
    void cleanup(const JobPtr &job);
    void recordSpeed(const JobPtr &job);
    void notifyEnd(const JobPtr &job);
    void forwardProgress(const QString &path, int progress, int frame);
    void forwardStatus(const QString &path, int status, const QString &error);
    void onConnection();
    void read(QLocalSocket *socket);
    JobPtr running(const QString &path) const;
    void handle(const QJsonObject &json, QLocalSocket *socket);

    std::vector<JobPtr> m_jobs;
    QLocalServer m_server;
    QString m_serverName;
    qint64 m_pid = 0;
    int m_counter = 0;
};
