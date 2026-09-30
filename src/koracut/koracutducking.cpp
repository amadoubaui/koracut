/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutducking.h"

#include <QLocale>
#include <QMap>
#include <QStringList>
#include <cmath>

namespace KoraCutDucking {

namespace {
int msToFrames(int ms, double fps)
{
    return qMax(0, int(std::lround(ms * fps / 1000.)));
}
}

double peakToDb(int peak)
{
    if (peak <= 0) {
        return -144.;
    }
    return 20. * std::log10(double(peak) / 32768.);
}

QList<Interval> duckedIntervals(const QVector<double> &voiceDb, const Settings &settings, double fps)
{
    // 1. Les images où l'on parle, regroupées en segments.
    QList<Interval> voice;
    int start = -1;
    for (int i = 0; i <= voiceDb.size(); ++i) {
        const bool on = i < voiceDb.size() && voiceDb.at(i) >= settings.thresholdDb;
        if (on && start < 0) {
            start = i;
        } else if (!on && start >= 0) {
            voice << Interval(start, i);
            start = -1;
        }
    }
    // 2. Les silences courts entre deux paroles ne comptent pas : une phrase a des respirations.
    const int mergeGap = msToFrames(settings.mergeGapMs, fps);
    QList<Interval> merged;
    for (const Interval &v : std::as_const(voice)) {
        if (!merged.isEmpty() && v.first - merged.last().second <= mergeGap) {
            merged.last().second = v.second;
        } else {
            merged << v;
        }
    }
    // 3. Un clic, une porte qui claque : trop court pour être de la voix.
    const int minVoice = qMax(1, msToFrames(settings.minVoiceMs, fps));
    // 4. On élargit : la musique est déjà basse quand la voix arrive, et le reste un peu après.
    const int anticipation = msToFrames(settings.anticipationMs, fps);
    const int hold = msToFrames(settings.holdMs, fps);
    const int fade = msToFrames(settings.fadeMs, fps);
    QList<Interval> result;
    for (const Interval &v : std::as_const(merged)) {
        if (v.second - v.first < minVoice) {
            continue;
        }
        Interval d(qMax(0, v.first - anticipation), v.second + hold);
        // Deux zones assez proches pour que la remontée et la descente se touchent : une seule.
        if (!result.isEmpty() && d.first - result.last().second <= 2 * fade) {
            result.last().second = qMax(result.last().second, d.second);
        } else {
            result << d;
        }
    }
    return result;
}

QList<QPair<int, double>> clipKeyframes(const QList<Interval> &ducked, int clipStart, int clipLength, const Settings &settings, double fps)
{
    const int clipEnd = clipStart + clipLength;
    const int fade = qMax(1, msToFrames(settings.fadeMs, fps));
    const double low = -qAbs(settings.duckDb);
    // Position de timeline -> gain. Une QMap garde l'ordre et écrase les doublons.
    QMap<int, double> points;
    bool touched = false;
    for (const Interval &d : ducked) {
        if (d.second + fade <= clipStart || d.first - fade >= clipEnd) {
            continue;
        }
        touched = true;
        points.insert(d.first - fade, 0.);
        points.insert(d.first, low);
        points.insert(d.second, low);
        points.insert(d.second + fade, 0.);
    }
    if (!touched) {
        return {};
    }
    // Valeur du gain à une position quelconque : interpolation linéaire entre les points posés.
    auto valueAt = [&points](int pos) {
        auto next = points.lowerBound(pos);
        if (next == points.end()) {
            return std::prev(next).value();
        }
        if (next.key() == pos || next == points.begin()) {
            return next.value();
        }
        auto prev = std::prev(next);
        const double t = double(pos - prev.key()) / double(next.key() - prev.key());
        return prev.value() + t * (next.value() - prev.value());
    };
    const int last = clipEnd - 1;
    QList<QPair<int, double>> result;
    result << qMakePair(0, valueAt(clipStart));
    for (auto it = points.cbegin(); it != points.cend(); ++it) {
        if (it.key() > clipStart && it.key() < last) {
            result << qMakePair(it.key() - clipStart, it.value());
        }
    }
    if (last > clipStart) {
        result << qMakePair(last - clipStart, valueAt(last));
    }
    return result;
}

QString animationString(const QList<QPair<int, double>> &keyframes, int inPoint)
{
    QStringList parts;
    const QLocale c = QLocale::c();
    for (const auto &k : keyframes) {
        parts << QStringLiteral("%1=%2").arg(k.first + inPoint).arg(c.toString(k.second, 'f', 2));
    }
    return parts.join(QLatin1Char(';'));
}

}
