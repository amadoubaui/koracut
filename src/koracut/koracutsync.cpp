/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutsync.h"

#include "lib/audio/fftCorrelation.h"

#include <cmath>
#include <vector>

namespace KoraCutSync {

QVector<float> envelopeFromLevels(const QVector<int16_t> &levels, int channels)
{
    channels = qMax(1, channels);
    QVector<float> envelope;
    envelope.reserve(levels.size() / channels);
    for (qsizetype i = 0; i + channels <= levels.size(); i += channels) {
        int peak = 0;
        for (int c = 0; c < channels; ++c) {
            peak = qMax(peak, qAbs(int(levels.at(i + c))));
        }
        envelope << float(peak);
    }
    return envelope;
}

namespace {
/** Moyenne retirée et énergie ramenée à 1 : la corrélation devient un cosinus, comparable d'un clip
 *  à l'autre même quand l'un est deux fois moins fort (micro de caméra lointain). */
std::vector<float> normalized(const QVector<float> &v)
{
    std::vector<float> out(size_t(v.size()), 0.f);
    if (v.isEmpty()) {
        return out;
    }
    double mean = 0.;
    for (float x : v) {
        mean += x;
    }
    mean /= v.size();
    double energy = 0.;
    for (qsizetype i = 0; i < v.size(); ++i) {
        out[size_t(i)] = float(v.at(i) - mean);
        energy += double(out[size_t(i)]) * out[size_t(i)];
    }
    const double norm = energy > 0. ? std::sqrt(energy) : 1.;
    for (float &x : out) {
        x = float(x / norm);
    }
    return out;
}
}

Result align(const QVector<float> &reference, const QVector<float> &other, int maxLag)
{
    Result result;
    if (reference.size() < 2 || other.size() < 2) {
        return result;
    }
    const std::vector<float> ref = normalized(reference);
    const std::vector<float> oth = normalized(other);
    // Corrélation = convolution avec l'autre signal retourné.
    std::vector<float> reversed(oth.rbegin(), oth.rend());
    std::vector<float> out(ref.size() + reversed.size() + 1, 0.f);
    FFTCorrelation::convolve(ref.data(), ref.size(), reversed.data(), reversed.size(), out.data());
    // out[m] = somme sur i de ref[i + lag] * oth[i], avec lag = m - (taille de oth - 1).
    const int shift = int(oth.size()) - 1;
    double best = -2.;
    for (int m = 0; m < int(ref.size() + oth.size() - 1); ++m) {
        const int lag = m - shift;
        if (qAbs(lag) > maxLag) {
            continue;
        }
        if (out[size_t(m)] > best) {
            best = out[size_t(m)];
            result.lag = lag;
        }
    }
    // La convolution par FFT de Kdenlive n'est pas normalisée (elle multiplie par la taille) : elle
    // sert à trouver le meilleur décalage, pas à dire à quel point il est bon. La confiance se calcule
    // donc directement à ce décalage : un cosinus entre les deux enveloppes normalisées.
    double dot = 0.;
    for (int i = 0; i < int(oth.size()); ++i) {
        const int j = i + result.lag;
        if (j >= 0 && j < int(ref.size())) {
            dot += double(ref[size_t(j)]) * oth[size_t(i)];
        }
    }
    result.confidence = qBound(0., dot, 1.);
    return result;
}

}
