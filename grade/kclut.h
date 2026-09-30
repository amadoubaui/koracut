/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// LUT 3D en virgule flottante : lecture et écriture .cube (format Resolve / Adobe), interpolation
// tétraédrique (celle de Resolve), et la LUT « cuite » qui porte tout un grade quand il ne dépend
// que de la couleur du pixel.

#pragma once

#include "kccolor.h"

#include <functional>
#include <string>
#include <vector>

namespace kcgrade {

class Lut3D
{
public:
    Lut3D() = default;
    explicit Lut3D(int size);

    int size() const { return m_size; }
    bool isValid() const { return m_size >= 2; }

    // Données rangées r le plus rapide, puis g, puis b (ordre du .cube).
    float *entry(int r, int g, int b) { return &m_data[3 * (size_t(b) * m_size * m_size + size_t(g) * m_size + r)]; }
    const float *entry(int r, int g, int b) const { return &m_data[3 * (size_t(b) * m_size * m_size + size_t(g) * m_size + r)]; }

    // Entrée hors 0..1 : ramenée au bord du cube (comme Resolve pour une LUT 3D).
    void apply(const float in[3], float out[3]) const;
    Vec3 apply(const Vec3 &in) const;

    // Remplit la LUT par une fonction de la couleur (le grade cuit). Multi-fil si threads > 1.
    void bake(const std::function<Vec3(const Vec3 &)> &fn, int threads = 1);

    // Plage d'entrée des .cube qui la déclarent (DOMAIN_MIN / DOMAIN_MAX).
    double domainMin[3] = {0, 0, 0};
    double domainMax[3] = {1, 1, 1};
    // LUT 1D éventuelle du même fichier (appliquée avant le cube).
    std::vector<float> shaper;
    int shaperSize = 0;

    static Lut3D loadCube(const std::string &path, std::string *error = nullptr);
    bool saveCube(const std::string &path, const std::string &title) const;

private:
    int m_size = 0;
    std::vector<float> m_data;
};

} // namespace kcgrade
