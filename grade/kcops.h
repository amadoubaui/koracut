/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Les opérations d'un nœud correcteur, par pixel, dans l'ordre de Resolve (manuel 21, ch. 144) :
// CST → Color Boost → Temp/Tint → Lift/Gamma/Gain/Offset (YRGB) → Contraste → Courbes → Roues log
// → Teinte/Saturation → LUT → Courbes de teinte. Les formules sont celles mesurées sur Resolve 21
// (document 22, section « Mesures »). Toutes les valeurs sont celles AFFICHÉES par Resolve
// (Lift 0.00, Gamma 0.00, Gain 1.00, Offset 25.00, Saturation 50, Teinte 50, Lum Mix 100…).

#pragma once

#include "kccolor.h"
#include "kcjson.h"
#include "kclut.h"

#include <memory>
#include <vector>

namespace kcgrade {

// Une courbe de points (x, y) : spline cubique monotone, ou périodique pour les courbes de teinte.
class Curve
{
public:
    Curve() = default;
    Curve(std::vector<std::pair<double, double>> pts, bool periodic, double neutral);
    bool isIdentity() const { return m_identity; }
    double eval(double x) const;

private:
    std::vector<double> m_x, m_y, m_m;
    bool m_periodic = false;
    bool m_identity = true;
    double m_neutral = 0.;
};

// Contraste de Resolve (linéaire jusqu'à 1, courbe en S mesurée au-dessus).
double resolveContrast(double v, double contrast, double pivot);
// Gains von Kries de Temp et Tint (grille mesurée), matrice RVB 3x3 finale.
Mat3 tempTintMatrix(double temp, double tint);

struct Primaries
{
    // Valeurs affichées par Resolve.
    double lift[4] = {0, 0, 0, 0};   // Y R V B
    double gamma[4] = {0, 0, 0, 0};
    double gain[4] = {1, 1, 1, 1};
    double offset[3] = {25, 25, 25}; // R V B
    double contrast = 1., pivot = 0.435;
    double saturation = 50., hue = 50., lumMix = 100.;
    double temp = 0., tint = 0., boost = 0.;
    double midDetail = 0., shadows = 0., highlights = 0.; // spatiaux
    bool read(const Json &j);
    bool isIdentity() const;
    bool hasSpatial() const { return midDetail != 0. || shadows != 0. || highlights != 0.; }
};

struct LogWheels
{
    double shadow[3] = {0, 0, 0}, mid[3] = {0, 0, 0}, high[3] = {0, 0, 0};
    double lowRange = 0.333, highRange = 0.550;
    bool read(const Json &j);
    bool isIdentity() const;
};

struct Curves
{
    Curve y, r, g, b;
    double intensity[4] = {100, 100, 100, 100};
    bool read(const Json &j);
    bool isIdentity() const { return y.isIdentity() && r.isIdentity() && g.isIdentity() && b.isIdentity(); }
};

struct HueCurves
{
    Curve hueHue, hueSat, hueLum, lumSat, satSat, satLum;
    bool read(const Json &j);
    bool isIdentity() const;
};

// Tout ce qu'un nœud correcteur fait à une couleur (sans clé).
class NodeColor
{
public:
    bool read(const Json &node, std::string *error);
    bool isIdentity() const { return m_identity; }
    bool usesLog() const { return m_cst && (m_cstLog); }
    Vec3 apply(const Vec3 &in) const;
    const Primaries &primaries() const { return m_prim; }

private:
    Primaries m_prim;
    LogWheels m_log;
    Curves m_curves;
    HueCurves m_hue;
    std::unique_ptr<Cst> m_cst;
    bool m_cstLog = false;
    std::shared_ptr<const Lut3D> m_lut;
    double m_lutMix = 1.;
    bool m_identity = true;
    // Constantes précalculées
    Mat3 m_tt = identity3();
    bool m_hasTT = false, m_hasPrim = false, m_hasLog = false, m_hasHS = false;
    double m_cosH = 1., m_sinH = 0., m_sat = 1.;
};

// La clé d'un nœud : qualificateur HSL / RVB / luma, par pixel.
struct Qualifier
{
    bool active = false;
    std::string mode = "hsl";
    bool hueOn = true, satOn = true, lumOn = true;
    double hueCenter = 0., hueWidth = 20., hueSoft = 5., hueSym = 50.;
    double satLow = 0., satHigh = 100., satLowSoft = 0., satHighSoft = 0.;
    double lumLow = 0., lumHigh = 100., lumLowSoft = 0., lumHighSoft = 0.;
    double rgbLow[3] = {0, 0, 0}, rgbHigh[3] = {100, 100, 100}, rgbSoft = 5.;
    double blackClip = 0., whiteClip = 100.;
    double blur = 0.; // rayon Resolve 0..100 (spatial)
    bool invert = false;
    bool read(const Json &j);
    double key(const Vec3 &rgb) const;
};

// Une fenêtre (Power Window) : coordonnées normalisées 0..1, origine en haut à gauche.
struct Window
{
    std::string type = "cercle"; // cercle, rectangle, polygone, degrade
    double x = 0.5, y = 0.5, size = 0.25, aspect = 1., rotation = 0., soft = 0.05, opacity = 1.;
    bool invert = false, subtract = false;
    std::vector<std::pair<double, double>> points;
    bool read(const Json &j);
    double key(double u, double v, double imageAspect) const;
};

} // namespace kcgrade
