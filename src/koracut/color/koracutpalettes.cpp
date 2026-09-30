/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutpalettes.h"
#include "koracutcolorwidgets.h"
#include "koracutgradecontroller.h"
#include "koracutnodegraph.h"

#include "kccolor.h"
#include "kcgrade.h"

#include <KLocalizedString>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

using namespace KoraCutColorUi;
using namespace KoraCutNodeGraph;

namespace {

KoraCutGradeController *ctrl()
{
    return KoraCutGradeController::self();
}

QJsonArray toArray(const QVector<double> &v, int from)
{
    QJsonArray a;
    for (int i = from; i < v.size(); ++i) {
        a.append(std::round(v[i] * 100000.) / 100000.);
    }
    return a;
}

QVector<double> fromArray(const QJsonValue &v, int count, double def)
{
    QVector<double> out(count, def);
    const QJsonArray a = v.toArray();
    for (int i = 0; i < count && i < a.size(); ++i) {
        out[i] = a.at(i).toDouble(def);
    }
    return out;
}

QToolButton *textButton(const QString &text, QWidget *parent, bool checkable = false)
{
    auto *b = new QToolButton(parent);
    b->setText(text);
    b->setCheckable(checkable);
    b->setAutoRaise(true);
    b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return b;
}

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    QFont f = l->font();
    f.setBold(true);
    l->setFont(f);
    return l;
}

} // namespace

// Ce que toutes les palettes partagent : lire et écrire le nœud courant.
class KoraCutPanelBase : public QWidget
{
public:
    explicit KoraCutPanelBase(QWidget *parent)
        : QWidget(parent)
    {
        connect(ctrl(), &KoraCutGradeController::graphChanged, this, [this]() {
            if (!m_busy) {
                reloadAll();
            }
        });
        connect(ctrl(), &KoraCutGradeController::nodeChanged, this, [this]() { reloadAll(); });
        connect(ctrl(), &KoraCutGradeController::clipChanged, this, [this]() { reloadAll(); });
    }
    virtual void reload() = 0;
    void reloadAll()
    {
        const QJsonObject n = node(ctrl()->graph(), ctrl()->currentNode());
        const bool usable = !n.isEmpty() && isCorrector(n) && (ctrl()->currentClip() >= 0 || ctrl()->scope() == KoraCutGradeController::Scope::Timeline);
        setEnabled(usable);
        reload();
    }

protected:
    QJsonObject current() const { return node(ctrl()->graph(), ctrl()->currentNode()); }
    QJsonValue val(const QString &section, const QString &key) const { return current().value(section).toObject().value(key); }
    void set(const QString &section, const QString &key, const QJsonValue &v, const QString &gesture, bool final)
    {
        QJsonObject g = ctrl()->graph();
        if (!hasNode(g, ctrl()->currentNode())) {
            return;
        }
        m_busy = true;
        setValue(g, ctrl()->currentNode(), section, key, v);
        ctrl()->edit(g, gesture, final);
        m_busy = false;
    }
    void setWhole(const QString &section, const QJsonValue &v, const QString &gesture, bool final)
    {
        QJsonObject g = ctrl()->graph();
        if (!hasNode(g, ctrl()->currentNode())) {
            return;
        }
        m_busy = true;
        setSection(g, ctrl()->currentNode(), section, v);
        ctrl()->edit(g, gesture, final);
        m_busy = false;
    }
    bool m_busy = false;
};

// ---- Primaires ----------------------------------------------------------------------------------------

namespace {
// Barres verticales (mode Barres de Resolve) : Y R V B pour Lift, Gamma, Gain ; R V B pour Offset.
class BarGroup : public QWidget
{
public:
    BarGroup(const QString &title, bool hasY, double min, double max, double def, int decimals, QWidget *parent)
        : QWidget(parent)
        , m_hasY(hasY)
        , m_min(min)
        , m_max(max)
        , m_def(def)
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(4, 0, 4, 0);
        v->setSpacing(4);
        auto *t = new QLabel(title, this);
        t->setAlignment(Qt::AlignCenter);
        v->addWidget(t);
        auto *row = new QHBoxLayout;
        row->setSpacing(6);
        for (int i = hasY ? 0 : 1; i < 4; ++i) {
            auto *col = new QVBoxLayout;
            auto *s = new QSlider(Qt::Vertical, this);
            s->setRange(0, 1000);
            s->setMinimumHeight(90);
            auto *f = new ValueField(QString(), min, max, def, decimals, (max - min) / 400., this);
            f->setFixedWidth(46);
            col->addWidget(s, 1, Qt::AlignHCenter);
            col->addWidget(f, 0, Qt::AlignHCenter);
            row->addLayout(col);
            m_sliders << s;
            m_fields << f;
            const int idx = i;
            connect(s, &QSlider::valueChanged, this, [this, idx, f](int pos) {
                if (m_loading) {
                    return;
                }
                m_values[idx] = m_min + (m_max - m_min) * pos / 1000.;
                f->setValue(m_values[idx]);
                if (onChange) {
                    onChange(m_values, false);
                }
            });
            connect(s, &QSlider::sliderReleased, this, [this]() {
                if (onChange) {
                    onChange(m_values, true);
                }
            });
            connect(f, &ValueField::valueChanged, this, [this, idx](double val, bool final) {
                m_values[idx] = val;
                setValues(m_values);
                if (onChange) {
                    onChange(m_values, final);
                }
            });
        }
        v->addLayout(row, 1);
        m_values = QVector<double>(4, def);
    }
    void setValues(const QVector<double> &values)
    {
        m_loading = true;
        m_values = values;
        int k = 0;
        for (int i = m_hasY ? 0 : 1; i < 4; ++i) {
            m_sliders.at(k)->setValue(int(std::round((values[i] - m_min) / (m_max - m_min) * 1000.)));
            m_fields.at(k)->setValue(values[i]);
            ++k;
        }
        m_loading = false;
    }
    std::function<void(const QVector<double> &, bool)> onChange;

private:
    bool m_hasY;
    double m_min, m_max, m_def;
    bool m_loading = false;
    QVector<double> m_values;
    QList<QSlider *> m_sliders;
    QList<ValueField *> m_fields;
};
} // namespace

class KoraCutPrimariesPanel : public KoraCutPanelBase
{
public:
    explicit KoraCutPrimariesPanel(QWidget *parent)
        : KoraCutPanelBase(parent)
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(8, 4, 8, 6);
        v->setSpacing(6);
        // En-tête : le mode (roues, barres, log) et la remise à zéro de la palette.
        auto *head = new QHBoxLayout;
        m_heading = new QLabel(this);
        head->addWidget(m_heading);
        head->addStretch(1);
        m_modes = new QButtonGroup(this);
        const QList<QPair<QString, QString>> modes = {{QStringLiteral("roues"), i18nc("@action:button primaries mode", "Wheels")},
                                                      {QStringLiteral("barres"), i18nc("@action:button primaries mode", "Bars")},
                                                      {QStringLiteral("log"), i18nc("@action:button primaries mode", "Log")}};
        for (const auto &m : modes) {
            QToolButton *b = textButton(m.second, this, true);
            b->setProperty("mode", m.first);
            m_modes->addButton(b);
            head->addWidget(b);
            connect(b, &QToolButton::clicked, this, [this, m]() { setMode(m.first); });
        }
        QToolButton *reset = textButton(i18nc("@action:button reset the whole palette", "Reset"), this);
        reset->setToolTip(i18nc("@info:tooltip", "Reset the primaries of this node"));
        head->addWidget(reset);
        connect(reset, &QToolButton::clicked, this, [this]() {
            setWhole(QStringLiteral("primaires"), QJsonValue(), i18nc("@action undo text", "reset primaries"), true);
            if (m_mode == QLatin1String("log")) {
                setWhole(QStringLiteral("log"), QJsonValue(), i18nc("@action undo text", "reset log wheels"), true);
            }
        });
        v->addLayout(head);

        // Ligne du haut.
        auto *top = new QHBoxLayout;
        top->setSpacing(10);
        m_temp = field(top, i18nc("@label white balance temperature", "Temp"), -4000, 4000, 0, 1, 10, QStringLiteral("primaires"), QStringLiteral("temp"));
        m_tint = field(top, i18nc("@label white balance tint, magenta green", "Tint"), -100, 100, 0, 2, 0.5, QStringLiteral("primaires"), QStringLiteral("tint"));
        m_contrast = field(top, i18nc("@label", "Contrast"), 0, 2, 1, 3, 0.005, QStringLiteral("primaires"), QStringLiteral("contraste"));
        m_pivot = field(top, i18nc("@label contrast pivot", "Pivot"), 0, 1, 0.435, 3, 0.002, QStringLiteral("primaires"), QStringLiteral("pivot"));
        m_md = field(top, i18nc("@label midtone detail", "Mid/Detail"), -100, 100, 0, 2, 0.5, QStringLiteral("primaires"), QStringLiteral("mid_detail"));
        m_low = field(top, i18nc("@label log wheels low range", "Low Range"), 0, 1, 0.333, 3, 0.002, QStringLiteral("log"), QStringLiteral("plage_basse"));
        m_high = field(top, i18nc("@label log wheels high range", "High Range"), 0, 1, 0.55, 3, 0.002, QStringLiteral("log"), QStringLiteral("plage_haute"));
        v->addLayout(top);

        // Les roues (et les barres).
        m_center = new QStackedWidget(this);
        auto *wheels = new QWidget(m_center);
        auto *wl = new QHBoxLayout(wheels);
        wl->setContentsMargins(0, 0, 0, 0);
        m_lift = wheel(wl, i18nc("@title color wheel", "Lift"), true, 0., 0.25, 2, QStringLiteral("primaires"), QStringLiteral("lift"));
        m_gamma = wheel(wl, i18nc("@title color wheel", "Gamma"), true, 0., 0.25, 2, QStringLiteral("primaires"), QStringLiteral("gamma"));
        m_gain = wheel(wl, i18nc("@title color wheel", "Gain"), true, 1., 0.5, 2, QStringLiteral("primaires"), QStringLiteral("gain"));
        m_offset = wheel(wl, i18nc("@title color wheel, Resolve's Offset", "Offset"), false, 25., 12.5, 2, QStringLiteral("primaires"), QStringLiteral("offset"));
        m_center->addWidget(wheels);
        auto *logw = new QWidget(m_center);
        auto *ll = new QHBoxLayout(logw);
        ll->setContentsMargins(0, 0, 0, 0);
        m_shadow = wheel(ll, i18nc("@title log color wheel", "Shadow"), false, 0., 0.25, 2, QStringLiteral("log"), QStringLiteral("ombres"));
        m_mid = wheel(ll, i18nc("@title log color wheel", "Midtone"), false, 0., 0.25, 2, QStringLiteral("log"), QStringLiteral("tons_moyens"));
        m_hi = wheel(ll, i18nc("@title log color wheel", "Highlight"), false, 0., 0.25, 2, QStringLiteral("log"), QStringLiteral("hautes_lumieres"));
        m_offset2 = wheel(ll, i18nc("@title color wheel, Resolve's Offset", "Offset"), false, 25., 12.5, 2, QStringLiteral("primaires"), QStringLiteral("offset"));
        m_center->addWidget(logw);
        auto *bars = new QWidget(m_center);
        auto *bl = new QHBoxLayout(bars);
        bl->setContentsMargins(0, 0, 0, 0);
        m_bars << bar(bl, i18nc("@title color wheel", "Lift"), true, -1, 1, 0, QStringLiteral("lift"));
        m_bars << bar(bl, i18nc("@title color wheel", "Gamma"), true, -1, 1, 0, QStringLiteral("gamma"));
        m_bars << bar(bl, i18nc("@title color wheel", "Gain"), true, 0, 4, 1, QStringLiteral("gain"));
        m_bars << bar(bl, i18nc("@title color wheel, Resolve's Offset", "Offset"), false, 0, 100, 25, QStringLiteral("offset"));
        m_center->addWidget(bars);
        v->addWidget(m_center, 1);

        // Ligne du bas.
        auto *bottom = new QHBoxLayout;
        bottom->setSpacing(10);
        m_boost = field(bottom, i18nc("@label color boost", "Boost"), -100, 100, 0, 2, 0.5, QStringLiteral("primaires"), QStringLiteral("boost"));
        m_shadows = field(bottom, i18nc("@label recover shadows", "Shadows"), -100, 100, 0, 2, 0.5, QStringLiteral("primaires"), QStringLiteral("ombres"));
        m_highlights = field(bottom, i18nc("@label recover highlights", "Highlights"), -100, 100, 0, 2, 0.5, QStringLiteral("primaires"), QStringLiteral("hautes_lumieres"));
        m_sat = field(bottom, i18nc("@label", "Saturation"), 0, 100, 50, 2, 0.25, QStringLiteral("primaires"), QStringLiteral("saturation"));
        m_hue = field(bottom, i18nc("@label hue rotation", "Hue"), 0, 100, 50, 2, 0.25, QStringLiteral("primaires"), QStringLiteral("teinte"));
        m_lumMix = field(bottom, i18nc("@label", "Lum Mix"), 0, 100, 100, 2, 0.5, QStringLiteral("primaires"), QStringLiteral("lum_mix"));
        v->addLayout(bottom);
        setMode(QStringLiteral("roues"));
        reloadAll();
    }

    void setMode(const QString &mode)
    {
        m_mode = mode;
        for (QAbstractButton *b : m_modes->buttons()) {
            b->setChecked(b->property("mode").toString() == mode);
        }
        m_center->setCurrentIndex(mode == QLatin1String("log") ? 1 : (mode == QLatin1String("barres") ? 2 : 0));
        const bool log = mode == QLatin1String("log");
        m_md->parentWidget()->setVisible(!log);
        m_low->parentWidget()->setVisible(log);
        m_high->parentWidget()->setVisible(log);
        m_heading->setText(log ? i18nc("@title palette title", "Primaries - Log Wheels")
                               : (mode == QLatin1String("barres") ? i18nc("@title palette title", "Primaries - Bars") : i18nc("@title palette title", "Primaries - Color Wheels")));
    }
    QString mode() const { return m_mode; }

    void reload() override
    {
        const QJsonObject p = current().value(QStringLiteral("primaires")).toObject();
        const QJsonObject l = current().value(QStringLiteral("log")).toObject();
        m_lift->setValues(fromArray(p.value(QStringLiteral("lift")), 4, 0.));
        m_gamma->setValues(fromArray(p.value(QStringLiteral("gamma")), 4, 0.));
        m_gain->setValues(fromArray(p.value(QStringLiteral("gain")), 4, 1.));
        QVector<double> off = fromArray(p.value(QStringLiteral("offset")), 3, 25.);
        off.prepend(25.);
        m_offset->setValues(off);
        m_offset2->setValues(off);
        auto logv = [&](const QString &k) {
            QVector<double> x = fromArray(l.value(k), 3, 0.);
            x.prepend(0.);
            return x;
        };
        m_shadow->setValues(logv(QStringLiteral("ombres")));
        m_mid->setValues(logv(QStringLiteral("tons_moyens")));
        m_hi->setValues(logv(QStringLiteral("hautes_lumieres")));
        m_bars.at(0)->setValues(fromArray(p.value(QStringLiteral("lift")), 4, 0.));
        m_bars.at(1)->setValues(fromArray(p.value(QStringLiteral("gamma")), 4, 0.));
        m_bars.at(2)->setValues(fromArray(p.value(QStringLiteral("gain")), 4, 1.));
        m_bars.at(3)->setValues(off);
        for (auto it = m_fieldKeys.constBegin(); it != m_fieldKeys.constEnd(); ++it) {
            const QStringList sk = it.value();
            it.key()->setValue(current().value(sk.at(0)).toObject().value(sk.at(1)).toDouble(sk.at(2).toDouble()));
        }
    }

private:
    ValueField *field(QHBoxLayout *row, const QString &label, double min, double max, double def, int dec, double step, const QString &section, const QString &key)
    {
        auto *f = new ValueField(label, min, max, def, dec, step, this);
        // Chaque champ dans une case à parts égales, jamais plus étroite que lui (libellé entier) ;
        // cacher un champ cache sa case, la rangée reste équilibrée.
        f->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
        auto *cell = new QWidget(this);
        auto *cl = new QHBoxLayout(cell);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->addStretch(1);
        cl->addWidget(f);
        row->addWidget(cell, 1);
        m_fieldKeys.insert(f, {section, key, QString::number(def)});
        connect(f, &ValueField::valueChanged, this, [this, section, key, label](double value, bool final) { set(section, key, value, label, final); });
        return f;
    }
    ColorWheel *wheel(QHBoxLayout *row, const QString &title, bool hasY, double neutral, double scale, int dec, const QString &section, const QString &key)
    {
        auto *w = new ColorWheel(title, hasY, neutral, scale, dec, this);
        row->addWidget(w, 1);
        connect(w, &ColorWheel::valuesChanged, this, [this, section, key, title, hasY](const QVector<double> &values, bool final) {
            set(section, key, toArray(values, hasY ? 0 : 1), title, final);
        });
        return w;
    }
    BarGroup *bar(QHBoxLayout *row, const QString &title, bool hasY, double min, double max, double def, const QString &key)
    {
        auto *b = new BarGroup(title, hasY, min, max, def, 2, this);
        row->addWidget(b, 1);
        b->onChange = [this, key, title, hasY](const QVector<double> &values, bool final) {
            set(QStringLiteral("primaires"), key, toArray(values, hasY ? 0 : 1), title, final);
        };
        return b;
    }

    QString m_mode;
    QLabel *m_heading;
    QButtonGroup *m_modes;
    QStackedWidget *m_center;
    ValueField *m_temp, *m_tint, *m_contrast, *m_pivot, *m_md, *m_low, *m_high;
    ValueField *m_boost, *m_shadows, *m_highlights, *m_sat, *m_hue, *m_lumMix;
    ColorWheel *m_lift, *m_gamma, *m_gain, *m_offset, *m_shadow, *m_mid, *m_hi, *m_offset2;
    QList<BarGroup *> m_bars;
    QHash<ValueField *, QStringList> m_fieldKeys;
};

// ---- Courbes --------------------------------------------------------------------------------------------

class KoraCutCurvesPanel : public KoraCutPanelBase
{
public:
    explicit KoraCutCurvesPanel(QWidget *parent)
        : KoraCutPanelBase(parent)
    {
        auto *v = new QVBoxLayout(this);
        v->setContentsMargins(8, 4, 8, 6);
        auto *head = new QHBoxLayout;
        m_heading = new QLabel(this);
        head->addWidget(m_heading);
        head->addStretch(1);
        m_kinds = new QButtonGroup(this);
        const QList<QPair<QString, QString>> kinds = {{QStringLiteral("custom"), i18nc("@action:button curves mode", "Custom")},
                                                      {QStringLiteral("teinte_teinte"), i18nc("@action:button curves mode", "Hue vs Hue")},
                                                      {QStringLiteral("teinte_sat"), i18nc("@action:button curves mode", "Hue vs Sat")},
                                                      {QStringLiteral("teinte_lum"), i18nc("@action:button curves mode", "Hue vs Lum")},
                                                      {QStringLiteral("lum_sat"), i18nc("@action:button curves mode", "Lum vs Sat")},
                                                      {QStringLiteral("sat_sat"), i18nc("@action:button curves mode", "Sat vs Sat")},
                                                      {QStringLiteral("sat_lum"), i18nc("@action:button curves mode", "Sat vs Lum")}};
        for (const auto &k : kinds) {
            QToolButton *b = textButton(k.second, this, true);
            b->setProperty("kind", k.first);
            m_kinds->addButton(b);
            head->addWidget(b);
            m_kindNames.insert(k.first, k.second);
            connect(b, &QToolButton::clicked, this, [this, k]() { setKind(k.first); });
        }
        v->addLayout(head);
        auto *body = new QHBoxLayout;
        m_editor = new CurveEditor(this);
        body->addWidget(m_editor, 3);
        // À droite : les canaux (Y R V B, liés par défaut) et leur intensité.
        auto *side = new QVBoxLayout;
        auto *chan = new QHBoxLayout;
        m_link = textButton(i18nc("@action:button link the Y R G B curves together", "Linked"), this, true);
        m_link->setChecked(true);
        m_link->setToolTip(i18nc("@info:tooltip", "Linked: the curve applies to red, green and blue together (contrast that also saturates)"));
        chan->addWidget(m_link);
        m_channels = new QButtonGroup(this);
        const QStringList names = {QStringLiteral("Y"), QStringLiteral("R"), QStringLiteral("V"), QStringLiteral("B")};
        const QStringList keys = {QStringLiteral("y"), QStringLiteral("r"), QStringLiteral("v"), QStringLiteral("b")};
        for (int i = 0; i < 4; ++i) {
            QToolButton *b = textButton(names.at(i), this, true);
            b->setProperty("key", keys.at(i));
            m_channels->addButton(b);
            chan->addWidget(b);
            connect(b, &QToolButton::clicked, this, [this, k = keys.at(i)]() {
                m_channel = k;
                m_link->setChecked(false);
                reload();
            });
        }
        side->addLayout(chan);
        side->addWidget(sectionLabel(i18nc("@title curves intensity", "Intensity"), this));
        for (int i = 0; i < 4; ++i) {
            auto *f = new ValueField(names.at(i), 0, 100, 100, 1, 0.5, this);
            m_intensity << f;
            side->addWidget(f);
            connect(f, &ValueField::valueChanged, this, [this](double, bool final) {
                QJsonArray a;
                for (ValueField *x : std::as_const(m_intensity)) {
                    a.append(x->value());
                }
                set(QStringLiteral("courbes"), QStringLiteral("intensite"), a, i18nc("@action undo text", "curve intensity"), final);
            });
        }
        side->addStretch(1);
        body->addLayout(side, 1);
        v->addLayout(body, 1);
        connect(m_editor, &CurveEditor::pointsChanged, this, [this](const QJsonArray &pts, bool final) {
            if (m_kind == QLatin1String("custom")) {
                if (m_link->isChecked()) {
                    // Liées : la même courbe sur R, V et B.
                    QJsonObject g = ctrl()->graph();
                    QJsonObject c = current().value(QStringLiteral("courbes")).toObject();
                    c.insert(QStringLiteral("r"), pts);
                    c.insert(QStringLiteral("v"), pts);
                    c.insert(QStringLiteral("b"), pts);
                    setWhole(QStringLiteral("courbes"), c, i18nc("@action undo text", "custom curve"), final);
                } else {
                    set(QStringLiteral("courbes"), m_channel, pts, i18nc("@action undo text", "custom curve"), final);
                }
            } else {
                set(QStringLiteral("courbes_teinte"), m_kind, pts, m_kindNames.value(m_kind), final);
            }
        });
        setKind(QStringLiteral("custom"));
        reloadAll();
    }

    void setKind(const QString &kind)
    {
        m_kind = kind;
        for (QAbstractButton *b : m_kinds->buttons()) {
            b->setChecked(b->property("kind").toString() == kind);
        }
        const bool custom = kind == QLatin1String("custom");
        m_link->setEnabled(custom);
        for (QAbstractButton *b : m_channels->buttons()) {
            b->setEnabled(custom);
        }
        for (ValueField *f : std::as_const(m_intensity)) {
            f->setEnabled(custom);
        }
        if (custom) {
            m_editor->setKind(CurveEditor::Kind::Diagonal, 0., 0., 1.);
        } else if (kind == QLatin1String("teinte_teinte")) {
            m_editor->setKind(CurveEditor::Kind::HueFlat, 0., -180., 180.);
        } else if (kind.startsWith(QLatin1String("teinte"))) {
            m_editor->setKind(CurveEditor::Kind::HueFlat, 1., 0., 2.);
        } else {
            m_editor->setKind(CurveEditor::Kind::Flat, 1., 0., 2.);
        }
        m_heading->setText(i18nc("@title palette title, %1 is the curve kind", "Curves - %1", m_kindNames.value(kind)));
        reload();
    }

    void reload() override
    {
        const QJsonObject c = current().value(QStringLiteral("courbes")).toObject();
        if (m_kind == QLatin1String("custom")) {
            const QString key = m_link->isChecked() ? QStringLiteral("r") : m_channel;
            static const QHash<QString, QColor> colors{{QStringLiteral("y"), QColor()},
                                                        {QStringLiteral("r"), QColor(0xFF, 0x72, 0x62)},
                                                        {QStringLiteral("v"), QColor(0x4A, 0xDE, 0x80)},
                                                        {QStringLiteral("b"), QColor(0x0D, 0x99, 0xFF)}};
            m_editor->setColor(m_link->isChecked() ? QColor() : colors.value(key));
            m_editor->setPoints(c.value(key).toArray());
            for (QAbstractButton *b : m_channels->buttons()) {
                b->setChecked(!m_link->isChecked() && b->property("key").toString() == m_channel);
            }
            const QVector<double> in = fromArray(c.value(QStringLiteral("intensite")), 4, 100.);
            for (int i = 0; i < 4; ++i) {
                m_intensity.at(i)->setValue(in[i]);
            }
        } else {
            m_editor->setColor(QColor());
            m_editor->setPoints(current().value(QStringLiteral("courbes_teinte")).toObject().value(m_kind).toArray());
        }
    }

private:
    QLabel *m_heading;
    QButtonGroup *m_kinds;
    QButtonGroup *m_channels;
    QToolButton *m_link;
    CurveEditor *m_editor;
    QList<ValueField *> m_intensity;
    QString m_kind = QStringLiteral("custom");
    QString m_channel = QStringLiteral("y");
    QHash<QString, QString> m_kindNames;
};

// ---- Qualificateur --------------------------------------------------------------------------------------

class KoraCutQualifierPanel : public KoraCutPanelBase
{
public:
    explicit KoraCutQualifierPanel(QWidget *parent)
        : KoraCutPanelBase(parent)
    {
        auto *v = new QHBoxLayout(this);
        v->setContentsMargins(8, 4, 8, 6);
        auto *left = new QVBoxLayout;
        auto *head = new QHBoxLayout;
        m_active = new QCheckBox(i18nc("@option:check qualifier on", "Qualifier"), this);
        head->addWidget(m_active);
        head->addStretch(1);
        m_modes = new QButtonGroup(this);
        for (const auto &m : {QPair<QString, QString>{QStringLiteral("hsl"), QStringLiteral("HSL")}, QPair<QString, QString>{QStringLiteral("rvb"), i18nc("@action:button qualifier mode", "RGB")},
                              QPair<QString, QString>{QStringLiteral("lum"), i18nc("@action:button qualifier mode", "Luma")}}) {
            QToolButton *b = textButton(m.second, this, true);
            b->setProperty("mode", m.first);
            m_modes->addButton(b);
            head->addWidget(b);
            connect(b, &QToolButton::clicked, this, [this, m]() { writeQualifier(QStringLiteral("mode"), m.first, true); });
        }
        m_invert = new QCheckBox(i18nc("@option:check invert the key", "Invert"), this);
        head->addWidget(m_invert);
        left->addLayout(head);
        connect(m_active, &QCheckBox::clicked, this, [this](bool on) { writeQualifier(QStringLiteral("actif"), on, true); });
        connect(m_invert, &QCheckBox::clicked, this, [this](bool on) { writeQualifier(QStringLiteral("inverser"), on, true); });

        // Teinte, saturation, luminance : une barre et ses nombres.
        m_hueBar = new RangeBar(RangeBar::Style::Hue, this);
        auto *hr = new QHBoxLayout;
        hr->addWidget(sectionLabel(i18nc("@title qualifier", "Hue"), this));
        m_center = qfield(hr, i18nc("@label qualifier hue", "Center"), 0, 100, 16.7, QStringLiteral("teinte"), QStringLiteral("centre"));
        m_width = qfield(hr, i18nc("@label qualifier hue", "Width"), 0, 100, 20, QStringLiteral("teinte"), QStringLiteral("largeur"));
        m_soft = qfield(hr, i18nc("@label qualifier softness", "Soft"), 0, 100, 5, QStringLiteral("teinte"), QStringLiteral("doux"));
        m_sym = qfield(hr, i18nc("@label qualifier symmetry", "Sym"), 0, 100, 50, QStringLiteral("teinte"), QStringLiteral("sym"));
        left->addLayout(hr);
        left->addWidget(m_hueBar);
        m_satBar = new RangeBar(RangeBar::Style::Saturation, this);
        auto *sr = new QHBoxLayout;
        sr->addWidget(sectionLabel(i18nc("@title qualifier", "Saturation"), this));
        m_satLow = qfield(sr, i18nc("@label qualifier range", "Low"), 0, 100, 0, QStringLiteral("sat"), QStringLiteral("bas"));
        m_satHigh = qfield(sr, i18nc("@label qualifier range", "High"), 0, 100, 100, QStringLiteral("sat"), QStringLiteral("haut"));
        m_satLS = qfield(sr, i18nc("@label qualifier low softness", "L. Soft"), 0, 100, 0, QStringLiteral("sat"), QStringLiteral("doux_bas"));
        m_satHS = qfield(sr, i18nc("@label qualifier high softness", "H. Soft"), 0, 100, 0, QStringLiteral("sat"), QStringLiteral("doux_haut"));
        left->addLayout(sr);
        left->addWidget(m_satBar);
        m_lumBar = new RangeBar(RangeBar::Style::Luma, this);
        auto *lr = new QHBoxLayout;
        lr->addWidget(sectionLabel(i18nc("@title qualifier", "Luminance"), this));
        m_lumLow = qfield(lr, i18nc("@label qualifier range", "Low"), 0, 100, 0, QStringLiteral("lum"), QStringLiteral("bas"));
        m_lumHigh = qfield(lr, i18nc("@label qualifier range", "High"), 0, 100, 100, QStringLiteral("lum"), QStringLiteral("haut"));
        m_lumLS = qfield(lr, i18nc("@label qualifier low softness", "L. Soft"), 0, 100, 0, QStringLiteral("lum"), QStringLiteral("doux_bas"));
        m_lumHS = qfield(lr, i18nc("@label qualifier high softness", "H. Soft"), 0, 100, 0, QStringLiteral("lum"), QStringLiteral("doux_haut"));
        left->addLayout(lr);
        left->addWidget(m_lumBar);
        left->addStretch(1);
        v->addLayout(left, 3);

        // Matte Finesse : nettoyer la clé.
        auto *right = new QVBoxLayout;
        right->addWidget(sectionLabel(i18nc("@title qualifier key cleanup, Resolve's Matte Finesse", "Matte Finesse"), this));
        m_black = new ValueField(i18nc("@label key black clip", "Black Clip"), 0, 100, 0, 1, 0.25, this);
        m_white = new ValueField(i18nc("@label key white clip", "White Clip"), 0, 100, 100, 1, 0.25, this);
        m_blur = new ValueField(i18nc("@label key blur", "Blur Radius"), 0, 100, 0, 1, 0.25, this);
        for (auto [f, key] : {std::pair{m_black, QStringLiteral("noir")}, std::pair{m_white, QStringLiteral("blanc")}, std::pair{m_blur, QStringLiteral("flou")}}) {
            right->addWidget(f);
            connect(f, &ValueField::valueChanged, this, [this, key](double val, bool final) { writeQualifier(key, val, final); });
        }
        right->addStretch(1);
        v->addLayout(right, 1);

        auto bars = [this](RangeBar *bar, const QString &section, bool hue) {
            connect(bar, &RangeBar::rangeChanged, this, [this, section, hue](double lo, double hi, double ls, double hs, bool final) {
                QJsonObject q = current().value(QStringLiteral("qualificateur")).toObject();
                QJsonObject s = q.value(section).toObject();
                if (hue) {
                    double c = std::fmod((lo + hi) / 2. + 200., 100.);
                    s.insert(QStringLiteral("centre"), std::round(c * 10.) / 10.);
                    s.insert(QStringLiteral("largeur"), std::round((hi - lo) * 10.) / 10.);
                    s.insert(QStringLiteral("doux"), std::round(std::max(ls, hs) * 10.) / 10.);
                } else {
                    s.insert(QStringLiteral("bas"), std::round(lo * 10.) / 10.);
                    s.insert(QStringLiteral("haut"), std::round(hi * 10.) / 10.);
                    s.insert(QStringLiteral("doux_bas"), std::round(ls * 10.) / 10.);
                    s.insert(QStringLiteral("doux_haut"), std::round(hs * 10.) / 10.);
                }
                q.insert(section, s);
                q.insert(QStringLiteral("actif"), true);
                setWhole(QStringLiteral("qualificateur"), q, i18nc("@action undo text", "qualifier"), final);
            });
        };
        bars(m_hueBar, QStringLiteral("teinte"), true);
        bars(m_satBar, QStringLiteral("sat"), false);
        bars(m_lumBar, QStringLiteral("lum"), false);
        reloadAll();
    }

    void reload() override
    {
        const QJsonObject q = current().value(QStringLiteral("qualificateur")).toObject();
        m_active->setChecked(q.value(QStringLiteral("actif")).toBool(false));
        m_invert->setChecked(q.value(QStringLiteral("inverser")).toBool(false));
        const QString mode = q.value(QStringLiteral("mode")).toString(QStringLiteral("hsl"));
        for (QAbstractButton *b : m_modes->buttons()) {
            b->setChecked(b->property("mode").toString() == mode);
        }
        for (auto it = m_keys.constBegin(); it != m_keys.constEnd(); ++it) {
            it.key()->setValue(q.value(it.value().at(0)).toObject().value(it.value().at(1)).toDouble(it.value().at(2).toDouble()));
        }
        const QJsonObject h = q.value(QStringLiteral("teinte")).toObject();
        const double c = h.value(QStringLiteral("centre")).toDouble(16.7), w = h.value(QStringLiteral("largeur")).toDouble(20), s = h.value(QStringLiteral("doux")).toDouble(5);
        m_hueBar->setRange(c - w / 2., c + w / 2., s, s);
        auto range = [&](RangeBar *bar, const QString &k) {
            const QJsonObject o = q.value(k).toObject();
            bar->setRange(o.value(QStringLiteral("bas")).toDouble(0), o.value(QStringLiteral("haut")).toDouble(100), o.value(QStringLiteral("doux_bas")).toDouble(0),
                          o.value(QStringLiteral("doux_haut")).toDouble(0));
        };
        range(m_satBar, QStringLiteral("sat"));
        range(m_lumBar, QStringLiteral("lum"));
        m_black->setValue(q.value(QStringLiteral("noir")).toDouble(0));
        m_white->setValue(q.value(QStringLiteral("blanc")).toDouble(100));
        m_blur->setValue(q.value(QStringLiteral("flou")).toDouble(0));
        const bool hsl = mode == QLatin1String("hsl");
        m_hueBar->setEnabled(hsl);
        m_satBar->setEnabled(hsl);
    }

private:
    void writeQualifier(const QString &key, const QJsonValue &value, bool final)
    {
        QJsonObject q = current().value(QStringLiteral("qualificateur")).toObject();
        q.insert(key, value);
        if (key != QLatin1String("actif")) {
            q.insert(QStringLiteral("actif"), true);
        }
        setWhole(QStringLiteral("qualificateur"), q, i18nc("@action undo text", "qualifier"), final);
    }
    ValueField *qfield(QHBoxLayout *row, const QString &label, double min, double max, double def, const QString &section, const QString &key)
    {
        auto *f = new ValueField(label, min, max, def, 1, 0.2, this);
        row->addStretch(1);
        row->addWidget(f, 0);
        m_keys.insert(f, {section, key, QString::number(def)});
        connect(f, &ValueField::valueChanged, this, [this, section, key](double val, bool final) {
            QJsonObject q = current().value(QStringLiteral("qualificateur")).toObject();
            QJsonObject s = q.value(section).toObject();
            s.insert(key, val);
            q.insert(section, s);
            q.insert(QStringLiteral("actif"), true);
            setWhole(QStringLiteral("qualificateur"), q, i18nc("@action undo text", "qualifier"), final);
        });
        return f;
    }
    QCheckBox *m_active, *m_invert;
    QButtonGroup *m_modes;
    RangeBar *m_hueBar, *m_satBar, *m_lumBar;
    ValueField *m_center, *m_width, *m_soft, *m_sym, *m_satLow, *m_satHigh, *m_satLS, *m_satHS, *m_lumLow, *m_lumHigh, *m_lumLS, *m_lumHS;
    ValueField *m_black, *m_white, *m_blur;
    QHash<ValueField *, QStringList> m_keys;
};

// ---- Fenêtres ---------------------------------------------------------------------------------------------

class KoraCutWindowsPanel : public KoraCutPanelBase
{
public:
    explicit KoraCutWindowsPanel(QWidget *parent)
        : KoraCutPanelBase(parent)
    {
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(8, 4, 8, 6);
        auto *left = new QVBoxLayout;
        auto *adds = new QHBoxLayout;
        const QList<QPair<QString, QString>> types = {{QStringLiteral("rectangle"), i18nc("@action:button add a power window", "Linear")},
                                                      {QStringLiteral("cercle"), i18nc("@action:button add a power window", "Circle")},
                                                      {QStringLiteral("polygone"), i18nc("@action:button add a power window", "Polygon")},
                                                      {QStringLiteral("degrade"), i18nc("@action:button add a power window", "Gradient")}};
        for (const auto &t : types) {
            QToolButton *b = textButton(QStringLiteral("+ ") + t.second, this);
            adds->addWidget(b);
            connect(b, &QToolButton::clicked, this, [this, t]() { addWindow(t.first); });
        }
        left->addLayout(adds);
        m_list = new QListWidget(this);
        left->addWidget(m_list, 1);
        auto *rm = textButton(i18nc("@action:button delete the window", "Delete Window"), this);
        left->addWidget(rm);
        h->addLayout(left, 1);
        connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { reloadFields(); });
        connect(m_list, &QListWidget::itemChanged, this, [this](QListWidgetItem *it) {
            if (m_loading) {
                return;
            }
            writeWindow(m_list->row(it), QStringLiteral("actif"), it->checkState() == Qt::Checked, true);
        });
        connect(rm, &QToolButton::clicked, this, [this]() {
            QJsonArray w = current().value(QStringLiteral("fenetres")).toArray();
            const int row = m_list->currentRow();
            if (row >= 0 && row < w.size()) {
                w.removeAt(row);
                setWhole(QStringLiteral("fenetres"), w.isEmpty() ? QJsonValue() : QJsonValue(w), i18nc("@action undo text", "delete window"), true);
            }
        });
        auto *grid = new QGridLayout;
        const QList<std::tuple<QString, QString, double, double, double, int, double>> fields = {
            {QStringLiteral("x"), i18nc("@label window position", "Pan"), 0, 1, 0.5, 3, 0.002},
            {QStringLiteral("y"), i18nc("@label window position", "Tilt"), 0, 1, 0.5, 3, 0.002},
            {QStringLiteral("taille"), i18nc("@label window size", "Size"), 0.01, 2, 0.25, 3, 0.002},
            {QStringLiteral("aspect"), i18nc("@label window aspect", "Aspect"), 0.05, 20, 1, 2, 0.01},
            {QStringLiteral("rotation"), i18nc("@label window rotation", "Rotate"), -180, 180, 0, 1, 0.5},
            {QStringLiteral("doux"), i18nc("@label window softness", "Soft"), 0, 1, 0.05, 3, 0.002},
            {QStringLiteral("opacite"), i18nc("@label window opacity", "Opacity"), 0, 1, 1, 2, 0.005}};
        int r = 0;
        for (const auto &[key, label, mn, mx, def, dec, step] : fields) {
            auto *f = new ValueField(label, mn, mx, def, dec, step, this);
            f->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
            grid->addWidget(f, r / 2, r % 2, Qt::AlignLeft | Qt::AlignVCenter);
            ++r;
            m_fields.insert(key, f);
            connect(f, &ValueField::valueChanged, this, [this, key = key](double val, bool final) { writeWindow(m_list->currentRow(), key, val, final); });
        }
        m_invert = new QCheckBox(i18nc("@option:check invert window", "Invert"), this);
        m_subtract = new QCheckBox(i18nc("@option:check subtract the window from the others (Resolve's Mask off)", "Subtract"), this);
        grid->addWidget(m_invert, 4, 0);
        grid->addWidget(m_subtract, 4, 1);
        connect(m_invert, &QCheckBox::clicked, this, [this](bool on) { writeWindow(m_list->currentRow(), QStringLiteral("inverser"), on, true); });
        connect(m_subtract, &QCheckBox::clicked, this, [this](bool on) { writeWindow(m_list->currentRow(), QStringLiteral("soustraire"), on, true); });
        grid->setHorizontalSpacing(24);
        grid->setColumnStretch(2, 1);
        auto *right = new QVBoxLayout;
        right->addWidget(sectionLabel(i18nc("@title window transform", "Transform"), this));
        right->addLayout(grid);
        right->addStretch(1);
        h->addLayout(right, 2);
        reloadAll();
    }

    void reload() override
    {
        m_loading = true;
        const int keep = m_list->currentRow();
        m_list->clear();
        const QJsonArray w = current().value(QStringLiteral("fenetres")).toArray();
        const QHash<QString, QString> names{{QStringLiteral("rectangle"), i18nc("@item power window type", "Linear")},
                                            {QStringLiteral("cercle"), i18nc("@item power window type", "Circle")},
                                            {QStringLiteral("polygone"), i18nc("@item power window type", "Polygon")},
                                            {QStringLiteral("degrade"), i18nc("@item power window type", "Gradient")}};
        for (int i = 0; i < w.size(); ++i) {
            const QJsonObject o = w.at(i).toObject();
            auto *it = new QListWidgetItem(QStringLiteral("%1 %2").arg(names.value(o.value(QStringLiteral("type")).toString())).arg(i + 1), m_list);
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(o.value(QStringLiteral("actif")).toBool(true) ? Qt::Checked : Qt::Unchecked);
        }
        m_list->setCurrentRow(std::clamp(keep, w.isEmpty() ? -1 : 0, int(w.size()) - 1));
        m_loading = false;
        reloadFields();
    }

private:
    void reloadFields()
    {
        const QJsonArray w = current().value(QStringLiteral("fenetres")).toArray();
        const int row = m_list->currentRow();
        const QJsonObject o = row >= 0 && row < w.size() ? w.at(row).toObject() : QJsonObject();
        for (auto it = m_fields.constBegin(); it != m_fields.constEnd(); ++it) {
            it.value()->setEnabled(!o.isEmpty());
            static const QHash<QString, double> defs{{QStringLiteral("x"), 0.5},      {QStringLiteral("y"), 0.5}, {QStringLiteral("taille"), 0.25},
                                                     {QStringLiteral("aspect"), 1.},  {QStringLiteral("rotation"), 0.}, {QStringLiteral("doux"), 0.05},
                                                     {QStringLiteral("opacite"), 1.}};
            it.value()->setValue(o.value(it.key()).toDouble(defs.value(it.key())));
        }
        m_invert->setChecked(o.value(QStringLiteral("inverser")).toBool(false));
        m_subtract->setChecked(o.value(QStringLiteral("soustraire")).toBool(false));
        m_invert->setEnabled(!o.isEmpty());
        m_subtract->setEnabled(!o.isEmpty());
    }
    void addWindow(const QString &type)
    {
        QJsonArray w = current().value(QStringLiteral("fenetres")).toArray();
        QJsonObject o{{QStringLiteral("type"), type}, {QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.5}, {QStringLiteral("taille"), 0.25}, {QStringLiteral("doux"), 0.05}};
        if (type == QLatin1String("polygone")) {
            o.insert(QStringLiteral("points"), QJsonArray{QJsonArray{0.35, 0.3}, QJsonArray{0.65, 0.3}, QJsonArray{0.7, 0.7}, QJsonArray{0.3, 0.7}});
        }
        if (type == QLatin1String("rectangle")) {
            o.insert(QStringLiteral("aspect"), 1.3);
        }
        w.append(o);
        setWhole(QStringLiteral("fenetres"), w, i18nc("@action undo text", "add window"), true);
        m_list->setCurrentRow(int(w.size()) - 1);
    }
    void writeWindow(int row, const QString &key, const QJsonValue &value, bool final)
    {
        QJsonArray w = current().value(QStringLiteral("fenetres")).toArray();
        if (row < 0 || row >= w.size()) {
            return;
        }
        QJsonObject o = w.at(row).toObject();
        o.insert(key, value);
        w[row] = o;
        setWhole(QStringLiteral("fenetres"), w, i18nc("@action undo text", "window"), final);
    }
    QListWidget *m_list;
    QHash<QString, ValueField *> m_fields;
    QCheckBox *m_invert, *m_subtract;
    bool m_loading = false;
};

// ---- Flou et Clé -------------------------------------------------------------------------------------------

class KoraCutBlurKeyPanel : public KoraCutPanelBase
{
public:
    explicit KoraCutBlurKeyPanel(bool key, QWidget *parent)
        : KoraCutPanelBase(parent)
        , m_key(key)
    {
        auto *outer = new QHBoxLayout(this);
        outer->setContentsMargins(8, 4, 8, 6);
        outer->setSpacing(32);
        auto *v = new QVBoxLayout;
        outer->addLayout(v);
        outer->setAlignment(v, Qt::AlignTop); // la colonne en haut, à sa hauteur : pas de vide entre les champs
        v->setSpacing(6);
        if (!key) {
            v->addWidget(sectionLabel(i18nc("@title palette", "Blur"), this));
            auto *hint = new QLabel(i18nc("@info", "0.50 changes nothing. Above, the image blurs; below, it gets sharper."), this);
            hint->setWordWrap(false); // une ligne : un QLabel replié dans une rangée se coupe (CLAUDE.md)
            v->addWidget(hint);
            add(v, QStringLiteral("flou"), QStringLiteral("rayon"), i18nc("@label blur radius", "Radius"), 0, 1, 0.5, 3, 0.002);
            add(v, QStringLiteral("flou"), QStringLiteral("hv"), i18nc("@label blur horizontal vertical ratio", "H/V Ratio"), 0, 1, 0.5, 3, 0.002);
        } else {
            v->addWidget(sectionLabel(i18nc("@title key palette", "Key Input"), this));
            addCheck(v, QStringLiteral("entree_inverser"), i18nc("@option:check", "Invert"));
            add(v, QStringLiteral("cle"), QStringLiteral("entree_gain"), i18nc("@label key gain", "Gain"), 0, 2, 1, 2, 0.005);
            add(v, QStringLiteral("cle"), QStringLiteral("entree_decalage"), i18nc("@label key offset", "Offset"), -1, 1, 0, 2, 0.005);
            v->addStretch(1);
            v = new QVBoxLayout;
            outer->addLayout(v);
            outer->setAlignment(v, Qt::AlignTop);
            v->setSpacing(6);
            v->addWidget(sectionLabel(i18nc("@title key palette", "Key Output"), this));
            addCheck(v, QStringLiteral("sortie_inverser"), i18nc("@option:check", "Invert"));
            add(v, QStringLiteral("cle"), QStringLiteral("sortie_gain"), i18nc("@label key gain", "Gain"), 0, 2, 1, 2, 0.005);
            add(v, QStringLiteral("cle"), QStringLiteral("sortie_decalage"), i18nc("@label key offset", "Offset"), -1, 1, 0, 2, 0.005);
            v->addStretch(1);
            v = new QVBoxLayout;
            outer->addLayout(v);
            outer->setAlignment(v, Qt::AlignTop);
            v->setSpacing(6);
            v->addWidget(sectionLabel(i18nc("@title key palette", "Qualifier"), this));
            add(v, QStringLiteral("cle"), QStringLiteral("qualif_gain"), i18nc("@label key gain", "Gain"), 0, 2, 1, 2, 0.005);
            add(v, QStringLiteral("cle"), QStringLiteral("qualif_decalage"), i18nc("@label key offset", "Offset"), -1, 1, 0, 2, 0.005);
        }
        v->addStretch(1);
        outer->addStretch(1);
        reloadAll();
    }
    void reload() override
    {
        for (auto it = m_fields.constBegin(); it != m_fields.constEnd(); ++it) {
            it.key()->setValue(val(it.value().at(0), it.value().at(1)).toDouble(it.value().at(2).toDouble()));
        }
        for (auto it = m_checks.constBegin(); it != m_checks.constEnd(); ++it) {
            it.key()->setChecked(val(QStringLiteral("cle"), it.value()).toBool(false));
        }
    }

private:
    void add(QVBoxLayout *v, const QString &section, const QString &key, const QString &label, double mn, double mx, double def, int dec, double step)
    {
        auto *f = new ValueField(label, mn, mx, def, dec, step, this);
        // Le champ à sa largeur, collé à gauche sous son titre.
        auto *row = new QHBoxLayout;
        row->addWidget(f);
        row->addStretch(1);
        v->addLayout(row);
        m_fields.insert(f, {section, key, QString::number(def)});
        connect(f, &ValueField::valueChanged, this, [this, section, key, label](double value, bool final) { set(section, key, value, label, final); });
    }
    void addCheck(QVBoxLayout *v, const QString &key, const QString &label)
    {
        auto *c = new QCheckBox(label, this);
        v->addWidget(c);
        m_checks.insert(c, key);
        connect(c, &QCheckBox::clicked, this, [this, key, label](bool on) { set(QStringLiteral("cle"), key, on, label, true); });
    }
    bool m_key;
    QHash<ValueField *, QStringList> m_fields;
    QHash<QCheckBox *, QString> m_checks;
};

// ---- Espace couleur (CST) et LUT ----------------------------------------------------------------------------

class KoraCutSpacePanel : public KoraCutPanelBase
{
public:
    explicit KoraCutSpacePanel(QWidget *parent)
        : KoraCutPanelBase(parent)
    {
        auto *h = new QHBoxLayout(this);
        h->setContentsMargins(8, 4, 8, 6);
        auto *left = new QGridLayout;
        auto *head = new QHBoxLayout;
        m_active = new QCheckBox(i18nc("@option:check Color Space Transform on", "Color Space Transform"), this);
        head->addWidget(m_active);
        head->addStretch(1);
        auto *presets = textButton(i18nc("@action:button camera presets for the color space transform", "Camera…"), this);
        presets->setPopupMode(QToolButton::InstantPopup);
        auto *menu = new QMenu(presets);
        const QList<std::tuple<QString, QString, QString>> cams = {
            {QStringLiteral("Sony S-Log3 / S-Gamut3.Cine"), QStringLiteral("sgamut3cine"), QStringLiteral("slog3")},
            {QStringLiteral("Sony S-Log3 / S-Gamut3"), QStringLiteral("sgamut3"), QStringLiteral("slog3")},
            {QStringLiteral("Sony S-Log2 / S-Gamut"), QStringLiteral("sgamut3"), QStringLiteral("slog2")},
            {QStringLiteral("Panasonic V-Log / V-Gamut"), QStringLiteral("vgamut"), QStringLiteral("vlog")},
            {QStringLiteral("Canon Log 3 / Cinema Gamut"), QStringLiteral("cinemagamut"), QStringLiteral("clog3")},
            {QStringLiteral("Canon Log 2 / Cinema Gamut"), QStringLiteral("cinemagamut"), QStringLiteral("clog2")},
            {QStringLiteral("ARRI LogC3 / AWG3"), QStringLiteral("awg3"), QStringLiteral("logc3")},
            {QStringLiteral("ARRI LogC4 / AWG4"), QStringLiteral("awg4"), QStringLiteral("logc4")},
            {QStringLiteral("Blackmagic Film Gen 5"), QStringLiteral("bmdwg5"), QStringLiteral("bmdfilm5")},
            {QStringLiteral("RED Log3G10 / REDWideGamutRGB"), QStringLiteral("rwg"), QStringLiteral("log3g10")},
            {QStringLiteral("Apple Log (iPhone)"), QStringLiteral("rec2020"), QStringLiteral("applelog")},
            {QStringLiteral("iPhone HDR (HLG, Dolby Vision)"), QStringLiteral("rec2020"), QStringLiteral("hlg")},
            {QStringLiteral("DJI D-Log / D-Gamut"), QStringLiteral("dgamut"), QStringLiteral("dlog")},
            {QStringLiteral("Nikon N-Log"), QStringLiteral("rec2020"), QStringLiteral("nlog")},
            {QStringLiteral("Fujifilm F-Log2 / F-Gamut"), QStringLiteral("rec2020"), QStringLiteral("flog2")},
            {QStringLiteral("Fujifilm F-Log / F-Gamut"), QStringLiteral("rec2020"), QStringLiteral("flog")},
            {QStringLiteral("Leica L-Log"), QStringLiteral("rec2020"), QStringLiteral("llog")},
            {QStringLiteral("GoPro GP-Log2"), QStringLiteral("rec2020"), QStringLiteral("gplog2")},
            {QStringLiteral("DaVinci Intermediate / DaVinci Wide Gamut"), QStringLiteral("dwg"), QStringLiteral("davinci_intermediate")}};
        for (const auto &[name, gamut, gamma] : cams) {
            menu->addAction(name, this, [this, gamut = gamut, gamma = gamma]() {
                QJsonObject c = current().value(QStringLiteral("cst")).toObject();
                c.insert(QStringLiteral("actif"), true);
                c.insert(QStringLiteral("entree_gamut"), gamut);
                c.insert(QStringLiteral("entree_gamma"), gamma);
                if (!c.contains(QStringLiteral("sortie_gamut"))) {
                    c.insert(QStringLiteral("sortie_gamut"), QStringLiteral("rec709"));
                    c.insert(QStringLiteral("sortie_gamma"), QStringLiteral("gamma24"));
                }
                c.insert(QStringLiteral("tone_mapping"), c.value(QStringLiteral("tone_mapping")).toString(QStringLiteral("davinci")));
                c.insert(QStringLiteral("gamut_mapping"), c.value(QStringLiteral("gamut_mapping")).toString(QStringLiteral("compression")));
                setWhole(QStringLiteral("cst"), c, i18nc("@action undo text", "color space transform"), true);
            });
        }
        presets->setMenu(menu);
        head->addWidget(presets);
        auto *swap = textButton(i18nc("@action:button swap input and output of the color space transform", "Swap"), this);
        head->addWidget(swap);
        connect(swap, &QToolButton::clicked, this, [this]() {
            QJsonObject c = current().value(QStringLiteral("cst")).toObject();
            const QJsonValue ig = c.value(QStringLiteral("entree_gamut")), it = c.value(QStringLiteral("entree_gamma"));
            c.insert(QStringLiteral("entree_gamut"), c.value(QStringLiteral("sortie_gamut")));
            c.insert(QStringLiteral("entree_gamma"), c.value(QStringLiteral("sortie_gamma")));
            c.insert(QStringLiteral("sortie_gamut"), ig);
            c.insert(QStringLiteral("sortie_gamma"), it);
            setWhole(QStringLiteral("cst"), c, i18nc("@action undo text", "swap color space transform"), true);
        });
        connect(m_active, &QCheckBox::clicked, this, [this](bool on) { setCst(QStringLiteral("actif"), on); });
        auto *leftBox = new QVBoxLayout;
        leftBox->addLayout(head);
        m_inGamut = combo(left, 0, i18nc("@label", "Input Color Space"), QStringLiteral("entree_gamut"), true);
        m_inTf = combo(left, 1, i18nc("@label", "Input Gamma"), QStringLiteral("entree_gamma"), false);
        m_outGamut = combo(left, 2, i18nc("@label", "Output Color Space"), QStringLiteral("sortie_gamut"), true);
        m_outTf = combo(left, 3, i18nc("@label", "Output Gamma"), QStringLiteral("sortie_gamma"), false);
        m_tone = new QComboBox(this);
        for (const auto &[id, name] : {std::pair{QStringLiteral("aucun"), i18nc("@item tone mapping", "None")}, std::pair{QStringLiteral("simple"), i18nc("@item tone mapping", "Simple")},
                                       std::pair{QStringLiteral("davinci"), i18nc("@item tone mapping", "DaVinci")},
                                       std::pair{QStringLiteral("luminance"), i18nc("@item tone mapping", "Luminance Mapping")}}) {
            m_tone->addItem(name, id);
        }
        left->addWidget(new QLabel(i18nc("@label", "Tone Mapping"), this), 4, 0);
        left->addWidget(m_tone, 4, 1);
        connect(m_tone, &QComboBox::activated, this, [this]() { setCst(QStringLiteral("tone_mapping"), m_tone->currentData().toString()); });
        m_gamutMap = new QComboBox(this);
        for (const auto &[id, name] : {std::pair{QStringLiteral("aucun"), i18nc("@item gamut mapping", "None")}, std::pair{QStringLiteral("ecreter"), i18nc("@item gamut mapping", "Clip")},
                                       std::pair{QStringLiteral("compression"), i18nc("@item gamut mapping", "Saturation Compression")}}) {
            m_gamutMap->addItem(name, id);
        }
        left->addWidget(new QLabel(i18nc("@label", "Gamut Mapping"), this), 5, 0);
        left->addWidget(m_gamutMap, 5, 1);
        connect(m_gamutMap, &QComboBox::activated, this, [this]() { setCst(QStringLiteral("gamut_mapping"), m_gamutMap->currentData().toString()); });
        m_maxIn = new ValueField(i18nc("@label tone mapping, 0 = automatic", "Max Input (nits)"), 0, 10000, 0, 0, 10, this);
        m_maxOut = new ValueField(i18nc("@label tone mapping", "Max Output (nits)"), 48, 10000, 100, 0, 5, this);
        auto *nits = new QHBoxLayout;
        nits->addWidget(m_maxIn);
        nits->addStretch(1);
        nits->addWidget(m_maxOut);
        left->addLayout(nits, 6, 0, 1, 2);
        left->setVerticalSpacing(4);
        connect(m_maxIn, &ValueField::valueChanged, this, [this](double v, bool final) { setCst(QStringLiteral("nits_entree"), v, final); });
        connect(m_maxOut, &ValueField::valueChanged, this, [this](double v, bool final) { setCst(QStringLiteral("nits_sortie"), v, final); });
        leftBox->addLayout(left);
        leftBox->addStretch(1);
        h->addLayout(leftBox, 3);

        // La LUT du nœud (appliquée après les primaires, comme dans Resolve).
        auto *right = new QVBoxLayout;
        right->addWidget(sectionLabel(i18nc("@title", "LUT"), this));
        m_lutName = new QLabel(this);
        m_lutName->setWordWrap(true);
        right->addWidget(m_lutName);
        m_lutList = new QListWidget(this);
        // La liste se contente de peu de hauteur : les boutons et le mélange restent visibles en dessous.
        m_lutList->setMinimumHeight(48);
        m_lutList->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
        right->addWidget(m_lutList, 1);
        auto *lutButtons = new QHBoxLayout;
        auto *choose = textButton(i18nc("@action:button choose a LUT file", "Open…"), this);
        auto *remove = textButton(i18nc("@action:button remove the LUT", "Remove"), this);
        lutButtons->addWidget(choose);
        lutButtons->addWidget(remove);
        right->addLayout(lutButtons);
        m_lutMix = new ValueField(i18nc("@label LUT strength", "Mix"), 0, 100, 100, 1, 0.5, this);
        right->addWidget(m_lutMix);
        h->addLayout(right, 2);
        connect(choose, &QToolButton::clicked, this, [this]() {
            const QString file = QFileDialog::getOpenFileName(this, i18nc("@title:window", "Choose a LUT"), QString(), i18nc("@item file type", "LUT (*.cube)"));
            if (!file.isEmpty()) {
                setLut(file);
            }
        });
        connect(remove, &QToolButton::clicked, this, [this]() { setWhole(QStringLiteral("lut"), QJsonValue(), i18nc("@action undo text", "remove LUT"), true); });
        connect(m_lutList, &QListWidget::itemActivated, this, [this](QListWidgetItem *it) { setLut(it->data(Qt::UserRole).toString()); });
        connect(m_lutMix, &ValueField::valueChanged, this, [this](double v, bool final) { set(QStringLiteral("lut"), QStringLiteral("melange"), v, i18nc("@action undo text", "LUT mix"), final); });
        reloadAll();
        fillLuts();
    }

    void reload() override
    {
        const QJsonObject c = current().value(QStringLiteral("cst")).toObject();
        m_active->setChecked(!c.isEmpty() && c.value(QStringLiteral("actif")).toBool(true));
        auto sel = [](QComboBox *box, const QString &id) {
            const int i = box->findData(id);
            box->setCurrentIndex(std::max(0, i));
        };
        sel(m_inGamut, c.value(QStringLiteral("entree_gamut")).toString(QStringLiteral("rec709")));
        sel(m_inTf, c.value(QStringLiteral("entree_gamma")).toString(QStringLiteral("gamma24")));
        sel(m_outGamut, c.value(QStringLiteral("sortie_gamut")).toString(QStringLiteral("rec709")));
        sel(m_outTf, c.value(QStringLiteral("sortie_gamma")).toString(QStringLiteral("gamma24")));
        sel(m_tone, c.value(QStringLiteral("tone_mapping")).toString(QStringLiteral("davinci")));
        sel(m_gamutMap, c.value(QStringLiteral("gamut_mapping")).toString(QStringLiteral("compression")));
        m_maxIn->setValue(c.value(QStringLiteral("nits_entree")).toDouble(0));
        m_maxOut->setValue(c.value(QStringLiteral("nits_sortie")).toDouble(100));
        const QJsonObject l = current().value(QStringLiteral("lut")).toObject();
        const QString file = l.value(QStringLiteral("fichier")).toString();
        m_lutName->setText(file.isEmpty() ? i18nc("@info no LUT on this node", "No LUT on this node.") : QFileInfo(file).fileName());
        m_lutMix->setValue(l.value(QStringLiteral("melange")).toDouble(100));
    }

private:
    QComboBox *combo(QGridLayout *grid, int row, const QString &label, const QString &key, bool gamut)
    {
        auto *box = new QComboBox(this);
        if (gamut) {
            for (const kcgrade::GamutInfo &g : kcgrade::gamuts()) {
                box->addItem(QString::fromUtf8(g.label), QString::fromLatin1(g.id));
            }
        } else {
            for (const kcgrade::TransferInfo &t : kcgrade::transfers()) {
                box->addItem(QString::fromUtf8(t.label), QString::fromLatin1(t.id));
            }
        }
        grid->addWidget(new QLabel(label, this), row, 0);
        grid->addWidget(box, row, 1);
        connect(box, &QComboBox::activated, this, [this, box, key]() { setCst(key, box->currentData().toString()); });
        return box;
    }
    void setCst(const QString &key, const QJsonValue &v, bool final = true)
    {
        QJsonObject c = current().value(QStringLiteral("cst")).toObject();
        c.insert(key, v);
        if (key != QLatin1String("actif")) {
            c.insert(QStringLiteral("actif"), true);
        }
        setWhole(QStringLiteral("cst"), c, i18nc("@action undo text", "color space transform"), final);
    }
    void setLut(const QString &file)
    {
        QJsonObject l = current().value(QStringLiteral("lut")).toObject();
        l.insert(QStringLiteral("fichier"), file);
        if (!l.contains(QStringLiteral("melange"))) {
            l.insert(QStringLiteral("melange"), 100);
        }
        setWhole(QStringLiteral("lut"), l, i18nc("@action undo text", "apply LUT"), true);
    }
    void fillLuts()
    {
        m_lutList->clear();
        for (const QString &folder : KoraCutGradeController::lutFolders()) {
            const QFileInfoList files = QDir(folder).entryInfoList({QStringLiteral("*.cube")}, QDir::Files, QDir::Name);
            for (const QFileInfo &f : files) {
                auto *it = new QListWidgetItem(f.completeBaseName(), m_lutList);
                it->setData(Qt::UserRole, f.absoluteFilePath());
                it->setToolTip(f.absoluteFilePath());
            }
        }
    }
    QCheckBox *m_active;
    QComboBox *m_inGamut, *m_inTf, *m_outGamut, *m_outTf, *m_tone, *m_gamutMap;
    ValueField *m_maxIn, *m_maxOut, *m_lutMix;
    QLabel *m_lutName;
    QListWidget *m_lutList;
};

// ---- La barre des palettes -----------------------------------------------------------------------------------

KoraCutColorPalettes::KoraCutColorPalettes(QWidget *parent)
    : QWidget(parent)
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("koracutPaletteBar"));
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(8, 2, 8, 2);
    bl->setSpacing(2);
    m_buttons = new QButtonGroup(this);
    m_stack = new QStackedWidget(this);
    auto addPalette = [&](const QString &id, const QString &name, QWidget *panel) {
        QToolButton *b = textButton(name, bar, true);
        m_buttons->addButton(b, int(m_ids.size()));
        bl->addWidget(b);
        m_ids << id;
        auto *scroll = new QScrollArea(m_stack);
        scroll->setWidget(panel);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        m_stack->addWidget(scroll);
    };
    m_primaries = new KoraCutPrimariesPanel(this);
    addPalette(QStringLiteral("primaires"), i18nc("@action:button palette", "Primaries"), m_primaries);
    addPalette(QStringLiteral("courbes"), i18nc("@action:button palette", "Curves"), new KoraCutCurvesPanel(this));
    addPalette(QStringLiteral("qualificateur"), i18nc("@action:button palette", "Qualifier"), new KoraCutQualifierPanel(this));
    addPalette(QStringLiteral("fenetres"), i18nc("@action:button palette, power windows", "Windows"), new KoraCutWindowsPanel(this));
    addPalette(QStringLiteral("flou"), i18nc("@action:button palette", "Blur"), new KoraCutBlurKeyPanel(false, this));
    addPalette(QStringLiteral("cle"), i18nc("@action:button palette", "Key"), new KoraCutBlurKeyPanel(true, this));
    addPalette(QStringLiteral("espace"), i18nc("@action:button palette, color space transform and LUT", "Color Space"), new KoraCutSpacePanel(this));
    bl->addStretch(1);
    m_title = new QLabel(bar);
    bl->addWidget(m_title);
    v->addWidget(bar);
    v->addWidget(m_stack, 1);
    connect(m_buttons, &QButtonGroup::idClicked, this, [this](int id) { m_stack->setCurrentIndex(id); });
    m_buttons->button(0)->setChecked(true);
    connect(KoraCutGradeController::self(), &KoraCutGradeController::nodeChanged, this, &KoraCutColorPalettes::updateTitle);
    connect(KoraCutGradeController::self(), &KoraCutGradeController::clipChanged, this, &KoraCutColorPalettes::updateTitle);
    connect(KoraCutGradeController::self(), &KoraCutGradeController::graphChanged, this, &KoraCutColorPalettes::updateTitle);
    updateTitle();
}

void KoraCutColorPalettes::updateTitle()
{
    auto *c = KoraCutGradeController::self();
    const QJsonObject g = c->graph();
    const int number = displayNumber(g, c->currentNode());
    const QString label = node(g, c->currentNode()).value(QStringLiteral("etiquette")).toString();
    QString text = number > 0 ? i18nc("@info current node in the color palettes, %1 is its number", "Node %1", QStringLiteral("%1").arg(number, 2, 10, QLatin1Char('0'))) : QString();
    if (!label.isEmpty()) {
        text += QStringLiteral(" · ") + label;
    }
    m_title->setText(text);
}

void KoraCutColorPalettes::showPalette(const QString &id)
{
    const int i = int(m_ids.indexOf(id));
    if (i >= 0) {
        m_buttons->button(i)->setChecked(true);
        m_stack->setCurrentIndex(i);
    }
}

QString KoraCutColorPalettes::currentPalette() const
{
    return m_ids.value(m_stack->currentIndex());
}

void KoraCutColorPalettes::setPrimariesMode(const QString &mode)
{
    showPalette(QStringLiteral("primaires"));
    m_primaries->setMode(mode);
}

void KoraCutColorPalettes::togglePrimariesLog()
{
    setPrimariesMode(m_primaries->mode() == QLatin1String("log") ? QStringLiteral("roues") : QStringLiteral("log"));
}
