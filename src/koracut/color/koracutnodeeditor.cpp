/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutnodeeditor.h"
#include "koracutcolorwidgets.h"
#include "koracutgradecontroller.h"
#include "koracutnodegraph.h"

#include <KLocalizedString>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#include <QWheelEvent>

using namespace KoraCutNodeGraph;
using KoraCutColorUi::tone;

namespace {
constexpr int kBar = 30;          // barre du haut (Plan / Séquence, vignettes)
constexpr double kUnitX = 150.;   // une colonne
constexpr double kUnitY = 108.;   // une rangée
constexpr double kNodeW = 104.;
constexpr double kNodeHThumb = 82.;
constexpr double kNodeH = 42.;
constexpr double kMixerW = 34.;
const QColor kRgb(0x4A, 0xDE, 0x80);  // entrées et sorties RVB (vert, comme dans Resolve)
const QColor kKey(0x38, 0xB6, 0xFF);  // entrées et sorties de clé (bleu)

QString nodeNumber(int n)
{
    return n < 10 ? QStringLiteral("0%1").arg(n) : QString::number(n);
}

QString toolLetters(const QStringList &tools)
{
    static const QHash<QString, QString> short_{{QStringLiteral("primaires"), QStringLiteral("P")}, {QStringLiteral("log"), QStringLiteral("L")},
                                                 {QStringLiteral("courbes"), QStringLiteral("C")},   {QStringLiteral("courbes_teinte"), QStringLiteral("T")},
                                                 {QStringLiteral("qualificateur"), QStringLiteral("Q")}, {QStringLiteral("fenetres"), QStringLiteral("F")},
                                                 {QStringLiteral("flou"), QStringLiteral("B")},      {QStringLiteral("cst"), QStringLiteral("CST")},
                                                 {QStringLiteral("lut"), QStringLiteral("LUT")}};
    QStringList out;
    for (const QString &t : tools) {
        out << short_.value(t, t.left(1).toUpper());
    }
    return out.join(QLatin1Char(' '));
}
} // namespace

KoraCutNodeEditor::KoraCutNodeEditor(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(260, 180);
    auto *bar = new QWidget(this);
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(8, 3, 8, 3);
    lay->setSpacing(4);
    auto makeButton = [this, bar](const QString &text, const QString &tip, bool checkable) {
        auto *b = new QToolButton(bar);
        b->setText(text);
        b->setToolTip(tip);
        b->setCheckable(checkable);
        b->setAutoRaise(true);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        return b;
    };
    // Plan ou Séquence : les nœuds du plan, ou ceux qui s'appliquent à toute la séquence (Timeline de Resolve).
    m_scopeClip = makeButton(i18nc("@action:button nodes of the current clip", "Clip"), i18nc("@info:tooltip", "The grade of the current clip"), true);
    m_scopeTimeline =
        makeButton(i18nc("@action:button nodes applied to the whole sequence", "Sequence"), i18nc("@info:tooltip", "The grade applied to the whole sequence, after the clips"), true);
    m_scopeClip->setChecked(true);
    lay->addWidget(m_scopeClip);
    lay->addWidget(m_scopeTimeline);
    lay->addStretch(1);
    m_thumbButton = makeButton(i18nc("@action:button show node thumbnails", "Thumbnails"), i18nc("@info:tooltip", "Show the image of each node"), true);
    m_thumbButton->setChecked(true);
    lay->addWidget(m_thumbButton);
    auto *tidy = makeButton(i18nc("@action:button tidy the node graph", "Tidy"), i18nc("@info:tooltip", "Tidy up the node graph"), false);
    lay->addWidget(tidy);
    bar->setGeometry(0, 0, width(), kBar);
    bar->setObjectName(QStringLiteral("koracutNodeBar"));

    connect(m_scopeClip, &QToolButton::clicked, this, [this]() {
        KoraCutGradeController::self()->setScope(KoraCutGradeController::Scope::Clip);
    });
    connect(m_scopeTimeline, &QToolButton::clicked, this, [this]() {
        KoraCutGradeController::self()->setScope(KoraCutGradeController::Scope::Timeline);
    });
    connect(m_thumbButton, &QToolButton::toggled, this, [this](bool on) {
        m_thumbs = on;
        refreshThumbnails();
        update();
    });
    connect(tidy, &QToolButton::clicked, this, &KoraCutNodeEditor::cleanUp);

    m_thumbTimer.setSingleShot(true);
    m_thumbTimer.setInterval(180);
    connect(&m_thumbTimer, &QTimer::timeout, this, &KoraCutNodeEditor::refreshThumbnails);

    auto *c = KoraCutGradeController::self();
    connect(c, &KoraCutGradeController::graphChanged, this, &KoraCutNodeEditor::reload);
    connect(c, &KoraCutGradeController::clipChanged, this, [this]() {
        m_thumbCache.clear();
        reload();
    });
    connect(c, &KoraCutGradeController::nodeChanged, this, [this](int id) {
        m_current = id;
        update();
    });
    reload();
}

void KoraCutNodeEditor::reload()
{
    auto *c = KoraCutGradeController::self();
    m_graph = c->graph();
    m_current = c->currentNode();
    if (!hasNode(m_graph, m_current)) {
        const QList<int> ids = nodeIds(m_graph);
        m_current = ids.isEmpty() ? 0 : ids.first();
    }
    m_scopeClip->setChecked(c->scope() == KoraCutGradeController::Scope::Clip);
    m_scopeTimeline->setChecked(c->scope() == KoraCutGradeController::Scope::Timeline);
    m_thumbTimer.start();
    fitView();
}

void KoraCutNodeEditor::refreshThumbnails()
{
    m_thumbCache.clear();
    auto *c = KoraCutGradeController::self();
    if (!m_thumbs || c->currentClip() < 0) {
        update();
        return;
    }
    // Chaque vignette montre l'image à la sortie de son nœud (le grade jusqu'à lui).
    const int w = int(std::round((kNodeW - 8) * devicePixelRatioF()));
    for (int id : nodeIds(m_graph)) {
        const QJsonObject n = node(m_graph, id);
        if (!isCorrector(n)) {
            continue;
        }
        QImage img = c->clipImage(c->currentClip(), w, true, m_graph, id);
        img.setDevicePixelRatio(devicePixelRatioF());
        m_thumbCache.insert(id, img);
    }
    update();
}

void KoraCutNodeEditor::resizeEvent(QResizeEvent *)
{
    if (auto *bar = findChild<QWidget *>(QStringLiteral("koracutNodeBar"))) {
        bar->setGeometry(0, 0, width(), kBar);
    }
    fitView();
}

QRectF KoraCutNodeEditor::nodeRect(const QJsonObject &n) const
{
    const double x = n.value(QStringLiteral("x")).toDouble(1.);
    const double y = n.value(QStringLiteral("y")).toDouble(0.);
    const bool corr = isCorrector(n) || n.value(QStringLiteral("type")).toString() == QLatin1String("separateur");
    const double h = corr ? (m_thumbs ? kNodeHThumb : kNodeH) : std::max(40., 18. + 18. * std::max<qsizetype>(2, n.value(QStringLiteral("entrees")).toArray().size()));
    const double w = corr ? kNodeW : kMixerW;
    const double left = 70. + (x - 1.) * kUnitX + (corr ? 0. : (kNodeW - kMixerW) / 2.);
    const double top = kBar + 40. + y * kUnitY;
    return QRectF(left * m_zoom + m_pan.x(), (top - kBar) * m_zoom + kBar + m_pan.y(), w * m_zoom, h * m_zoom);
}

QPointF KoraCutNodeEditor::sourcePos() const
{
    return QPointF(18, (kBar + 40. + (m_thumbs ? kNodeHThumb : kNodeH) / 2. - kBar) * m_zoom + kBar + m_pan.y());
}

QPointF KoraCutNodeEditor::outputPos() const
{
    const QJsonObject out = node(m_graph, outputNode(m_graph));
    const double y = out.isEmpty() ? sourcePos().y() : nodeRect(out).center().y();
    return QPointF(width() - 18, y);
}

QRectF KoraCutNodeEditor::numberRect(const QRectF &r) const
{
    return QRectF(r.left() + 4, r.bottom() - 16 * m_zoom, 24 * m_zoom, 14 * m_zoom);
}

QPointF KoraCutNodeEditor::portPos(const Port &p) const
{
    if (p.node == -1) {
        return sourcePos();
    }
    if (p.node == -3) {
        return outputPos();
    }
    const QJsonObject n = node(m_graph, p.node);
    const QRectF r = nodeRect(n);
    const QString type = n.value(QStringLiteral("type")).toString(QStringLiteral("correcteur"));
    if (type == QLatin1String("separateur") && p.output) {
        return QPointF(r.right(), r.top() + r.height() * (p.slot + 1) / 4.);
    }
    if (isMixer(n)) {
        if (p.output) {
            return QPointF(r.right(), r.center().y());
        }
        return QPointF(r.left(), r.top() + (18. + 18. * std::max(0, p.slot)) * m_zoom);
    }
    const double dy = p.key ? r.height() - 12 * m_zoom : 14 * m_zoom;
    return QPointF(p.output ? r.right() : r.left(), r.top() + dy);
}

KoraCutNodeEditor::Port KoraCutNodeEditor::portAt(const QPointF &pos) const
{
    const double tol = 8.;
    Port best;
    double bestD = tol;
    auto consider = [&](const Port &p) {
        const double d = QLineF(portPos(p), pos).length();
        if (d < bestD) {
            bestD = d;
            best = p;
        }
    };
    consider(Port{-1, true, false, -1});
    consider(Port{-3, false, false, -1});
    for (const QJsonValue &v : nodes(m_graph)) {
        const QJsonObject n = v.toObject();
        const int id = n.value(QStringLiteral("id")).toInt();
        const QString type = n.value(QStringLiteral("type")).toString(QStringLiteral("correcteur"));
        if (isMixer(n)) {
            const int count = int(n.value(QStringLiteral("entrees")).toArray().size());
            for (int i = 0; i <= count; ++i) { // une entrée libre de plus en bas
                consider(Port{id, false, type == QLatin1String("melangeur_cles"), i});
            }
            consider(Port{id, true, type == QLatin1String("melangeur_cles"), -1});
        } else if (type == QLatin1String("separateur")) {
            consider(Port{id, false, false, -1});
            for (int k = 0; k < 3; ++k) {
                consider(Port{id, true, false, k});
            }
        } else {
            consider(Port{id, false, false, -1});
            consider(Port{id, false, true, -1});
            consider(Port{id, true, false, -1});
            consider(Port{id, true, true, -1});
        }
    }
    return best;
}

int KoraCutNodeEditor::nodeAt(const QPointF &pos) const
{
    for (const QJsonValue &v : nodes(m_graph)) {
        const QJsonObject n = v.toObject();
        if (nodeRect(n).contains(pos)) {
            return n.value(QStringLiteral("id")).toInt();
        }
    }
    return -1;
}

void KoraCutNodeEditor::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), tone("contenu"));
    // Une grille de points discrète.
    p.setPen(QPen(tone("bordure"), 1));
    const double step = 24. * m_zoom;
    for (double x = std::fmod(m_pan.x(), step); x < width(); x += step) {
        for (double y = kBar + std::fmod(m_pan.y(), step); y < height(); y += step) {
            p.drawPoint(QPointF(x, y));
        }
    }
    p.fillRect(QRect(0, 0, width(), kBar), tone("panneau"));
    auto *c = KoraCutGradeController::self();
    if (c->scope() == KoraCutGradeController::Scope::Clip && c->currentClip() < 0) {
        p.setPen(tone("texte_secondaire"));
        p.drawText(rect().adjusted(0, kBar, 0, 0), Qt::AlignCenter,
                   i18nc("@info empty state of the node editor", "No clip under the playhead.\nMove the playhead onto a clip to grade it."));
        return;
    }
    const bool bypass = !m_graph.value(QStringLiteral("actif")).toBool(true);

    // Les liens.
    auto link = [&](const QPointF &a, const QPointF &b, const QColor &col) {
        QPainterPath path(a);
        const double dx = std::max(30., (b.x() - a.x()) / 2.);
        path.cubicTo(a + QPointF(dx, 0), b - QPointF(dx, 0), b);
        p.setPen(QPen(col, 2.2 * std::min(1., m_zoom + 0.2), Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    };
    const QColor linkColor = tone("texte_secondaire");
    for (const QJsonValue &v : nodes(m_graph)) {
        const QJsonObject n = v.toObject();
        const int id = n.value(QStringLiteral("id")).toInt();
        auto fromPort = [&](int src, bool key) {
            if (src == 0) {
                return Port{-1, true, false, -1};
            }
            const QJsonObject s = node(m_graph, src);
            const int canal = n.value(QStringLiteral("entree")).toObject().value(QStringLiteral("canal")).toInt(-1);
            return Port{src, true, key, s.value(QStringLiteral("type")).toString() == QLatin1String("separateur") ? std::max(0, canal) : -1};
        };
        if (isMixer(n)) {
            const QJsonArray ins = n.value(QStringLiteral("entrees")).toArray();
            const bool keyMixer = n.value(QStringLiteral("type")).toString() == QLatin1String("melangeur_cles");
            for (int i = 0; i < ins.size(); ++i) {
                const int src = ins.at(i).toInt();
                Port from = src == 0 ? Port{-1, true, false, -1} : Port{src, true, keyMixer, -1};
                link(portPos(from), portPos(Port{id, false, keyMixer, i}), keyMixer ? kKey : linkColor);
            }
        } else if (n.contains(QStringLiteral("entree"))) {
            const QJsonObject in = n.value(QStringLiteral("entree")).toObject();
            if (in.contains(QStringLiteral("rvb"))) {
                link(portPos(fromPort(in.value(QStringLiteral("rvb")).toInt(), false)), portPos(Port{id, false, false, -1}), linkColor);
            }
            if (in.value(QStringLiteral("cle")).isDouble()) {
                link(portPos(Port{in.value(QStringLiteral("cle")).toInt(), true, true, -1}), portPos(Port{id, false, true, -1}), kKey);
            }
        }
    }
    const int out = outputNode(m_graph);
    link(out == 0 ? sourcePos() : portPos(Port{out, true, false, -1}), outputPos(), linkColor);
    if (m_linking) {
        link(portPos(m_linkFrom), m_linkPos, m_linkFrom.key ? kKey : tone("accent"));
    }

    // Les nœuds.
    QFont small = font();
    if (small.pixelSize() > 0) {
        small.setPixelSize(std::max(9, small.pixelSize() - 1));
    } else {
        small.setPointSizeF(std::max(7.5, small.pointSizeF() - 1.));
    }
    for (const QJsonValue &v : nodes(m_graph)) {
        const QJsonObject n = v.toObject();
        const int id = n.value(QStringLiteral("id")).toInt();
        const QRectF r = nodeRect(n);
        const bool enabled = n.value(QStringLiteral("actif")).toBool(true) && !bypass;
        const bool current = id == m_current;
        const QString type = n.value(QStringLiteral("type")).toString(QStringLiteral("correcteur"));
        p.setOpacity(enabled ? 1. : 0.55);
        p.setPen(QPen(current ? tone("accent") : tone("bordure"), current ? 2. : 1.));
        p.setBrush(tone("surface"));
        p.drawRoundedRect(r, 6, 6);
        if (isMixer(n)) {
            // Parallèle : deux barres ; calque : trois lignes empilées ; combineur : R V B.
            p.setPen(QPen(tone("texte"), 1.4));
            const QPointF ctr = r.center();
            if (type == QLatin1String("parallele")) {
                p.drawLine(ctr + QPointF(-4, -8), ctr + QPointF(-4, 8));
                p.drawLine(ctr + QPointF(4, -8), ctr + QPointF(4, 8));
            } else if (type == QLatin1String("calque")) {
                for (int k = -1; k <= 1; ++k) {
                    p.drawLine(ctr + QPointF(-8, k * 6), ctr + QPointF(8, k * 6));
                }
            } else {
                p.setFont(small);
                p.drawText(r, Qt::AlignCenter, type == QLatin1String("combineur") ? QStringLiteral("RVB") : QStringLiteral("CLÉ"));
            }
            const int count = int(n.value(QStringLiteral("entrees")).toArray().size());
            const bool keyMixer = type == QLatin1String("melangeur_cles");
            for (int i = 0; i < count; ++i) {
                const QPointF pp = portPos(Port{id, false, keyMixer, i});
                p.setPen(Qt::NoPen);
                p.setBrush(keyMixer ? kKey : kRgb);
                p.drawPolygon(QPolygonF{pp + QPointF(-6, -5), pp + QPointF(-6, 5), pp + QPointF(1, 0)});
            }
            const QPointF op = portPos(Port{id, true, keyMixer, -1});
            p.setBrush(keyMixer ? kKey : kRgb);
            p.drawRect(QRectF(op.x() - 1, op.y() - 4, 7, 8));
            p.setOpacity(1.);
            continue;
        }
        // La vignette.
        if (m_thumbs && type == QLatin1String("correcteur")) {
            const QRectF tr(r.left() + 4, r.top() + 4, r.width() - 8, r.height() - 24 * m_zoom);
            const QImage img = m_thumbCache.value(id);
            p.save();
            QPainterPath clip;
            clip.addRoundedRect(tr, 3, 3);
            p.setClipPath(clip);
            if (!img.isNull()) {
                p.drawImage(tr, img);
            } else {
                p.fillRect(tr, tone("champ"));
            }
            p.restore();
        }
        // Le numéro (cliquer le coupe ou le rallume, comme dans Resolve) et les palettes utilisées.
        p.setFont(small);
        const QRectF nr = numberRect(r);
        p.setPen(n.value(QStringLiteral("actif")).toBool(true) ? tone("texte") : tone("rouge"));
        if (type == QLatin1String("separateur")) {
            p.drawText(r, Qt::AlignCenter, i18nc("@label node that splits R G B", "Split"));
        } else {
            p.drawText(nr, Qt::AlignLeft | Qt::AlignVCenter, nodeNumber(displayNumber(m_graph, id)));
            p.setPen(tone("texte_secondaire"));
            p.drawText(QRectF(nr.right(), nr.top(), r.right() - nr.right() - 4, nr.height()), Qt::AlignRight | Qt::AlignVCenter, toolLetters(usedTools(n)));
        }
        // L'étiquette au-dessus du nœud.
        const QString label = n.value(QStringLiteral("etiquette")).toString();
        if (!label.isEmpty()) {
            p.setPen(tone("texte_secondaire"));
            p.drawText(QRectF(r.left() - 20, r.top() - 18, r.width() + 40, 16), Qt::AlignCenter, label);
        }
        // Entrées (triangles) et sorties (carrés) : vert pour l'image, bleu pour la clé.
        p.setPen(Qt::NoPen);
        if (type == QLatin1String("separateur")) {
            const QPointF ip = portPos(Port{id, false, false, -1});
            p.setBrush(kRgb);
            p.drawPolygon(QPolygonF{ip + QPointF(-6, -5), ip + QPointF(-6, 5), ip + QPointF(1, 0)});
            const QColor chans[3] = {QColor(0xFF, 0x72, 0x62), kRgb, QColor(0x0D, 0x99, 0xFF)};
            for (int k = 0; k < 3; ++k) {
                const QPointF op = portPos(Port{id, true, false, k});
                p.setBrush(chans[k]);
                p.drawRect(QRectF(op.x() - 1, op.y() - 4, 7, 8));
            }
            p.setOpacity(1.);
            continue;
        }
        for (bool key : {false, true}) {
            const QPointF ip = portPos(Port{id, false, key, -1});
            p.setBrush(key ? kKey : kRgb);
            p.drawPolygon(QPolygonF{ip + QPointF(-6, -5), ip + QPointF(-6, 5), ip + QPointF(1, 0)});
            const QPointF op = portPos(Port{id, true, key, -1});
            p.drawRect(QRectF(op.x() - 1, op.y() - 4, 7, 8));
        }
        p.setOpacity(1.);
    }
    // La source et la sortie : deux pastilles vertes aux bords.
    p.setPen(QPen(tone("contenu"), 2));
    p.setBrush(kRgb);
    p.drawEllipse(sourcePos(), 6, 6);
    p.drawEllipse(outputPos(), 6, 6);
    if (bypass) {
        p.setPen(tone("jaune"));
        p.drawText(QRectF(0, kBar + 4, width() - 10, 18), Qt::AlignRight | Qt::AlignVCenter, i18nc("@info the whole grade is switched off", "Grade off"));
    }
}

void KoraCutNodeEditor::selectNode(int id)
{
    m_current = id;
    KoraCutGradeController::self()->setCurrentNode(id);
    update();
}

void KoraCutNodeEditor::commit(const QJsonObject &graph, const QString &gesture)
{
    m_graph = graph;
    // Le nœud courant d'abord : l'écriture du graphe recharge l'éditeur depuis le contrôleur.
    KoraCutGradeController::self()->setCurrentNode(m_current);
    KoraCutGradeController::self()->edit(graph, gesture, true);
    fitView();
}

void KoraCutNodeEditor::mousePressEvent(QMouseEvent *e)
{
    setFocus();
    const QPointF pos = e->position();
    if (pos.y() < kBar) {
        return;
    }
    if (e->button() == Qt::MiddleButton || (e->button() == Qt::LeftButton && e->modifiers() & Qt::AltModifier && nodeAt(pos) < 0)) {
        m_panning = true;
        m_panStart = pos;
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton) {
        return;
    }
    const Port port = portAt(pos);
    if (port.node != -2) {
        if (port.output) {
            m_linking = true;
            m_linkFrom = port;
            m_linkPos = pos;
            return;
        }
        // Tirer sur une entrée la détache (on peut la rebrancher ailleurs).
        if (port.node >= 0) {
            QJsonObject g = m_graph;
            const QJsonObject n = node(g, port.node);
            if (isMixer(n)) {
                QJsonArray ins = n.value(QStringLiteral("entrees")).toArray();
                if (port.slot >= 0 && port.slot < ins.size()) {
                    const int src = ins.at(port.slot).toInt();
                    ins.removeAt(port.slot);
                    QJsonObject m = n;
                    m.insert(QStringLiteral("entrees"), ins);
                    putNode(g, m);
                    m_linking = true;
                    m_linkFrom = src == 0 ? Port{-1, true, false, -1} : Port{src, true, false, -1};
                    m_linkPos = pos;
                    m_graph = g;
                    update();
                    return;
                }
            } else {
                const QJsonObject in = n.value(QStringLiteral("entree")).toObject();
                const QString k = port.key ? QStringLiteral("cle") : QStringLiteral("rvb");
                if (in.contains(k) && (!port.key || in.value(k).isDouble())) {
                    const int src = in.value(k).toInt();
                    disconnectInput(g, port.node, port.key);
                    m_graph = g;
                    m_linking = true;
                    m_linkFrom = src == 0 ? Port{-1, true, false, -1} : Port{src, true, port.key, -1};
                    m_linkPos = pos;
                    update();
                    return;
                }
            }
        }
    }
    const int id = nodeAt(pos);
    if (id >= 0) {
        const QRectF r = nodeRect(node(m_graph, id));
        if (numberRect(r).contains(pos) && isCorrector(node(m_graph, id))) {
            selectNode(id);
            toggleCurrentNode();
            return;
        }
        selectNode(id);
        m_dragNode = id;
        m_dragOffset = pos - r.topLeft();
        m_moved = false;
    }
}

void KoraCutNodeEditor::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    if (m_panning) {
        m_pan += pos - m_panStart;
        m_panStart = pos;
        update();
        return;
    }
    if (m_linking) {
        m_linkPos = pos;
        update();
        return;
    }
    if (m_dragNode >= 0) {
        QJsonObject n = node(m_graph, m_dragNode);
        const QPointF tl = pos - m_dragOffset;
        const bool corr = !isMixer(n);
        const double left = (tl.x() - m_pan.x()) / m_zoom - (corr ? 0. : (kNodeW - kMixerW) / 2.);
        const double top = (tl.y() - kBar - m_pan.y()) / m_zoom + kBar;
        n.insert(QStringLiteral("x"), (left - 70.) / kUnitX + 1.);
        n.insert(QStringLiteral("y"), (top - kBar - 40.) / kUnitY);
        putNode(m_graph, n);
        m_moved = true;
        update();
        return;
    }
    const Port port = portAt(pos);
    setCursor(port.node != -2 ? Qt::PointingHandCursor : (nodeAt(pos) >= 0 ? Qt::OpenHandCursor : Qt::ArrowCursor));
}

void KoraCutNodeEditor::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_panning) {
        m_panning = false;
        setCursor(Qt::ArrowCursor);
        return;
    }
    if (m_linking) {
        m_linking = false;
        const Port to = portAt(e->position());
        QJsonObject g = m_graph;
        bool changed = false;
        const int from = m_linkFrom.node == -1 ? 0 : m_linkFrom.node;
        if (to.node == -3 && !m_linkFrom.key) {
            g.insert(QStringLiteral("sortie"), from);
            changed = true;
        } else if (to.node >= 0 && !to.output && to.node != from) {
            changed = connectNodes(g, from, to.node, to.key, to.slot);
            // Depuis un séparateur : le correcteur prend le canal de la sortie tirée.
            if (changed && m_linkFrom.node >= 0 && node(g, m_linkFrom.node).value(QStringLiteral("type")).toString() == QLatin1String("separateur")) {
                QJsonObject t = node(g, to.node);
                QJsonObject in = t.value(QStringLiteral("entree")).toObject();
                in.insert(QStringLiteral("canal"), std::max(0, m_linkFrom.slot));
                t.insert(QStringLiteral("entree"), in);
                putNode(g, t);
            }
        }
        // Même un lien lâché dans le vide est un geste (une entrée détachée).
        commit(changed ? g : m_graph, i18nc("@action undo text", "connect nodes"));
        return;
    }
    if (m_dragNode >= 0) {
        if (m_moved) {
            commit(m_graph, i18nc("@action undo text", "move node"));
        }
        m_dragNode = -1;
    }
}

void KoraCutNodeEditor::mouseDoubleClickEvent(QMouseEvent *e)
{
    const int id = nodeAt(e->position());
    if (id >= 0) {
        selectNode(id);
        renameCurrentNode();
    }
}

void KoraCutNodeEditor::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() & Qt::ControlModifier) {
        const double factor = std::pow(1.0015, e->angleDelta().y());
        const QPointF at = e->position();
        const double z = std::clamp(m_zoom * factor, 0.4, 2.0);
        m_pan = at - (at - m_pan) * (z / m_zoom);
        m_zoom = z;
    } else {
        m_pan += QPointF(e->pixelDelta().isNull() ? e->angleDelta() / 4 : e->pixelDelta());
    }
    update();
}

void KoraCutNodeEditor::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        deleteCurrentNode();
        return;
    }
    QWidget::keyPressEvent(e);
}

void KoraCutNodeEditor::contextMenuEvent(QContextMenuEvent *e)
{
    const int id = nodeAt(e->pos());
    if (id >= 0) {
        selectNode(id);
    }
    QMenu menu(this);
    const bool onNode = id >= 0;
    const QJsonObject n = node(m_graph, id);
    auto add = [&](const QString &text, const QString &keys, auto fn, bool enabled = true) {
        QAction *a = menu.addAction(text, this, fn);
        a->setShortcut(QKeySequence(keys));
        a->setShortcutVisibleInContextMenu(true);
        a->setEnabled(enabled);
    };
    const bool corr = onNode && isCorrector(n);
    add(i18nc("@action", "Add Serial Node"), QStringLiteral("Alt+S"), &KoraCutNodeEditor::addSerial, !onNode || corr || isMixer(n));
    add(i18nc("@action", "Add Serial Node Before"), QStringLiteral("Shift+S"), &KoraCutNodeEditor::addSerialBefore, corr);
    add(i18nc("@action", "Append Node"), QStringLiteral("Alt+K"), &KoraCutNodeEditor::appendNode);
    add(i18nc("@action", "Add Parallel Node"), QStringLiteral("Alt+P"), &KoraCutNodeEditor::addParallel, corr);
    add(i18nc("@action", "Add Layer Node"), QStringLiteral("Alt+L"), &KoraCutNodeEditor::addLayer, corr);
    add(i18nc("@action", "Add Outside Node"), QStringLiteral("Alt+O"), &KoraCutNodeEditor::addOutside, corr);
    add(i18nc("@action", "Add Splitter/Combiner Nodes"), QStringLiteral("Alt+Y"), &KoraCutNodeEditor::addSplitterCombiner, !onNode || corr);
    menu.addSeparator();
    add(i18nc("@action", "Enable or Disable Node"), QStringLiteral("Ctrl+D"), &KoraCutNodeEditor::toggleCurrentNode, corr);
    add(i18nc("@action", "Rename Node"), QString(), &KoraCutNodeEditor::renameCurrentNode, corr);
    add(i18nc("@action", "Reset Node Grade"), QStringLiteral("Shift+Home"), &KoraCutNodeEditor::resetCurrentNode, corr);
    add(i18nc("@action", "Delete Node"), QStringLiteral("Del"), &KoraCutNodeEditor::deleteCurrentNode, onNode);
    if (onNode && n.value(QStringLiteral("type")).toString() == QLatin1String("calque")) {
        QMenu *modes = menu.addMenu(i18nc("@title:menu blend mode of a layer mixer", "Composite Mode"));
        const QList<QPair<QString, QString>> list = {{QStringLiteral("normal"), i18nc("@item composite mode", "Normal")},
                                                     {QStringLiteral("addition"), i18nc("@item composite mode", "Add")},
                                                     {QStringLiteral("soustraction"), i18nc("@item composite mode", "Subtract")},
                                                     {QStringLiteral("difference"), i18nc("@item composite mode", "Difference")},
                                                     {QStringLiteral("multiplication"), i18nc("@item composite mode", "Multiply")},
                                                     {QStringLiteral("ecran"), i18nc("@item composite mode", "Screen")},
                                                     {QStringLiteral("incrustation"), i18nc("@item composite mode", "Overlay")},
                                                     {QStringLiteral("lumiere_douce"), i18nc("@item composite mode", "Soft Light")},
                                                     {QStringLiteral("lumiere_forte"), i18nc("@item composite mode", "Hard Light")},
                                                     {QStringLiteral("assombrir"), i18nc("@item composite mode", "Darken")},
                                                     {QStringLiteral("eclaircir"), i18nc("@item composite mode", "Lighten")},
                                                     {QStringLiteral("luminosite"), i18nc("@item composite mode", "Luminosity")},
                                                     {QStringLiteral("couleur"), i18nc("@item composite mode", "Color")}};
        const QString cur = n.value(QStringLiteral("mode")).toString(QStringLiteral("normal"));
        for (const auto &m : list) {
            QAction *a = modes->addAction(m.second);
            a->setCheckable(true);
            a->setChecked(m.first == cur);
            connect(a, &QAction::triggered, this, [this, id, m]() {
                QJsonObject g = m_graph;
                setField(g, id, QStringLiteral("mode"), m.first);
                commit(g, i18nc("@action undo text", "layer composite mode"));
            });
        }
    }
    menu.addSeparator();
    add(i18nc("@action", "Tidy Up Node Graph"), QString(), &KoraCutNodeEditor::cleanUp);
    menu.exec(e->globalPos());
}

void KoraCutNodeEditor::addSerial()
{
    QJsonObject g = m_graph;
    const int after = hasNode(g, m_current) ? m_current : outputNode(g);
    const int id = KoraCutNodeGraph::addSerial(g, after);
    if (id > 0) {
        m_current = id;
        commit(g, i18nc("@action undo text", "add serial node"));
    }
}

void KoraCutNodeEditor::addSerialBefore()
{
    QJsonObject g = m_graph;
    const int id = KoraCutNodeGraph::addSerialBefore(g, m_current);
    if (id > 0) {
        m_current = id;
        commit(g, i18nc("@action undo text", "add serial node before"));
    }
}

void KoraCutNodeEditor::appendNode()
{
    QJsonObject g = m_graph;
    const int id = KoraCutNodeGraph::appendNode(g);
    if (id > 0) {
        m_current = id;
        commit(g, i18nc("@action undo text", "append node"));
    }
}

void KoraCutNodeEditor::addParallel()
{
    QJsonObject g = m_graph;
    const int id = KoraCutNodeGraph::addParallel(g, m_current);
    if (id > 0) {
        m_current = id;
        commit(g, i18nc("@action undo text", "add parallel node"));
    }
}

void KoraCutNodeEditor::addLayer()
{
    QJsonObject g = m_graph;
    const int id = KoraCutNodeGraph::addLayer(g, m_current);
    if (id > 0) {
        m_current = id;
        commit(g, i18nc("@action undo text", "add layer node"));
    }
}

void KoraCutNodeEditor::addOutside()
{
    QJsonObject g = m_graph;
    const int id = KoraCutNodeGraph::addOutside(g, m_current);
    if (id > 0) {
        m_current = id;
        commit(g, i18nc("@action undo text", "add outside node"));
    }
}

void KoraCutNodeEditor::addSplitterCombiner()
{
    QJsonObject g = m_graph;
    const int id = KoraCutNodeGraph::addSplitterCombiner(g, hasNode(g, m_current) ? m_current : outputNode(g));
    if (id > 0) {
        m_current = id;
        commit(g, i18nc("@action undo text", "add splitter and combiner"));
    }
}

void KoraCutNodeEditor::toggleCurrentNode()
{
    QJsonObject g = m_graph;
    const QJsonObject n = node(g, m_current);
    if (n.isEmpty()) {
        return;
    }
    const bool on = !n.value(QStringLiteral("actif")).toBool(true);
    setField(g, m_current, QStringLiteral("actif"), on);
    commit(g, on ? i18nc("@action undo text", "enable node") : i18nc("@action undo text", "disable node"));
}

void KoraCutNodeEditor::resetCurrentNode()
{
    QJsonObject g = m_graph;
    resetNode(g, m_current);
    commit(g, i18nc("@action undo text", "reset node"));
}

void KoraCutNodeEditor::deleteCurrentNode()
{
    QJsonObject g = m_graph;
    if (!hasNode(g, m_current)) {
        return;
    }
    deleteNode(g, m_current);
    const QList<int> left = nodeIds(g);
    m_current = left.isEmpty() ? 0 : left.last();
    commit(g, i18nc("@action undo text", "delete node"));
}

void KoraCutNodeEditor::renameCurrentNode()
{
    const QJsonObject n = node(m_graph, m_current);
    if (n.isEmpty() || !isCorrector(n)) {
        return;
    }
    if (!m_rename) {
        m_rename = new QLineEdit(this);
        m_rename->setMaxLength(24);
        m_rename->setAlignment(Qt::AlignCenter);
        connect(m_rename, &QLineEdit::editingFinished, this, [this]() {
            if (!m_rename->isVisible()) {
                return;
            }
            m_rename->hide();
            QJsonObject g = m_graph;
            setField(g, m_rename->property("node").toInt(), QStringLiteral("etiquette"), m_rename->text().trimmed());
            commit(g, i18nc("@action undo text", "rename node"));
        });
    }
    const QRectF r = nodeRect(n);
    m_rename->setProperty("node", m_current);
    m_rename->setGeometry(QRectF(r.left() - 10, r.top() - 24, r.width() + 20, 22).toRect());
    m_rename->setText(n.value(QStringLiteral("etiquette")).toString());
    m_rename->show();
    m_rename->selectAll();
    m_rename->setFocus();
}

void KoraCutNodeEditor::cleanUp()
{
    QJsonObject g = m_graph;
    autoLayout(g);
    m_zoom = 1.;
    m_pan = QPointF(0, 0);
    commit(g, i18nc("@action undo text", "tidy node graph"));
}

void KoraCutNodeEditor::fitView()
{
    // Tout le graphe tient entre la source et la sortie : on réduit si besoin (jamais au-delà de 1).
    double maxX = 1., maxY = 0.;
    for (const QJsonValue &v : nodes(m_graph)) {
        maxX = std::max(maxX, v.toObject().value(QStringLiteral("x")).toDouble(1.));
        maxY = std::max(maxY, v.toObject().value(QStringLiteral("y")).toDouble(0.));
    }
    const double needW = 70. + (maxX - 1.) * kUnitX + kNodeW + 60.;
    const double needH = 40. + maxY * kUnitY + kNodeHThumb + 30.;
    const double z = std::min({1., (width() - 10.) / needW, (height() - kBar - 10.) / needH});
    m_zoom = std::max(0.45, z);
    m_pan = QPointF(0, 0);
    update();
}
