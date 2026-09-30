/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kcops.h"
#include "kcresolvetables.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace kcgrade {

std::shared_ptr<const Lut3D> loadLutCached(const std::string &name, std::string *error); // kcgrade.cpp

namespace {

constexpr double kPi = 3.14159265358979323846;
// Une unité d'Offset stockée (affiché 25 → 26) vaut 25 × 7,5 / 1023 en sortie (mesuré).
constexpr double kOffsetUnit = 7.5 / 1023.;

// Plan de teinte et saturation des primaires (mesuré par Thatcher Freeman, vérifié le 28 septembre 2026).
const Mat3 &hsMatrix()
{
    static const Mat3 m{0.21, 0.70, 0.09, 1., -0.60133142, -0.39866858, 0., -1.07120062, 1.07120062};
    return m;
}
const Mat3 &hsInverse()
{
    static const Mat3 m = inverse(hsMatrix());
    return m;
}
// Plan des courbes HSL et du Color Boost : luma Rec.709, C1, C2.
const Mat3 &hslMatrix()
{
    static const Mat3 m{0.2126, 0.7152, 0.0722, 1., -0.5, -0.5, 0., -0.86667, 0.86667};
    return m;
}
const Mat3 &hslInverse()
{
    static const Mat3 m = inverse(hslMatrix());
    return m;
}

double smoothstep(double a, double b, double x)
{
    if (b <= a) {
        return x < a ? 0. : 1.;
    }
    const double t = std::clamp((x - a) / (b - a), 0., 1.);
    return t * t * (3. - 2. * t);
}

// Lift, Gamma, Gain, Offset d'un canal (valeurs stockées : lift ×2, gamma ×4 de l'affiché).
inline double lgg(double v, double L, double g, double G, double off)
{
    L = std::min(L, 0.999);
    const double noir = -L / (1. - L);
    const double blanc = 1. / std::max(G, 1e-6);
    double t = (v - noir) / (blanc - noir) + off;
    const double e = g >= 0 ? 1. / (1. + g) : 1. - g;
    if (e == 1.) {
        return t;
    }
    return t < 0 ? -std::pow(-t, e) : std::pow(t, e);
}

// Raccord quadratique de deux pentes autour de x0 (roues log).
inline double knee(double x, double x0, double k0, double k1, double d)
{
    if (x <= x0 - d) {
        return (x - x0) * k0;
    }
    if (x >= x0 + d) {
        return (x - x0) * k1;
    }
    const double u = x - x0 + d;
    return -d * k0 + k0 * u + (k1 - k0) * u * u / (4. * d);
}

double readArray(const Json &a, size_t i, double def)
{
    return a.isArray() && i < a.size() ? a.at(i).toNumber(def) : def;
}

std::vector<std::pair<double, double>> readPoints(const Json &a)
{
    std::vector<std::pair<double, double>> pts;
    if (!a.isArray()) {
        return pts;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        const Json &p = a.at(i);
        if (p.isArray() && p.size() >= 2) {
            pts.emplace_back(p.at(0).toNumber(), p.at(1).toNumber());
        }
    }
    return pts;
}

} // namespace

// --- Courbes ------------------------------------------------------------------------------------

Curve::Curve(std::vector<std::pair<double, double>> pts, bool periodic, double neutral)
    : m_periodic(periodic)
    , m_neutral(neutral)
{
    std::sort(pts.begin(), pts.end());
    const bool diagonal = std::isnan(neutral);
    m_identity = true;
    for (const auto &p : pts) {
        const double want = diagonal ? p.first : neutral;
        if (std::abs(p.second - want) > 1e-6) {
            m_identity = false;
        }
    }
    if (m_identity || pts.empty()) {
        m_identity = true;
        return;
    }
    if (diagonal) {
        // Les extrémités (noir et blanc) existent toujours dans Resolve.
        if (pts.front().first > 0.) {
            pts.insert(pts.begin(), {0., 0.});
        }
        if (pts.back().first < 1.) {
            pts.emplace_back(1., 1.);
        }
    }
    if (periodic) {
        // Une copie de chaque côté pour que la courbe se raccorde (rouge à rouge).
        std::vector<std::pair<double, double>> ext;
        for (const auto &p : pts) {
            ext.emplace_back(p.first - 360., p.second);
        }
        ext.insert(ext.end(), pts.begin(), pts.end());
        for (const auto &p : pts) {
            ext.emplace_back(p.first + 360., p.second);
        }
        pts = ext;
    }
    const size_t n = pts.size();
    for (const auto &p : pts) {
        m_x.push_back(p.first);
        m_y.push_back(p.second);
    }
    m_m.assign(n, 0.);
    if (n == 1) {
        return;
    }
    std::vector<double> delta(n - 1);
    for (size_t i = 0; i + 1 < n; ++i) {
        const double h = std::max(m_x[i + 1] - m_x[i], 1e-9);
        delta[i] = (m_y[i + 1] - m_y[i]) / h;
    }
    m_m[0] = delta[0];
    m_m[n - 1] = delta[n - 2];
    for (size_t i = 1; i + 1 < n; ++i) {
        m_m[i] = (delta[i - 1] * delta[i] <= 0) ? 0. : (delta[i - 1] + delta[i]) / 2.;
    }
    // Fritsch et Carlson : la courbe ne dépasse jamais ses points (pas de rebond).
    for (size_t i = 0; i + 1 < n; ++i) {
        if (delta[i] == 0.) {
            m_m[i] = m_m[i + 1] = 0.;
            continue;
        }
        const double a = m_m[i] / delta[i], b = m_m[i + 1] / delta[i];
        const double s = a * a + b * b;
        if (s > 9.) {
            const double t = 3. / std::sqrt(s);
            m_m[i] = t * a * delta[i];
            m_m[i + 1] = t * b * delta[i];
        }
    }
}

double Curve::eval(double x) const
{
    if (m_identity) {
        return std::isnan(m_neutral) ? x : m_neutral;
    }
    const size_t n = m_x.size();
    if (n == 1) {
        return m_y[0];
    }
    if (m_periodic) {
        x = std::fmod(x, 360.);
        if (x < 0) {
            x += 360.;
        }
    }
    if (x <= m_x.front()) {
        return m_y.front() + (x - m_x.front()) * m_m.front();
    }
    if (x >= m_x.back()) {
        return m_y.back() + (x - m_x.back()) * m_m.back();
    }
    const size_t i = size_t(std::upper_bound(m_x.begin(), m_x.end(), x) - m_x.begin()) - 1;
    const double h = m_x[i + 1] - m_x[i];
    const double t = (x - m_x[i]) / h;
    const double t2 = t * t, t3 = t2 * t;
    return (2 * t3 - 3 * t2 + 1) * m_y[i] + (t3 - 2 * t2 + t) * h * m_m[i] + (-2 * t3 + 3 * t2) * m_y[i + 1] + (t3 - t2) * h * m_m[i + 1];
}

// --- Contraste et Temp/Tint mesurés ----------------------------------------------------------

namespace {
// Une moitié de la courbe en S, interpolée dans la table ; prolongée linéairement sous t = 0.
double contrastSide(const float *table, double c, double t)
{
    using namespace resolve;
    const double ci = std::clamp((c - 1.) / 0.02, 0., double(kContrastRows - 1) - 1e-9);
    const int i = int(ci);
    const double fc = ci - i;
    auto row = [&](int k) { return table[i * kContrastCols + k] * (1. - fc) + table[(i + 1) * kContrastCols + k] * fc; };
    if (t <= 0.) {
        return t * (row(1) - row(0)) * (kContrastCols - 1);
    }
    if (t >= 1.) {
        return row(kContrastCols - 1) + (t - 1.);
    }
    const double tt = t * (kContrastCols - 1);
    const int k = std::min(int(tt), kContrastCols - 2);
    const double f = tt - k;
    return row(k) * (1. - f) + row(k + 1) * f;
}
} // namespace

double resolveContrast(double v, double c, double p)
{
    if (c <= 1.) {
        return (v - p) * c + p;
    }
    p = std::clamp(p, 1e-4, 1. - 1e-4);
    if (v < p) {
        return p * contrastSide(resolve::kContrastLow, c, v / p);
    }
    return 1. - (1. - p) * contrastSide(resolve::kContrastHigh, c, (1. - v) / (1. - p));
}

Mat3 tempTintMatrix(double temp, double tint)
{
    using namespace resolve;
    const double ti = std::clamp((temp + 4000.) / 500., 0., double(kTempSteps - 1) - 1e-9);
    const double ni = std::clamp((tint + 100.) / 20., 0., double(kTintSteps - 1) - 1e-9);
    const int i = int(ti), j = int(ni);
    const double ft = ti - i, fn = ni - j;
    double g[3];
    for (int c = 0; c < 3; ++c) {
        auto at = [&](int a, int b) { return std::log(double(kTempTintGains[(a * kTintSteps + b) * 3 + c])); };
        g[c] = std::exp(at(i, j) * (1 - ft) * (1 - fn) + at(i + 1, j) * ft * (1 - fn) + at(i, j + 1) * (1 - ft) * fn + at(i + 1, j + 1) * ft * fn);
    }
    Mat3 v;
    std::copy(kConeBasis, kConeBasis + 9, v.begin());
    const Mat3 d{g[0], 0, 0, 0, g[1], 0, 0, 0, g[2]};
    return mul(mul(v, d), inverse(v));
}

// --- Lecture des réglages -----------------------------------------------------------------------

bool Primaries::read(const Json &j)
{
    if (!j.isObject()) {
        return true;
    }
    for (int i = 0; i < 4; ++i) {
        lift[i] = readArray(j.get("lift"), size_t(i), 0.);
        gamma[i] = readArray(j.get("gamma"), size_t(i), 0.);
        gain[i] = readArray(j.get("gain"), size_t(i), 1.);
    }
    for (int i = 0; i < 3; ++i) {
        offset[i] = readArray(j.get("offset"), size_t(i), 25.);
    }
    contrast = std::clamp(j.num("contraste", 1.), 0., 2.);
    pivot = std::clamp(j.num("pivot", 0.435), 0., 1.);
    saturation = std::clamp(j.num("saturation", 50.), 0., 100.);
    hue = std::clamp(j.num("teinte", 50.), 0., 100.);
    lumMix = std::clamp(j.num("lum_mix", 100.), 0., 100.);
    temp = std::clamp(j.num("temp", 0.), -4000., 4000.);
    tint = std::clamp(j.num("tint", 0.), -100., 100.);
    boost = std::clamp(j.num("boost", 0.), -100., 100.);
    midDetail = std::clamp(j.num("mid_detail", 0.), -100., 100.);
    shadows = std::clamp(j.num("ombres", 0.), -100., 100.);
    highlights = std::clamp(j.num("hautes_lumieres", 0.), -100., 100.);
    return true;
}

bool Primaries::isIdentity() const
{
    for (int i = 0; i < 4; ++i) {
        if (lift[i] != 0. || gamma[i] != 0. || gain[i] != 1.) {
            return false;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (offset[i] != 25.) {
            return false;
        }
    }
    return contrast == 1. && saturation == 50. && hue == 50. && temp == 0. && tint == 0. && boost == 0.;
}

bool LogWheels::read(const Json &j)
{
    if (!j.isObject()) {
        return true;
    }
    for (int i = 0; i < 3; ++i) {
        shadow[i] = readArray(j.get("ombres"), size_t(i), 0.);
        mid[i] = readArray(j.get("tons_moyens"), size_t(i), 0.);
        high[i] = readArray(j.get("hautes_lumieres"), size_t(i), 0.);
    }
    lowRange = std::clamp(j.num("plage_basse", 0.333), 0.01, 0.99);
    highRange = std::clamp(j.num("plage_haute", 0.550), 0.01, 0.99);
    return true;
}

bool LogWheels::isIdentity() const
{
    for (int i = 0; i < 3; ++i) {
        if (shadow[i] != 0. || mid[i] != 0. || high[i] != 0.) {
            return false;
        }
    }
    return true;
}

bool Curves::read(const Json &j)
{
    if (!j.isObject()) {
        return true;
    }
    const double diag = std::nan("");
    y = Curve(readPoints(j.get("y")), false, diag);
    r = Curve(readPoints(j.get("r")), false, diag);
    g = Curve(readPoints(j.get("v")), false, diag);
    b = Curve(readPoints(j.get("b")), false, diag);
    for (int i = 0; i < 4; ++i) {
        intensity[i] = std::clamp(readArray(j.get("intensite"), size_t(i), 100.), 0., 100.);
    }
    return true;
}

bool HueCurves::read(const Json &j)
{
    if (!j.isObject()) {
        return true;
    }
    hueHue = Curve(readPoints(j.get("teinte_teinte")), true, 0.);
    hueSat = Curve(readPoints(j.get("teinte_sat")), true, 1.);
    hueLum = Curve(readPoints(j.get("teinte_lum")), true, 1.);
    lumSat = Curve(readPoints(j.get("lum_sat")), false, 1.);
    satSat = Curve(readPoints(j.get("sat_sat")), false, 1.);
    satLum = Curve(readPoints(j.get("sat_lum")), false, 1.);
    return true;
}

bool HueCurves::isIdentity() const
{
    return hueHue.isIdentity() && hueSat.isIdentity() && hueLum.isIdentity() && lumSat.isIdentity() && satSat.isIdentity() && satLum.isIdentity();
}

bool NodeColor::read(const Json &node, std::string *error)
{
    m_prim.read(node.get("primaires"));
    m_log.read(node.get("log"));
    m_curves.read(node.get("courbes"));
    m_hue.read(node.get("courbes_teinte"));
    const Json &cst = node.get("cst");
    if (cst.isObject() && cst.flag("actif", true)) {
        CstParams p;
        p.inGamut = cst.str("entree_gamut", p.inGamut);
        p.inTf = cst.str("entree_gamma", p.inTf);
        p.outGamut = cst.str("sortie_gamut", p.outGamut);
        p.outTf = cst.str("sortie_gamma", p.outTf);
        p.toneMapping = cst.str("tone_mapping", p.toneMapping);
        p.gamutMapping = cst.str("gamut_mapping", p.gamutMapping);
        p.maxInNits = cst.num("nits_entree", 0.);
        p.maxOutNits = cst.num("nits_sortie", 100.);
        p.adaptation = cst.num("adaptation", 9.);
        p.forwardOotf = cst.flag("ootf", p.inTf == "hlg");
        p.inverseOotf = cst.flag("ootf_inverse", false);
        p.adaptWhite = cst.flag("adapter_blanc", true);
        if (!gamutInfo(p.inGamut) || !gamutInfo(p.outGamut)) {
            if (error) {
                *error = "Gamut inconnu dans le CST : « " + (gamutInfo(p.inGamut) ? p.outGamut : p.inGamut) + " ».";
            }
            return false;
        }
        if (!transferInfo(p.inTf) || !transferInfo(p.outTf)) {
            if (error) {
                *error = "Courbe inconnue dans le CST : « " + (transferInfo(p.inTf) ? p.outTf : p.inTf) + " ».";
            }
            return false;
        }
        m_cst = std::make_unique<Cst>(p);
        if (m_cst->isIdentity()) {
            m_cst.reset();
        } else {
            const TransferInfo *ti = transferInfo(p.inTf);
            m_cstLog = (ti && ti->cameraLog) || p.inTf == "applelog" || p.inTf == "hlg" || p.inTf == "pq" || p.inTf == "davinci_intermediate" ||
                       p.inTf == "acescct";
        }
    }
    const Json &lut = node.get("lut");
    if (lut.isObject() && !lut.str("fichier").empty() && lut.flag("actif", true)) {
        std::string err;
        m_lut = loadLutCached(lut.str("fichier"), &err);
        if (!m_lut) {
            if (error) {
                *error = "LUT illisible : « " + lut.str("fichier") + " » (" + err + ").";
            }
            return false;
        }
        m_lutMix = std::clamp(lut.num("melange", 100.), 0., 100.) / 100.;
    }

    m_hasTT = m_prim.temp != 0. || m_prim.tint != 0.;
    if (m_hasTT) {
        m_tt = tempTintMatrix(m_prim.temp, m_prim.tint);
    }
    m_hasPrim = false;
    for (int i = 0; i < 4; ++i) {
        m_hasPrim = m_hasPrim || m_prim.lift[i] != 0. || m_prim.gamma[i] != 0. || m_prim.gain[i] != 1.;
    }
    for (int i = 0; i < 3; ++i) {
        m_hasPrim = m_hasPrim || m_prim.offset[i] != 25.;
    }
    m_hasPrim = m_hasPrim || m_prim.contrast != 1.;
    m_hasLog = !m_log.isIdentity();
    const double angle = (m_prim.hue - 50.) / 50. * kPi;
    m_cosH = std::cos(angle);
    m_sinH = std::sin(angle);
    m_sat = m_prim.saturation / 50.;
    m_hasHS = m_prim.hue != 50. || m_prim.saturation != 50.;
    m_identity = !m_cst && m_prim.boost == 0. && !m_hasTT && !m_hasPrim && m_curves.isIdentity() && !m_hasLog && !m_hasHS && !m_lut && m_hue.isIdentity();
    return true;
}

Vec3 NodeColor::apply(const Vec3 &in) const
{
    if (m_identity) {
        return in;
    }
    Vec3 c = in;
    if (m_cst) {
        c = m_cst->apply(c);
    }
    // Color Boost : chroma du plan HSL, luma Rec.709 gardée.
    if (m_prim.boost != 0.) {
        Vec3 yc = mul(hslMatrix(), c);
        const double s = std::hypot(yc[1], yc[2]);
        if (s > 1e-9) {
            const double k = m_prim.boost > 0 ? 0.00402 : 0.00574;
            const double s2 = std::max(0., s + k * m_prim.boost * std::pow(s, 0.4));
            yc[1] *= s2 / s;
            yc[2] *= s2 / s;
            c = mul(hslInverse(), yc);
        }
    }
    if (m_hasTT) {
        c = mul(m_tt, c);
    }
    // Primaires YRGB et Lum Mix.
    if (m_hasPrim) {
        const double yin = luma709(c[0], c[1], c[2]);
        double off[3], ch[3];
        for (int i = 0; i < 3; ++i) {
            off[i] = (m_prim.offset[i] - 25.) * kOffsetUnit;
            ch[i] = lgg(c[i], m_prim.lift[i + 1] * 2., m_prim.gamma[i + 1] * 4., m_prim.gain[i + 1], off[i]);
        }
        double yo = lgg(yin, m_prim.lift[0] * 2., m_prim.gamma[0] * 4., m_prim.gain[0], luma709(off[0], off[1], off[2]));
        if (m_prim.contrast != 1.) {
            for (double &v : ch) {
                v = resolveContrast(v, m_prim.contrast, m_prim.pivot);
            }
            yo = resolveContrast(yo, m_prim.contrast, m_prim.pivot);
        }
        const double d = m_prim.lumMix / 100. * (yo - luma709(ch[0], ch[1], ch[2]));
        c = {ch[0] + d, ch[1] + d, ch[2] + d};
    }
    // Courbes Custom : R, V, B par canal, puis Y comme les roues Y (dosé par Lum Mix).
    if (!m_curves.isIdentity()) {
        const Curve *cv[3] = {&m_curves.r, &m_curves.g, &m_curves.b};
        for (int i = 0; i < 3; ++i) {
            if (!cv[i]->isIdentity()) {
                const double k = m_curves.intensity[i + 1] / 100.;
                c[i] = c[i] + k * (cv[i]->eval(c[i]) - c[i]);
            }
        }
        if (!m_curves.y.isIdentity()) {
            const double y = luma709(c[0], c[1], c[2]);
            const double k = m_curves.intensity[0] / 100.;
            const double d = (y + k * (m_curves.y.eval(y) - y) - y) * m_prim.lumMix / 100.;
            c = {c[0] + d, c[1] + d, c[2] + d};
        }
    }
    // Roues log, par canal.
    if (m_hasLog) {
        const double lo = m_log.lowRange, hi = std::max(m_log.highRange, m_log.lowRange + 0.01);
        constexpr double dk = 0.08008;
        for (int i = 0; i < 3; ++i) {
            const double x = c[i];
            double y = x;
            const double S = m_log.shadow[i], M = m_log.mid[i], H = m_log.high[i];
            if (S != 0.) {
                const double k = S >= 0 ? 1. - S : 1. - S / lo;
                y += knee(x, lo, k, 1., dk) - (x - lo);
            }
            if (H != 0.) {
                const double k = H >= 0 ? 1. + H / (1. - hi) : 1. + H;
                y += knee(x, hi, 1., k, dk) - (x - hi);
            }
            if (M != 0.) {
                using namespace resolve;
                const double a = lo - kMidDa, b = hi + kMidDb, w = b - a;
                const double t = std::clamp((x - a) / w, 1e-9, 1.);
                const double qe = kMidQ * (1. + kMidS * (M > 0 ? 1. : -1.));
                y += kMidA * M * w * std::pow(std::sin(kPi * t), kMidP) * std::pow(t, qe);
            }
            c[i] = y;
        }
    }
    // Teinte et saturation.
    if (m_hasHS) {
        Vec3 yc = mul(hsMatrix(), c);
        const double a1 = yc[1] * m_cosH - yc[2] * m_sinH;
        const double a2 = yc[1] * m_sinH + yc[2] * m_cosH;
        yc[1] = a1 * m_sat;
        yc[2] = a2 * m_sat;
        c = mul(hsInverse(), yc);
    }
    if (m_lut) {
        const Vec3 l = m_lut->apply(c);
        for (int i = 0; i < 3; ++i) {
            c[i] = c[i] + m_lutMix * (l[i] - c[i]);
        }
    }
    // Courbes de teinte (plan HSL : luma, C1, C2).
    if (!m_hue.isIdentity()) {
        Vec3 yc = mul(hslMatrix(), c);
        double s = std::hypot(yc[1], yc[2]);
        // Abscisse des courbes de teinte comme dans Resolve : rouge 0, jaune 60, vert 120, cyan 180,
        // bleu 240, magenta 300 (l'angle du plan C1/C2 tourne dans l'autre sens).
        const double h = std::fmod(720. - std::atan2(yc[2], yc[1]) * 180. / kPi, 360.);
        double y = yc[0];
        const double w = std::min(1., s * 4.); // les gris ne bougent pas
        const double s2 = s * m_hue.hueSat.eval(h) * m_hue.lumSat.eval(y) * m_hue.satSat.eval(s);
        const double h2 = -(h + m_hue.hueHue.eval(h)) * kPi / 180.;
        y = y * (1. + (m_hue.hueLum.eval(h) - 1.) * w) * (1. + (m_hue.satLum.eval(s) - 1.) * w);
        c = mul(hslInverse(), Vec3{y, std::max(0., s2) * std::cos(h2), std::max(0., s2) * std::sin(h2)});
    }
    return c;
}

// --- Qualificateur ----------------------------------------------------------------------------

bool Qualifier::read(const Json &j)
{
    if (!j.isObject()) {
        return true;
    }
    active = j.flag("actif", true);
    mode = j.str("mode", "hsl");
    const Json &h = j.get("teinte");
    hueOn = h.flag("actif", true);
    hueCenter = h.num("centre", hueCenter);
    hueWidth = h.num("largeur", hueWidth);
    hueSoft = h.num("doux", hueSoft);
    hueSym = h.num("sym", hueSym);
    const Json &s = j.get("sat");
    satOn = s.flag("actif", true);
    satLow = s.num("bas", satLow);
    satHigh = s.num("haut", satHigh);
    satLowSoft = s.num("doux_bas", satLowSoft);
    satHighSoft = s.num("doux_haut", satHighSoft);
    const Json &l = j.get("lum");
    lumOn = l.flag("actif", true);
    lumLow = l.num("bas", lumLow);
    lumHigh = l.num("haut", lumHigh);
    lumLowSoft = l.num("doux_bas", lumLowSoft);
    lumHighSoft = l.num("doux_haut", lumHighSoft);
    const char *names[3] = {"r", "v", "b"};
    for (int i = 0; i < 3; ++i) {
        const Json &c = j.get(names[i]);
        rgbLow[i] = c.num("bas", 0.);
        rgbHigh[i] = c.num("haut", 100.);
    }
    rgbSoft = j.num("doux_rvb", rgbSoft);
    blackClip = j.num("noir", 0.);
    whiteClip = j.num("blanc", 100.);
    blur = std::clamp(j.num("flou", 0.), 0., 100.);
    invert = j.flag("inverser", false);
    return true;
}

namespace {
double rangeKey(double v, double lo, double hi, double loSoft, double hiSoft)
{
    const double a = loSoft > 0 ? smoothstep(lo - loSoft, lo, v) : (v >= lo ? 1. : 0.);
    const double b = hiSoft > 0 ? 1. - smoothstep(hi, hi + hiSoft, v) : (v <= hi ? 1. : 0.);
    return a * b;
}
} // namespace

double Qualifier::key(const Vec3 &rgb) const
{
    if (!active) {
        return 1.;
    }
    double k = 1.;
    const double r = std::clamp(rgb[0], 0., 1.), g = std::clamp(rgb[1], 0., 1.), b = std::clamp(rgb[2], 0., 1.);
    if (mode == "rvb") {
        const double v[3] = {r, g, b};
        for (int i = 0; i < 3; ++i) {
            k *= rangeKey(v[i] * 100., rgbLow[i], rgbHigh[i], rgbSoft, rgbSoft);
        }
    } else {
        const double lum = luma709(r, g, b) * 100.;
        if (mode == "hsl") {
            const Vec3 yc = mul(hslMatrix(), Vec3{r, g, b});
            // Échelle de teinte de Resolve (barre du qualificateur) : magenta 0, rouge 16,7, jaune 33,3,
            // vert 50, cyan 66,7, bleu 83,3. L'angle du plan C1/C2 tourne dans l'autre sens.
            const double angle = std::atan2(yc[2], yc[1]) * 180. / kPi;
            const double h = std::fmod(60. - angle + 720., 360.) / 3.6;
            const double mx = std::max({r, g, b}), mn = std::min({r, g, b});
            const double l = (mx + mn) / 2.;
            const double sat = (mx - mn) < 1e-9 ? 0. : (mx - mn) / (1. - std::abs(2. * l - 1.) + 1e-9) * 100.;
            if (hueOn) {
                double d = h - hueCenter;
                while (d > 50.) {
                    d -= 100.;
                }
                while (d < -50.) {
                    d += 100.;
                }
                const double half = hueWidth / 2.;
                const double skew = std::clamp((hueSym - 50.) / 50., -1., 1.);
                const double softL = hueSoft * (1. - skew), softR = hueSoft * (1. + skew);
                double hk;
                if (std::abs(d) <= half) {
                    hk = 1.;
                } else if (d < 0) {
                    hk = softL > 0 ? 1. - smoothstep(0., softL, -d - half) : 0.;
                } else {
                    hk = softR > 0 ? 1. - smoothstep(0., softR, d - half) : 0.;
                }
                k *= hk;
            }
            if (satOn) {
                k *= rangeKey(sat, satLow, satHigh, satLowSoft, satHighSoft);
            }
        }
        if (lumOn) {
            k *= rangeKey(lum, lumLow, lumHigh, lumLowSoft, lumHighSoft);
        }
    }
    // Clip noir et clip blanc de la clé (Matte Finesse).
    const double lo = blackClip / 100., hi = std::max(whiteClip / 100., lo + 1e-3);
    k = std::clamp((k - lo) / (hi - lo), 0., 1.);
    return invert ? 1. - k : k;
}

// --- Fenêtres ----------------------------------------------------------------------------------

bool Window::read(const Json &j)
{
    type = j.str("type", "cercle");
    x = j.num("x", 0.5);
    y = j.num("y", 0.5);
    size = std::max(j.num("taille", 0.25), 1e-4);
    aspect = std::max(j.num("aspect", 1.), 1e-3);
    rotation = j.num("rotation", 0.);
    soft = std::max(j.num("doux", 0.05), 0.);
    opacity = std::clamp(j.num("opacite", 1.), 0., 1.);
    invert = j.flag("inverser", false);
    subtract = j.flag("soustraire", false);
    points = readPoints(j.get("points"));
    return true;
}

double Window::key(double u, double v, double imageAspect) const
{
    // Distance signée au bord, en unités de hauteur d'image (négative à l'intérieur).
    double du = (u - x) * imageAspect, dv = v - y;
    const double a = -rotation * kPi / 180.;
    const double ru = du * std::cos(a) - dv * std::sin(a);
    const double rv = du * std::sin(a) + dv * std::cos(a);
    double dist;
    if (type == "rectangle") {
        const double hw = size * aspect, hh = size;
        const double qx = std::abs(ru) - hw, qy = std::abs(rv) - hh;
        dist = std::hypot(std::max(qx, 0.), std::max(qy, 0.)) + std::min(std::max(qx, qy), 0.);
    } else if (type == "degrade") {
        // Clé 1 du côté où pointe la flèche (vers le haut si rotation 0).
        dist = rv;
    } else if (type == "polygone" && points.size() >= 3) {
        bool inside = false;
        double best = 1e9;
        const size_t n = points.size();
        const double pu = u * imageAspect, pv = v;
        for (size_t i = 0, k = n - 1; i < n; k = i++) {
            const double xi = points[i].first * imageAspect, yi = points[i].second;
            const double xk = points[k].first * imageAspect, yk = points[k].second;
            if (((yi > pv) != (yk > pv)) && (pu < (xk - xi) * (pv - yi) / (yk - yi + 1e-12) + xi)) {
                inside = !inside;
            }
            const double ex = xk - xi, ey = yk - yi;
            const double t = std::clamp(((pu - xi) * ex + (pv - yi) * ey) / (ex * ex + ey * ey + 1e-12), 0., 1.);
            best = std::min(best, std::hypot(pu - xi - t * ex, pv - yi - t * ey));
        }
        dist = inside ? -best : best;
    } else {
        // Cercle ou ellipse : aspect > 1 l'élargit.
        const double rx = size * aspect, ry = size;
        const double r = std::hypot(ru / rx, rv / ry);
        dist = (r - 1.) * std::min(rx, ry);
    }
    double k = soft > 0 ? 1. - smoothstep(-soft / 2., soft / 2., dist) : (dist <= 0 ? 1. : 0.);
    if (invert) {
        k = 1. - k;
    }
    return k * opacity;
}

} // namespace kcgrade
