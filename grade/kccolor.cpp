/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kccolor.h"

#include <algorithm>
#include <cmath>

namespace kcgrade {

Vec3 mul(const Mat3 &m, const Vec3 &v)
{
    return {m[0] * v[0] + m[1] * v[1] + m[2] * v[2], m[3] * v[0] + m[4] * v[1] + m[5] * v[2], m[6] * v[0] + m[7] * v[1] + m[8] * v[2]};
}

Mat3 mul(const Mat3 &a, const Mat3 &b)
{
    Mat3 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
        }
    }
    return r;
}

Mat3 inverse(const Mat3 &m)
{
    const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const double det = a * A + b * B + c * C;
    const double k = std::abs(det) < 1e-15 ? 0. : 1. / det;
    return {A * k, -(b * i - c * h) * k, (b * f - c * e) * k, B * k, (a * i - c * g) * k, -(a * f - c * d) * k, C * k, -(a * h - b * g) * k, (a * e - b * d) * k};
}

Mat3 identity3()
{
    return {1, 0, 0, 0, 1, 0, 0, 0, 1};
}

// --- Courbes ------------------------------------------------------------------------------------

namespace {

// Une image vidéo « video range » étendue par le décodeur (64 → 0, 940 → 1) repasse en code 10 bits
// normalisé, le domaine des formules des constructeurs. C'est aussi ce que fait Resolve pour un plan
// en niveaux Vidéo (noir du S-Log3 à 3,5 % sur son oscilloscope).
inline double vfToCv(double v)
{
    return (v * 876. + 64.) / 1023.;
}
inline double cvToVf(double cv)
{
    return (cv * 1023. - 64.) / 876.;
}

double spow(double x, double p)
{
    return x < 0 ? -std::pow(-x, p) : std::pow(x, p);
}

// Paramètres LogC3 à EI 800 (ARRI, « Log C Curve Usage in VFX », 2017).
const double LC3_CUT = 0.010591, LC3_A = 5.555556, LC3_B = 0.052272, LC3_C = 0.247190, LC3_D = 0.385537, LC3_E = 5.367655, LC3_F = 0.092809;

// LogC4 (ARRI LogC4 Specification, 2025).
struct LogC4
{
    double a, b, c, s, t;
    LogC4()
    {
        a = (std::pow(2., 18.) - 16.) / 117.45;
        b = (1023. - 95.) / 1023.;
        c = 95. / 1023.;
        s = (7. * std::log(2.) * std::pow(2., 7. - 14. * c / b)) / (a * b);
        t = (std::pow(2., 14. * (-c / b) + 6.) - 64.) / a;
    }
};
const LogC4 &logc4()
{
    static const LogC4 k;
    return k;
}

// Apple Log (Apple Log Profile White Paper, 2023).
const double AL_R0 = -0.05641088, AL_RT = 0.01, AL_S = 47.28711236, AL_B = 0.00964052, AL_G = 0.08550479, AL_D = 0.69336945;

// Canon Log 2 et 3 (Canon, v1.2).
double clogDecode(double cv, double slope, double mult, double off)
{
    const double t = cv >= off ? (std::pow(10., (cv - off) / slope) - 1.) / mult : -(std::pow(10., (off - cv) / slope) - 1.) / mult;
    return 0.9 * t;
}
double clogEncode(double x, double slope, double mult, double off)
{
    const double t = x / 0.9;
    return t >= 0 ? slope * std::log10(mult * t + 1.) + off : -slope * std::log10(-mult * t + 1.) + off;
}

// Fujifilm F-Log et F-Log2 (Data Sheets 1.2 et 1.1).
struct FLog
{
    double a, b, c, d, e, f, cut1, cut2;
};
const FLog FLOG1{0.555556, 0.009468, 0.344676, 0.790453, 8.735631, 0.092864, 0.00089, 0.100537775223865};
const FLog FLOG2{5.555556, 0.064829, 0.245281, 0.384316, 8.799461, 0.092864, 0.000889, 0.100686685370811};

// HLG (BT.2100).
const double HLG_A = 0.17883277, HLG_B = 0.28466892, HLG_C = 0.55991073;
// PQ (ST 2084).
const double PQ_M1 = 0.1593017578125, PQ_M2 = 78.84375, PQ_C1 = 0.8359375, PQ_C2 = 18.8515625, PQ_C3 = 18.6875;

double pqDecodeNits(double v)
{
    const double vp = std::pow(std::max(v, 0.), 1. / PQ_M2);
    return 10000. * std::pow(std::max(vp - PQ_C1, 0.) / (PQ_C2 - PQ_C3 * vp), 1. / PQ_M1);
}
double pqEncodeNits(double nits)
{
    const double y = std::pow(std::max(nits, 0.) / 10000., PQ_M1);
    return std::pow((PQ_C1 + PQ_C2 * y) / (1. + PQ_C3 * y), PQ_M2);
}
double hlgInverseOetf(double e)
{
    return e <= 0.5 ? e * e / 3. : (std::exp((e - HLG_C) / HLG_A) + HLG_B) / 12.;
}
double hlgOetf(double e)
{
    e = std::max(e, 0.);
    return e <= 1. / 12. ? std::sqrt(3. * e) : HLG_A * std::log(12. * e - HLG_B) + HLG_C;
}

} // namespace

double decode(const std::string &tf, double v)
{
    const TransferInfo *info = transferInfo(tf);
    const double cv = (info && info->cameraLog) ? vfToCv(v) : v;
    if (tf == "lineaire") {
        return v;
    }
    if (tf == "gamma22") {
        return spow(v, 2.2);
    }
    if (tf == "gamma24") {
        return spow(v, 2.4);
    }
    if (tf == "gamma26") {
        return spow(v, 2.6);
    }
    if (tf == "rec709") { // OETF BT.709 inverse (Rec.709 « scène »)
        return v < 0.081 ? v / 4.5 : spow((v + 0.099) / 1.099, 1. / 0.45);
    }
    if (tf == "srgb") {
        return v <= 0.04045 ? v / 12.92 : spow((v + 0.055) / 1.055, 2.4);
    }
    if (tf == "pq") { // 1.0 = 100 nits
        return pqDecodeNits(v) / 100.;
    }
    if (tf == "hlg") { // lumière de scène 0..1 (l'OOTF est appliquée par le CST si demandé)
        return hlgInverseOetf(v);
    }
    if (tf == "slog3") {
        return cv >= 171.2102946929 / 1023. ? std::pow(10., (cv * 1023. - 420.) / 261.5) * 0.19 - 0.01 : (cv * 1023. - 95.) * 0.01125 / (171.2102946929 - 95.);
    }
    if (tf == "slog2") {
        const double y = (cv * 1023. - 64.) / 876.;
        const double t = y >= 0.030001222851889303 ? std::pow(10., (y - 0.646596) / 0.432699) - 0.037584 : (y - 0.030001222851889303) / 5.;
        return t * 0.9 * 219. / 155.;
    }
    if (tf == "vlog") {
        return cv < 0.181 ? (cv - 0.125) / 5.6 : std::pow(10., (cv - 0.598206) / 0.241514) - 0.00873;
    }
    if (tf == "clog") {
        return clogDecode(cv, 0.45310179, 10.1596, 0.12512248);
    }
    if (tf == "clog2") {
        return clogDecode(cv, 0.24136077, 87.09937546, 0.092864125);
    }
    if (tf == "clog3") {
        double t;
        if (cv < 0.097465473) {
            t = -(std::pow(10., (0.12783901 - cv) / 0.36726845) - 1.) / 14.98325;
        } else if (cv <= 0.15277891) {
            t = (cv - 0.12512219) / 1.9754798;
        } else {
            t = (std::pow(10., (cv - 0.12240537) / 0.36726845) - 1.) / 14.98325;
        }
        return 0.9 * t;
    }
    if (tf == "logc3") {
        return cv > LC3_E * LC3_CUT + LC3_F ? (std::pow(10., (cv - LC3_D) / LC3_C) - LC3_B) / LC3_A : (cv - LC3_F) / LC3_E;
    }
    if (tf == "logc4") {
        const LogC4 &k = logc4();
        return cv >= 0 ? (std::pow(2., 14. * (cv - k.c) / k.b + 6.) - 64.) / k.a : cv * k.s + k.t;
    }
    if (tf == "bmdfilm5") {
        const double A = 0.08692876065491224, B = 0.005494072432257808, C = 0.5300133392291939, D = 8.283605932402494, E = 0.09246575342465753;
        return cv < D * 0.005 + E ? (cv - E) / D : std::exp((cv - C) / A) - B;
    }
    if (tf == "davinci_intermediate") {
        return v <= 0.02740668 ? v / 10.44426855 : std::pow(2., v / 0.07329248 - 7.) - 0.0075;
    }
    if (tf == "log3g10") {
        return cv < 0 ? cv / 15.1927 - 0.01 : (std::pow(10., cv / 0.224282) - 1.) / 155.975327 - 0.01;
    }
    if (tf == "applelog") {
        const double pt = AL_S * (AL_RT - AL_R0) * (AL_RT - AL_R0);
        return v >= pt ? std::pow(2., (v - AL_D) / AL_G) - AL_B : (v >= 0 ? std::sqrt(v / AL_S) + AL_R0 : AL_R0);
    }
    if (tf == "dlog") {
        return cv <= 0.14 ? (cv - 0.0929) / 6.025 : (std::pow(10., 3.89616 * cv - 2.27752) - 0.0108) / 0.9892;
    }
    if (tf == "nlog") {
        const double code = cv * 1023.;
        return code < 452. ? std::pow(code / 650., 3.) - 0.0075 : std::exp((code - 619.) / 150.);
    }
    if (tf == "flog" || tf == "flog2") {
        const FLog &k = tf == "flog" ? FLOG1 : FLOG2;
        return cv >= k.cut2 ? std::pow(10., (cv - k.d) / k.c) / k.a - k.b / k.a : (cv - k.f) / k.e;
    }
    if (tf == "llog") {
        return cv <= 0.138 ? (cv - 0.09) / 8. : (std::pow(10., (cv - 0.6) / 0.27) - 0.0115) / 1.3;
    }
    if (tf == "acescct") {
        return v > 0.155251141552511 ? std::pow(2., v * 17.52 - 9.72) : (v - 0.0729055341958355) / 10.5402377416545;
    }
    if (tf == "gplog2") { // GoPro Labs GP-Log2, normalisé pour que le gris retombe à 0.18
        return (std::pow(600., v) - 1.) / 599. * (0.18 / 0.0517);
    }
    if (tf == "protune") {
        return (std::pow(113., v) - 1.) / 112.;
    }
    return v;
}

double encode(const std::string &tf, double x)
{
    const TransferInfo *info = transferInfo(tf);
    auto out = [info](double cv) { return (info && info->cameraLog) ? cvToVf(cv) : cv; };
    if (tf == "lineaire") {
        return x;
    }
    if (tf == "gamma22") {
        return spow(x, 1. / 2.2);
    }
    if (tf == "gamma24") {
        return spow(x, 1. / 2.4);
    }
    if (tf == "gamma26") {
        return spow(x, 1. / 2.6);
    }
    if (tf == "rec709") {
        return x < 0.018 ? 4.5 * x : 1.099 * spow(x, 0.45) - 0.099;
    }
    if (tf == "srgb") {
        return x <= 0.0031308 ? 12.92 * x : 1.055 * spow(x, 1. / 2.4) - 0.055;
    }
    if (tf == "pq") {
        return pqEncodeNits(x * 100.);
    }
    if (tf == "hlg") {
        return hlgOetf(x);
    }
    if (tf == "slog3") {
        return out(x >= 0.01125 ? (420. + std::log10((x + 0.01) / 0.19) * 261.5) / 1023. : (x * (171.2102946929 - 95.) / 0.01125 + 95.) / 1023.);
    }
    if (tf == "slog2") {
        const double t = x / 0.9 * 155. / 219.;
        const double y = t >= 0 ? 0.432699 * std::log10(t + 0.037584) + 0.646596 : t * 5. + 0.030001222851889303;
        return out((y * 876. + 64.) / 1023.);
    }
    if (tf == "vlog") {
        return out(x < 0.01 ? 5.6 * x + 0.125 : 0.241514 * std::log10(x + 0.00873) + 0.598206);
    }
    if (tf == "clog") {
        return out(clogEncode(x, 0.45310179, 10.1596, 0.12512248));
    }
    if (tf == "clog2") {
        return out(clogEncode(x, 0.24136077, 87.09937546, 0.092864125));
    }
    if (tf == "clog3") {
        const double t = x / 0.9;
        double cv;
        if (t < -0.014) {
            cv = -0.36726845 * std::log10(-14.98325 * t + 1.) + 0.12783901;
        } else if (t <= 0.014) {
            cv = 1.9754798 * t + 0.12512219;
        } else {
            cv = 0.36726845 * std::log10(14.98325 * t + 1.) + 0.12240537;
        }
        return out(cv);
    }
    if (tf == "logc3") {
        return out(x > LC3_CUT ? LC3_C * std::log10(LC3_A * x + LC3_B) + LC3_D : LC3_E * x + LC3_F);
    }
    if (tf == "logc4") {
        const LogC4 &k = logc4();
        return out(x >= k.t ? (std::log2(k.a * x + 64.) - 6.) / 14. * k.b + k.c : (x - k.t) / k.s);
    }
    if (tf == "bmdfilm5") {
        const double A = 0.08692876065491224, B = 0.005494072432257808, C = 0.5300133392291939, D = 8.283605932402494, E = 0.09246575342465753;
        return out(x < 0.005 ? D * x + E : A * std::log(x + B) + C);
    }
    if (tf == "davinci_intermediate") {
        return x <= 0.00262409 ? x * 10.44426855 : (std::log2(x + 0.0075) + 7.) * 0.07329248;
    }
    if (tf == "log3g10") {
        const double xc = x + 0.01;
        return out(xc < 0 ? xc * 15.1927 : 0.224282 * std::log10(xc * 155.975327 + 1.));
    }
    if (tf == "applelog") {
        if (x >= AL_RT) {
            return AL_G * std::log2(x + AL_B) + AL_D;
        }
        return x >= AL_R0 ? AL_S * (x - AL_R0) * (x - AL_R0) : 0.;
    }
    if (tf == "dlog") {
        return out(x <= 0.0078 ? 6.025 * x + 0.0929 : std::log10(x * 0.9892 + 0.0108) * 0.256663 + 0.584555);
    }
    if (tf == "nlog") {
        const double code = x < 0.328 ? 650. * std::cbrt(x + 0.0075) : 150. * std::log(std::max(x, 1e-9)) + 619.;
        return out(code / 1023.);
    }
    if (tf == "flog" || tf == "flog2") {
        const FLog &k = tf == "flog" ? FLOG1 : FLOG2;
        return out(x >= k.cut1 ? k.c * std::log10(k.a * x + k.b) + k.d : k.e * x + k.f);
    }
    if (tf == "llog") {
        return out(x <= 0.006 ? 8. * x + 0.09 : 0.27 * std::log10(1.3 * x + 0.0115) + 0.6);
    }
    if (tf == "acescct") {
        return x <= 0.0078125 ? 10.5402377416545 * x + 0.0729055341958355 : (std::log2(std::max(x, 1e-10)) + 9.72) / 17.52;
    }
    if (tf == "gplog2") {
        const double l = x / (0.18 / 0.0517);
        return std::log(std::max(l * 599. + 1., 1e-10)) / std::log(600.);
    }
    if (tf == "protune") {
        return std::log1p(112. * std::max(x, -0.0089)) / std::log(113.);
    }
    return x;
}

const std::vector<TransferInfo> &transfers()
{
    static const std::vector<TransferInfo> list = {
        {"gamma24", "Rec.709 Gamma 2.4", false},
        {"rec709", "Rec.709 (Scene)", false},
        {"srgb", "sRGB", false},
        {"gamma22", "Gamma 2.2", false},
        {"gamma26", "Gamma 2.6", false},
        {"lineaire", "Linear", false},
        {"hlg", "Rec.2100 HLG", false},
        {"pq", "Rec.2100 ST2084 (PQ)", false},
        {"davinci_intermediate", "DaVinci Intermediate", false},
        {"acescct", "ACEScct", false},
        {"slog3", "Sony S-Log3", true},
        {"slog2", "Sony S-Log2", true},
        {"vlog", "Panasonic V-Log", true},
        {"clog", "Canon Log", true},
        {"clog2", "Canon Log 2", true},
        {"clog3", "Canon Log 3", true},
        {"logc3", "ARRI LogC3", true},
        {"logc4", "ARRI LogC4", true},
        {"bmdfilm5", "Blackmagic Film Gen 5", true},
        {"log3g10", "RED Log3G10", false},
        {"applelog", "Apple Log", false},
        {"dlog", "DJI D-Log", true},
        {"nlog", "Nikon N-Log", true},
        {"flog", "Fujifilm F-Log", true},
        {"flog2", "Fujifilm F-Log2", true},
        {"llog", "Leica L-Log", true},
        {"gplog2", "GoPro GP-Log2", false},
        {"protune", "GoPro Protune Flat", false},
    };
    return list;
}

const TransferInfo *transferInfo(const std::string &id)
{
    for (const TransferInfo &t : transfers()) {
        if (id == t.id) {
            return &t;
        }
    }
    return nullptr;
}

double grayCode(const std::string &tf)
{
    return encode(tf, 0.18);
}

// --- Gamuts -------------------------------------------------------------------------------------

const std::vector<GamutInfo> &gamuts()
{
    static const std::vector<GamutInfo> list = {
        {"rec709", "Rec.709", 0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.3127, 0.3290},
        {"rec2020", "Rec.2020", 0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.3127, 0.3290},
        {"p3d65", "P3-D65", 0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.3127, 0.3290},
        {"dcip3", "DCI-P3", 0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.314, 0.351},
        {"sgamut3", "Sony S-Gamut3", 0.730, 0.280, 0.140, 0.855, 0.100, -0.050, 0.3127, 0.3290},
        {"sgamut3cine", "Sony S-Gamut3.Cine", 0.766, 0.275, 0.225, 0.800, 0.089, -0.087, 0.3127, 0.3290},
        {"vgamut", "Panasonic V-Gamut", 0.730, 0.280, 0.165, 0.840, 0.100, -0.030, 0.3127, 0.3290},
        {"cinemagamut", "Canon Cinema Gamut", 0.740, 0.270, 0.170, 1.140, 0.080, -0.100, 0.3127, 0.3290},
        {"awg3", "ARRI Wide Gamut 3", 0.6840, 0.3130, 0.2210, 0.8480, 0.0861, -0.1020, 0.3127, 0.3290},
        {"awg4", "ARRI Wide Gamut 4", 0.7347, 0.2653, 0.1424, 0.8576, 0.0991, -0.0308, 0.3127, 0.3290},
        {"bmdwg5", "Blackmagic Wide Gamut Gen 5", 0.7177215, 0.3171181, 0.2280410, 0.8615690, 0.1005841, -0.0820452, 0.3127170, 0.3290312},
        {"dwg", "DaVinci Wide Gamut", 0.8000, 0.3130, 0.1682, 0.9877, 0.0790, -0.1155, 0.3127, 0.3290},
        {"rwg", "REDWideGamutRGB", 0.780308, 0.304253, 0.121595, 1.493994, 0.095612, -0.084589, 0.3127, 0.3290},
        {"dgamut", "DJI D-Gamut", 0.71, 0.31, 0.21, 0.88, 0.09, -0.08, 0.3127, 0.3290},
        {"fgamutc", "Fujifilm F-Gamut C", 0.73470, 0.26530, 0.02630, 0.97370, 0.11730, -0.02240, 0.3127, 0.3290},
        {"protune", "GoPro Protune Native", 0.698480461493841, 0.193026445370121, 0.329555378387345, 1.024596624134644, 0.108442631407675, -0.034678569754016, 0.3127, 0.3290},
        {"ap0", "ACES AP0", 0.7347, 0.2653, 0.0, 1.0, 0.0001, -0.077, 0.32168, 0.33767},
        {"ap1", "ACES AP1", 0.713, 0.293, 0.165, 0.830, 0.128, 0.044, 0.32168, 0.33767},
    };
    return list;
}

const GamutInfo *gamutInfo(const std::string &id)
{
    for (const GamutInfo &g : gamuts()) {
        if (id == g.id) {
            return &g;
        }
    }
    return nullptr;
}

namespace {
Vec3 xyzOf(double x, double y)
{
    return {x / y, 1., (1. - x - y) / y};
}
} // namespace

Mat3 rgbToXyz(const GamutInfo &g)
{
    const Vec3 r = xyzOf(g.rx, g.ry), gg = xyzOf(g.gx, g.gy), b = xyzOf(g.bx, g.by);
    const Mat3 p{r[0], gg[0], b[0], r[1], gg[1], b[1], r[2], gg[2], b[2]};
    const Vec3 s = mul(inverse(p), xyzOf(g.wx, g.wy));
    return {p[0] * s[0], p[1] * s[1], p[2] * s[2], p[3] * s[0], p[4] * s[1], p[5] * s[2], p[6] * s[0], p[7] * s[1], p[8] * s[2]};
}

Mat3 gamutMatrix(const std::string &from, const std::string &to, bool adaptWhite)
{
    const GamutInfo *a = gamutInfo(from);
    const GamutInfo *b = gamutInfo(to);
    if (!a || !b || from == to) {
        return identity3();
    }
    Mat3 m = rgbToXyz(*a);
    if (adaptWhite && (std::abs(a->wx - b->wx) > 1e-6 || std::abs(a->wy - b->wy) > 1e-6)) {
        // CAT02, celle des IDT ARRI, Sony et Canon.
        const Mat3 cat{0.7328, 0.4296, -0.1624, -0.7036, 1.6975, 0.0061, 0.0030, 0.0136, 0.9834};
        const Vec3 ws = mul(cat, xyzOf(a->wx, a->wy));
        const Vec3 wd = mul(cat, xyzOf(b->wx, b->wy));
        const Mat3 d{wd[0] / ws[0], 0, 0, 0, wd[1] / ws[1], 0, 0, 0, wd[2] / ws[2]};
        m = mul(mul(inverse(cat), mul(d, cat)), m);
    }
    return mul(inverse(rgbToXyz(*b)), m);
}

// --- CST ------------------------------------------------------------------------------------------

double toneMapDavinci(double x, double wi, double wo, double adaptation)
{
    if (wi <= wo) {
        return x;
    }
    const double ad = adaptation / 100.;
    const double b = (wi - ad * (wi / wo)) / ((wi / wo) - 1.);
    const double a = wo * (wi + b) / wi;
    // Sous zéro, on garde la pente à l'origine pour ne pas casser les noirs négatifs.
    if (x <= 0) {
        return x * a / b;
    }
    x = std::min(x, wi);
    return std::min(a * x / (x + b), wo);
}

Vec3 acesGamutCompress(const Vec3 &rgb)
{
    const double lim[3] = {1.147, 1.264, 1.312};
    const double thr[3] = {0.815, 0.803, 0.880};
    const double pwr = 1.2;
    const double ach = std::max({rgb[0], rgb[1], rgb[2]});
    if (ach <= 0) {
        return rgb;
    }
    Vec3 out;
    for (int i = 0; i < 3; ++i) {
        const double d = (ach - rgb[i]) / std::abs(ach);
        double nd = d;
        if (d >= thr[i]) {
            const double scl = (lim[i] - thr[i]) / std::pow(std::pow((1. - thr[i]) / (lim[i] - thr[i]), -pwr) - 1., 1. / pwr);
            const double n = (d - thr[i]) / scl;
            nd = thr[i] + scl * n / std::pow(1. + std::pow(n, pwr), 1. / pwr);
        }
        out[i] = ach - nd * std::abs(ach);
    }
    return out;
}

Cst::Cst(const CstParams &p)
    : m_p(p)
{
    m_matrix = gamutMatrix(p.inGamut, p.outGamut, p.adaptWhite);
    m_identity = p.inGamut == p.outGamut && p.inTf == p.outTf;
    m_hlgIn = p.inTf == "hlg";
    m_pqIn = p.inTf == "pq";
    m_hlgOut = p.outTf == "hlg";
    m_pqOut = p.outTf == "pq";
    const TransferInfo *ti = transferInfo(p.inTf);
    m_sceneIn = (ti && ti->cameraLog) || p.inTf == "applelog" || p.inTf == "davinci_intermediate" || p.inTf == "acescct" || p.inTf == "lineaire" ||
                p.inTf == "gplog2" || p.inTf == "protune";
    // Luminances en « 1.0 = 100 nits ».
    double maxIn = p.maxInNits;
    if (maxIn <= 0) {
        if (m_hlgIn || m_pqIn) {
            maxIn = 1000.;
        } else if (m_sceneIn) {
            maxIn = std::max(100., decode(p.inTf, 1.) * 100.);
        } else {
            maxIn = 100.;
        }
    }
    m_wi = maxIn / 100.;
    m_wo = (m_pqOut || m_hlgOut) ? std::max(p.maxOutNits, 1000.) / 100. : p.maxOutNits / 100.;
    m_tone = p.toneMapping != "aucun" && m_wi > m_wo * 1.0001;
}

Vec3 Cst::apply(const Vec3 &in) const
{
    if (m_identity) {
        return in;
    }
    Vec3 lin{decode(m_p.inTf, in[0]), decode(m_p.inTf, in[1]), decode(m_p.inTf, in[2])};
    if (m_hlgIn && m_p.forwardOotf) {
        // OOTF BT.2100 à 1000 nits : gamma système 1.2, sur la luma Rec.2020.
        const double ys = 0.2627 * lin[0] + 0.6780 * lin[1] + 0.0593 * lin[2];
        const double k = 10. * std::pow(std::max(ys, 1e-9), 0.2);
        lin = {lin[0] * k, lin[1] * k, lin[2] * k};
    }
    Vec3 v = mul(m_matrix, lin);
    if (m_tone) {
        if (m_p.toneMapping == "simple") {
            for (double &c : v) {
                c = c <= 0 ? c : m_wo * (1. - std::exp(-c / m_wo * (1. + m_wo / m_wi)));
            }
        } else if (m_p.toneMapping == "luminance") {
            // Même courbe que DaVinci, appliquée à la luma puis reportée sur les trois canaux.
            const double y = luma709(v[0], v[1], v[2]);
            if (y > 0) {
                const double k = toneMapDavinci(y, m_wi, m_wo, m_p.adaptation) / y;
                v = {v[0] * k, v[1] * k, v[2] * k};
            }
        } else {
            for (double &c : v) {
                c = toneMapDavinci(c, m_wi, m_wo, m_p.adaptation);
            }
        }
    }
    if (m_p.gamutMapping == "compression") {
        v = acesGamutCompress(v);
    } else if (m_p.gamutMapping == "ecreter") {
        for (double &c : v) {
            c = std::max(c, 0.);
        }
    }
    if (m_hlgOut && m_p.inverseOotf) {
        const double yd = 0.2627 * v[0] + 0.6780 * v[1] + 0.0593 * v[2];
        const double k = yd > 0 ? std::pow(yd / 10., (1. / 1.2) - 1.) / 10. : 0.;
        v = {v[0] * k, v[1] * k, v[2] * k};
    }
    return {encode(m_p.outTf, v[0]), encode(m_p.outTf, v[1]), encode(m_p.outTf, v[2])};
}

} // namespace kcgrade
