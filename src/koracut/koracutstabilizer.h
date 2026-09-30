/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "koracutcliptools.h"

#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUuid>
#include <memory>

class QProcess;
class TimelineItemModel;

/** @brief Stabiliser un plan de la timeline en un clic, avec trois niveaux (document 24).
 *
 * Le moteur est celui de Kdenlive (vid.stab par MLT), mais sans la fenêtre à réglages ni les deux
 * passes à comprendre : une seule analyse du mouvement par média (processus `melt` à part, basse
 * priorité, progression visible), puis chaque niveau est un petit fichier `.mlt` qui relit la même
 * analyse avec un lissage différent. Changer de niveau est donc instantané après la première fois.
 *
 * Le plan de la timeline est repointé sur la version stabilisée (place, entrée, sortie et effets
 * gardés, une étape d'annulation) ; retirer la stabilisation le repointe sur le média d'origine. Les
 * fichiers vont dans `<dossier du projet>/Stabilisation/` (ou `~/Movies/KoraCut/Stabilisation/` tant
 * que le projet n'est pas enregistré) : un fichier d'analyse de quelques kilo-octets à quelques
 * méga-octets par média, et un `.mlt` de 5 Ko par niveau, aucune vidéo réencodée.
 */
class KoraCutStabilizer : public QObject
{
    Q_OBJECT
public:
    static KoraCutStabilizer *instance();

    /** @brief Les trois niveaux : leger (recadrage minimal), moyen (recommandé), fort (le plus stable). */
    static QList<KoraCutClipTools::Choice> levels();

    struct Status
    {
        bool ok = false;
        QString code;    ///< code d'erreur pour les agents
        QString message; ///< en français simple
        bool running = false; ///< l'analyse tourne, le plan sera repointé à la fin
        bool done = false;    ///< le plan est déjà repointé
    };

    /** @brief Stabilise le plan (sa partie image) à ce niveau. */
    Status start(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, const QString &level);
    /** @brief Remet le média d'origine. */
    Status remove(const std::shared_ptr<TimelineItemModel> &timeline, int clipId);
    /** @brief Le niveau posé sur ce plan, vide s'il n'est pas stabilisé. */
    QString currentLevel(const std::shared_ptr<TimelineItemModel> &timeline, int clipId) const;
    /** @brief Avancement de l'analyse qui concerne ce plan (0 à 100), -1 s'il n'y en a pas. */
    int progress(const std::shared_ptr<TimelineItemModel> &timeline, int clipId) const;
    /** @brief Le niveau demandé pour ce plan pendant l'analyse, vide sinon. */
    QString pendingLevel(const std::shared_ptr<TimelineItemModel> &timeline, int clipId) const;
    void cancel(const std::shared_ptr<TimelineItemModel> &timeline, int clipId);

    /** @brief Réglages vid.stab de l'analyse (communs aux trois niveaux) et de chaque niveau. */
    static QMap<QString, QString> analysisParams();
    static QMap<QString, QString> levelParams(const QString &level);

    /** @brief Écrit le fichier d'un niveau à partir du fichier d'analyse (texte XML de MLT). */
    static bool writeLevelFile(const QString &analysisFile, const QString &levelFile, const QString &level);

Q_SIGNALS:
    void progressChanged(int clipId, int percent);
    void finished(int clipId, bool ok, const QString &message);

private:
    explicit KoraCutStabilizer(QObject *parent = nullptr);

    struct Waiting
    {
        QUuid timeline;
        int clipId = -1;
        QString level;
    };
    struct Job
    {
        QString sourceBinId;
        QString analysisFile;
        QPointer<QProcess> process;
        int percent = 0;
        QList<Waiting> waiting;
        QString log;
        bool canceled = false;
    };

    /** Le média d'origine d'un plan (le média stabilisé renvoie à sa source). */
    QString sourceBinId(const std::shared_ptr<TimelineItemModel> &timeline, int clipId) const;
    QString baseName(const QString &sourceBinId) const;
    void finishJob(const QString &sourceBinId, bool ok);
    /** Pose le niveau sur le plan (fichier du niveau, média du chutier, repointage). */
    void applyLevel(const Waiting &waiting, const QString &sourceBinId, const QString &analysisFile);
    void replace(const Waiting &waiting, const QString &newBinId);

    QMap<QString, Job *> m_jobs;
};
