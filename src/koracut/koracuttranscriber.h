/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "koracutautocut.h"

#include <QObject>
#include <QStringList>
#include <memory>

class QProcess;
class TimelineItemModel;

/** @brief Transcrit au mot les clips des pistes de voix, en tâche de fond, pour la coupe automatique.
 *
 * Lance data/scripts/whisper/koracut_mots.py avec le Python du venv de la reconnaissance vocale,
 * un fichier après l'autre. Chaque transcription est gardée en cache (dossier de cache de
 * l'application, sous-dossier « mots ») : relancer l'analyse avec d'autres réglages ne retranscrit
 * rien. Les mots rendus sont en secondes de timeline, déjà ramenés à la place de chaque clip.
 * Document 15, lot 2.
 */
class KoraCutTranscriber : public QObject
{
    Q_OBJECT
public:
    explicit KoraCutTranscriber(QObject *parent = nullptr);
    ~KoraCutTranscriber() override;

    /** @brief Le modèle Whisper qui sera utilisé (chemin du .pt), vide si aucun n'est installé.
     * Préférence : small, puis base, puis le premier trouvé. Les gros modèles sont plus justes mais
     * trop lents pour les machines modestes. */
    static QString chooseModel();
    /** @brief Télécharge le modèle « base » (145 Mo) dans ~/.cache/whisper, là où Whisper et
     * chooseModel() le cherchent, et vérifie sa somme SHA-256 (celle qu'OpenAI met dans l'URL). */
    void downloadModel();

    /** @brief Nom court du modèle, pour l'afficher (« small »). */
    static QString modelName(const QString &modelPath);
    /** @brief Le fichier de cache d'une transcription (JSON du script), qu'il existe ou non. Sert aux
     * outils d'agent pour répondre sans relancer Whisper. @param modelPath vide : le modèle choisi. */
    static QString cacheFileFor(const QString &file, const QString &language, const QString &modelPath = QString());

    /** @brief Démarre la transcription des clips des pistes données. */
    void start(const std::shared_ptr<TimelineItemModel> &timeline, const QList<int> &tracks, const QString &language = QStringLiteral("fr"));
    /** @brief Transcrit un seul fichier, en entier ; les mots rendus sont en secondes du fichier.
     *  Sert à la porte de commande (medias.analyser transcription, document 18). */
    void startFile(const QString &file, const QString &language = QStringLiteral("fr"));
    void cancel();
    bool isRunning() const;

Q_SIGNALS:
    /** @brief Où en est le travail, en clair. */
    void progress(const QString &message);
    /** @brief Fini : les mots de toutes les pistes, triés, en secondes de timeline. */
    void finished(const QList<KoraCutAutoCut::Word> &words);
    void failed(const QString &message);
    /** @brief Téléchargement du modèle : reçu et total en octets. */
    void downloadProgress(qint64 received, qint64 total);
    /** @brief Téléchargement fini : vide si tout va bien, sinon ce qui a échoué. */
    void downloadFinished(const QString &error);

private:
    struct Job
    {
        QString file;
        int position = 0; ///< position du clip dans la timeline (images)
        int in = 0;       ///< point d'entrée dans le fichier (images)
        int length = 0;   ///< durée du clip dans la timeline (images)
    };
    void next();
    QString cachePath(const QString &file) const;
    void addWords(const Job &job, const QByteArray &json);

    QList<Job> m_jobs;
    QList<Job> m_pending;
    QList<KoraCutAutoCut::Word> m_words;
    QString m_model;
    QString m_language;
    QString m_python;
    QString m_script;
    double m_fps = 25.;
    QProcess *m_process{nullptr};
    QByteArray m_output;
    int m_done = 0;
};
