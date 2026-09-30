/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QVector>

/** @brief Baisse automatique de la musique sous la voix (« ducking »).
 *
 * Demande d'Amadou Ba du 22 septembre 2026. On choisit les pistes de voix et les pistes de
 * musique, KoraCut repère où l'on parle et pose des images clés de volume sur la musique :
 * elle descend un peu avant la voix, reste basse pendant, remonte après.
 *
 * Ce fichier ne fait que le calcul, sans rien savoir de la timeline : il se teste seul
 * (tests/koracutduckingtest.cpp). L'application sur les clips est dans koracutduckingdialog.
 */
namespace KoraCutDucking {

struct Settings
{
    double duckDb = 12.;        ///< de combien la musique descend, en dB (valeur positive)
    double thresholdDb = -40.;  ///< au-dessus de ce niveau (dBFS, crête), on considère qu'on parle
    int anticipationMs = 150;   ///< la musique a fini de descendre ce temps avant la voix
    int holdMs = 400;           ///< elle reste basse ce temps après la voix
    int fadeMs = 250;           ///< durée de la descente et de la remontée
    int mergeGapMs = 600;       ///< deux paroles séparées de moins que ça : on ne remonte pas entre les deux
    int minVoiceMs = 120;       ///< un bruit plus court que ça n'est pas de la voix
};

/** @brief Un intervalle de timeline, en images : [start, end[. */
using Interval = QPair<int, int>;

/** @brief Convertit une crête 16 bits (0 à 32768) en dBFS. */
double peakToDb(int peak);

/** @brief Où la musique doit être basse, en images de timeline.
 * @param voiceDb niveau de voix par image de timeline (le plus fort de toutes les pistes de voix),
 *        -144 là où il n'y a rien.
 * Les intervalles rendus incluent l'anticipation et le maintien, pas les fondus. */
QList<Interval> duckedIntervals(const QVector<double> &voiceDb, const Settings &settings, double fps);

/** @brief Images clés de volume d'un clip de musique.
 * @param clipStart position du clip dans la timeline
 * @param clipLength durée du clip dans la timeline
 * @return des paires (décalage depuis le début du clip, gain en dB), triées, sans doublon.
 *         Vide si aucune parole ne touche le clip. */
QList<QPair<int, double>> clipKeyframes(const QList<Interval> &ducked, int clipStart, int clipLength, const Settings &settings, double fps);

/** @brief Chaîne d'animation MLT (« 120=0;126=-12 »), positions décalées de @p inPoint. */
QString animationString(const QList<QPair<int, double>> &keyframes, int inPoint);

}
