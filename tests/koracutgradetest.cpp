/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/
// Document 22 : le moteur d'étalonnage. Les valeurs attendues sont lues dans des LUT exportées de
// DaVinci Resolve Studio 21.0.3 le 28 septembre 2026 (grade écrit en DRX, LUT 33 points). Une couleur
// d'entrée (r, g, b) correspond au point (r/32, g/32, b/32) de la LUT.

#include "catch.hpp"

#include "kccolor.h"
#include "kcgrade.h"
#include "kcjson.h"
#include "kclut.h"

#include <mlt++/Mlt.h>

#include <QDir>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <chrono>
#include <cmath>

using namespace kcgrade;

namespace {

std::string oneNode(const std::string &body)
{
    return R"({"format":1,"sortie":1,"noeuds":[{"id":1,"type":"correcteur","entree":{"rvb":0},)" + body + "}]}";
}

Vec3 at(int r, int g, int b)
{
    return {r / 32., g / 32., b / 32.};
}

// Compare au pixel près (la LUT de Resolve est écrêtée à 0..1).
bool near(const Vec3 &got, const Vec3 &want, double tol)
{
    for (int i = 0; i < 3; ++i) {
        const double g = std::clamp(got[size_t(i)], 0., 1.);
        if (std::abs(g - want[size_t(i)]) > tol) {
            WARN("obtenu " << got[0] << " " << got[1] << " " << got[2] << " attendu " << want[0] << " " << want[1] << " " << want[2]);
            return false;
        }
    }
    return true;
}

Vec3 grade(const std::string &json, const Vec3 &in)
{
    std::string err;
    auto g = Grade::compile(json, &err);
    INFO(err);
    REQUIRE(g);
    return g->evalColor(in);
}

} // namespace

TEST_CASE("Étalonnage : JSON", "[KoraCutGrade]")
{
    bool ok = false;
    Json j = Json::parse(R"({"a":[1,2.5,-3e-2],"b":"é\n\"x\"","c":true,"d":null})", &ok);
    REQUIRE(ok);
    CHECK(j.get("a").at(1).toNumber() == 2.5);
    CHECK(j.get("b").toString() == "é\n\"x\"");
    CHECK(Json::parse(j.dump(), &ok).dump() == j.dump());
    Json::parse("{\"a\":", &ok);
    CHECK_FALSE(ok);
}

TEST_CASE("Étalonnage : primaires identiques à Resolve", "[KoraCutGrade]")
{
    const double tol = 3e-4;
    SECTION("Lift Y affiché 0,10 (Lum Mix 100)")
    {
        const std::string j = oneNode(R"("primaires":{"lift":[0.1,0,0,0]})");
        CHECK(near(grade(j, at(16, 16, 16)), {0.6, 0.6, 0.6}, tol));
        CHECK(near(grade(j, at(6, 16, 26)), {0.29627, 0.60877, 0.92128}, tol));
        CHECK(near(grade(j, at(30, 10, 3)), {1.0, 0.42658, 0.20783}, tol));
    }
    SECTION("Gain rouge 1,30 : la luma est gardée (Lum Mix 100)")
    {
        const std::string j = oneNode(R"("primaires":{"gain":[1,1.3,1,1]})");
        CHECK(near(grade(j, at(16, 16, 16)), {0.6181, 0.4681, 0.4681}, tol));
        CHECK(near(grade(j, at(6, 16, 26)), {0.23177, 0.48803, 0.80053}, tol));
    }
    SECTION("Gamma rouge 0,10 avec Lum Mix 0")
    {
        const std::string j = oneNode(R"("primaires":{"gamma":[0,0.1,0,0],"lum_mix":0})");
        CHECK(near(grade(j, at(16, 16, 16)), {0.60951, 0.49999, 0.49999}, tol));
        CHECK(near(grade(j, at(30, 10, 3)), {0.95494, 0.31249, 0.09374}, tol));
    }
    SECTION("Lift, Gamma, Gain Y et Offset 30 : l'offset passe avant la puissance du gamma")
    {
        const std::string j = oneNode(R"("primaires":{"lift":[0.1,0,0,0],"gamma":[0.1,0,0,0],"gain":[0.8,1,1,1],"offset":[30,30,30]})");
        CHECK(near(grade(j, at(16, 16, 16)), {0.64109, 0.64109, 0.64109}, tol));
        CHECK(near(grade(j, at(20, 28, 12)), {0.63571, 0.88571, 0.3857}, tol));
    }
    SECTION("Contraste 1,5 (courbe en S mesurée)")
    {
        const std::string j = oneNode(R"("primaires":{"contraste":1.5})");
        CHECK(near(grade(j, at(16, 16, 16)), {0.57133, 0.57133, 0.57133}, 2e-3));
        CHECK(near(grade(j, at(24, 24, 24)), {0.90248, 0.90248, 0.90248}, 2e-3));
        CHECK(near(grade(j, at(6, 16, 26)), {0.05794, 0.55744, 0.91899}, 2e-3));
    }
    SECTION("Saturation 75 et Teinte 62,5")
    {
        CHECK(near(grade(oneNode(R"("primaires":{"saturation":75})"), at(6, 16, 26)), {0.04999, 0.51875, 0.9875}, tol));
        CHECK(near(grade(oneNode(R"("primaires":{"teinte":62.5})"), at(6, 16, 26)), {0.01613, 0.58885, 0.52129}, tol));
        CHECK(near(grade(oneNode(R"("primaires":{"teinte":62.5})"), at(30, 10, 3)), {1.0, 0.22869, 0.54414}, tol));
    }
    SECTION("Temp 1000 et Tint 30")
    {
        CHECK(near(grade(oneNode(R"("primaires":{"temp":1000})"), at(16, 16, 16)), {0.5501, 0.49355, 0.41657}, tol));
        CHECK(near(grade(oneNode(R"("primaires":{"temp":1000})"), at(6, 16, 26)), {0.22881, 0.49528, 0.6835}, tol));
        CHECK(near(grade(oneNode(R"("primaires":{"tint":30})"), at(20, 28, 12)), {0.6573, 0.86224, 0.38895}, tol));
    }
    SECTION("Color Boost 50")
    {
        CHECK(near(grade(oneNode(R"("primaires":{"boost":50})"), at(6, 16, 26)), {0.1089, 0.5122, 0.91551}, 2e-3));
        CHECK(near(grade(oneNode(R"("primaires":{"boost":50})"), at(20, 28, 12)), {0.57133, 0.90437, 0.23828}, 2e-3));
    }
    SECTION("Grade combiné (Lift, Gamma, Gain par canal, Offset bleu, Contraste 1,2, Lum Mix 70)")
    {
        const std::string j = oneNode(R"("primaires":{"lift":[0.025,0.015,0,0],"gamma":[0.025,0,0,0],"gain":[1,1,1.05,1],"offset":[25,25,27.5],"contraste":1.2,"lum_mix":70})");
        CHECK(near(grade(j, at(16, 16, 16)), {0.56802, 0.58083, 0.57227}, 1e-3));
        CHECK(near(grade(j, at(6, 16, 26)), {0.20224, 0.58283, 0.9059}, 1e-3));
        CHECK(near(grade(j, at(20, 28, 12)), {0.69609, 0.94873, 0.39132}, 1e-3));
    }
}

TEST_CASE("Étalonnage : nœuds parallèles et calques comme Resolve", "[KoraCutGrade]")
{
    // Mesuré : deux nœuds en parallèle (offset CDL +0,1 rouge et +0,1 vert) donnent la somme des écarts.
    const std::string par = R"({"format":1,"sortie":3,"noeuds":[
        {"id":1,"type":"correcteur","entree":{"rvb":0},"primaires":{"offset":[25,25,25],"lift":[0,0,0,0],"gain":[1,1,1,1]},"courbes":{"r":[[0,0.1],[1,1.1]]}},
        {"id":2,"type":"correcteur","entree":{"rvb":0},"courbes":{"v":[[0,0.1],[1,1.1]]}},
        {"id":3,"type":"parallele","entrees":[1,2]}]})";
    CHECK(near(grade(par, at(8, 8, 8)), {0.35, 0.35, 0.25}, 1e-6));
    // Calque : l'entrée du bas recouvre celle du dessus.
    std::string lay = par;
    lay.replace(lay.find("\"parallele\""), 11, "\"calque\"");
    CHECK(near(grade(lay, at(8, 8, 8)), {0.25, 0.35, 0.25}, 1e-6));
    // Un nœud désactivé laisse passer l'image.
    const std::string off = oneNode(R"("actif":false,"primaires":{"gain":[2,2,2,2]})");
    CHECK(near(grade(off, at(8, 8, 8)), {0.25, 0.25, 0.25}, 1e-9));
    // Tout le grade désactivé (Shift-D de Resolve).
    std::string bypass = oneNode(R"("primaires":{"gain":[2,2,2,2]})");
    bypass.insert(1, R"("actif":false,)");
    auto g = Grade::compile(bypass);
    REQUIRE(g);
    CHECK(g->isIdentity());
}

TEST_CASE("Étalonnage : clés, qualificateur et outside", "[KoraCutGrade]")
{
    // Un qualificateur sur les rouges ne touche qu'eux.
    const std::string q = oneNode(R"("primaires":{"saturation":0},"qualificateur":{"mode":"hsl","teinte":{"centre":16.7,"largeur":10,"doux":2},"sat":{"bas":30,"haut":100},"lum":{"bas":0,"haut":100}})");
    const Vec3 red = grade(q, {0.9, 0.1, 0.1});
    const Vec3 blue = grade(q, {0.1, 0.1, 0.9});
    CHECK(std::abs(red[0] - red[1]) < 1e-3); // désaturé
    CHECK(near(blue, {0.1, 0.1, 0.9}, 1e-9)); // intact
    // Outside : le nœud 2 reçoit la clé du nœud 1 inversée et corrige tout le reste.
    const std::string outside = R"({"format":1,"sortie":2,"noeuds":[
        {"id":1,"type":"correcteur","entree":{"rvb":0},"qualificateur":{"mode":"hsl","teinte":{"centre":16.7,"largeur":10,"doux":2},"sat":{"bas":30,"haut":100}}},
        {"id":2,"type":"correcteur","entree":{"rvb":1,"cle":1},"cle":{"entree_inverser":true},"primaires":{"saturation":0}}]})";
    CHECK(near(grade(outside, {0.9, 0.1, 0.1}), {0.9, 0.1, 0.1}, 1e-6));
    const Vec3 b = grade(outside, {0.1, 0.1, 0.9});
    CHECK(std::abs(b[0] - b[2]) < 1e-3);
    // Gain de sortie de clé à 0 : le nœud n'a plus d'effet.
    CHECK(near(grade(oneNode(R"("primaires":{"gain":[2,2,2,2]},"cle":{"sortie_gain":0})"), at(8, 8, 8)), {0.25, 0.25, 0.25}, 1e-9));
}

TEST_CASE("Étalonnage : courbes log des caméras", "[KoraCutGrade]")
{
    // Gris 18 % en code 10 bits (livres blancs) ; l'image arrive en niveaux vidéo étendus.
    auto code = [](const std::string &tf) { return (grayCode(tf) * 876. + 64.); };
    CHECK(std::abs(code("slog3") - 420.) < 0.5);
    CHECK(std::abs(code("vlog") - 433.) < 0.5);
    CHECK(std::abs(code("logc3") - 400.) < 0.5);
    CHECK(std::abs(code("clog3") - 351.3) < 0.5);
    CHECK(std::abs(code("clog2") - 407.4) < 0.5);
    CHECK(std::abs(code("dlog") - 408.) < 1.);
    CHECK(std::abs(code("flog2") - 400.) < 0.5);
    CHECK(std::abs(code("nlog") - 372.) < 0.5);
    CHECK(std::abs(grayCode("applelog") - 0.488272) < 1e-5);
    CHECK(std::abs(grayCode("davinci_intermediate") - 0.336043) < 1e-5);
    CHECK(std::abs(grayCode("log3g10") - 1. / 3.) < 1e-5);
    for (const TransferInfo &t : transfers()) {
        for (double x : {0.0, 0.01, 0.18, 0.9, 2.0}) {
            const double back = decode(t.id, encode(t.id, x));
            INFO(t.id << " " << x);
            CHECK(std::abs(back - x) < 1e-6 * std::max(1., x));
        }
    }
    // Matrice ARRI Wide Gamut 3 vers Rec.709 (document ARRI).
    const Mat3 m = gamutMatrix("awg3", "rec709");
    CHECK(std::abs(m[0] - 1.617523) < 1e-4);
    CHECK(std::abs(m[4] - 1.334613) < 1e-4);
    CHECK(std::abs(m[8] - 1.248056) < 1e-4);
    // Un CST S-Log3 vers Rec.709 : le gris sort en gris, sous le blanc, et l'image garde son ordre.
    const std::string cst = oneNode(R"("cst":{"entree_gamut":"sgamut3cine","entree_gamma":"slog3","sortie_gamut":"rec709","sortie_gamma":"gamma24"})");
    const double g = grayCode("slog3");
    const Vec3 o = grade(cst, {g, g, g});
    CHECK(std::abs(o[0] - o[1]) < 1e-3);
    CHECK(o[0] > 0.35);
    CHECK(o[0] < 0.6);
    const Vec3 dark = grade(cst, {g * 0.7, g * 0.7, g * 0.7});
    CHECK(dark[0] < o[0]);
}

TEST_CASE("Étalonnage : LUT et image", "[KoraCutGrade]")
{
    QTemporaryDir dir;
    // Un grade qui ne dépend que de la couleur tient dans une LUT ; exportée puis relue, elle rend le même résultat.
    const std::string j = oneNode(R"("primaires":{"lift":[0.05,0,0,0],"gain":[1.1,1,1,1],"saturation":60,"temp":500})");
    auto g = Grade::compile(j);
    REQUIRE(g);
    CHECK(g->isPerPixel());
    const std::string path = dir.filePath(QStringLiteral("grade.cube")).toStdString();
    REQUIRE(g->exportCube(path, 33));
    const Lut3D lut = Lut3D::loadCube(path);
    REQUIRE(lut.isValid());
    for (const Vec3 &c : {at(3, 9, 27), at(16, 16, 16), at(30, 20, 5)}) {
        const Vec3 a = lut.apply(c), b = g->evalColor(c);
        CHECK(near(a, {std::clamp(b[0], 0., 1.), std::clamp(b[1], 0., 1.), std::clamp(b[2], 0., 1.)}, 2e-3));
    }
    // Image 8 bits : la LUT cuite rend la même chose que le calcul exact, à un code près.
    std::vector<uint8_t> img(64 * 36 * 4);
    for (size_t i = 0; i < img.size(); ++i) {
        img[i] = uint8_t((i * 37) % 256);
    }
    std::vector<uint8_t> ref = img;
    g->processRgba8(img.data(), 64, 36, 64 * 4, threadRunner(4));
    int worst = 0;
    for (size_t i = 0; i < ref.size(); i += 4) {
        const Vec3 e = g->evalColor({ref[i] / 255., ref[i + 1] / 255., ref[i + 2] / 255.});
        for (int c = 0; c < 3; ++c) {
            worst = std::max(worst, std::abs(int(img[i + size_t(c)]) - int(std::clamp(e[size_t(c)], 0., 1.) * 255. + 0.5)));
        }
        CHECK(img[i + 3] == ref[i + 3]); // alpha intact
    }
    CHECK(worst <= 1);
    // Une fenêtre rend le grade spatial : seul le centre change.
    const std::string w = oneNode(R"("primaires":{"gain":[0,0,0,0]},"fenetres":[{"type":"cercle","x":0.5,"y":0.5,"taille":0.2,"doux":0}])");
    auto gw = Grade::compile(w);
    REQUIRE(gw);
    CHECK_FALSE(gw->isPerPixel());
    std::vector<uint8_t> px(100 * 100 * 4, 200);
    gw->processRgba8(px.data(), 100, 100, 400, threadRunner(2));
    CHECK(px[(50 * 100 + 50) * 4] == 0);  // centre noirci
    CHECK(px[(5 * 100 + 5) * 4] == 200);  // coin intact
}

TEST_CASE("Étalonnage : erreurs lisibles", "[KoraCutGrade]")
{
    std::string err;
    CHECK_FALSE(Grade::compile("{", &err));
    CHECK_FALSE(err.empty());
    CHECK_FALSE(Grade::compile(R"({"format":1,"sortie":2,"noeuds":[{"id":1,"type":"correcteur"}]})", &err));
    CHECK(err.find("absent") != std::string::npos);
    CHECK_FALSE(Grade::compile(R"({"format":1,"sortie":1,"noeuds":[{"id":1,"type":"correcteur","entree":{"rvb":2}},{"id":2,"type":"correcteur","entree":{"rvb":1}}]})", &err));
    CHECK(err.find("boucle") != std::string::npos);
    CHECK_FALSE(Grade::compile(oneNode(R"("cst":{"entree_gamma":"slog9"})"), &err));
    CHECK(Grade::compile(defaultGraph()));
    CHECK(Grade::compile(defaultGraph())->isIdentity());
}

TEST_CASE("Étalonnage : légèreté", "[KoraCutGrade]")
{
    // Une image 1080p étalonnée (grade par pixel) : quelques millisecondes, la LUT n'est cuite qu'une fois.
    const std::string j = oneNode(R"("primaires":{"lift":[0.02,0,0,0],"gamma":[0.03,0,0,0],"gain":[1.05,1,1,1],"contraste":1.2,"saturation":58,"temp":300},"cst":{"entree_gamut":"sgamut3cine","entree_gamma":"slog3","sortie_gamut":"rec709","sortie_gamma":"gamma24"})");
    QElapsedTimer t;
    t.start();
    auto g = Grade::compile(j);
    const qint64 compileMs = t.elapsed();
    REQUIRE(g);
    std::vector<uint8_t> img(1920 * 1080 * 4, 128);
    t.restart();
    g->processRgba8(img.data(), 1920, 1080, 1920 * 4, threadRunner(4));
    const qint64 frameMs = t.elapsed();
    WARN("cuisson " << compileMs << " ms, image 1080p " << frameMs << " ms");
    CHECK(frameMs < 200);
}

TEST_CASE("Étalonnage : le filtre MLT koracut_grade", "[KoraCutGrade]")
{
    Mlt::Profile profile;
    Mlt::Filter filter(profile, "koracut_grade");
    if (!filter.is_valid()) {
        WARN("module MLT koracut_grade introuvable (pas encore installé) : essai sauté");
        return;
    }
    auto read = [&](bool withGrade) {
        Mlt::Producer producer(profile, "color", "0x404040ff");
        REQUIRE(producer.is_valid());
        Mlt::Filter f(profile, "koracut_grade");
        if (withGrade) {
            f.set("graphe", oneNode(R"("primaires":{"gain":[2,2,2,2]})").c_str());
            producer.attach(f);
        }
        std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
        mlt_image_format fmt = mlt_image_rgba;
        int w = 64, h = 36;
        const uint8_t *img = frame->get_image(fmt, w, h);
        REQUIRE(img);
        return int(img[0]);
    };
    const int base = read(false);
    const int graded = read(true);
    INFO("sans grade " << base << ", avec gain 2 " << graded);
    CHECK(base > 40);
    CHECK(std::abs(graded - std::min(255, base * 2)) <= 1);
}
