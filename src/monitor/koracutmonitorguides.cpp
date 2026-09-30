/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutmonitorguides.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>
#include <cmath>

namespace KoraCutMonitorGuides {

static QRectF readRect(const QJsonObject &o)
{
    return QRectF(o.value(QLatin1String("x")).toDouble(), o.value(QLatin1String("y")).toDouble(), o.value(QLatin1String("w")).toDouble(),
                  o.value(QLatin1String("h")).toDouble());
}

QList<Platform> loadPlatforms(const QString &path)
{
    QList<Platform> result;
    QFile file(path.isEmpty() ? QStringLiteral(":/data/koracut-reperes-plateformes.json") : path);
    if (!file.open(QIODevice::ReadOnly)) {
        return result;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray list = root.value(QLatin1String("plateformes")).toArray();
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        Platform p;
        p.id = o.value(QLatin1String("id")).toString();
        p.name = o.value(QLatin1String("nom")).toString();
        const QJsonArray kw = o.value(QLatin1String("motsCles")).toArray();
        for (const QJsonValue &k : kw) {
            p.keywords << k.toString().toLower();
        }
        const QJsonArray ratio = o.value(QLatin1String("ratio")).toArray();
        p.ratio = ratio.size() == 2 ? QSize(ratio.at(0).toInt(), ratio.at(1).toInt()) : QSize(16, 9);
        const QJsonArray zones = o.value(QLatin1String("zones")).toArray();
        for (const QJsonValue &z : zones) {
            const QJsonObject zo = z.toObject();
            Zone zone;
            zone.id = zo.value(QLatin1String("id")).toString();
            zone.label = zo.value(QLatin1String("etiquette")).toString();
            zone.rect = readRect(zo);
            zone.lastSeconds = zo.value(QLatin1String("dernieresSecondes")).toInt();
            p.zones << zone;
        }
        p.safe = readRect(o.value(QLatin1String("sure")).toObject());
        if (!p.id.isEmpty() && p.ratio.width() > 0 && p.ratio.height() > 0) {
            result << p;
        }
    }
    return result;
}

Platform platform(const QString &id, const QList<Platform> &platforms)
{
    for (const Platform &p : platforms) {
        if (p.id == id) {
            return p;
        }
    }
    return Platform();
}

QRectF platformFrame(const Platform &platform, const QSize &project)
{
    if (project.isEmpty() || platform.ratio.isEmpty()) {
        return QRectF();
    }
    const double target = double(platform.ratio.width()) / platform.ratio.height();
    const double current = double(project.width()) / project.height();
    double w = project.width();
    double h = project.height();
    if (current > target) {
        // Projet plus large que la plateforme : on garde la hauteur, on centre en largeur.
        w = std::round(project.height() * target);
    } else if (current < target) {
        h = std::round(project.width() / target);
    }
    return QRectF(std::round((project.width() - w) / 2.), std::round((project.height() - h) / 2.), w, h);
}

static QVariantMap zoneMap(const QString &id, const QString &label, const QString &kind, const QRectF &r, int lastFrames)
{
    QVariantMap m;
    m.insert(QStringLiteral("id"), id);
    m.insert(QStringLiteral("label"), label);
    m.insert(QStringLiteral("kind"), kind);
    m.insert(QStringLiteral("x"), r.x());
    m.insert(QStringLiteral("y"), r.y());
    m.insert(QStringLiteral("w"), r.width());
    m.insert(QStringLiteral("h"), r.height());
    m.insert(QStringLiteral("lastFrames"), lastFrames);
    return m;
}

QVariantList platformZones(const Platform &platform, const QSize &project, double fps)
{
    QVariantList result;
    const QRectF frame = platformFrame(platform, project);
    if (frame.isEmpty()) {
        return result;
    }
    // Ce qui déborde du cadre de la plateforme : la vidéo y sera recadrée ou entourée de noir.
    if (frame.width() < project.width()) {
        result << zoneMap(QStringLiteral("hors-gauche"), QString(), QStringLiteral("outside"), QRectF(0, 0, frame.x(), project.height()), 0);
        result << zoneMap(QStringLiteral("hors-droite"), QString(), QStringLiteral("outside"),
                          QRectF(frame.right(), 0, project.width() - frame.right(), project.height()), 0);
    }
    if (frame.height() < project.height()) {
        result << zoneMap(QStringLiteral("hors-haut"), QString(), QStringLiteral("outside"), QRectF(0, 0, project.width(), frame.y()), 0);
        result << zoneMap(QStringLiteral("hors-bas"), QString(), QStringLiteral("outside"),
                          QRectF(0, frame.bottom(), project.width(), project.height() - frame.bottom()), 0);
    }
    auto scaled = [&frame](const QRectF &f) {
        return QRectF(frame.x() + f.x() * frame.width(), frame.y() + f.y() * frame.height(), f.width() * frame.width(), f.height() * frame.height());
    };
    for (const Zone &z : platform.zones) {
        const int lastFrames = z.lastSeconds > 0 && fps > 0 ? int(std::lround(z.lastSeconds * fps)) : 0;
        result << zoneMap(z.id, z.label, QStringLiteral("zone"), scaled(z.rect), lastFrames);
    }
    if (!platform.safe.isEmpty()) {
        result << zoneMap(QStringLiteral("sure"), QString(), QStringLiteral("safe"), scaled(platform.safe), 0);
    }
    return result;
}

QString platformForVideoType(const QString &platformsText, const QList<Platform> &platforms)
{
    const QString text = platformsText.toLower();
    // La première citée gagne ; à position égale, le mot-clé le plus long (« youtube shorts »
    // l'emporte sur « youtube »).
    int bestPos = -1;
    int bestLen = 0;
    QString best;
    for (const Platform &p : platforms) {
        for (const QString &k : p.keywords) {
            const int pos = text.indexOf(k);
            if (pos > -1 && (bestPos == -1 || pos < bestPos || (pos == bestPos && k.length() > bestLen))) {
                bestPos = pos;
                bestLen = k.length();
                best = p.id;
            }
        }
    }
    return best;
}

static void snapAxis(double start, double size, const QList<double> &targets, double threshold, double &shift, double &line)
{
    shift = 0;
    line = -1;
    double bestDist = threshold + 1;
    const double edges[3] = {start, start + size / 2., start + size};
    for (double edge : edges) {
        for (double t : targets) {
            const double d = std::abs(edge - t);
            if (d <= threshold && d < bestDist) {
                bestDist = d;
                shift = t - edge;
                line = t;
            }
        }
    }
}

SnapResult snapRect(const QRectF &rect, const QList<double> &targetsX, const QList<double> &targetsY, double threshold)
{
    SnapResult r;
    double shiftX = 0, shiftY = 0;
    snapAxis(rect.x(), rect.width(), targetsX, threshold, shiftX, r.lineX);
    snapAxis(rect.y(), rect.height(), targetsY, threshold, shiftY, r.lineY);
    r.rect = QRectF(rect.x() + shiftX, rect.y() + shiftY, rect.width(), rect.height());
    return r;
}

QList<double> axisTargets(double size, const QList<double> &guides)
{
    QList<double> t{0., size / 3., size / 2., 2. * size / 3., size};
    t.append(guides);
    return t;
}

double rulerStep(double scale)
{
    static const double steps[] = {1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000};
    if (scale <= 0) {
        return 100;
    }
    for (double s : steps) {
        if (s * scale >= 60.) {
            return s;
        }
    }
    return 5000;
}

double zoomToPercent(double zoom, double displayWidth, int profileWidth)
{
    if (profileWidth <= 0 || displayWidth <= 0) {
        return 100.;
    }
    return zoom * displayWidth / profileWidth * 100.;
}

double percentToZoom(double percent, double displayWidth, int profileWidth)
{
    if (profileWidth <= 0 || displayWidth <= 0) {
        return 1.;
    }
    return percent / 100. * profileWidth / displayWidth;
}

QList<int> zoomSteps()
{
    return {25, 50, 100, 200, 400};
}

} // namespace KoraCutMonitorGuides
