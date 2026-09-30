/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QVector>
#include <cstdint>

/** @brief Synchroniser des micros et des caméras par leur son (document 15, lot 4).
 *
 * Corrélation croisée des enveloppes, calculée par FFT (FFTCorrelation de Kdenlive). On travaille
 * sur les crêtes déjà calculées pour les formes d'onde (5 par image, soit 8 ms à 25 i/s) : aucun
 * décodage à refaire, ce qui compte sur une heure de podcast. Le « Aligner l'audio » hérité, lui,
 * redécode tout le son image par image.
 */
namespace KoraCutSync {

struct Result
{
    int lag = 0;             ///< décalage en points : other[i] ressemble à reference[i + lag]
    double confidence = 0.;  ///< corrélation normalisée au meilleur décalage, de 0 à 1
};

/** @brief Enveloppe d'un clip à partir de son cache de niveaux (crêtes 16 bits entrelacées par
 * canal, AUDIOLEVELS_POINTS_PER_FRAME points par image) : un point par crête, tous canaux fondus. */
QVector<float> envelopeFromLevels(const QVector<int16_t> &levels, int channels);

/** @brief Le décalage qui cale @p other sur @p reference.
 * @param maxLag décalage maximal cherché, en points, dans les deux sens */
Result align(const QVector<float> &reference, const QVector<float> &other, int maxLag);

}
