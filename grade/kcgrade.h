/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Le moteur d'étalonnage de KoraCut (document 22) : un graphe de nœuds comme la page Color de
// DaVinci Resolve, calculé en virgule flottante. Sans Qt : le même code tourne dans l'application
// (aperçus, scopes, outils des agents) et dans le module MLT `koracut_grade` (lecture, rendu, melt).
//
// Le graphe est un JSON (format 1) :
//   { "format": 1, "actif": true, "sortie": <id>, "noeuds": [ { "id": 1, "type": "correcteur", ... } ] }
// Types de nœuds : correcteur, parallele, calque, separateur, combineur, melangeur_cles.
// Un nœud « outside » est un correcteur dont l'entrée de clé est inversée (cle.entree_inverser).
// L'entrée 0 est la source (l'image du plan). Détail des champs : kcgrade.cpp, parseNode().

#pragma once

#include "kccolor.h"
#include "kcjson.h"
#include "kclut.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kcgrade {

// Exécute jobs tâches (index 0..jobs-1), en parallèle si possible. Le module MLT passe mlt_slices.
using Runner = std::function<void(int jobs, const std::function<void(int)> &job)>;
Runner threadRunner(int threads);

class GradePrivate;

class Grade
{
public:
    // Compile un graphe. En cas d'erreur, rend nullptr et remplit error (en français, pour les agents).
    static std::shared_ptr<const Grade> compile(const std::string &json, std::string *error = nullptr);
    ~Grade();

    // Le graphe ne change rien (aucun réglage, ou étalonnage désactivé).
    bool isIdentity() const;
    // Toutes les opérations ne dépendent que de la couleur du pixel : le grade entier tient dans une LUT.
    bool isPerPixel() const;
    // Le grade appliqué à une couleur (les opérations spatiales sont ignorées). Entrée et sortie 0..1
    // (la sortie peut dépasser : c'est le flottant de Resolve, écrêté seulement à la fin).
    Vec3 evalColor(const Vec3 &rgb) const;
    // La LUT qui porte tout le grade (cuite à la compilation) quand isPerPixel().
    const Lut3D &lut() const;

    // Applique le grade à une image 8 bits RVBA (alpha intact), sur place.
    void processRgba8(uint8_t *data, int width, int height, int stride, const Runner &run) const;
    // Applique le grade à une image 16 bits RVBA (mlt_image_rgba64), sur place.
    void processRgba16(uint16_t *data, int width, int height, int stride, const Runner &run) const;
    // Chemin général, en flottants RVB (3 par pixel), sur place.
    void processFloat(float *rgb, int width, int height, const Runner &run) const;

    // Exporte le grade en LUT .cube (comme « Generate LUT » de Resolve), s'il ne dépend que de la couleur.
    bool exportCube(const std::string &path, int size, std::string *error = nullptr) const;

private:
    Grade();
    std::unique_ptr<GradePrivate> d;
};

// Un graphe neuf : un nœud correcteur « 01 » relié à la source et à la sortie.
std::string defaultGraph();

// Chemins où chercher une LUT donnée par son nom seul (le moteur ne connaît pas Qt).
void setLutSearchPaths(const std::vector<std::string> &paths);

} // namespace kcgrade
