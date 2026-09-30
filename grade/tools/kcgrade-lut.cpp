/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/
// kcgrade-lut <graphe.json> <sortie.cube> [taille] : exporte le grade en LUT (vérification contre Resolve).
#include "../kcgrade.h"
#include <fstream>
#include <iostream>
#include <sstream>
int main(int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage : kcgrade-lut graphe.json sortie.cube [taille]\n";
        return 2;
    }
    std::ifstream f(argv[1]);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    auto g = kcgrade::Grade::compile(ss.str(), &err);
    if (!g) {
        std::cerr << "erreur : " << err << "\n";
        return 1;
    }
    // Sans écrêtage : on écrit la fonction brute (comme la LUT de Resolve, écrêtée ensuite par la comparaison).
    kcgrade::Lut3D lut(argc > 3 ? std::atoi(argv[3]) : 33);
    lut.bake([&](const kcgrade::Vec3 &c) { return g->evalColor(c); }, 4);
    return lut.saveCube(argv[2], "kcgrade") ? 0 : 1;
}
