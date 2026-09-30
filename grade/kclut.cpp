/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kclut.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <locale>
#include <sstream>
#include <thread>

namespace kcgrade {

Lut3D::Lut3D(int size)
    : m_size(size)
    , m_data(size_t(size) * size * size * 3, 0.f)
{
}

void Lut3D::apply(const float in[3], float out[3]) const
{
    const int n = m_size - 1;
    float c[3];
    for (int i = 0; i < 3; ++i) {
        float v = in[i];
        if (shaperSize > 1) {
            // LUT 1D du même fichier, interpolée linéairement.
            float t = std::clamp(float((v - domainMin[i]) / (domainMax[i] - domainMin[i])), 0.f, 1.f) * (shaperSize - 1);
            int k = std::min(int(t), shaperSize - 2);
            float f = t - k;
            v = shaper[3 * k + i] * (1 - f) + shaper[3 * (k + 1) + i] * f;
            c[i] = std::clamp(v, 0.f, 1.f) * n;
        } else {
            c[i] = std::clamp(float((v - domainMin[i]) / (domainMax[i] - domainMin[i])), 0.f, 1.f) * n;
        }
    }
    const int r0 = std::min(int(c[0]), n - 1), g0 = std::min(int(c[1]), n - 1), b0 = std::min(int(c[2]), n - 1);
    const float fr = c[0] - r0, fg = c[1] - g0, fb = c[2] - b0;
    const float *p000 = entry(r0, g0, b0);
    const float *p111 = entry(r0 + 1, g0 + 1, b0 + 1);
    // Interpolation tétraédrique : six tétraèdres selon l'ordre des fractions.
    const float *pA, *pB;
    float wA, wB, w0, w1;
    if (fr > fg) {
        if (fg > fb) { // r > g > b
            pA = entry(r0 + 1, g0, b0);
            pB = entry(r0 + 1, g0 + 1, b0);
            w0 = 1 - fr; wA = fr - fg; wB = fg - fb; w1 = fb;
        } else if (fr > fb) { // r > b >= g
            pA = entry(r0 + 1, g0, b0);
            pB = entry(r0 + 1, g0, b0 + 1);
            w0 = 1 - fr; wA = fr - fb; wB = fb - fg; w1 = fg;
        } else { // b >= r > g
            pA = entry(r0, g0, b0 + 1);
            pB = entry(r0 + 1, g0, b0 + 1);
            w0 = 1 - fb; wA = fb - fr; wB = fr - fg; w1 = fg;
        }
    } else {
        if (fb > fg) { // b > g >= r
            pA = entry(r0, g0, b0 + 1);
            pB = entry(r0, g0 + 1, b0 + 1);
            w0 = 1 - fb; wA = fb - fg; wB = fg - fr; w1 = fr;
        } else if (fb > fr) { // g >= b > r
            pA = entry(r0, g0 + 1, b0);
            pB = entry(r0, g0 + 1, b0 + 1);
            w0 = 1 - fg; wA = fg - fb; wB = fb - fr; w1 = fr;
        } else { // g >= r >= b
            pA = entry(r0, g0 + 1, b0);
            pB = entry(r0 + 1, g0 + 1, b0);
            w0 = 1 - fg; wA = fg - fr; wB = fr - fb; w1 = fb;
        }
    }
    for (int i = 0; i < 3; ++i) {
        out[i] = w0 * p000[i] + wA * pA[i] + wB * pB[i] + w1 * p111[i];
    }
}

Vec3 Lut3D::apply(const Vec3 &in) const
{
    const float i[3] = {float(in[0]), float(in[1]), float(in[2])};
    float o[3];
    apply(i, o);
    return {o[0], o[1], o[2]};
}

void Lut3D::bake(const std::function<Vec3(const Vec3 &)> &fn, int threads)
{
    const int n = m_size;
    const double step = 1. / (n - 1);
    auto work = [&](int bFrom, int bTo) {
        for (int b = bFrom; b < bTo; ++b) {
            for (int g = 0; g < n; ++g) {
                for (int r = 0; r < n; ++r) {
                    const Vec3 v = fn({r * step, g * step, b * step});
                    float *e = entry(r, g, b);
                    e[0] = float(v[0]);
                    e[1] = float(v[1]);
                    e[2] = float(v[2]);
                }
            }
        }
    };
    threads = std::clamp(threads, 1, n);
    if (threads == 1) {
        work(0, n);
        return;
    }
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) {
        pool.emplace_back(work, n * t / threads, n * (t + 1) / threads);
    }
    for (auto &th : pool) {
        th.join();
    }
}

Lut3D Lut3D::loadCube(const std::string &path, std::string *error)
{
    std::ifstream in(path);
    if (!in) {
        if (error) {
            *error = "fichier illisible";
        }
        return Lut3D();
    }
    int size3 = 0, size1 = 0;
    double dmin[3] = {0, 0, 0}, dmax[3] = {1, 1, 1};
    std::vector<float> values;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos || line[s] == '#') {
            continue;
        }
        std::istringstream ls(line.substr(s));
        ls.imbue(std::locale::classic());
        std::string key;
        if (std::isalpha(static_cast<unsigned char>(line[s]))) {
            ls >> key;
            if (key == "LUT_3D_SIZE") {
                ls >> size3;
            } else if (key == "LUT_1D_SIZE") {
                ls >> size1;
            } else if (key == "DOMAIN_MIN" || key == "LUT_3D_INPUT_RANGE") {
                if (key == "DOMAIN_MIN") {
                    ls >> dmin[0] >> dmin[1] >> dmin[2];
                } else {
                    double a, b;
                    ls >> a >> b;
                    dmin[0] = dmin[1] = dmin[2] = a;
                    dmax[0] = dmax[1] = dmax[2] = b;
                }
            } else if (key == "DOMAIN_MAX") {
                ls >> dmax[0] >> dmax[1] >> dmax[2];
            }
            continue;
        }
        float a, b, c;
        if (ls >> a >> b >> c) {
            values.push_back(a);
            values.push_back(b);
            values.push_back(c);
        }
    }
    Lut3D lut;
    const size_t n1 = size_t(size1) * 3;
    const size_t n3 = size_t(size3) * size3 * size3 * 3;
    if (size3 < 2 && size1 >= 2 && values.size() == n1) {
        // LUT 1D seule : on la pose dans un cube de 33 points.
        Lut3D one(33);
        one.shaper.assign(values.begin(), values.end());
        one.shaperSize = size1;
        std::copy(dmin, dmin + 3, one.domainMin);
        std::copy(dmax, dmax + 3, one.domainMax);
        std::vector<float> sh = one.shaper;
        int ss = size1;
        one.bake([&](const Vec3 &v) {
            Vec3 o;
            for (int i = 0; i < 3; ++i) {
                double t = std::clamp(v[i], 0., 1.) * (ss - 1);
                int k = std::min(int(t), ss - 2);
                double f = t - k;
                o[i] = sh[3 * k + i] * (1 - f) + sh[3 * (k + 1) + i] * f;
            }
            return o;
        });
        one.shaper.clear();
        one.shaperSize = 0;
        return one;
    }
    if (size3 < 2 || values.size() != n3 + (size1 >= 2 ? n1 : 0)) {
        if (error) {
            *error = "format .cube non reconnu";
        }
        return Lut3D();
    }
    lut = Lut3D(size3);
    size_t off = 0;
    if (size1 >= 2) {
        lut.shaper.assign(values.begin(), values.begin() + long(n1));
        lut.shaperSize = size1;
        off = n1;
    }
    std::copy(values.begin() + long(off), values.end(), lut.m_data.begin());
    std::copy(dmin, dmin + 3, lut.domainMin);
    std::copy(dmax, dmax + 3, lut.domainMax);
    return lut;
}

bool Lut3D::saveCube(const std::string &path, const std::string &title) const
{
    std::ofstream out(path);
    if (!out) {
        return false;
    }
    out.imbue(std::locale::classic());
    out << "TITLE \"" << title << "\"\n";
    out << "# Fait avec KoraCut\n";
    out << "LUT_3D_SIZE " << m_size << "\n\n";
    char buf[64];
    for (size_t i = 0; i < m_data.size(); i += 3) {
        std::snprintf(buf, sizeof buf, "%.6f %.6f %.6f\n", m_data[i], m_data[i + 1], m_data[i + 2]);
        // snprintf suit la locale C du processus MLT ; on garde le point décimal quoi qu'il arrive.
        for (char *p = buf; *p; ++p) {
            if (*p == ',') {
                *p = '.';
            }
        }
        out << buf;
    }
    return bool(out);
}

} // namespace kcgrade
