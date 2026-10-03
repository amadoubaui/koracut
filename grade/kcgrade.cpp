/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "kcgrade.h"
#include "kcops.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

namespace kcgrade {

// --- LUT chargées une fois ----------------------------------------------------------------------

namespace {
std::mutex s_lutMutex;
std::map<std::string, std::shared_ptr<const Lut3D>> s_luts;
std::vector<std::string> s_lutPaths;

bool fileExists(const std::string &p)
{
    if (FILE *f = std::fopen(p.c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    return false;
}
} // namespace

void setLutSearchPaths(const std::vector<std::string> &paths)
{
    std::lock_guard<std::mutex> lock(s_lutMutex);
    s_lutPaths = paths;
}

std::shared_ptr<const Lut3D> loadLutCached(const std::string &name, std::string *error)
{
    std::lock_guard<std::mutex> lock(s_lutMutex);
    auto it = s_luts.find(name);
    if (it != s_luts.end()) {
        return it->second;
    }
    std::string path = name;
    if (!fileExists(path)) {
        for (const std::string &dir : s_lutPaths) {
            for (const std::string &cand : {dir + "/" + name, dir + "/" + name + ".cube"}) {
                if (fileExists(cand)) {
                    path = cand;
                    break;
                }
            }
            if (path != name) {
                break;
            }
        }
    }
    Lut3D lut = Lut3D::loadCube(path, error);
    if (!lut.isValid()) {
        return nullptr;
    }
    auto shared = std::make_shared<const Lut3D>(std::move(lut));
    s_luts[name] = shared;
    return shared;
}

Runner threadRunner(int threads)
{
    return [threads](int jobs, const std::function<void(int)> &job) {
        const int n = std::max(1, std::min(threads, jobs));
        if (n == 1) {
            for (int i = 0; i < jobs; ++i) {
                job(i);
            }
            return;
        }
        std::vector<std::thread> pool;
        for (int t = 0; t < n; ++t) {
            pool.emplace_back([&, t] {
                for (int i = t; i < jobs; i += n) {
                    job(i);
                }
            });
        }
        for (auto &th : pool) {
            th.join();
        }
    };
}

// --- Le graphe ----------------------------------------------------------------------------------

namespace {

struct KeyInput
{
    bool invert = false, mask = false;
    double gain = 1., offset = 0.;
};

struct Node
{
    int id = 0;
    std::string type = "correcteur";
    bool enabled = true;
    // Entrées
    int rgbIn = 0;    // 0 = la source
    int channel = -1; // derrière un séparateur : 0 R, 1 V, 2 B
    int keyIn = -1;   // nœud dont la sortie de clé arrive ici (-1 : aucune)
    std::vector<int> inputs;
    std::vector<KeyInput> keyInputs; // mélangeur de clés
    std::string mode = "normal";
    // Correcteur
    NodeColor color;
    Qualifier qual;
    std::vector<Window> windows;
    double blurRadius = 0.5, blurHV = 0.5;
    KeyInput keyInCfg;
    bool keyOutInvert = false;
    double keyOutGain = 1., keyOutOffset = 0.;
    double qualGain = 1., qualOffset = 0.;
    // Déduit
    bool colorIdentity = true;
    bool usesKey = false;
    bool spatial = false;
    std::shared_ptr<Lut3D> lut; // la couleur du nœud cuite (chemin image)
};

struct Value
{
    Vec3 out{0, 0, 0};  // sortie RVB du nœud
    Vec3 in{0, 0, 0};   // ce qui est entré
    Vec3 corr{0, 0, 0}; // corrigé, avant la clé
    double mix = 1.;    // clé appliquée (0..2)
    double key = 1.;    // sortie de clé (pour les nœuds suivants)
};

double composite(const std::string &mode, double a, double b)
{
    // a : image du dessus (déjà composée), b : nœud du calque. Modes courants de Resolve.
    if (mode == "addition") {
        return a + b;
    }
    if (mode == "soustraction") {
        return a - b;
    }
    if (mode == "difference") {
        return std::abs(a - b);
    }
    if (mode == "multiplication") {
        return a * b;
    }
    if (mode == "ecran") {
        return 1. - (1. - a) * (1. - b);
    }
    if (mode == "incrustation") {
        return a < 0.5 ? 2. * a * b : 1. - 2. * (1. - a) * (1. - b);
    }
    if (mode == "lumiere_forte") {
        return b < 0.5 ? 2. * a * b : 1. - 2. * (1. - a) * (1. - b);
    }
    if (mode == "lumiere_douce") {
        return b < 0.5 ? a - (1. - 2. * b) * a * (1. - a) : a + (2. * b - 1.) * ((a < 0.25 ? ((16. * a - 12.) * a + 4.) * a : std::sqrt(std::max(a, 0.))) - a);
    }
    if (mode == "assombrir") {
        return std::min(a, b);
    }
    if (mode == "eclaircir") {
        return std::max(a, b);
    }
    return b; // normal
}

Vec3 compositeColor(const std::string &mode, const Vec3 &a, const Vec3 &b)
{
    if (mode == "luminosite" || mode == "couleur") {
        const double ya = luma709(a[0], a[1], a[2]), yb = luma709(b[0], b[1], b[2]);
        if (mode == "luminosite") {
            return {a[0] + yb - ya, a[1] + yb - ya, a[2] + yb - ya};
        }
        return {b[0] + ya - yb, b[1] + ya - yb, b[2] + ya - yb};
    }
    return {composite(mode, a[0], b[0]), composite(mode, a[1], b[1]), composite(mode, a[2], b[2])};
}

KeyInput readKeyInput(const Json &j, const char *prefix, const char *defaultMode = "matte")
{
    KeyInput k;
    const std::string p(prefix);
    k.invert = j.flag(p + "inverser", false);
    k.mask = j.str(p + "mode", defaultMode) == "masque";
    k.gain = j.num(p + "gain", 1.);
    k.offset = j.num(p + "decalage", 0.);
    return k;
}

} // namespace

class GradePrivate
{
public:
    bool enabled = true;
    int output = 0;
    std::map<int, Node> nodes;
    std::vector<int> order; // ordre topologique jusqu'à la sortie
    std::map<int, int> slot; // identifiant -> case dans le tableau des valeurs (0 = la source)
    bool identity = true;
    bool perPixel = true;
    Lut3D lut;
    int lutSize = 33;

    bool build(const Json &root, std::string *error);
    Vec3 evalColor(const Vec3 &rgb) const;
    void processImage(float *rgb, int w, int h, const Runner &run) const;
};

namespace {
bool fail(std::string *error, const std::string &msg)
{
    if (error) {
        *error = msg;
    }
    return false;
}
} // namespace

bool GradePrivate::build(const Json &root, std::string *error)
{
    if (!root.isObject()) {
        return fail(error, "Le graphe d'étalonnage n'est pas un objet JSON.");
    }
    if (root.num("format", 1.) > 1.) {
        return fail(error, "Ce graphe a été écrit par une version plus récente de KoraCut.");
    }
    enabled = root.flag("actif", true);
    const Json &list = root.get("noeuds");
    for (size_t i = 0; i < list.size(); ++i) {
        const Json &j = list.at(i);
        Node n;
        n.id = int(j.num("id", 0.));
        if (n.id <= 0 || nodes.count(n.id)) {
            return fail(error, "Identifiant de nœud absent ou en double (" + std::to_string(n.id) + ").");
        }
        n.type = j.str("type", "correcteur");
        n.enabled = j.flag("actif", true);
        const Json &in = j.get("entree");
        n.rgbIn = int(in.num("rvb", 0.));
        n.channel = int(in.num("canal", -1.));
        n.keyIn = in.get("cle").isNumber() ? int(in.num("cle", -1.)) : -1;
        const Json &ins = j.get("entrees");
        for (size_t k = 0; k < ins.size(); ++k) {
            n.inputs.push_back(int(ins.at(k).toNumber()));
        }
        n.mode = j.str("mode", "normal");
        if (n.type == "correcteur") {
            if (!n.color.read(j, error)) {
                return false;
            }
            n.qual.read(j.get("qualificateur"));
            const Json &wins = j.get("fenetres");
            for (size_t k = 0; k < wins.size(); ++k) {
                Window w;
                w.read(wins.at(k));
                if (wins.at(k).flag("actif", true)) {
                    n.windows.push_back(w);
                }
            }
            const Json &fl = j.get("flou");
            n.blurRadius = std::clamp(fl.num("rayon", 0.5), 0., 1.);
            n.blurHV = std::clamp(fl.num("hv", 0.5), 0., 1.);
            const Json &cle = j.get("cle");
            n.keyInCfg = readKeyInput(cle, "entree_");
            n.keyOutInvert = cle.flag("sortie_inverser", false);
            n.keyOutGain = std::clamp(cle.num("sortie_gain", 1.), 0., 2.);
            n.keyOutOffset = std::clamp(cle.num("sortie_decalage", 0.), -1., 1.);
            n.qualGain = cle.num("qualif_gain", 1.);
            n.qualOffset = cle.num("qualif_decalage", 0.);
            n.colorIdentity = n.color.isIdentity() && n.blurRadius == 0.5 && !n.color.primaries().hasSpatial();
            n.usesKey = n.qual.active || !n.windows.empty() || n.keyIn >= 0 || n.keyOutGain != 1. || n.keyOutOffset != 0. || n.keyOutInvert;
            n.spatial = !n.windows.empty() || (n.qual.active && n.qual.blur > 0.) || n.blurRadius != 0.5 || n.color.primaries().hasSpatial();
        } else if (n.type == "melangeur_cles") {
            const Json &ks = j.get("cles");
            for (size_t k = 0; k < n.inputs.size(); ++k) {
                n.keyInputs.push_back(readKeyInput(ks.at(k), "", "masque")); // les clés s'additionnent par défaut
            }
            n.keyOutInvert = j.flag("sortie_inverser", false);
            n.keyOutGain = j.num("sortie_gain", 1.);
            n.keyOutOffset = j.num("sortie_decalage", 0.);
        } else if (n.type != "parallele" && n.type != "calque" && n.type != "separateur" && n.type != "combineur") {
            return fail(error, "Type de nœud inconnu : « " + n.type + " ».");
        }
        nodes[n.id] = std::move(n);
    }
    output = int(root.num("sortie", nodes.empty() ? 0. : double(nodes.begin()->first)));
    if (output != 0 && !nodes.count(output)) {
        return fail(error, "La sortie du graphe désigne un nœud absent (" + std::to_string(output) + ").");
    }
    // Ordre topologique depuis la sortie ; refuse les boucles et les liens vers des nœuds absents.
    std::set<int> done, visiting;
    std::function<bool(int)> visit = [&](int id) -> bool {
        if (id == 0 || done.count(id)) {
            return true;
        }
        if (!nodes.count(id)) {
            return fail(error, "Un lien désigne un nœud absent (" + std::to_string(id) + ").");
        }
        if (visiting.count(id)) {
            return fail(error, "Le graphe contient une boucle (nœud " + std::to_string(id) + ").");
        }
        visiting.insert(id);
        const Node &n = nodes.at(id);
        std::vector<int> deps = n.inputs;
        if (n.type == "correcteur" || n.type == "separateur") {
            deps.push_back(n.rgbIn);
        }
        if (n.keyIn >= 0) {
            deps.push_back(n.keyIn);
        }
        for (int dep : deps) {
            if (!visit(dep)) {
                return false;
            }
        }
        visiting.erase(id);
        done.insert(id);
        order.push_back(id);
        return true;
    };
    if (!visit(output)) {
        return false;
    }
    slot[0] = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        slot[order[i]] = int(i) + 1;
    }
    identity = !enabled || output == 0;
    bool anyLog = false;
    if (!identity) {
        identity = true;
        for (int id : order) {
            const Node &n = nodes.at(id);
            if (!n.enabled) {
                continue;
            }
            if (n.type != "correcteur" || !n.colorIdentity || n.usesKey || n.channel >= 0) {
                identity = false;
            }
            if (n.type == "correcteur" && n.color.usesLog()) {
                anyLog = true;
            }
        }
    }
    perPixel = true;
    for (int id : order) {
        const Node &n = nodes.at(id);
        if (n.enabled && n.spatial) {
            perPixel = false;
        }
    }
    lutSize = anyLog ? 65 : 33;
    if (!perPixel) {
        // Chemin image : la couleur de chaque nœud est cuite dans sa propre LUT (la lecture reste légère).
        for (int id : order) {
            Node &n = nodes.at(id);
            if (n.type == "correcteur" && n.enabled && !n.color.isIdentity()) {
                n.lut = std::make_shared<Lut3D>(n.color.usesLog() ? 65 : 33);
                const NodeColor *c = &n.color;
                n.lut->bake([c](const Vec3 &x) { return c->apply(x); }, 4);
            }
        }
    }
    return true;
}

Vec3 GradePrivate::evalColor(const Vec3 &src) const
{
    if (identity) {
        return src;
    }
    // Pas d'allocation par appel : la cuisson d'une LUT 65 points appelle cette fonction 274 625 fois.
    Value vals[64];
    std::vector<Value> big;
    Value *v = vals;
    if (order.size() + 1 > 64) {
        big.resize(order.size() + 1);
        v = big.data();
    }
    auto V = [&](int id) -> Value & { return v[slot.at(id)]; };
    Value s;
    s.out = s.in = s.corr = src;
    v[0] = s;
    for (int id : order) {
        const Node &n = nodes.at(id);
        Value r;
        if (n.type == "correcteur") {
            Vec3 in = V(n.rgbIn).out;
            if (n.channel >= 0 && n.channel < 3) {
                in = {in[size_t(n.channel)], in[size_t(n.channel)], in[size_t(n.channel)]};
            }
            r.in = in;
            if (!n.enabled) {
                r.out = r.corr = in;
                r.key = 1.;
                r.mix = 0.;
                V(id) = r;
                continue;
            }
            r.corr = n.color.apply(in);
            double k = 1.;
            if (n.qual.active) {
                k = std::clamp(n.qual.key(in) * n.qualGain + n.qualOffset, 0., 1.);
            }
            if (n.keyIn >= 0) {
                double kin = V(n.keyIn).key;
                if (n.keyInCfg.invert) {
                    kin = 1. - kin;
                }
                kin = std::clamp(kin * n.keyInCfg.gain + n.keyInCfg.offset, 0., 1.);
                k = n.keyInCfg.mask ? std::max(k, kin) : k * kin;
            }
            r.key = n.keyOutInvert ? 1. - k : k;
            r.mix = std::clamp(r.key * n.keyOutGain + n.keyOutOffset, 0., 2.);
            for (int c = 0; c < 3; ++c) {
                r.out[size_t(c)] = in[size_t(c)] + r.mix * (r.corr[size_t(c)] - in[size_t(c)]);
            }
        } else if (n.type == "separateur") {
            r.out = r.in = r.corr = V(n.rgbIn).out;
        } else if (n.type == "combineur") {
            for (size_t c = 0; c < 3; ++c) {
                r.out[c] = c < n.inputs.size() ? V(n.inputs[c]).out[c] : 0.;
            }
            r.in = r.corr = r.out;
        } else if (n.type == "parallele") {
            // Mesuré sur Resolve : l'entrée commune plus la somme des écarts de chaque branche.
            if (n.inputs.empty()) {
                r.out = src;
            } else {
                const Vec3 base = V(n.inputs.front()).in;
                r.out = base;
                for (int in : n.inputs) {
                    const Value &b = V(in);
                    for (size_t c = 0; c < 3; ++c) {
                        r.out[c] += b.out[c] - b.in[c];
                    }
                }
            }
            r.in = r.corr = r.out;
        } else if (n.type == "calque") {
            // Mesuré sur Resolve : l'entrée du bas (dernière de la liste) recouvre celles du dessus,
            // dosée par sa clé ; les modes de composite s'appliquent au calque sur ce qui est dessous.
            if (n.inputs.empty()) {
                r.out = src;
            } else {
                r.out = V(n.inputs.front()).out;
                for (size_t i = 1; i < n.inputs.size(); ++i) {
                    const Value &b = V(n.inputs[i]);
                    const double a = std::clamp(b.mix, 0., 1.);
                    const Vec3 top = compositeColor(n.mode, r.out, b.corr);
                    for (size_t c = 0; c < 3; ++c) {
                        r.out[c] = r.out[c] + a * (top[c] - r.out[c]);
                    }
                }
            }
            r.in = r.corr = r.out;
        } else if (n.type == "melangeur_cles") {
            double k = 0.;
            for (size_t i = 0; i < n.inputs.size(); ++i) {
                const KeyInput &cfg = i < n.keyInputs.size() ? n.keyInputs[i] : KeyInput{};
                double ki = V(n.inputs[i]).key;
                if (cfg.invert) {
                    ki = 1. - ki;
                }
                ki = std::clamp(ki * cfg.gain + cfg.offset, 0., 1.);
                // « masque » (défaut) additionne, « matte » garde l'intersection.
                k = i == 0 ? ki : (cfg.mask ? std::min(1., k + ki) : k * ki);
            }
            if (n.keyOutInvert) {
                k = 1. - k;
            }
            r.key = std::clamp(k * n.keyOutGain + n.keyOutOffset, 0., 1.);
            r.out = r.in = r.corr = src;
        }
        V(id) = r;
    }
    return V(output).out;
}

// --- Chemin image (opérations spatiales) ---------------------------------------------------------

namespace {

// Un vecteur de flottants qui ne remet pas sa mémoire à zéro à la création : chaque plan de travail est
// entièrement écrit avant d'être lu, la mise à zéro de plusieurs Mo par image ne servait à rien (chantier 10).
template<typename T>
struct NoInit : std::allocator<T> {
    template<typename U>
    struct rebind {
        using other = NoInit<U>;
    };
    NoInit() = default;
    template<typename U>
    NoInit(const NoInit<U> &) noexcept
    {
    }
    template<typename U>
    void construct(U *p) noexcept
    {
        ::new (static_cast<void *>(p)) U;
    }
    template<typename U, typename... A>
    void construct(U *p, A &&...a)
    {
        ::new (static_cast<void *>(p)) U(std::forward<A>(a)...);
    }
};

using Plane = std::vector<float, NoInit<float>>;

// Flou gaussien approché par trois passes de boîte (séparable), sur un plan de flottants.
void boxBlurPlane(Plane &p, int w, int h, int stride, int channels, double sigmaX, double sigmaY, const Runner &run)
{
    auto boxes = [](double sigma) {
        // Largeurs des trois boîtes pour un sigma donné (Kovesi).
        const double wIdeal = std::sqrt(12. * sigma * sigma / 3. + 1.);
        int wl = int(std::floor(wIdeal));
        if (wl % 2 == 0) {
            --wl;
        }
        const int wu = wl + 2;
        const double mIdeal = (12. * sigma * sigma - 3. * wl * wl - 12. * wl - 9.) / (-4. * wl - 4.);
        const int m = int(std::round(mIdeal));
        std::array<int, 3> r{};
        for (int i = 0; i < 3; ++i) {
            r[size_t(i)] = ((i < m ? wl : wu) - 1) / 2;
        }
        return r;
    };
    Plane tmp(p.size());
    auto passH = [&](Plane &src, Plane &dst, int r) {
        run(h, [&](int y) {
            for (int c = 0; c < channels; ++c) {
                const float *s = &src[size_t(y) * stride + c];
                float *d = &dst[size_t(y) * stride + c];
                double acc = 0.;
                for (int x = -r; x <= r; ++x) {
                    acc += s[size_t(std::clamp(x, 0, w - 1)) * channels];
                }
                const double inv = 1. / (2 * r + 1);
                for (int x = 0; x < w; ++x) {
                    d[size_t(x) * channels] = float(acc * inv);
                    acc += s[size_t(std::min(x + r + 1, w - 1)) * channels] - s[size_t(std::max(x - r, 0)) * channels];
                }
            }
        });
    };
    // Passe verticale : ligne par ligne sur un bloc de colonnes, une somme glissante par colonne. Avant, chaque
    // colonne descendait l'image entière (un saut de ligne à chaque lecture, la mémoire cache ne servait pas).
    // Les additions se font dans le même ordre qu'avant pour chaque colonne : même résultat au bit près.
    auto passV = [&](Plane &src, Plane &dst, int r) {
        const int width = w * channels; // flottants utiles par ligne
        const int block = 64;
        const int blocks = (width + block - 1) / block;
        const double inv = 1. / (2 * r + 1);
        run(blocks, [&](int bi) {
            const int x0 = bi * block, x1 = std::min(width, x0 + block);
            double acc[block];
            for (int x = x0; x < x1; ++x) {
                acc[x - x0] = 0.;
            }
            for (int y = -r; y <= r; ++y) {
                const float *row = &src[size_t(std::clamp(y, 0, h - 1)) * stride];
                for (int x = x0; x < x1; ++x) {
                    acc[x - x0] += row[x];
                }
            }
            for (int y = 0; y < h; ++y) {
                float *out = &dst[size_t(y) * stride];
                const float *add = &src[size_t(std::min(y + r + 1, h - 1)) * stride];
                const float *sub = &src[size_t(std::max(y - r, 0)) * stride];
                for (int x = x0; x < x1; ++x) {
                    out[x] = float(acc[x - x0] * inv);
                    acc[x - x0] += add[x] - sub[x];
                }
            }
        });
    };
    if (sigmaX > 0.3) {
        for (int r : boxes(sigmaX)) {
            if (r > 0) {
                passH(p, tmp, r);
                p.swap(tmp);
            }
        }
    }
    if (sigmaY > 0.3) {
        for (int r : boxes(sigmaY)) {
            if (r > 0) {
                passV(p, tmp, r);
                p.swap(tmp);
            }
        }
    }
}

} // namespace

void GradePrivate::processImage(float *rgb, int w, int h, const Runner &run) const
{
    const size_t npx = size_t(w) * size_t(h);
    const double imageAspect = double(w) / std::max(h, 1);
    const double scale = h / 1080.; // les rayons sont pensés pour du 1080
    struct Buf
    {
        std::shared_ptr<Plane> out, in, corr;
        std::shared_ptr<Plane> mix, key; // 1 flottant par pixel (null = 1 partout)
    };
    std::map<int, Buf> bufs;
    auto src = std::make_shared<Plane>(rgb, rgb + npx * 3);
    bufs[0] = {src, src, src, nullptr, nullptr};
    const int rows = h;
    auto keyAt = [](const std::shared_ptr<Plane> &k, size_t i) { return k ? double((*k)[i]) : 1.; };

    for (int id : order) {
        const Node &n = nodes.at(id);
        Buf b;
        if (n.type == "correcteur") {
            std::shared_ptr<Plane> in = bufs[n.rgbIn].out;
            if (n.channel >= 0 && n.channel < 3) {
                auto ch = std::make_shared<Plane>(npx * 3);
                const int c = n.channel;
                run(rows, [&](int y) {
                    for (size_t i = size_t(y) * w; i < size_t(y + 1) * w; ++i) {
                        const float v = (*in)[i * 3 + size_t(c)];
                        (*ch)[i * 3] = (*ch)[i * 3 + 1] = (*ch)[i * 3 + 2] = v;
                    }
                });
                in = ch;
            }
            b.in = in;
            if (!n.enabled) {
                b.out = b.corr = in;
                bufs[id] = b;
                continue;
            }
            // 1. Opérations spatiales des primaires (avant tout, comme dans Resolve) : contraste local.
            std::shared_ptr<Plane> pre = in;
            const Primaries &pr = n.color.primaries();
            if (pr.hasSpatial()) {
                pre = std::make_shared<Plane>(*in);
                Plane lum(npx);
                run(rows, [&](int y) {
                    for (size_t i = size_t(y) * w; i < size_t(y + 1) * w; ++i) {
                        lum[i] = float(luma709((*in)[i * 3], (*in)[i * 3 + 1], (*in)[i * 3 + 2]));
                    }
                });
                Plane base = lum;
                boxBlurPlane(base, w, h, w, 1, 12. * scale, 12. * scale, run);
                Plane wide = lum;
                if (pr.shadows != 0. || pr.highlights != 0.) {
                    boxBlurPlane(wide, w, h, w, 1, 40. * scale, 40. * scale, run);
                }
                const double md = pr.midDetail / 100., sh = pr.shadows / 100., hl = pr.highlights / 100.;
                run(rows, [&](int y) {
                    for (size_t i = size_t(y) * w; i < size_t(y + 1) * w; ++i) {
                        const double Y = lum[i];
                        double add = 0.;
                        if (md != 0.) {
                            // Mid/Détail : le détail (luma moins sa version floue) pèse surtout dans les tons moyens.
                            add += md * 1.2 * (Y - base[i]) * 4. * Y * (1. - Y);
                        }
                        double gain = 1.;
                        const double B = wide[i];
                        if (sh != 0. && B < 0.5) {
                            const double wgt = (0.5 - B) / 0.5;
                            gain *= 1. + sh * 0.9 * wgt * wgt;
                        }
                        if (hl != 0. && B > 0.5) {
                            const double wgt = (B - 0.5) / 0.5;
                            gain *= 1. + hl * 0.45 * wgt * wgt;
                        }
                        for (size_t c = 0; c < 3; ++c) {
                            (*pre)[i * 3 + c] = float((*in)[i * 3 + c] * gain + add);
                        }
                    }
                });
            }
            // 2. La couleur du nœud, par sa LUT cuite à la compilation.
            auto corr = std::make_shared<Plane>(n.lut ? Plane(npx * 3) : *pre);
            if (n.lut) {
                run(rows, [&](int y) {
                    for (size_t i = size_t(y) * w; i < size_t(y + 1) * w; ++i) {
                        n.lut->apply(&(*pre)[i * 3], &(*corr)[i * 3]);
                    }
                });
            }
            // 3. Flou ou netteté (palette Flou : 0,5 = neutre).
            if (n.blurRadius != 0.5) {
                const double hv = n.blurHV;
                if (n.blurRadius > 0.5) {
                    const double sigma = (n.blurRadius - 0.5) * 2. * 27. * scale;
                    boxBlurPlane(*corr, w, h, w * 3, 3, sigma * hv / 0.5, sigma * (1. - hv) / 0.5, run);
                } else {
                    Plane soft = *corr;
                    boxBlurPlane(soft, w, h, w * 3, 3, 1.6 * scale, 1.6 * scale, run);
                    const double amount = (0.5 - n.blurRadius) * 2. * 1.5;
                    run(rows, [&](int y) {
                        for (size_t i = size_t(y) * w * 3; i < size_t(y + 1) * w * 3; ++i) {
                            (*corr)[i] = float((*corr)[i] + amount * ((*corr)[i] - soft[i]));
                        }
                    });
                }
            }
            b.corr = corr;
            // 4. La clé : qualificateur × fenêtres × clé d'entrée.
            if (n.usesKey) {
                auto key = std::make_shared<Plane>(npx, 1.f);
                if (n.qual.active) {
                    run(rows, [&](int y) {
                        for (size_t i = size_t(y) * w; i < size_t(y + 1) * w; ++i) {
                            (*key)[i] = float(n.qual.key({(*in)[i * 3], (*in)[i * 3 + 1], (*in)[i * 3 + 2]}));
                        }
                    });
                    if (n.qual.blur > 0.) {
                        const double s = n.qual.blur / 100. * 20. * scale;
                        boxBlurPlane(*key, w, h, w, 1, s, s, run);
                    }
                    run(rows, [&](int y) {
                        for (size_t i = size_t(y) * w; i < size_t(y + 1) * w; ++i) {
                            (*key)[i] = float(std::clamp((*key)[i] * n.qualGain + n.qualOffset, 0., 1.));
                        }
                    });
                }
                if (!n.windows.empty()) {
                    bool anyAdd = false;
                    for (const Window &win : n.windows) {
                        anyAdd = anyAdd || !win.subtract;
                    }
                    run(rows, [&](int y) {
                        const double v = (y + 0.5) / h;
                        for (int x = 0; x < w; ++x) {
                            const double u = (x + 0.5) / w;
                            double add = anyAdd ? 0. : 1., sub = 1.;
                            for (const Window &win : n.windows) {
                                const double k = win.key(u, v, imageAspect);
                                if (win.subtract) {
                                    sub *= 1. - k;
                                } else {
                                    add = std::max(add, k);
                                }
                            }
                            (*key)[size_t(y) * w + x] *= float(add * sub);
                        }
                    });
                }
                if (n.keyIn >= 0) {
                    const std::shared_ptr<Plane> kin = bufs[n.keyIn].key;
                    run(rows, [&](int y) {
                        for (size_t i = size_t(y) * w; i < size_t(y + 1) * w; ++i) {
                            double ki = keyAt(kin, i);
                            if (n.keyInCfg.invert) {
                                ki = 1. - ki;
                            }
                            ki = std::clamp(ki * n.keyInCfg.gain + n.keyInCfg.offset, 0., 1.);
                            (*key)[i] = float(n.keyInCfg.mask ? std::max(double((*key)[i]), ki) : (*key)[i] * ki);
                        }
                    });
                }
                auto mix = std::make_shared<Plane>(npx);
                run(rows, [&](int y) {
                    for (size_t i = size_t(y) * w; i < size_t(y + 1) * w; ++i) {
                        double k = (*key)[i];
                        if (n.keyOutInvert) {
                            k = 1. - k;
                        }
                        (*key)[i] = float(k);
                        (*mix)[i] = float(std::clamp(k * n.keyOutGain + n.keyOutOffset, 0., 2.));
                    }
                });
                b.key = key;
                b.mix = mix;
                auto out = std::make_shared<Plane>(npx * 3);
                run(rows, [&](int y) {
                    for (size_t i = size_t(y) * w; i < size_t(y + 1) * w; ++i) {
                        const float m = (*mix)[i];
                        for (size_t c = 0; c < 3; ++c) {
                            (*out)[i * 3 + c] = (*in)[i * 3 + c] + m * ((*corr)[i * 3 + c] - (*in)[i * 3 + c]);
                        }
                    }
                });
                b.out = out;
            } else {
                b.out = corr;
            }
        } else if (n.type == "separateur") {
            b.out = b.in = b.corr = bufs[n.rgbIn].out;
        } else if (n.type == "combineur") {
            auto out = std::make_shared<Plane>(npx * 3, 0.f);
            for (size_t c = 0; c < 3 && c < n.inputs.size(); ++c) {
                const std::shared_ptr<Plane> s = bufs[n.inputs[c]].out;
                run(rows, [&](int y) {
                    for (size_t i = size_t(y) * w; i < size_t(y + 1) * w; ++i) {
                        (*out)[i * 3 + c] = (*s)[i * 3 + c];
                    }
                });
            }
            b.out = b.in = b.corr = out;
        } else if (n.type == "parallele") {
            auto out = std::make_shared<Plane>(n.inputs.empty() ? *src : *bufs[n.inputs.front()].in);
            for (int inId : n.inputs) {
                const Buf &ib = bufs[inId];
                run(rows, [&](int y) {
                    for (size_t i = size_t(y) * w * 3; i < size_t(y + 1) * w * 3; ++i) {
                        (*out)[i] += (*ib.out)[i] - (*ib.in)[i];
                    }
                });
            }
            b.out = b.in = b.corr = out;
        } else if (n.type == "calque") {
            auto out = std::make_shared<Plane>(n.inputs.empty() ? *src : *bufs[n.inputs.front()].out);
            for (size_t k = 1; k < n.inputs.size(); ++k) {
                const Buf &ib = bufs[n.inputs[k]];
                run(rows, [&](int y) {
                    for (size_t i = size_t(y) * w; i < size_t(y + 1) * w; ++i) {
                        const double a = std::clamp(keyAt(ib.mix, i), 0., 1.);
                        const Vec3 below{(*out)[i * 3], (*out)[i * 3 + 1], (*out)[i * 3 + 2]};
                        const Vec3 top = compositeColor(n.mode, below, {(*ib.corr)[i * 3], (*ib.corr)[i * 3 + 1], (*ib.corr)[i * 3 + 2]});
                        for (size_t c = 0; c < 3; ++c) {
                            (*out)[i * 3 + c] = float(below[c] + a * (top[c] - below[c]));
                        }
                    }
                });
            }
            b.out = b.in = b.corr = out;
        } else if (n.type == "melangeur_cles") {
            auto key = std::make_shared<Plane>(npx, 0.f);
            run(rows, [&](int y) {
                for (size_t i = size_t(y) * w; i < size_t(y + 1) * w; ++i) {
                    double k = 0.;
                    for (size_t m = 0; m < n.inputs.size(); ++m) {
                        const KeyInput &cfg = m < n.keyInputs.size() ? n.keyInputs[m] : KeyInput{};
                        double ki = keyAt(bufs[n.inputs[m]].key, i);
                        if (cfg.invert) {
                            ki = 1. - ki;
                        }
                        ki = std::clamp(ki * cfg.gain + cfg.offset, 0., 1.);
                        k = m == 0 ? ki : (cfg.mask ? std::min(1., k + ki) : k * ki);
                    }
                    if (n.keyOutInvert) {
                        k = 1. - k;
                    }
                    (*key)[i] = float(std::clamp(k * n.keyOutGain + n.keyOutOffset, 0., 1.));
                }
            });
            b.key = key;
            b.out = b.in = b.corr = src;
        }
        bufs[id] = b;
    }
    const std::shared_ptr<Plane> res = bufs[output].out;
    std::copy(res->begin(), res->end(), rgb);
}

// --- Grade --------------------------------------------------------------------------------------

Grade::Grade()
    : d(std::make_unique<GradePrivate>())
{
}

Grade::~Grade() = default;

std::shared_ptr<const Grade> Grade::compile(const std::string &json, std::string *error)
{
    bool ok = false;
    const Json root = Json::parse(json, &ok);
    if (!ok) {
        if (error) {
            *error = "Le graphe d'étalonnage n'est pas un JSON valable.";
        }
        return nullptr;
    }
    std::shared_ptr<Grade> g(new Grade());
    if (!g->d->build(root, error)) {
        return nullptr;
    }
    if (!g->d->identity) {
        g->d->lut = Lut3D(g->d->lutSize);
        const GradePrivate *p = g->d.get();
        g->d->lut.bake([p](const Vec3 &c) { return p->evalColor(c); }, std::max(1u, std::min(8u, std::thread::hardware_concurrency())));
    }
    return g;
}

bool Grade::isIdentity() const
{
    return d->identity;
}

bool Grade::isPerPixel() const
{
    return d->perPixel;
}

Vec3 Grade::evalColor(const Vec3 &rgb) const
{
    return d->evalColor(rgb);
}

const Lut3D &Grade::lut() const
{
    return d->lut;
}

void Grade::processFloat(float *rgb, int width, int height, const Runner &run) const
{
    if (d->identity) {
        return;
    }
    if (d->perPixel) {
        run(height, [&](int y) {
            float *p = rgb + size_t(y) * width * 3;
            for (int x = 0; x < width; ++x, p += 3) {
                float o[3];
                d->lut.apply(p, o);
                p[0] = o[0];
                p[1] = o[1];
                p[2] = o[2];
            }
        });
        return;
    }
    d->processImage(rgb, width, height, run);
}

namespace {
inline uint8_t to8(float v)
{
    return uint8_t(std::clamp(int(v * 255.f + 0.5f), 0, 255));
}
inline uint16_t to16(float v)
{
    return uint16_t(std::clamp(int(v * 65535.f + 0.5f), 0, 65535));
}
} // namespace

void Grade::processRgba8(uint8_t *data, int width, int height, int stride, const Runner &run) const
{
    if (d->identity) {
        return;
    }
    if (d->perPixel) {
        // Le chemin de la lecture : une interpolation tétraédrique par pixel, lignes réparties sur les cœurs.
        run(height, [&](int y) {
            uint8_t *p = data + size_t(y) * stride;
            for (int x = 0; x < width; ++x, p += 4) {
                const float in[3] = {p[0] / 255.f, p[1] / 255.f, p[2] / 255.f};
                float o[3];
                d->lut.apply(in, o);
                p[0] = to8(o[0]);
                p[1] = to8(o[1]);
                p[2] = to8(o[2]);
            }
        });
        return;
    }
    Plane buf(size_t(width) * height * 3);
    run(height, [&](int y) {
        const uint8_t *p = data + size_t(y) * stride;
        float *f = &buf[size_t(y) * width * 3];
        for (int x = 0; x < width; ++x, p += 4, f += 3) {
            f[0] = p[0] / 255.f;
            f[1] = p[1] / 255.f;
            f[2] = p[2] / 255.f;
        }
    });
    d->processImage(buf.data(), width, height, run);
    run(height, [&](int y) {
        uint8_t *p = data + size_t(y) * stride;
        const float *f = &buf[size_t(y) * width * 3];
        for (int x = 0; x < width; ++x, p += 4, f += 3) {
            p[0] = to8(f[0]);
            p[1] = to8(f[1]);
            p[2] = to8(f[2]);
        }
    });
}

void Grade::processRgba16(uint16_t *data, int width, int height, int stride, const Runner &run) const
{
    if (d->identity) {
        return;
    }
    Plane buf(size_t(width) * height * 3);
    const int s16 = stride / 2;
    run(height, [&](int y) {
        const uint16_t *p = data + size_t(y) * s16;
        float *f = &buf[size_t(y) * width * 3];
        for (int x = 0; x < width; ++x, p += 4, f += 3) {
            f[0] = p[0] / 65535.f;
            f[1] = p[1] / 65535.f;
            f[2] = p[2] / 65535.f;
        }
    });
    processFloat(buf.data(), width, height, run);
    run(height, [&](int y) {
        uint16_t *p = data + size_t(y) * s16;
        const float *f = &buf[size_t(y) * width * 3];
        for (int x = 0; x < width; ++x, p += 4, f += 3) {
            p[0] = to16(f[0]);
            p[1] = to16(f[1]);
            p[2] = to16(f[2]);
        }
    });
}

bool Grade::exportCube(const std::string &path, int size, std::string *error) const
{
    if (!d->perPixel) {
        if (error) {
            *error = "Ce grade utilise des fenêtres, du flou ou du contraste local : une LUT ne peut pas le porter.";
        }
        return false;
    }
    Lut3D lut(std::clamp(size, 2, 129));
    const GradePrivate *p = d.get();
    lut.bake([p](const Vec3 &c) {
        Vec3 o = p->evalColor(c);
        for (double &v : o) {
            v = std::clamp(v, 0., 1.);
        }
        return o;
    }, 4);
    if (!lut.saveCube(path, "KoraCut")) {
        if (error) {
            *error = "Impossible d'écrire le fichier.";
        }
        return false;
    }
    return true;
}

std::string defaultGraph()
{
    return R"({"format":1,"actif":true,"sortie":1,"noeuds":[{"id":1,"type":"correcteur","etiquette":"","actif":true,"x":0,"y":0,"entree":{"rvb":0}}]})";
}

} // namespace kcgrade
