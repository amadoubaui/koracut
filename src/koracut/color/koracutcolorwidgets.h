/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

// Les contrôles de la page Étalonnage, qui se manipulent comme ceux de DaVinci Resolve
// (document 22) :
// - un nombre se règle en glissant sur son nom ou sa valeur, se saisit par double-clic sur la valeur,
//   revient à son défaut par double-clic sur le nom ; Maj pour un réglage fin ;
// - une roue se règle en glissant n'importe où dans l'anneau (comme une boule), double-clic : la
//   couleur revient au centre sans toucher au niveau ; la molette dessous règle le niveau (master) ;
// - une courbe : clic pour ajouter un point, glisser pour le déplacer, clic droit pour le retirer.

#include <QJsonArray>
#include <QList>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

class QLineEdit;

namespace KoraCutColorUi {

/** @brief Les couleurs de la page, tirées du thème courant (jetons de la charte). */
QColor tone(const char *token, int alpha = -1);

class ValueField : public QWidget
{
    Q_OBJECT
public:
    ValueField(const QString &label, double min, double max, double def, int decimals, double step, QWidget *parent = nullptr);
    double value() const { return m_value; }
    void setValue(double v);
    void setLabel(const QString &label);
    void setRange(double min, double max);
    QSize sizeHint() const override;

Q_SIGNALS:
    /** @brief final = false pendant le glisser. */
    void valueChanged(double value, bool final);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private:
    QRect valueRect() const;
    QString text() const;
    void startEdit();
    QString m_label;
    double m_min, m_max, m_def, m_step;
    int m_decimals;
    double m_value;
    bool m_dragging = false;
    bool m_moved = false;
    double m_pressValue = 0.;
    QPointF m_pressPos;
    QLineEdit *m_edit = nullptr;
};

/** @brief La molette horizontale sous une roue : glisser à gauche assombrit, à droite éclaircit. */
class Jog : public QWidget
{
    Q_OBJECT
public:
    explicit Jog(QWidget *parent = nullptr);
    QSize sizeHint() const override { return {150, 14}; }
Q_SIGNALS:
    void moved(double delta, bool final);
    void resetRequested();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;

private:
    double m_last = 0.;
    double m_phase = 0.;
    bool m_down = false;
};

/** @brief Une roue de couleur (Lift, Gamma, Gain, Offset, ou les roues log). Valeurs affichées de
 * Resolve : m_values = {Y, R, V, B} (Offset et les roues log : Y absent). */
class ColorWheel : public QWidget
{
    Q_OBJECT
public:
    ColorWheel(const QString &title, bool hasY, double neutral, double scale, int decimals, QWidget *parent = nullptr);
    void setValues(const QVector<double> &values); // taille 4 (Y R V B), Y ignoré sans hasY
    QVector<double> values() const { return m_values; }
    void setTitle(const QString &t);
    QSize sizeHint() const override { return {170, 230}; }
    // La roue suit la hauteur de la palette : à 1440 × 900, tout tient sans défiler (titre, roue, champs, molette).
    QSize minimumSizeHint() const override { return {112, 136}; }

Q_SIGNALS:
    void valuesChanged(const QVector<double> &values, bool final);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    QRectF wheelRect() const;
    QRect resetRect() const;
    QPointF point() const; // position du point, -1..1
    void setFromChroma(double u, double v, bool final);
    void emitChanged(bool final);
    QString m_title;
    bool m_hasY;
    double m_neutral, m_scale;
    int m_decimals;
    QVector<double> m_values;
    QList<ValueField *> m_fields;
    Jog *m_jog = nullptr;
    bool m_dragging = false;
    bool m_master = false;
    QPointF m_lastPos;
    double m_u = 0., m_v = 0.;
    QImage m_ring;
};

/** @brief Une courbe éditable : diagonale (Custom Y R V B) ou plate (courbes de teinte). */
class CurveEditor : public QWidget
{
    Q_OBJECT
public:
    enum class Kind { Diagonal, HueFlat, Flat };
    explicit CurveEditor(QWidget *parent = nullptr);
    void setKind(Kind kind, double neutral, double yMin, double yMax);
    void setColor(const QColor &c);
    void setPoints(const QJsonArray &points);
    QJsonArray points() const;
    QSize minimumSizeHint() const override { return {220, 160}; }

Q_SIGNALS:
    void pointsChanged(const QJsonArray &points, bool final);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;

private:
    QRectF area() const;
    QPointF toScreen(double x, double y) const;
    QPointF toValue(const QPointF &p) const;
    double xMax() const { return m_kind == Kind::HueFlat ? 360. : 1.; }
    double eval(double x) const;
    int hit(const QPointF &p) const;
    Kind m_kind = Kind::Diagonal;
    double m_neutral = 0., m_yMin = 0., m_yMax = 1.;
    QColor m_color;
    QList<QPointF> m_points;
    int m_drag = -1;
};

/** @brief Une barre de plage (qualificateur) : bas, haut et leurs adoucissements, sur 0..100. */
class RangeBar : public QWidget
{
    Q_OBJECT
public:
    enum class Style { Hue, Saturation, Luma };
    explicit RangeBar(Style style, QWidget *parent = nullptr);
    void setRange(double low, double high, double lowSoft, double highSoft);
    QSize minimumSizeHint() const override { return {200, 26}; }

Q_SIGNALS:
    void rangeChanged(double low, double high, double lowSoft, double highSoft, bool final);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    double toValue(double x) const;
    double toX(double v) const;
    Style m_style;
    double m_low = 0., m_high = 100., m_lowSoft = 0., m_highSoft = 0.;
    int m_drag = -1; // 0 bas, 1 haut, 2 tout, 3 doux bas, 4 doux haut
    double m_pressValue = 0.;
    double m_pressLow = 0., m_pressHigh = 0.;
};

} // namespace KoraCutColorUi
