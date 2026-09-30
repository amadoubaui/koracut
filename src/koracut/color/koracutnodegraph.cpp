/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutnodegraph.h"

#include <QHash>
#include <QSet>
#include <algorithm>
#include <functional>

namespace KoraCutNodeGraph {

namespace {
const QString kNodes = QStringLiteral("noeuds");
const QString kOut = QStringLiteral("sortie");
const QString kIn = QStringLiteral("entree");
const QString kIns = QStringLiteral("entrees");
const QString kType = QStringLiteral("type");
const QString kId = QStringLiteral("id");

int nextId(const QJsonObject &graph)
{
    int m = 0;
    for (int id : nodeIds(graph)) {
        m = std::max(m, id);
    }
    return m + 1;
}

QJsonObject corrector(int id, int input)
{
    return QJsonObject{{kId, id}, {kType, QStringLiteral("correcteur")}, {QStringLiteral("actif"), true}, {kIn, QJsonObject{{QStringLiteral("rvb"), input}}}};
}

int rgbInputOf(const QJsonObject &n)
{
    return n.value(kIn).toObject().value(QStringLiteral("rvb")).toInt(0);
}

void setRgbInput(QJsonObject &n, int input)
{
    QJsonObject in = n.value(kIn).toObject();
    in.insert(QStringLiteral("rvb"), input);
    n.insert(kIn, in);
}

// Tout ce qui lit la sortie RVB de « from » la lit désormais sur « to » (sauf « except »).
void redirectConsumers(QJsonObject &graph, int from, int to, int except)
{
    QJsonArray list = nodes(graph);
    for (int i = 0; i < list.size(); ++i) {
        QJsonObject n = list.at(i).toObject();
        const int id = n.value(kId).toInt();
        if (id == except) {
            continue;
        }
        if (isMixer(n)) {
            QJsonArray ins = n.value(kIns).toArray();
            for (int k = 0; k < ins.size(); ++k) {
                if (ins.at(k).toInt() == from) {
                    ins[k] = to;
                }
            }
            n.insert(kIns, ins);
        } else if (rgbInputOf(n) == from && n.contains(kIn)) {
            setRgbInput(n, to);
        }
        list[i] = n;
    }
    graph.insert(kNodes, list);
    if (outputNode(graph) == from) {
        graph.insert(kOut, to);
    }
}

QList<int> consumers(const QJsonObject &graph, int id)
{
    QList<int> out;
    for (const QJsonValue &v : nodes(graph)) {
        const QJsonObject n = v.toObject();
        if (isMixer(n)) {
            for (const QJsonValue &i : n.value(kIns).toArray()) {
                if (i.toInt() == id) {
                    out << n.value(kId).toInt();
                }
            }
        } else if (n.contains(kIn) && rgbInputOf(n) == id) {
            out << n.value(kId).toInt();
        }
    }
    return out;
}

bool isUpstream(const QJsonObject &graph, int candidate, int of)
{
    // candidate est-il en amont de « of » (ou « of » lui-même) ?
    QSet<int> seen;
    std::function<bool(int)> walk = [&](int id) -> bool {
        if (id == candidate) {
            return true;
        }
        if (id == 0 || seen.contains(id)) {
            return false;
        }
        seen.insert(id);
        const QJsonObject n = node(graph, id);
        for (int in : rgbInputs(n)) {
            if (walk(in)) {
                return true;
            }
        }
        const QJsonValue key = n.value(kIn).toObject().value(QStringLiteral("cle"));
        return key.isDouble() && walk(key.toInt());
    };
    return walk(of);
}

int addMixed(QJsonObject &graph, int beside, const QString &mixerType)
{
    if (!hasNode(graph, beside)) {
        return -1;
    }
    const QJsonObject b = node(graph, beside);
    const int input = isMixer(b) ? beside : rgbInputOf(b);
    const int n = nextId(graph);
    // Si le nœud alimente déjà un mélangeur du même type, la nouvelle branche s'y ajoute.
    for (int c : consumers(graph, beside)) {
        QJsonObject m = node(graph, c);
        if (m.value(kType).toString() == mixerType) {
            putNode(graph, corrector(n, input));
            QJsonArray ins = m.value(kIns).toArray();
            ins.append(n);
            m.insert(kIns, ins);
            putNode(graph, m);
            autoLayout(graph);
            return n;
        }
    }
    putNode(graph, corrector(n, input));
    const int mixer = n + 1;
    redirectConsumers(graph, beside, mixer, -1);
    putNode(graph, QJsonObject{{kId, mixer}, {kType, mixerType}, {QStringLiteral("actif"), true}, {kIns, QJsonArray{beside, n}}});
    autoLayout(graph);
    return n;
}
} // namespace

QJsonObject defaultGraph()
{
    QJsonObject g{{QStringLiteral("format"), 1}, {QStringLiteral("actif"), true}, {kOut, 1}, {kNodes, QJsonArray{corrector(1, 0)}}};
    autoLayout(g);
    return g;
}

QJsonObject normalized(const QJsonObject &graph)
{
    if (!graph.value(kNodes).isArray()) {
        return defaultGraph();
    }
    QJsonObject g = graph;
    if (!g.contains(QStringLiteral("format"))) {
        g.insert(QStringLiteral("format"), 1);
    }
    if (!g.contains(QStringLiteral("actif"))) {
        g.insert(QStringLiteral("actif"), true);
    }
    if (!g.contains(kOut) || (!hasNode(g, outputNode(g)) && outputNode(g) != 0)) {
        const QList<int> ids = nodeIds(g);
        g.insert(kOut, ids.isEmpty() ? 0 : ids.last());
    }
    bool placed = true;
    for (const QJsonValue &v : nodes(g)) {
        placed = placed && v.toObject().contains(QStringLiteral("x"));
    }
    if (!placed) {
        autoLayout(g);
    }
    return g;
}

QJsonArray nodes(const QJsonObject &graph)
{
    return graph.value(kNodes).toArray();
}

QJsonObject node(const QJsonObject &graph, int id)
{
    for (const QJsonValue &v : nodes(graph)) {
        if (v.toObject().value(kId).toInt() == id) {
            return v.toObject();
        }
    }
    return QJsonObject();
}

bool hasNode(const QJsonObject &graph, int id)
{
    return !node(graph, id).isEmpty();
}

void putNode(QJsonObject &graph, const QJsonObject &n)
{
    QJsonArray list = nodes(graph);
    const int id = n.value(kId).toInt();
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).toObject().value(kId).toInt() == id) {
            list[i] = n;
            graph.insert(kNodes, list);
            return;
        }
    }
    list.append(n);
    graph.insert(kNodes, list);
}

int outputNode(const QJsonObject &graph)
{
    return graph.value(kOut).toInt(0);
}

QList<int> nodeIds(const QJsonObject &graph)
{
    QList<int> ids;
    for (const QJsonValue &v : nodes(graph)) {
        ids << v.toObject().value(kId).toInt();
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool isCorrector(const QJsonObject &n)
{
    return n.value(kType).toString(QStringLiteral("correcteur")) == QLatin1String("correcteur");
}

bool isMixer(const QJsonObject &n)
{
    const QString t = n.value(kType).toString();
    return t == QLatin1String("parallele") || t == QLatin1String("calque") || t == QLatin1String("combineur") || t == QLatin1String("melangeur_cles");
}

int displayNumber(const QJsonObject &graph, int id)
{
    int rank = 0;
    for (int other : nodeIds(graph)) {
        if (isCorrector(node(graph, other))) {
            ++rank;
            if (other == id) {
                return rank;
            }
        }
    }
    return 0;
}

QList<int> rgbInputs(const QJsonObject &n)
{
    QList<int> ins;
    if (isMixer(n)) {
        for (const QJsonValue &v : n.value(kIns).toArray()) {
            ins << v.toInt();
        }
    } else if (n.contains(kIn)) {
        ins << rgbInputOf(n);
    }
    return ins;
}

int addSerial(QJsonObject &graph, int after)
{
    if (after != 0 && !hasNode(graph, after)) {
        return -1;
    }
    const int n = nextId(graph);
    redirectConsumers(graph, after, n, -1);
    putNode(graph, corrector(n, after));
    if (nodeIds(graph).size() == 1) {
        graph.insert(kOut, n);
    }
    autoLayout(graph);
    return n;
}

int addSerialBefore(QJsonObject &graph, int before)
{
    QJsonObject b = node(graph, before);
    if (b.isEmpty() || isMixer(b)) {
        return -1;
    }
    const int n = nextId(graph);
    putNode(graph, corrector(n, rgbInputOf(b)));
    setRgbInput(b, n);
    putNode(graph, b);
    autoLayout(graph);
    return n;
}

int appendNode(QJsonObject &graph)
{
    return addSerial(graph, outputNode(graph));
}

int addParallel(QJsonObject &graph, int beside)
{
    return addMixed(graph, beside, QStringLiteral("parallele"));
}

int addLayer(QJsonObject &graph, int beside)
{
    return addMixed(graph, beside, QStringLiteral("calque"));
}

int addOutside(QJsonObject &graph, int after)
{
    if (!hasNode(graph, after)) {
        return -1;
    }
    const int n = nextId(graph);
    redirectConsumers(graph, after, n, -1);
    QJsonObject o = corrector(n, after);
    o.insert(kIn, QJsonObject{{QStringLiteral("rvb"), after}, {QStringLiteral("cle"), after}});
    o.insert(QStringLiteral("cle"), QJsonObject{{QStringLiteral("entree_inverser"), true}});
    putNode(graph, o);
    autoLayout(graph);
    return n;
}

int addSplitterCombiner(QJsonObject &graph, int after)
{
    if (after != 0 && !hasNode(graph, after)) {
        return -1;
    }
    const int s = nextId(graph);
    const int c0 = s + 1, c1 = s + 2, c2 = s + 3, comb = s + 4;
    redirectConsumers(graph, after, comb, -1);
    putNode(graph, QJsonObject{{kId, s}, {kType, QStringLiteral("separateur")}, {QStringLiteral("actif"), true}, {kIn, QJsonObject{{QStringLiteral("rvb"), after}}}});
    for (int k = 0; k < 3; ++k) {
        QJsonObject c = corrector(s + 1 + k, s);
        c.insert(kIn, QJsonObject{{QStringLiteral("rvb"), s}, {QStringLiteral("canal"), k}});
        putNode(graph, c);
    }
    putNode(graph, QJsonObject{{kId, comb}, {kType, QStringLiteral("combineur")}, {QStringLiteral("actif"), true}, {kIns, QJsonArray{c0, c1, c2}}});
    autoLayout(graph);
    return c0;
}

bool deleteNode(QJsonObject &graph, int id)
{
    const QJsonObject n = node(graph, id);
    if (n.isEmpty()) {
        return false;
    }
    const QList<int> ins = rgbInputs(n);
    const int replacement = ins.isEmpty() ? 0 : ins.first();
    // Les mélangeurs qui lisaient ce nœud le perdent ; les autres lisent désormais son entrée.
    QJsonArray list = nodes(graph);
    QJsonArray kept;
    for (const QJsonValue &v : list) {
        QJsonObject o = v.toObject();
        if (o.value(kId).toInt() == id) {
            continue;
        }
        if (isMixer(o) && !isMixer(n)) {
            QJsonArray mi = o.value(kIns).toArray();
            QJsonArray mo;
            for (const QJsonValue &x : mi) {
                if (x.toInt() != id) {
                    mo.append(x);
                }
            }
            o.insert(kIns, mo);
        }
        QJsonObject in = o.value(kIn).toObject();
        if (in.value(QStringLiteral("cle")).toInt(-1) == id) {
            in.remove(QStringLiteral("cle"));
            o.insert(kIn, in);
        }
        kept.append(o);
    }
    graph.insert(kNodes, kept);
    redirectConsumers(graph, id, replacement, -1);
    // Un mélangeur réduit à une seule entrée disparaît : sa branche reprend sa place.
    for (const QJsonValue &v : nodes(graph)) {
        const QJsonObject m = v.toObject();
        if (isMixer(m) && m.value(kType).toString() != QLatin1String("combineur") && m.value(kIns).toArray().size() <= 1) {
            const int mid = m.value(kId).toInt();
            const int only = m.value(kIns).toArray().isEmpty() ? replacement : m.value(kIns).toArray().at(0).toInt();
            QJsonArray rest;
            for (const QJsonValue &x : nodes(graph)) {
                if (x.toObject().value(kId).toInt() != mid) {
                    rest.append(x);
                }
            }
            graph.insert(kNodes, rest);
            redirectConsumers(graph, mid, only, -1);
            break;
        }
    }
    autoLayout(graph);
    return true;
}

bool connectNodes(QJsonObject &graph, int from, int to, bool key, int mixerSlot)
{
    QJsonObject t = node(graph, to);
    if (t.isEmpty() || (from != 0 && !hasNode(graph, from)) || from == to) {
        return false;
    }
    if (from != 0 && isUpstream(graph, to, from)) {
        return false; // une boucle
    }
    if (isMixer(t) && !key) {
        QJsonArray ins = t.value(kIns).toArray();
        if (mixerSlot >= 0 && mixerSlot < ins.size()) {
            ins[mixerSlot] = from;
        } else {
            ins.append(from);
        }
        t.insert(kIns, ins);
    } else {
        QJsonObject in = t.value(kIn).toObject();
        in.insert(key ? QStringLiteral("cle") : QStringLiteral("rvb"), from);
        t.insert(kIn, in);
    }
    putNode(graph, t);
    return true;
}

bool disconnectInput(QJsonObject &graph, int to, bool key)
{
    QJsonObject t = node(graph, to);
    if (t.isEmpty()) {
        return false;
    }
    QJsonObject in = t.value(kIn).toObject();
    in.remove(key ? QStringLiteral("cle") : QStringLiteral("rvb"));
    t.insert(kIn, in);
    putNode(graph, t);
    return true;
}

void resetNode(QJsonObject &graph, int id)
{
    QJsonObject n = node(graph, id);
    if (n.isEmpty()) {
        return;
    }
    QJsonObject kept;
    for (const QString &k : {kId, kType, QStringLiteral("etiquette"), QStringLiteral("actif"), QStringLiteral("x"), QStringLiteral("y"), kIn, kIns, QStringLiteral("mode")}) {
        if (n.contains(k)) {
            kept.insert(k, n.value(k));
        }
    }
    // La clé d'un nœud outside fait partie de sa structure : elle reste inversée.
    if (n.value(QStringLiteral("cle")).toObject().value(QStringLiteral("entree_inverser")).toBool()) {
        kept.insert(QStringLiteral("cle"), QJsonObject{{QStringLiteral("entree_inverser"), true}});
    }
    putNode(graph, kept);
}

void resetAll(QJsonObject &graph, bool keepNodes)
{
    if (!keepNodes) {
        graph = defaultGraph();
        return;
    }
    for (int id : nodeIds(graph)) {
        resetNode(graph, id);
    }
    graph.insert(QStringLiteral("actif"), true);
}

void appendGraph(QJsonObject &graph, const QJsonObject &other)
{
    const int base = nextId(graph);
    const int out = outputNode(graph);
    QHash<int, int> map;
    map.insert(0, out);
    int k = 0;
    for (int id : nodeIds(other)) {
        map.insert(id, base + k++);
    }
    for (const QJsonValue &v : nodes(other)) {
        QJsonObject n = v.toObject();
        n.insert(kId, map.value(n.value(kId).toInt()));
        if (n.contains(kIn)) {
            QJsonObject in = n.value(kIn).toObject();
            in.insert(QStringLiteral("rvb"), map.value(in.value(QStringLiteral("rvb")).toInt(0), out));
            if (in.contains(QStringLiteral("cle"))) {
                in.insert(QStringLiteral("cle"), map.value(in.value(QStringLiteral("cle")).toInt()));
            }
            n.insert(kIn, in);
        }
        if (n.contains(kIns)) {
            QJsonArray ins;
            for (const QJsonValue &i : n.value(kIns).toArray()) {
                ins.append(map.value(i.toInt(), out));
            }
            n.insert(kIns, ins);
        }
        putNode(graph, n);
    }
    graph.insert(kOut, map.value(outputNode(other), out));
    autoLayout(graph);
}

void autoLayout(QJsonObject &graph)
{
    // Colonne = plus long chemin depuis la source ; rangée = ordre de visite depuis la sortie, les
    // entrées d'un mélangeur de haut en bas (comme Resolve : la première entrée en haut).
    QHash<int, int> depth;
    std::function<int(int)> dep = [&](int id) -> int {
        if (id == 0) {
            return 0;
        }
        if (depth.contains(id)) {
            return depth.value(id);
        }
        depth.insert(id, 1); // garde contre une boucle
        int d = 0;
        const QJsonObject n = node(graph, id);
        for (int in : rgbInputs(n)) {
            d = std::max(d, dep(in));
        }
        const QJsonValue key = n.value(kIn).toObject().value(QStringLiteral("cle"));
        if (key.isDouble()) {
            d = std::max(d, dep(key.toInt()));
        }
        depth.insert(id, d + 1);
        return d + 1;
    };
    for (int id : nodeIds(graph)) {
        dep(id);
    }
    QHash<int, int> row;
    QHash<int, int> nextRow; // par colonne
    QSet<int> visited;
    std::function<void(int, int)> place = [&](int id, int wanted) {
        if (id == 0 || visited.contains(id)) {
            return;
        }
        visited.insert(id);
        const int col = depth.value(id);
        const int r = std::max(wanted, nextRow.value(col, 0));
        row.insert(id, r);
        nextRow.insert(col, r + 1);
        const QList<int> ins = rgbInputs(node(graph, id));
        for (int i = 0; i < ins.size(); ++i) {
            place(ins.at(i), r + i);
        }
    };
    place(outputNode(graph), 0);
    // Les nœuds qui ne mènent pas à la sortie (débranchés) vont en dessous.
    int spare = 0;
    for (int r : row) {
        spare = std::max(spare, r + 1);
    }
    for (int id : nodeIds(graph)) {
        if (!row.contains(id)) {
            row.insert(id, spare++);
        }
    }
    QJsonArray list = nodes(graph);
    for (int i = 0; i < list.size(); ++i) {
        QJsonObject n = list.at(i).toObject();
        const int id = n.value(kId).toInt();
        n.insert(QStringLiteral("x"), depth.value(id));
        n.insert(QStringLiteral("y"), row.value(id));
        list[i] = n;
    }
    graph.insert(kNodes, list);
}

QJsonValue value(const QJsonObject &graph, int id, const QString &section, const QString &key)
{
    return node(graph, id).value(section).toObject().value(key);
}

void setValue(QJsonObject &graph, int id, const QString &section, const QString &key, const QJsonValue &v)
{
    QJsonObject n = node(graph, id);
    if (n.isEmpty()) {
        return;
    }
    QJsonObject s = n.value(section).toObject();
    s.insert(key, v);
    n.insert(section, s);
    putNode(graph, n);
}

void setSection(QJsonObject &graph, int id, const QString &section, const QJsonValue &v)
{
    QJsonObject n = node(graph, id);
    if (n.isEmpty()) {
        return;
    }
    if (v.isNull() || v.isUndefined()) {
        n.remove(section);
    } else {
        n.insert(section, v);
    }
    putNode(graph, n);
}

void setField(QJsonObject &graph, int id, const QString &key, const QJsonValue &v)
{
    setSection(graph, id, key, v);
}

QStringList usedTools(const QJsonObject &n)
{
    QStringList tools;
    const QJsonObject p = n.value(QStringLiteral("primaires")).toObject();
    bool prim = false;
    for (auto it = p.begin(); it != p.end(); ++it) {
        const QString k = it.key();
        const QJsonValue v = it.value();
        static const QHash<QString, double> neutral{{QStringLiteral("contraste"), 1.},      {QStringLiteral("pivot"), 0.435},  {QStringLiteral("saturation"), 50.},
                                                    {QStringLiteral("teinte"), 50.},        {QStringLiteral("lum_mix"), 100.}, {QStringLiteral("temp"), 0.},
                                                    {QStringLiteral("tint"), 0.},           {QStringLiteral("boost"), 0.},     {QStringLiteral("mid_detail"), 0.},
                                                    {QStringLiteral("ombres"), 0.},         {QStringLiteral("hautes_lumieres"), 0.}};
        if (v.isArray()) {
            const double def = k == QLatin1String("gain") ? 1. : (k == QLatin1String("offset") ? 25. : 0.);
            for (const QJsonValue &x : v.toArray()) {
                prim = prim || std::abs(x.toDouble() - def) > 1e-9;
            }
        } else if (neutral.contains(k)) {
            prim = prim || std::abs(v.toDouble() - neutral.value(k)) > 1e-9;
        }
    }
    if (prim) {
        tools << QStringLiteral("primaires");
    }
    const QJsonObject l = n.value(QStringLiteral("log")).toObject();
    for (const QString &k : {QStringLiteral("ombres"), QStringLiteral("tons_moyens"), QStringLiteral("hautes_lumieres")}) {
        for (const QJsonValue &x : l.value(k).toArray()) {
            if (x.toDouble() != 0.) {
                tools << QStringLiteral("log");
                break;
            }
        }
        if (tools.contains(QStringLiteral("log"))) {
            break;
        }
    }
    const QJsonObject c = n.value(QStringLiteral("courbes")).toObject();
    for (const QString &k : {QStringLiteral("y"), QStringLiteral("r"), QStringLiteral("v"), QStringLiteral("b")}) {
        bool diag = true;
        for (const QJsonValue &pt : c.value(k).toArray()) {
            const QJsonArray a = pt.toArray();
            diag = diag && std::abs(a.at(0).toDouble() - a.at(1).toDouble()) < 1e-6;
        }
        if (!diag) {
            tools << QStringLiteral("courbes");
            break;
        }
    }
    const QJsonObject h = n.value(QStringLiteral("courbes_teinte")).toObject();
    for (auto it = h.begin(); it != h.end(); ++it) {
        if (!it.value().toArray().isEmpty()) {
            tools << QStringLiteral("courbes_teinte");
            break;
        }
    }
    if (n.value(QStringLiteral("qualificateur")).toObject().value(QStringLiteral("actif")).toBool(false)) {
        tools << QStringLiteral("qualificateur");
    }
    if (!n.value(QStringLiteral("fenetres")).toArray().isEmpty()) {
        tools << QStringLiteral("fenetres");
    }
    if (std::abs(n.value(QStringLiteral("flou")).toObject().value(QStringLiteral("rayon")).toDouble(0.5) - 0.5) > 1e-9) {
        tools << QStringLiteral("flou");
    }
    const QJsonObject cst = n.value(QStringLiteral("cst")).toObject();
    if (!cst.isEmpty() && cst.value(QStringLiteral("actif")).toBool(true)) {
        tools << QStringLiteral("cst");
    }
    if (!n.value(QStringLiteral("lut")).toObject().value(QStringLiteral("fichier")).toString().isEmpty()) {
        tools << QStringLiteral("lut");
    }
    return tools;
}

} // namespace KoraCutNodeGraph
