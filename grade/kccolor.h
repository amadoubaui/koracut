/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Espaces colorimétriques du moteur d'étalonnage : courbes (gammas et log des caméras), gamuts,
// matrices, et le Color Space Transform (CST) comme celui de DaVinci Resolve.
// Toutes les formules viennent des livres blancs des constructeurs (liste et contrôles dans
// Recherche/DaVinci Resolve - page Color.md, section 12).

#pragma once

#include <array>
#include <string>
#include <vector>

namespace kcgrade {

using Vec3 = std::array<double, 3>;
using Mat3 = std::array<double, 9>; // rangée par rangée

Vec3 mul(const Mat3 &m, const Vec3 &v);
Mat3 mul(const Mat3 &a, const Mat3 &b);
Mat3 inverse(const Mat3 &m);
Mat3 identity3();

// Luma Rec.709, celle des scopes et de Resolve.
inline double luma709(double r, double g, double b)
{
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

// --- Courbes de transfert ---------------------------------------------------------------------
// Identifiants stables (enregistrés dans le projet, donnés aux agents).
struct TransferInfo
{
    const char *id;
    const char *label;  // nom affiché (anglais, traduit par l'interface)
    bool cameraLog;     // formule du constructeur en « code value » 10 bits : l'entrée vidéo est convertie
};
const std::vector<TransferInfo> &transfers();
const TransferInfo *transferInfo(const std::string &id);
// Valeur du gris 18 % dans le domaine de l'image (0..1 telle que MLT la donne).
double grayCode(const std::string &tf);

// Décodage : valeur de l'image (0..1 telle que MLT la donne) vers linéaire scène (0.18 = gris).
// Pour un gamma d'affichage (rec709, gamma24…), le linéaire est la lumière de l'écran relative (1 = blanc).
double decode(const std::string &tf, double v);
double encode(const std::string &tf, double lin);

// --- Gamuts -------------------------------------------------------------------------------------
struct GamutInfo
{
    const char *id;
    const char *label;
    double rx, ry, gx, gy, bx, by, wx, wy;
};
const std::vector<GamutInfo> &gamuts();
const GamutInfo *gamutInfo(const std::string &id);
Mat3 rgbToXyz(const GamutInfo &g);
// Matrice d'un gamut vers un autre ; adapte le blanc (CAT02) quand les deux blancs diffèrent.
Mat3 gamutMatrix(const std::string &from, const std::string &to, bool adaptWhite = true);

// --- Color Space Transform --------------------------------------------------------------------
struct CstParams
{
    std::string inGamut = "rec709";
    std::string inTf = "gamma24";
    std::string outGamut = "rec709";
    std::string outTf = "gamma24";
    std::string toneMapping = "davinci"; // aucun, simple, davinci, luminance
    double maxInNits = 0.;               // 0 = automatique d'après la courbe d'entrée
    double maxOutNits = 100.;
    double adaptation = 9.;
    std::string gamutMapping = "compression"; // aucun, ecreter, compression
    bool forwardOotf = false;                 // entrée HLG : appliquer l'OOTF (la lumière de l'écran)
    bool inverseOotf = false;
    bool adaptWhite = true;
};

class Cst
{
public:
    explicit Cst(const CstParams &p);
    Vec3 apply(const Vec3 &rgb) const;
    bool isIdentity() const { return m_identity; }

private:
    CstParams m_p;
    Mat3 m_matrix;
    bool m_identity = false;
    double m_wi = 1., m_wo = 1.;
    bool m_tone = false;
    bool m_hlgIn = false, m_pqIn = false, m_sceneIn = false, m_hlgOut = false, m_pqOut = false;
};

// Tone mapping DaVinci (reconstruction publique, Adaptation 9), sur 1.0 = 100 nits, par canal.
double toneMapDavinci(double x, double wi, double wo, double adaptation);
// Compression de gamut ACES (aces-dev 1.3.1, Reference Gamut Compression), en AP1 linéaire.
Vec3 acesGamutCompress(const Vec3 &ap1);

} // namespace kcgrade
