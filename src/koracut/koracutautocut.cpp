/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutautocut.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace KoraCutAutoCut {

namespace {
int msToFrames(int ms, double fps)
{
    return qMax(0, int(std::lround(ms * fps / 1000.)));
}

double percentile(QVector<double> values, double p)
{
    if (values.isEmpty()) {
        return -144.;
    }
    std::sort(values.begin(), values.end());
    const int index = qBound(0, int(std::floor(p * (values.size() - 1))), int(values.size()) - 1);
    return values.at(index);
}
}

SilenceSettings SilenceSettings::forRythme(Rythme rythme)
{
    SilenceSettings s;
    switch (rythme) {
    case Rythme::Pose:
        s.minSilenceMs = 800;
        s.minSpeechMs = 250;
        s.padBeforeMs = 200;
        s.padAfterMs = 250;
        break;
    case Rythme::Normal:
        break;
    case Rythme::Nerveux:
        s.minSilenceMs = 300;
        s.minSpeechMs = 150;
        s.padBeforeMs = 50;
        s.padAfterMs = 80;
        break;
    }
    return s;
}

double estimateThreshold(const QVector<double> &levelsDb)
{
    // Les images où rien ne joue (pas de clip) ne disent rien du bruit de la prise.
    QVector<double> played;
    played.reserve(levelsDb.size());
    for (double v : levelsDb) {
        if (v > -143.) {
            played << v;
        }
    }
    if (played.isEmpty()) {
        return -40.;
    }
    // Une voix de synthèse a un silence numérique (-144) : on le ramène au plancher utile.
    const double floorDb = qMax(-90., percentile(played, 0.10));
    const double voiceDb = percentile(played, 0.90);
    return qBound(-60., floorDb + 0.25 * (voiceDb - floorDb), -25.);
}

QList<Interval> silenceIntervals(const QVector<double> &levelsDb, const SilenceSettings &settings, double fps, double *usedThreshold)
{
    const double threshold = settings.autoThreshold ? estimateThreshold(levelsDb) : settings.thresholdDb;
    if (usedThreshold) {
        *usedThreshold = threshold;
    }
    const int n = int(levelsDb.size());
    // 1. La parole : images au-dessus du seuil, regroupées.
    QList<Interval> speech;
    int start = -1;
    for (int i = 0; i <= n; ++i) {
        const bool on = i < n && levelsDb.at(i) >= threshold;
        if (on && start < 0) {
            start = i;
        } else if (!on && start >= 0) {
            speech << Interval(start, i);
            start = -1;
        }
    }
    // 2. Un clic, un souffle : trop court pour être de la parole, il fait partie du silence.
    const int minSpeech = msToFrames(settings.minSpeechMs, fps);
    QList<Interval> kept;
    for (const Interval &s : std::as_const(speech)) {
        if (s.second - s.first >= qMax(1, minSpeech)) {
            kept << s;
        }
    }
    // 3. Les silences sont les trous entre les paroles, plus le début et la fin.
    QList<Interval> gaps;
    int cursor = 0;
    for (const Interval &s : std::as_const(kept)) {
        if (s.first > cursor) {
            gaps << Interval(cursor, s.first);
        }
        cursor = s.second;
    }
    if (cursor < n) {
        gaps << Interval(cursor, n);
    }
    // 4. On garde les silences assez longs, rognés de leurs marges côté parole.
    const int minSilence = msToFrames(settings.minSilenceMs, fps);
    const int padBefore = msToFrames(settings.padBeforeMs, fps);
    const int padAfter = msToFrames(settings.padAfterMs, fps);
    QList<Interval> result;
    for (const Interval &g : std::as_const(gaps)) {
        if (g.second - g.first < qMax(1, minSilence)) {
            continue;
        }
        // Après une parole (le silence ne commence pas à 0) : on laisse finir la phrase.
        const int from = g.first > 0 ? g.first + padAfter : g.first;
        // Avant une parole (le silence ne va pas jusqu'au bout) : on laisse l'attaque.
        const int to = g.second < n ? g.second - padBefore : g.second;
        if (to > from) {
            result << Interval(from, to);
        }
    }
    return result;
}

int totalFrames(const QList<Interval> &intervals)
{
    int total = 0;
    for (const Interval &i : intervals) {
        total += i.second - i.first;
    }
    return total;
}


QList<Interval> mergeIntervals(QList<Interval> intervals)
{
    std::sort(intervals.begin(), intervals.end());
    QList<Interval> merged;
    for (const Interval &i : std::as_const(intervals)) {
        if (i.second <= i.first) {
            continue;
        }
        if (!merged.isEmpty() && i.first <= merged.last().second) {
            merged.last().second = qMax(merged.last().second, i.second);
        } else {
            merged << i;
        }
    }
    return merged;
}

QList<Word> parseWords(const QByteArray &json)
{
    QList<Word> words;
    const QJsonArray mots = QJsonDocument::fromJson(json).object().value(QStringLiteral("mots")).toArray();
    for (const QJsonValue &v : mots) {
        const QJsonObject o = v.toObject();
        Word w;
        w.text = o.value(QStringLiteral("m")).toString();
        w.start = o.value(QStringLiteral("d")).toDouble();
        w.end = o.value(QStringLiteral("f")).toDouble();
        if (!w.text.isEmpty() && w.end >= w.start) {
            words << w;
        }
    }
    return words;
}

QList<Word> mergeTranscriptions(QList<Word> words)
{
    std::sort(words.begin(), words.end(), [](const Word &a, const Word &b) { return a.start < b.start; });
    QList<Word> kept;
    for (const Word &w : std::as_const(words)) {
        const QString n = normalizeWord(w.text);
        bool duplicate = false;
        // Les mots gardés qui commencent avant la fin de celui-ci et n'ont pas fini avant son début.
        for (int k = int(kept.size()) - 1; k >= 0 && !duplicate; --k) {
            const Word &o = kept.at(k);
            if (o.end < w.start - 2.) {
                break;
            }
            const double overlap = qMin(o.end, w.end) - qMax(o.start, w.start);
            const double shorter = qMax(0.001, qMin(o.end - o.start, w.end - w.start));
            // Ou le même mot qui commence presque au même moment : Whisper donne parfois 20 ms à un
            // mot court (« 'art »), et deux micros calés à 50 ms près ne se chevauchent plus.
            duplicate = (overlap > 0.5 * shorter || qAbs(o.start - w.start) <= 0.15) && normalizeWord(o.text) == n;
        }
        if (!duplicate) {
            kept << w;
        }
    }
    return kept;
}

QString normalizeWord(const QString &word)
{
    // La ponctuation que Whisper colle aux mots, et l'apostrophe qu'il laisse en tête (« 'hui »).
    static const QRegularExpression bords(QStringLiteral("^[\\s'’\"«»(\\[.,;:!?…-]+|[\\s'’\"«»)\\].,;:!?…-]+$"));
    return word.toLower().remove(bords).trimmed();
}

QList<Filler> findFillers(const QList<Word> &words, const QVector<double> &levelsDb, double thresholdDb, const FillerSettings &settings, double fps)
{
    QList<Filler> fillers;
    const double margin = settings.marginMs / 1000.;
    const double isolatedMax = settings.isolatedMaxMs / 1000.;
    const double pause = settings.isolatedPauseMs / 1000.;
    auto toFrames = [fps](double from, double to) {
        // Arrondi au plus proche, pas plancher/plafond : sinon la coupe mord d'une image (40 ms)
        // sur le mot voisin, et on entend une syllabe amputée.
        return Interval(qMax(0, int(std::lround(from * fps))), int(std::lround(to * fps)));
    };
    // L'air autour d'un mot, sans jamais mordre sur le mot d'avant ni sur celui d'après.
    auto withMargin = [&](int first, int last) {
        const double lower = first > 0 ? words.at(first - 1).end : 0.;
        const double upper = last + 1 < words.size() ? words.at(last + 1).start : words.at(last).end + margin;
        return toFrames(qMax(lower, words.at(first).start - margin), qMin(upper, words.at(last).end + margin));
    };
    for (int i = 0; i < words.size(); ++i) {
        // Les expressions de plusieurs mots d'abord (« du coup », « en fait »).
        bool matched = false;
        for (const QString &phrase : settings.ambiguous) {
            const QStringList parts = phrase.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() < 2 || i + parts.size() > words.size()) {
                continue;
            }
            bool all = true;
            for (int k = 0; k < parts.size() && all; ++k) {
                all = normalizeWord(words.at(i + k).text) == parts.at(k);
            }
            if (all) {
                fillers << Filler{withMargin(i, i + int(parts.size()) - 1), FillerKind::Ambiguous, phrase};
                i += int(parts.size()) - 1;
                matched = true;
                break;
            }
        }
        if (matched) {
            continue;
        }
        const Word &w = words.at(i);
        const QString n = normalizeWord(w.text);
        if (settings.sure.contains(n)) {
            fillers << Filler{withMargin(i, i), FillerKind::Sure, n};
        } else if (settings.suspects.contains(n)) {
            const double before = i > 0 ? w.start - words.at(i - 1).end : w.start;
            const double after = i + 1 < words.size() ? words.at(i + 1).start - w.end : pause;
            if (w.end - w.start <= isolatedMax && before >= pause && after >= pause) {
                fillers << Filler{withMargin(i, i), FillerKind::Isolated, n};
            }
        } else if (settings.ambiguous.contains(n)) {
            fillers << Filler{withMargin(i, i), FillerKind::Ambiguous, n};
        }
    }
    // Une voix que la transcription n'a pas écrite : Whisper omet souvent les « euh ».
    if (settings.voiceWithoutWord && !levelsDb.isEmpty()) {
        QVector<bool> covered(levelsDb.size(), false);
        for (const Word &w : words) {
            const Interval f = toFrames(w.start, w.end);
            for (int k = qMax(0, f.first); k < qMin(int(covered.size()), f.second); ++k) {
                covered[k] = true;
            }
        }
        const int minFrames = qMax(1, int(std::lround(settings.voiceWithoutWordMinMs * fps / 1000.)));
        const int maxFrames = int(std::lround(settings.voiceWithoutWordMaxMs * fps / 1000.));
        int start = -1;
        bool touched = false;
        for (int k = 0; k <= levelsDb.size(); ++k) {
            const bool on = k < levelsDb.size() && levelsDb.at(k) >= thresholdDb;
            if (on) {
                if (start < 0) {
                    start = k;
                    touched = false;
                }
                touched = touched || covered.at(k);
            } else if (start >= 0) {
                const int length = k - start;
                if (!touched && length >= minFrames && length <= maxFrames) {
                    fillers << Filler{Interval(start, k), FillerKind::VoiceWithoutWord, QString()};
                }
                start = -1;
            }
        }
    }
    std::sort(fillers.begin(), fillers.end(), [](const Filler &a, const Filler &b) { return a.frames.first < b.frames.first; });
    return fillers;
}

QList<Repeat> findRepeats(const QList<Word> &words, const RepeatSettings &settings, double fps)
{
    QList<Repeat> repeats;
    QStringList norm;
    for (const Word &w : words) {
        norm << normalizeWord(w.text);
    }
    const double window = settings.windowMs / 1000.;
    const int n = int(words.size());
    int i = 0;
    while (i < n) {
        int bestJ = -1;
        int bestLength = 0;
        // La reprise la plus proche d'abord : c'est elle qu'on garde, la prise d'avant part.
        for (int j = i + 1; j < n && words.at(j).start - words.at(i).start <= window; ++j) {
            int length = 0;
            int mismatches = 0;
            while (i + length < j && j + length < n) {
                if (norm.at(i + length) != norm.at(j + length)) {
                    if (++mismatches > settings.maxMismatches) {
                        break;
                    }
                }
                ++length;
            }
            if (length >= settings.minWords && length > bestLength) {
                bestLength = length;
                bestJ = j;
                break;
            }
        }
        if (bestJ < 0) {
            ++i;
            continue;
        }
        const int from = int(std::lround(words.at(i).start * fps));
        const int to = int(std::lround(words.at(bestJ).start * fps));
        QStringList preview;
        for (int k = i; k < i + qMin(bestLength, 6); ++k) {
            preview << norm.at(k);
        }
        repeats << Repeat{Interval(from, to), preview.join(QLatin1Char(' ')) + (bestLength > 6 ? QStringLiteral("…") : QString()), bestLength};
        // On repart de la reprise : elle peut elle-même être reprise plus loin.
        i = bestJ;
    }
    return repeats;
}

QList<Shot> cameraPlan(const QList<QVector<double>> &speakers, const CameraSettings &settings, double fps)
{
    QList<Shot> shots;
    int n = 0;
    for (const auto &levels : speakers) {
        n = qMax(n, int(levels.size()));
    }
    if (speakers.isEmpty() || n == 0) {
        return shots;
    }
    constexpr int Nobody = -2;
    constexpr int Wide = -1;
    // 1. Qui parle à chaque image.
    QVector<int> raw(n, Nobody);
    for (int f = 0; f < n; ++f) {
        int best = -1;
        double bestDb = -1000.;
        double secondDb = -1000.;
        for (int s = 0; s < speakers.size(); ++s) {
            const double db = f < speakers.at(s).size() ? speakers.at(s).at(f) : -144.;
            if (db > bestDb) {
                secondDb = bestDb;
                bestDb = db;
                best = s;
            } else if (db > secondDb) {
                secondDb = db;
            }
        }
        if (bestDb < settings.thresholdDb) {
            continue;
        }
        // Deux voix presque aussi fortes : ils parlent ensemble.
        if (secondDb >= settings.thresholdDb && bestDb - secondDb < settings.marginDb) {
            raw[f] = settings.hasWide ? Wide : best;
        } else {
            raw[f] = best;
        }
    }
    // 2. Des plans : le silence prolonge le plan en cours, le tout début prend le premier qui parle.
    int current = Nobody;
    for (int f = 0; f < n && current == Nobody; ++f) {
        current = raw.at(f);
    }
    if (current == Nobody) {
        current = settings.hasWide ? Wide : 0;
    }
    QList<Shot> runs;
    for (int f = 0; f < n; ++f) {
        const int who = raw.at(f) == Nobody ? current : raw.at(f);
        if (runs.isEmpty() || runs.last().speaker != who) {
            runs << Shot{Interval(f, f + 1), who};
        } else {
            runs.last().frames.second = f + 1;
        }
        current = who;
    }
    // 3. Les plans trop courts rejoignent le précédent (ou le suivant, pour le tout premier).
    const int minShot = qMax(1, int(std::lround(settings.minShotMs * fps / 1000.)));
    for (const Shot &r : std::as_const(runs)) {
        if (!shots.isEmpty() && (r.frames.second - r.frames.first < minShot || r.speaker == shots.last().speaker)) {
            shots.last().frames.second = r.frames.second;
        } else {
            shots << r;
        }
    }
    if (shots.size() > 1 && shots.first().frames.second - shots.first().frames.first < minShot) {
        shots[1].frames.first = 0;
        shots.removeFirst();
    }
    // 4. Un plan trop long passe par le plan large, pour respirer.
    if (settings.hasWide) {
        const int maxShot = int(std::lround(settings.maxShotMs * fps / 1000.));
        const int cutaway = int(std::lround(settings.wideCutawayMs * fps / 1000.));
        QList<Shot> varied;
        for (const Shot &s : std::as_const(shots)) {
            if (s.speaker == Wide || s.frames.second - s.frames.first <= maxShot + cutaway + minShot) {
                varied << s;
                continue;
            }
            int start = s.frames.first;
            while (s.frames.second - start > maxShot + cutaway + minShot) {
                varied << Shot{Interval(start, start + maxShot), s.speaker};
                varied << Shot{Interval(start + maxShot, start + maxShot + cutaway), Wide};
                start += maxShot + cutaway;
            }
            varied << Shot{Interval(start, s.frames.second), s.speaker};
        }
        shots = varied;
    }
    return shots;
}

}
