/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "undohelper.hpp"

#include <QList>
#include <QPair>
#include <QVector>
#include <cstdint>
#include <memory>

class TimelineItemModel;

/** @brief Ce que les fonctions audio de KoraCut lisent dans la timeline.
 *
 * Partagé par la baisse automatique de la musique (koracutduckingdialog) et la coupe automatique
 * (koracutautocutdialog), pour qu'elles entendent exactement la même chose.
 */
namespace KoraCutTimelineAudio {

/** @brief Les clips (pas les compositions) d'une piste. */
QList<int> clipsOnTrack(const std::shared_ptr<TimelineItemModel> &timeline, int trackId);

/** @brief Le niveau de voix à chaque image de la timeline, en dBFS : la plus forte crête des pistes
 * données, -144 là où aucun clip ne joue. Lit les niveaux déjà calculés pour les formes d'onde
 * (ProjectClip::audioFrameCache), sur le flux audio choisi pour chaque clip.
 * @param missingLevels reçoit le nombre de clips dont les niveaux ne sont pas encore calculés
 *        (juste après l'import, ils le sont en tâche de fond). */
QVector<double> voiceLevels(const std::shared_ptr<TimelineItemModel> &timeline, const QList<int> &tracks, int *missingLevels = nullptr);

/** @brief Le niveau crête de chaque image d'un fichier, en dBFS, lu par MLT (son seul). C'est la mesure
 * des formes d'onde du chutier, faite tout de suite, sans attendre la tâche de fond. Vide si le
 * fichier ne se lit pas. @param stream le flux audio, -1 pour celui par défaut. */
QVector<double> fileLevels(const QString &path, double fps, int stream = -1);

/** @brief Le niveau crête d'un clip à chaque image de sa durée sur la timeline, en dBFS : les niveaux
 * des formes d'onde s'ils sont prêts, sinon lus du fichier (lot A des outils d'agent, 24 septembre
 * 2026). Vide si le clip n'a pas de son lisible. @param fromFile reçoit vrai si le fichier a été lu. */
QVector<double> clipLevels(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, bool *fromFile = nullptr);

/** @brief L'enveloppe du fichier entier d'un clip (5 points par image), sur son flux audio choisi.
 * Vide si les niveaux ne sont pas encore calculés. */
QVector<float> clipEnvelope(const std::shared_ptr<TimelineItemModel> &timeline, int clipId);

/** @brief L'énergie d'un fichier son, en dB, cent fois par seconde (12 octobre 2026, outil temps_musique).
 * Plus fin que les niveaux par image : c'est ce qu'il faut pour trouver un tempo. Vide si le fichier ne se
 * lit pas. @param stream le flux audio, -1 pour celui par défaut. @param maxSeconds on s'arrête là. */
QVector<float> fileEnergy(const QString &path, int stream = -1, double maxSeconds = 600.);

/** @brief Le tempo et les temps d'un morceau. Les instants sont en secondes depuis le début de ce qui a
 * été analysé. */
struct Beats
{
    double bpm = 0.;                      ///< 0 si aucun tempo ne ressort
    double confidence = 0.;               ///< de 0 à 1
    QVector<double> beats;                ///< les temps, réguliers (vide sans tempo)
    QVector<QPair<double, double>> hits;  ///< les temps forts : instant, force de 0 à 1, dans l'ordre du temps
};

/** @brief Cherche le tempo (60 à 180 à la minute) et les temps forts dans une énergie en dB échantillonnée
 * @p rate fois par seconde. Fonction pure. Un tempo deux fois plus lent ou plus rapide que celui que
 * l'oreille compte reste juste pour poser des coupes : on préfère celui qui est le plus près de 120. */
Beats analyseBeats(const QVector<float> &energyDb, double rate);

/** @brief Ce que la synchronisation a fait d'un clip. */
struct SyncOutcome
{
    int clipId = -1;
    int deltaFrames = 0;     ///< déplacement appliqué (images)
    double confidence = 0.;  ///< de 0 à 1
    bool moved = false;      ///< faux si la confiance était trop basse ou le niveau pas encore calculé
};

/** @brief Cale chaque clip de @p clips sur @p referenceClip par leur son (document 15, lot 4), dans une
 * macro d'annulation. Un clip dont la confiance est sous @p minConfidence ne bouge pas. Si un clip
 * devait partir avant le début de la timeline, tout (référence comprise) est décalé vers la droite
 * d'autant : rien n'est coupé.
 * @param ok reçoit faux si un déplacement a été refusé (l'annulation accumulée est alors rejouée) */
QList<SyncOutcome> syncToReference(const std::shared_ptr<TimelineItemModel> &timeline, int referenceClip, const QList<int> &clips, double minConfidence,
                                   Fun &undo, Fun &redo, bool *ok = nullptr);

}
