/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutcolorwidgets.h"

#include "kcops.h"
#include "koracut/koracutthemes.h"

#include <KLocalizedString>
#include <QApplication>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <cmath>

namespace KoraCutColorUi {

QColor tone(const char *token, int alpha)
{
    return KoraCutThemes::token(QString::fromLatin1(token), alpha);
}

namespace {
QFont smallFont(const QWidget *w, int delta = 0)
{
    // La police de l'interface est en pixels (11 px, charte) : on agrandit dans la même unité.
    QFont f = w->font();
    if (f.pixelSize() > 0) {
        f.setPixelSize(std::max(9, f.pixelSize() + delta));
    } else {
        f.setPointSizeF(std::max(8.0, f.pointSizeF() + delta));
    }
    return f;
}

bool fine(const QMouseEvent *e)
{
    return e->modifiers() & Qt::ShiftModifier;
}
} // namespace

// ---- ValueField -------------------------------------------------------------------------------------

ValueField::ValueField(const QString &label, double min, double max, double def, int decimals, double step, QWidget *parent)
    : QWidget(parent)
    , m_label(label)
    , m_min(min)
    , m_max(max)
    , m_def(def)
    , m_step(step)
    , m_decimals(decimals)
    , m_value(def)
{
    setCursor(Qt::SizeHorCursor);
    setFocusPolicy(Qt::ClickFocus);
    setMinimumHeight(24);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    setToolTip(i18nc("@info:tooltip", "Drag to adjust (Shift: fine). Double-click the value to type, the name to reset."));
}

void ValueField::setValue(double v)
{
    v = std::clamp(v, m_min, m_max);
    if (qFuzzyCompare(v + 1., m_value + 1.)) {
        return;
    }
    m_value = v;
    update();
}

void ValueField::setLabel(const QString &label)
{
    m_label = label;
    updateGeometry();
    update();
}

void ValueField::setRange(double min, double max)
{
    m_min = min;
    m_max = max;
    m_value = std::clamp(m_value, min, max);
    update();
}

QString ValueField::text() const
{
    return QLocale::c().toString(m_value, 'f', m_decimals);
}

QSize ValueField::sizeHint() const
{
    const QFontMetrics fm(font());
    const int labelW = m_label.isEmpty() ? 0 : fm.horizontalAdvance(m_label) + 6;
    const int valueW = fm.horizontalAdvance(QStringLiteral("-0000.000").left(std::max(5, int(QLocale::c().toString(m_max, 'f', m_decimals).size()) + 1))) + 12;
    return {labelW + valueW, std::max(24, fm.height() + 10)};
}

QRect ValueField::valueRect() const
{
    if (m_label.isEmpty()) {
        return rect().adjusted(0, 2, 0, -2);
    }
    const QFontMetrics fm(font());
    const int valueW = fm.horizontalAdvance(QStringLiteral("-0000.000").left(std::max(5, int(QLocale::c().toString(m_max, 'f', m_decimals).size()) + 1))) + 12;
    return QRect(width() - valueW, 2, valueW, height() - 4);
}

void ValueField::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRect vr = valueRect();
    if (!m_label.isEmpty()) {
        p.setPen(tone("texte_secondaire"));
        // Le libellé en entier : si la rangée est serrée, la police diminue (jamais de mot coupé).
        const QRect lr(0, 0, vr.left() - 4, height());
        QFont lf = font();
        for (int i = 0; i < 4 && QFontMetrics(lf).horizontalAdvance(m_label) > lr.width(); ++i) {
            if (lf.pixelSize() > 9) {
                lf.setPixelSize(lf.pixelSize() - 1);
            } else if (lf.pixelSize() <= 0 && lf.pointSizeF() > 8.) {
                lf.setPointSizeF(lf.pointSizeF() - 0.75);
            } else {
                break;
            }
        }
        p.setFont(lf);
        p.drawText(lr, Qt::AlignVCenter | Qt::AlignRight, m_label);
        p.setFont(font());
    }
    const bool changed = std::abs(m_value - m_def) > std::pow(10., -m_decimals) / 2.;
    // Sur un thème où le champ se distingue mal du panneau (thème clair), un filet le dessine.
    const QColor field = tone("champ");
    const bool faint = std::abs(field.lightnessF() - tone("panneau").lightnessF()) < 0.08;
    p.setPen(faint ? QPen(tone("bordure"), 1) : QPen(Qt::NoPen));
    p.setBrush(field);
    p.drawRoundedRect(QRectF(vr).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    p.setPen(changed ? tone("texte") : tone("texte_secondaire"));
    p.drawText(vr, Qt::AlignCenter, text());
    if (hasFocus()) {
        p.setPen(QPen(tone("accent"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(vr).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    }
}

void ValueField::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        return;
    }
    m_dragging = true;
    m_moved = false;
    m_pressValue = m_value;
    m_pressPos = e->position();
}

void ValueField::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging) {
        return;
    }
    const double dx = e->position().x() - m_pressPos.x();
    if (!m_moved && std::abs(dx) < 2) {
        return;
    }
    m_moved = true;
    const double v = std::clamp(m_pressValue + dx * (fine(e) ? m_step / 10. : m_step), m_min, m_max);
    const double rounded = std::round(v * std::pow(10., m_decimals)) / std::pow(10., m_decimals);
    if (rounded != m_value) {
        m_value = rounded;
        update();
        Q_EMIT valueChanged(m_value, false);
    }
}

void ValueField::mouseReleaseEvent(QMouseEvent *)
{
    if (m_dragging && m_moved) {
        Q_EMIT valueChanged(m_value, true);
    }
    m_dragging = false;
}

void ValueField::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (valueRect().contains(e->position().toPoint())) {
        startEdit();
        return;
    }
    // Double-clic sur le nom : le réglage revient à sa valeur par défaut (comme dans Resolve).
    m_value = m_def;
    update();
    Q_EMIT valueChanged(m_value, true);
}

void ValueField::wheelEvent(QWheelEvent *e)
{
    // La molette ne règle rien : on garde le défilement du panneau (un réglage ne doit pas bouger par accident).
    e->ignore();
}

void ValueField::startEdit()
{
    if (!m_edit) {
        m_edit = new QLineEdit(this);
        m_edit->setAlignment(Qt::AlignCenter);
        m_edit->setFrame(false);
        connect(m_edit, &QLineEdit::editingFinished, this, [this]() {
            bool ok = false;
            QString t = m_edit->text().trimmed();
            t.replace(QLatin1Char(','), QLatin1Char('.'));
            const double v = t.toDouble(&ok);
            m_edit->hide();
            if (ok) {
                m_value = std::clamp(v, m_min, m_max);
                update();
                Q_EMIT valueChanged(m_value, true);
            }
        });
    }
    m_edit->setGeometry(valueRect());
    m_edit->setText(text());
    m_edit->show();
    m_edit->selectAll();
    m_edit->setFocus();
}

// ---- Jog -------------------------------------------------------------------------------------------

Jog::Jog(QWidget *parent)
    : QWidget(parent)
{
    setCursor(Qt::SizeHorCursor);
    setToolTip(i18nc("@info:tooltip", "Master: drag left to darken, right to brighten (Shift: fine). Double-click to reset."));
}

void Jog::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 2, -1, -2);
    p.setPen(Qt::NoPen);
    p.setBrush(tone("champ"));
    p.drawRoundedRect(r, r.height() / 2, r.height() / 2);
    // Les crans de la molette : ils défilent quand on glisse, comme une vraie molette.
    p.setPen(QPen(m_down ? tone("texte") : tone("texte_secondaire"), 1));
    const double gap = 4.;
    for (double x = r.left() + 4 + std::fmod(m_phase, gap) + (m_phase < 0 ? gap : 0); x < r.right() - 3; x += gap) {
        p.drawLine(QPointF(x, r.top() + 3), QPointF(x, r.bottom() - 3));
    }
}

void Jog::mousePressEvent(QMouseEvent *e)
{
    m_down = true;
    m_last = e->position().x();
    update();
}

void Jog::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_down) {
        return;
    }
    const double dx = e->position().x() - m_last;
    m_last = e->position().x();
    m_phase += dx;
    update();
    Q_EMIT moved(dx * (fine(e) ? 0.2 : 1.), false);
}

void Jog::mouseReleaseEvent(QMouseEvent *)
{
    m_down = false;
    update();
    Q_EMIT moved(0., true);
}

void Jog::mouseDoubleClickEvent(QMouseEvent *)
{
    Q_EMIT resetRequested();
}

// ---- ColorWheel -----------------------------------------------------------------------------------

namespace {
// Coefficients BT.709 : la roue est orientée comme le vectorscope (rouge en haut à gauche,
// jaune à gauche, vert en bas, cyan en bas à droite, bleu à droite, magenta en haut à droite).
constexpr double kKr = 0.2126, kKb = 0.0722, kKg = 0.7152;
} // namespace

ColorWheel::ColorWheel(const QString &title, bool hasY, double neutral, double scale, int decimals, QWidget *parent)
    : QWidget(parent)
    , m_title(title)
    , m_hasY(hasY)
    , m_neutral(neutral)
    , m_scale(scale)
    , m_decimals(decimals)
    , m_values({neutral, neutral, neutral, neutral})
{
    const QStringList names = {QStringLiteral("Y"), QStringLiteral("R"), QStringLiteral("V"), QStringLiteral("B")};
    for (int i = hasY ? 0 : 1; i < 4; ++i) {
        auto *f = new ValueField(QString(), -1000, 1000, neutral, decimals, scale / 200., this);
        f->setToolTip(names.at(i));
        m_fields << f;
        connect(f, &ValueField::valueChanged, this, [this, i](double v, bool final) {
            m_values[i] = v;
            update();
            emitChanged(final);
        });
    }
    m_jog = new Jog(this);
    connect(m_jog, &Jog::moved, this, [this](double dx, bool final) {
        // Le niveau : Y, R, V, B bougent ensemble (Offset : R, V, B).
        const double d = dx * m_scale / 150.;
        for (int i = m_hasY ? 0 : 1; i < 4; ++i) {
            m_values[i] += d;
        }
        setValues(m_values);
        emitChanged(final);
    });
    connect(m_jog, &Jog::resetRequested, this, [this]() {
        const double m = m_hasY ? m_values[0] : kKr * m_values[1] + kKg * m_values[2] + kKb * m_values[3];
        const double shift = m_neutral - m;
        for (int i = m_hasY ? 0 : 1; i < 4; ++i) {
            m_values[i] += shift;
        }
        setValues(m_values);
        emitChanged(true);
    });
    setMouseTracking(false);
}

void ColorWheel::setTitle(const QString &t)
{
    m_title = t;
    update();
}

void ColorWheel::setValues(const QVector<double> &values)
{
    m_values = values;
    if (m_values.size() < 4) {
        m_values.resize(4);
    }
    int k = 0;
    for (int i = m_hasY ? 0 : 1; i < 4; ++i) {
        m_fields.at(k++)->setValue(m_values[i]);
    }
    update();
}

void ColorWheel::emitChanged(bool final)
{
    Q_EMIT valuesChanged(m_values, final);
}

QRectF ColorWheel::wheelRect() const
{
    const int top = 22;
    const int bottom = height() - 50;
    const double side = std::max(40, std::min(width() - 16, bottom - top));
    return QRectF((width() - side) / 2., top, side, side);
}

QRect ColorWheel::resetRect() const
{
    return QRect(width() - 22, 2, 18, 18);
}

void ColorWheel::resizeEvent(QResizeEvent *)
{
    const QRectF w = wheelRect();
    const int y = int(w.bottom()) + 4;
    const int n = int(m_fields.size());
    const int fw = std::min(54, (width() - 8) / n - 3);
    const int total = n * fw + (n - 1) * 3;
    int x = (width() - total) / 2;
    for (ValueField *f : m_fields) {
        f->setGeometry(x, y, fw, 22);
        x += fw + 3;
    }
    m_jog->setGeometry((width() - std::min(width() - 20, 160)) / 2, y + 28, std::min(width() - 20, 160), 14);
    m_ring = QImage();
}

QPointF ColorWheel::point() const
{
    const double m = kKr * m_values[1] + kKg * m_values[2] + kKb * m_values[3];
    const double cb = (m_values[3] - m) / 1.8556;
    const double cr = (m_values[1] - m) / 1.5748;
    return {cb / m_scale, -cr / m_scale};
}

void ColorWheel::setFromChroma(double u, double v, bool final)
{
    // u, v : point de la roue (-1..1). Le niveau (luma des R, V, B) ne bouge pas.
    const double r = std::hypot(u, v);
    if (r > 1.) {
        u /= r;
        v /= r;
    }
    const double m = kKr * m_values[1] + kKg * m_values[2] + kKb * m_values[3];
    const double cb = u * m_scale, cr = -v * m_scale;
    const double dR = 1.5748 * cr, dB = 1.8556 * cb;
    const double dG = -(kKr * dR + kKb * dB) / kKg;
    m_values[1] = m + dR;
    m_values[2] = m + dG;
    m_values[3] = m + dB;
    setValues(m_values);
    emitChanged(final);
}

void ColorWheel::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(tone("texte"));
    // Le titre en entier : la police diminue plutôt que de couper le mot.
    QFont tf = smallFont(this, 2);
    const int room = width() - 2 * 24;
    for (int i = 0; i < 6 && QFontMetrics(tf).horizontalAdvance(m_title) > room; ++i) {
        if (tf.pixelSize() > 9) {
            tf.setPixelSize(tf.pixelSize() - 1);
        } else if (tf.pixelSize() <= 0 && tf.pointSizeF() > 8.) {
            tf.setPointSizeF(tf.pointSizeF() - 0.75);
        } else {
            break;
        }
    }
    p.setFont(tf);
    p.drawText(QRect(0, 0, width(), 20), Qt::AlignCenter, m_title);
    p.setFont(font());
    // Le bouton de remise à zéro (couleur et niveau), en haut à droite comme dans Resolve.
    p.setPen(QPen(tone("texte_secondaire"), 1.4));
    const QRectF rr = QRectF(resetRect()).adjusted(4, 4, -4, -4);
    p.drawArc(rr, 60 * 16, 290 * 16);
    p.drawLine(QPointF(rr.right() - 1, rr.top() - 1), QPointF(rr.right() - 1, rr.top() + 3));

    const QRectF w = wheelRect();
    const QPointF c = w.center();
    const double R = w.width() / 2.;
    // L'anneau des teintes (dans l'orientation du vectorscope) : c'est un repère de couleur, pas un décor.
    if (m_ring.size() != w.size().toSize() * devicePixelRatio()) {
        const int s = int(w.width() * devicePixelRatio());
        m_ring = QImage(s, s, QImage::Format_ARGB32_Premultiplied);
        m_ring.fill(Qt::transparent);
        for (int y = 0; y < s; ++y) {
            for (int x = 0; x < s; ++x) {
                const double dx = (x + 0.5) / s * 2 - 1, dy = (y + 0.5) / s * 2 - 1;
                const double d = std::hypot(dx, dy);
                if (d > 1. || d < 0.84) {
                    continue;
                }
                const double cb = dx * 0.42, cr = -dy * 0.42;
                const double R0 = 0.55 + 1.5748 * cr, B0 = 0.55 + 1.8556 * cb;
                const double G0 = 0.55 - (kKr * 1.5748 * cr + kKb * 1.8556 * cb) / kKg;
                const double a = std::clamp((1. - d) * s / 1.5, 0., 1.) * std::clamp((d - 0.84) * s / 1.5, 0., 1.);
                const QColor col = QColor::fromRgbF(std::clamp(R0, 0., 1.), std::clamp(G0, 0., 1.), std::clamp(B0, 0., 1.));
                m_ring.setPixel(x, y, qPremultiply(qRgba(col.red(), col.green(), col.blue(), int(a * 255))));
            }
        }
        m_ring.setDevicePixelRatio(devicePixelRatio());
    }
    p.setPen(Qt::NoPen);
    p.setBrush(tone("champ"));
    p.drawEllipse(c, R * 0.84, R * 0.84);
    p.drawImage(w.topLeft(), m_ring);
    // Réticule et cercles de repère.
    p.setPen(QPen(tone("bordure"), 1));
    p.drawLine(QPointF(c.x() - R * 0.8, c.y()), QPointF(c.x() + R * 0.8, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - R * 0.8), QPointF(c.x(), c.y() + R * 0.8));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, R * 0.4, R * 0.4);
    // Le niveau (master) : un arc sur l'anneau extérieur, comme l'indicateur de Resolve.
    const double level = m_hasY ? m_values[0] : kKr * m_values[1] + kKg * m_values[2] + kKb * m_values[3];
    const double t = std::clamp((level - m_neutral) / (m_scale * 2.), -1., 1.);
    if (std::abs(t) > 1e-3) {
        p.setPen(QPen(tone("texte"), 2, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(c.x() - R - 3, c.y() - R - 3, 2 * R + 6, 2 * R + 6), 270 * 16, int(-t * 150 * 16));
    }
    // Le point de couleur.
    const QPointF pt = point();
    const QPointF ptScreen(c.x() + std::clamp(pt.x(), -1., 1.) * R * 0.8, c.y() + std::clamp(pt.y(), -1., 1.) * R * 0.8);
    p.setPen(QPen(QColor(0, 0, 0, 160), 1));
    p.setBrush(tone("texte"));
    p.drawEllipse(ptScreen, 4, 4);
    // Soulignés de couleur des champs Y R V B (blanc, rouge, vert, bleu).
    // Y dans la couleur du texte du thème (le blanc disparaissait sur le thème clair).
    const QColor under[4] = {tone("texte"), QColor(0xFF, 0x72, 0x62), QColor(0x4A, 0xDE, 0x80), QColor(0x0D, 0x99, 0xFF)};
    int k = 0;
    for (int i = m_hasY ? 0 : 1; i < 4; ++i) {
        const QRect g = m_fields.at(k++)->geometry();
        p.setPen(QPen(under[i], 2));
        p.drawLine(g.left() + 4, g.bottom() + 3, g.right() - 4, g.bottom() + 3);
    }
}

void ColorWheel::mousePressEvent(QMouseEvent *e)
{
    if (resetRect().contains(e->position().toPoint())) {
        for (int i = 0; i < 4; ++i) {
            m_values[i] = m_neutral;
        }
        setValues(m_values);
        emitChanged(true);
        return;
    }
    const QRectF w = wheelRect();
    if (std::hypot(e->position().x() - w.center().x(), e->position().y() - w.center().y()) > w.width() / 2. + 4) {
        return;
    }
    m_dragging = true;
    // Commande-glisser dans l'anneau : le niveau, comme la molette (Resolve).
    m_master = e->modifiers() & Qt::ControlModifier;
    m_lastPos = e->position();
    const QPointF pt = point();
    m_u = pt.x();
    m_v = pt.y();
    if (e->modifiers() & Qt::AltModifier) {
        // Clic absolu : le point saute sous la souris.
        const double R = w.width() / 2. * 0.8;
        m_u = (e->position().x() - w.center().x()) / R;
        m_v = (e->position().y() - w.center().y()) / R;
        setFromChroma(m_u, m_v, false);
    }
}

void ColorWheel::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging) {
        return;
    }
    const QPointF d = e->position() - m_lastPos;
    m_lastPos = e->position();
    const double gain = fine(e) ? 0.2 : 1.;
    const double R = wheelRect().width() / 2. * 0.8;
    if (m_master) {
        const double delta = d.x() * gain * m_scale / 150.;
        for (int i = m_hasY ? 0 : 1; i < 4; ++i) {
            m_values[i] += delta;
        }
        setValues(m_values);
        emitChanged(false);
        return;
    }
    // Comme une boule : le point se déplace du mouvement de la souris, où qu'on ait cliqué.
    m_u += d.x() / R * 0.5 * gain;
    m_v += d.y() / R * 0.5 * gain;
    setFromChroma(m_u, m_v, false);
}

void ColorWheel::mouseReleaseEvent(QMouseEvent *)
{
    if (m_dragging) {
        emitChanged(true);
    }
    m_dragging = false;
}

void ColorWheel::mouseDoubleClickEvent(QMouseEvent *e)
{
    const QRectF w = wheelRect();
    if (std::hypot(e->position().x() - w.center().x(), e->position().y() - w.center().y()) <= w.width() / 2.) {
        // La couleur revient au centre, le niveau reste.
        setFromChroma(0., 0., true);
    }
}

// ---- CurveEditor ------------------------------------------------------------------------------------

CurveEditor::CurveEditor(QWidget *parent)
    : QWidget(parent)
    , m_color() // sans couleur : la couleur du texte du thème (lisible en clair comme en sombre)
{
    setMouseTracking(true);
    setToolTip(i18nc("@info:tooltip", "Click to add a point, drag to move it, right-click to remove it."));
}

void CurveEditor::setKind(Kind kind, double neutral, double yMin, double yMax)
{
    m_kind = kind;
    m_neutral = neutral;
    m_yMin = yMin;
    m_yMax = yMax;
    update();
}

void CurveEditor::setColor(const QColor &c)
{
    m_color = c;
    update();
}

void CurveEditor::setPoints(const QJsonArray &points)
{
    if (m_drag >= 0) {
        return; // pas pendant un glisser
    }
    m_points.clear();
    for (const QJsonValue &v : points) {
        const QJsonArray a = v.toArray();
        m_points << QPointF(a.at(0).toDouble(), a.at(1).toDouble());
    }
    if (m_kind == Kind::Diagonal && m_points.isEmpty()) {
        m_points << QPointF(0, 0) << QPointF(1, 1);
    }
    std::sort(m_points.begin(), m_points.end(), [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    update();
}

QJsonArray CurveEditor::points() const
{
    QJsonArray a;
    for (const QPointF &p : m_points) {
        a.append(QJsonArray{std::round(p.x() * 10000.) / 10000., std::round(p.y() * 10000.) / 10000.});
    }
    return a;
}

QRectF CurveEditor::area() const
{
    const double bottomStrip = m_kind == Kind::HueFlat ? 14. : 0.;
    return QRectF(8, 8, width() - 16, height() - 16 - bottomStrip);
}

QPointF CurveEditor::toScreen(double x, double y) const
{
    const QRectF a = area();
    return {a.left() + x / xMax() * a.width(), a.bottom() - (y - m_yMin) / (m_yMax - m_yMin) * a.height()};
}

QPointF CurveEditor::toValue(const QPointF &p) const
{
    const QRectF a = area();
    return {std::clamp((p.x() - a.left()) / a.width(), 0., 1.) * xMax(), m_yMin + std::clamp((a.bottom() - p.y()) / a.height(), 0., 1.) * (m_yMax - m_yMin)};
}

double CurveEditor::eval(double x) const
{
    std::vector<std::pair<double, double>> pts;
    for (const QPointF &p : m_points) {
        pts.emplace_back(p.x(), p.y());
    }
    const kcgrade::Curve c(pts, m_kind == Kind::HueFlat, m_kind == Kind::Diagonal ? std::nan("") : m_neutral);
    return c.eval(x);
}

int CurveEditor::hit(const QPointF &p) const
{
    for (int i = 0; i < m_points.size(); ++i) {
        if (QLineF(toScreen(m_points.at(i).x(), m_points.at(i).y()), p).length() < 7) {
            return i;
        }
    }
    return -1;
}

void CurveEditor::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF a = area();
    p.setPen(Qt::NoPen);
    p.setBrush(tone("champ"));
    p.drawRoundedRect(a.adjusted(-4, -4, 4, 4), 6, 6);
    p.setPen(QPen(tone("bordure"), 1));
    for (int i = 1; i < 4; ++i) {
        p.drawLine(QPointF(a.left() + a.width() * i / 4., a.top()), QPointF(a.left() + a.width() * i / 4., a.bottom()));
        p.drawLine(QPointF(a.left(), a.top() + a.height() * i / 4.), QPointF(a.right(), a.top() + a.height() * i / 4.));
    }
    // La ligne neutre.
    p.setPen(QPen(tone("texte_desactive"), 1, Qt::DashLine));
    if (m_kind == Kind::Diagonal) {
        p.drawLine(toScreen(0, 0), toScreen(1, 1));
    } else {
        p.drawLine(toScreen(0, m_neutral), toScreen(xMax(), m_neutral));
    }
    if (m_kind == Kind::HueFlat) {
        // Les teintes sous la courbe, dans l'ordre de Resolve : rouge, jaune, vert, cyan, bleu, magenta, rouge.
        QLinearGradient g(a.left(), 0, a.right(), 0);
        const QColor hues[7] = {Qt::red, Qt::yellow, Qt::green, Qt::cyan, Qt::blue, Qt::magenta, Qt::red};
        for (int i = 0; i < 7; ++i) {
            g.setColorAt(i / 6., hues[i]);
        }
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawRoundedRect(QRectF(a.left(), a.bottom() + 6, a.width(), 6), 3, 3);
    }
    // La courbe (construite une fois pour tout le tracé).
    std::vector<std::pair<double, double>> pts;
    for (const QPointF &pt : m_points) {
        pts.emplace_back(pt.x(), pt.y());
    }
    const kcgrade::Curve curve(pts, m_kind == Kind::HueFlat, m_kind == Kind::Diagonal ? std::nan("") : m_neutral);
    QPainterPath path;
    const int steps = std::max(64, int(a.width() / 2));
    for (int i = 0; i <= steps; ++i) {
        const double x = xMax() * i / steps;
        const double y = std::clamp(curve.eval(x), m_yMin, m_yMax);
        const QPointF s = toScreen(x, y);
        if (i == 0) {
            path.moveTo(s);
        } else {
            path.lineTo(s);
        }
    }
    const QColor lineColor = m_color.isValid() ? m_color : tone("texte");
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(lineColor, 1.6));
    p.drawPath(path);
    for (int i = 0; i < m_points.size(); ++i) {
        const QPointF s = toScreen(m_points.at(i).x(), m_points.at(i).y());
        p.setPen(QPen(lineColor, 1.2));
        p.setBrush(i == m_drag ? lineColor : tone("champ"));
        p.drawEllipse(s, 4, 4);
    }
}

void CurveEditor::mousePressEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    int i = hit(pos);
    if (e->button() == Qt::RightButton) {
        // Clic droit : retire le point (les extrémités d'une courbe diagonale restent).
        if (i >= 0 && !(m_kind == Kind::Diagonal && (i == 0 || i == m_points.size() - 1))) {
            m_points.removeAt(i);
            update();
            Q_EMIT pointsChanged(points(), true);
        }
        return;
    }
    if (e->button() != Qt::LeftButton) {
        return;
    }
    if (i < 0) {
        const QPointF v = toValue(pos);
        // Maj-clic : ajoute un point sans déformer la courbe (Resolve).
        const QPointF add(v.x(), (e->modifiers() & Qt::ShiftModifier) ? eval(v.x()) : v.y());
        m_points << add;
        std::sort(m_points.begin(), m_points.end(), [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
        i = int(m_points.indexOf(add));
    }
    m_drag = i;
    update();
    Q_EMIT pointsChanged(points(), false);
}

void CurveEditor::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag < 0) {
        setCursor(hit(e->position()) >= 0 ? Qt::PointingHandCursor : Qt::CrossCursor);
        return;
    }
    QPointF v = toValue(e->position());
    // Un point ne passe pas son voisin ; les extrémités d'une diagonale restent en x = 0 et x = 1.
    const double eps = xMax() / 200.;
    const double lo = m_drag > 0 ? m_points.at(m_drag - 1).x() + eps : 0.;
    const double hi = m_drag < m_points.size() - 1 ? m_points.at(m_drag + 1).x() - eps : xMax();
    if (m_kind == Kind::Diagonal && (m_drag == 0 || m_drag == m_points.size() - 1)) {
        v.setX(m_points.at(m_drag).x());
    } else {
        v.setX(std::clamp(v.x(), lo, hi));
    }
    m_points[m_drag] = v;
    update();
    Q_EMIT pointsChanged(points(), false);
}

void CurveEditor::mouseReleaseEvent(QMouseEvent *)
{
    if (m_drag >= 0) {
        m_drag = -1;
        update();
        Q_EMIT pointsChanged(points(), true);
    }
}

void CurveEditor::mouseDoubleClickEvent(QMouseEvent *)
{
    // Double-clic : la courbe revient à son état neutre.
    m_points.clear();
    if (m_kind == Kind::Diagonal) {
        m_points << QPointF(0, 0) << QPointF(1, 1);
    }
    update();
    Q_EMIT pointsChanged(points(), true);
}

// ---- RangeBar ---------------------------------------------------------------------------------------

RangeBar::RangeBar(Style style, QWidget *parent)
    : QWidget(parent)
    , m_style(style)
{
    setMinimumHeight(26);
    // La touche porte un autre nom selon le système : Option sur Mac, Alt ailleurs.
#ifdef Q_OS_MACOS
    const QString altKey = i18nc("@info Mac modifier key", "Option");
#else
    const QString altKey = i18nc("@info modifier key", "Alt");
#endif
    setToolTip(i18nc("@info:tooltip %1 is the Alt or Option key", "Drag an edge to move it, the middle to move the range, %1-drag an edge for softness.", altKey));
}

void RangeBar::setRange(double low, double high, double lowSoft, double highSoft)
{
    if (m_drag >= 0) {
        return;
    }
    m_low = low;
    m_high = high;
    m_lowSoft = lowSoft;
    m_highSoft = highSoft;
    update();
}

double RangeBar::toValue(double x) const
{
    return std::clamp((x - 4.) / (width() - 8.) * 100., m_style == Style::Hue ? -1000. : 0., m_style == Style::Hue ? 1000. : 100.);
}

double RangeBar::toX(double v) const
{
    return 4. + v / 100. * (width() - 8.);
}

void RangeBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF bar(4, 4, width() - 8, height() - 8);
    QLinearGradient g(bar.left(), 0, bar.right(), 0);
    if (m_style == Style::Hue) {
        // La barre des teintes de Resolve : magenta, rouge, jaune, vert, cyan, bleu, violet.
        const QColor hues[7] = {Qt::magenta, Qt::red, Qt::yellow, Qt::green, Qt::cyan, Qt::blue, Qt::magenta};
        for (int i = 0; i < 7; ++i) {
            g.setColorAt(i / 6., hues[i]);
        }
    } else if (m_style == Style::Saturation) {
        g.setColorAt(0, QColor(128, 128, 128));
        g.setColorAt(1, QColor(0x0D, 0x55, 0xFF));
    } else {
        g.setColorAt(0, Qt::black);
        g.setColorAt(1, Qt::white);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(bar, 4, 4);
    // Hors de la plage : voilé.
    p.setBrush(QColor(0, 0, 0, 140));
    auto shade = [&](double a, double b) {
        a = std::clamp(a, 0., 100.);
        b = std::clamp(b, 0., 100.);
        if (b > a) {
            p.drawRect(QRectF(toX(a), bar.top(), toX(b) - toX(a), bar.height()));
        }
    };
    if (m_style == Style::Hue && (m_low < 0 || m_high > 100)) {
        // La plage de teinte fait le tour de la barre.
        const double lo = std::fmod(m_low + 200., 100.), hi = std::fmod(m_high + 200., 100.);
        shade(hi + m_highSoft, lo - m_lowSoft);
    } else {
        shade(0, m_low - m_lowSoft);
        shade(m_high + m_highSoft, 100);
    }
    // Poignées : bords (blancs) et adoucissements (triangles).
    p.setPen(QPen(Qt::white, 2));
    for (double v : {m_low, m_high}) {
        const double x = toX(std::fmod(v + 200., 100.));
        p.drawLine(QPointF(x, bar.top()), QPointF(x, bar.bottom()));
    }
    p.setPen(QPen(QColor(255, 255, 255, 180), 1));
    p.setBrush(QColor(255, 255, 255, 60));
    for (auto [edge, soft, dir] : {std::tuple{m_low, m_lowSoft, -1.}, std::tuple{m_high, m_highSoft, 1.}}) {
        if (soft <= 0) {
            continue;
        }
        const double x0 = toX(std::fmod(edge + 200., 100.)), x1 = x0 + dir * soft / 100. * (width() - 8.);
        QPolygonF tri{QPointF(x0, bar.top()), QPointF(x0, bar.bottom()), QPointF(x1, bar.bottom())};
        p.drawPolygon(tri);
    }
}

void RangeBar::mousePressEvent(QMouseEvent *e)
{
    const double v = toValue(e->position().x());
    const double tol = 800. / std::max(width(), 100);
    const bool alt = e->modifiers() & Qt::AltModifier;
    auto nearEdge = [&](double edge) {
        return std::abs(std::fmod(edge + 200., 100.) - v) < tol;
    };
    if (nearEdge(m_low)) {
        m_drag = alt ? 3 : 0;
    } else if (nearEdge(m_high)) {
        m_drag = alt ? 4 : 1;
    } else {
        m_drag = 2;
    }
    m_pressValue = v;
    m_pressLow = alt && m_drag == 3 ? m_lowSoft : m_low;
    m_pressHigh = alt && m_drag == 4 ? m_highSoft : m_high;
}

void RangeBar::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag < 0) {
        return;
    }
    const double d = toValue(e->position().x()) - m_pressValue;
    const bool hue = m_style == Style::Hue;
    switch (m_drag) {
    case 0:
        m_low = hue ? m_pressLow + d : std::clamp(m_pressLow + d, 0., m_high);
        break;
    case 1:
        m_high = hue ? m_pressHigh + d : std::clamp(m_pressHigh + d, m_low, 100.);
        break;
    case 2: {
        const double w = m_pressHigh - m_pressLow;
        m_low = hue ? m_pressLow + d : std::clamp(m_pressLow + d, 0., 100. - w);
        m_high = m_low + w;
        break;
    }
    case 3:
        m_lowSoft = std::clamp(m_pressLow - d, 0., 100.);
        break;
    case 4:
        m_highSoft = std::clamp(m_pressHigh + d, 0., 100.);
        break;
    }
    update();
    Q_EMIT rangeChanged(m_low, m_high, m_lowSoft, m_highSoft, false);
}

void RangeBar::mouseReleaseEvent(QMouseEvent *)
{
    if (m_drag >= 0) {
        m_drag = -1;
        Q_EMIT rangeChanged(m_low, m_high, m_lowSoft, m_highSoft, true);
    }
}

} // namespace KoraCutColorUi
