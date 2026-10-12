/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracuttimelineaudio.h"
#include "koracutducking.h"
#include "koracutsync.h"

#include "timeline2/model/timelinefunctions.hpp"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "definitions.h"
#include "lib/audio/audioStreamInfo.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <mlt++/Mlt.h>

namespace KoraCutTimelineAudio {

QList<int> clipsOnTrack(const std::shared_ptr<TimelineItemModel> &timeline, int trackId)
{
    QList<int> clips;
    const auto items = timeline->getItemsInRange(trackId, 0, -1, false);
    for (int id : items) {
        if (timeline->isClip(id)) {
            clips << id;
        }
    }
    return clips;
}

QVector<double> voiceLevels(const std::shared_ptr<TimelineItemModel> &timeline, const QList<int> &tracks, int *missingLevels)
{
    QVector<double> voiceDb(qMax(1, timeline->duration()), -144.);
    int missing = 0;
    for (int tid : tracks) {
        for (int cid : clipsOnTrack(timeline, tid)) {
            auto binClip = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(cid));
            if (!binClip || !binClip->audioInfo()) {
                continue;
            }
            const QMap<int, QString> streams = binClip->audioStreams();
            if (streams.isEmpty()) {
                continue;
            }
            // Le flux audio choisi pour ce clip dans la timeline ; le premier du média à défaut.
            int stream = timeline->data(timeline->makeClipIndexFromID(cid), TimelineModel::AudioStreamRole).toInt();
            if (!streams.contains(stream)) {
                stream = streams.firstKey();
            }
            const QVector<int16_t> levels = binClip->audioFrameCache(stream);
            const int channels = qMax(1, binClip->audioInfo()->channelsForStream(stream));
            const int block = AUDIOLEVELS_POINTS_PER_FRAME * channels;
            if (levels.size() < block) {
                ++missing;
                continue;
            }
            const int position = timeline->getClipPosition(cid);
            const int length = timeline->getClipPlaytime(cid);
            const int in = timeline->getClipIn(cid);
            const double speed = qAbs(timeline->getClipSpeed(cid));
            for (int t = 0; t < length && position + t < voiceDb.size(); ++t) {
                const int source = int((in + t) * (speed > 0. ? speed : 1.));
                const int offset = source * block;
                if (offset < 0 || offset + block > levels.size()) {
                    break;
                }
                int peak = 0;
                for (int k = 0; k < block; ++k) {
                    peak = qMax(peak, qAbs(int(levels.at(offset + k))));
                }
                voiceDb[position + t] = qMax(voiceDb.at(position + t), KoraCutDucking::peakToDb(peak));
            }
        }
    }
    if (missingLevels) {
        *missingLevels = missing;
    }
    return voiceDb;
}

QVector<double> fileLevels(const QString &path, double fps, int stream)
{
    QVector<double> levels;
    if (path.isEmpty()) {
        return levels;
    }
    Mlt::Profile profile;
    profile.set_frame_rate(int(std::lround(fps * 1000.)), 1000);
    Mlt::Producer producer(profile, path.toUtf8().constData());
    if (!producer.is_valid()) {
        return levels;
    }
    producer.set("video_index", -1);
    if (stream >= 0) {
        producer.set("audio_index", stream);
    }
    const int length = producer.get_length();
    if (length <= 0) {
        return levels;
    }
    levels.reserve(length);
    const int frequency = 48000;
    for (int i = 0; i < length; ++i) {
        std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
        if (!frame || !frame->is_valid()) {
            break;
        }
        mlt_audio_format format = mlt_audio_s16;
        int freq = frequency;
        int channels = 2;
        int samples = mlt_audio_calculate_frame_samples(float(fps), frequency, i);
        const auto *data = static_cast<const int16_t *>(frame->get_audio(format, freq, channels, samples));
        int peak = 0;
        if (data) {
            for (int k = 0; k < samples * channels; ++k) {
                peak = qMax(peak, qAbs(int(data[k])));
            }
        }
        levels.append(KoraCutDucking::peakToDb(peak));
    }
    return levels;
}

QVector<float> fileEnergy(const QString &path, int stream, double maxSeconds)
{
    QVector<float> energy;
    if (path.isEmpty()) {
        return energy;
    }
    // 25 images par seconde, 48 kHz : 1920 échantillons par image, quatre blocs de 480 (un centième de seconde).
    Mlt::Profile profile;
    profile.set_frame_rate(25, 1);
    Mlt::Producer producer(profile, path.toUtf8().constData());
    if (!producer.is_valid()) {
        return energy;
    }
    producer.set("video_index", -1);
    if (stream >= 0) {
        producer.set("audio_index", stream);
    }
    const int length = qMin(producer.get_length(), int(maxSeconds * 25.));
    if (length <= 0) {
        return energy;
    }
    energy.reserve(length * 4);
    for (int i = 0; i < length; ++i) {
        std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
        if (!frame || !frame->is_valid()) {
            break;
        }
        mlt_audio_format format = mlt_audio_s16;
        int freq = 48000;
        int channels = 2;
        int samples = 1920;
        const auto *data = static_cast<const int16_t *>(frame->get_audio(format, freq, channels, samples));
        const int block = qMax(1, samples / 4);
        for (int b = 0; b < 4; ++b) {
            double sum = 0.;
            int count = 0;
            if (data) {
                for (int k = b * block * channels; k < qMin(samples, (b + 1) * block) * channels; ++k) {
                    const double v = data[k] / 32768.;
                    sum += v * v;
                    ++count;
                }
            }
            const double rms = count > 0 ? std::sqrt(sum / count) : 0.;
            energy.append(float(rms > 1e-6 ? 20. * std::log10(rms) : -120.));
        }
    }
    return energy;
}

Beats analyseBeats(const QVector<float> &energyDb, double rate)
{
    Beats result;
    const int n = energyDb.size();
    if (n < int(rate * 4.) || rate < 20.) {
        return result;
    }
    // 1. Ce qui monte d'un coup : la hausse d'énergie d'un échantillon au suivant, plancher à -60 dB.
    QVector<double> flux(n, 0.);
    for (int i = 1; i < n; ++i) {
        flux[i] = qMax(0., qMax(-60., double(energyDb.at(i))) - qMax(-60., double(energyDb.at(i - 1))));
    }
    // 2. On retire la moyenne locale (une seconde), pour qu'un passage fort ne pèse pas plus qu'un passage doux.
    const int window = qMax(2, int(rate));
    QVector<double> prefix(n + 1, 0.);
    for (int i = 0; i < n; ++i) {
        prefix[i + 1] = prefix.at(i) + flux.at(i);
    }
    QVector<double> onset(n, 0.);
    double total = 0.;
    for (int i = 0; i < n; ++i) {
        const int from = qMax(0, i - window / 2);
        const int to = qMin(n, i + window / 2 + 1);
        onset[i] = qMax(0., flux.at(i) - (prefix.at(to) - prefix.at(from)) / (to - from));
        total += onset.at(i);
    }
    const double mean = total / n;
    if (mean <= 1e-9) {
        return result; // du silence, ou un son tenu sans aucune attaque
    }
    // 3. Les temps forts : les pics nets, à 0,15 s au moins l'un de l'autre, soixante au plus.
    double squares = 0.;
    for (int i = 0; i < n; ++i) {
        squares += (onset.at(i) - mean) * (onset.at(i) - mean);
    }
    const double floorLevel = mean + 2.5 * std::sqrt(squares / n);
    const int spacing = qMax(1, int(rate * 0.15));
    QVector<QPair<double, int>> peaks; // force, échantillon
    double strongest = 0.;
    for (int i = 1; i < n - 1; ++i) {
        if (onset.at(i) < floorLevel || onset.at(i) < onset.at(i - 1) || onset.at(i) < onset.at(i + 1)) {
            continue;
        }
        if (!peaks.isEmpty() && i - peaks.last().second < spacing) {
            if (onset.at(i) > peaks.last().first) {
                peaks.last() = {onset.at(i), i};
            }
        } else {
            peaks.append({onset.at(i), i});
        }
        strongest = qMax(strongest, onset.at(i));
    }
    std::sort(peaks.begin(), peaks.end(), [](const QPair<double, int> &a, const QPair<double, int> &b) { return a.first > b.first; });
    if (peaks.size() > 60) {
        peaks.resize(60);
    }
    std::sort(peaks.begin(), peaks.end(), [](const QPair<double, int> &a, const QPair<double, int> &b) { return a.second < b.second; });
    for (const auto &p : std::as_const(peaks)) {
        result.hits.append({p.second / rate, strongest > 0. ? p.first / strongest : 0.});
    }
    // 4. Le tempo : pour chaque période, la grille de temps qui tombe le mieux sur ce qui monte. Une fausse
    // période dérive le long du morceau, la bonne reste calée : c'est ce qui la fait sortir.
    const auto at = [&](double position) {
        const int i = int(std::lround(position));
        double best = 0.;
        for (int k = qMax(0, i - 1); k <= qMin(n - 1, i + 1); ++k) {
            best = qMax(best, onset.at(k));
        }
        return best;
    };
    const auto gridScore = [&](double period, double *bestPhase) {
        double best = 0.;
        for (int phase = 0; phase < int(period); ++phase) {
            double sum = 0.;
            int count = 0;
            for (double position = phase; position < n; position += period) {
                sum += at(position);
                ++count;
            }
            const double score = count > 0 ? sum / count : 0.;
            if (score > best) {
                best = score;
                if (bestPhase) {
                    *bestPhase = phase;
                }
            }
        }
        return best;
    };
    const auto weight = [](double bpm) {
        const double octaves = std::log2(bpm / 120.);
        // Une préférence douce : elle départage un tempo et son double (même grille, à égalité), sans faire
        // passer une subdivision à trois temps (108 pour un morceau à 72) devant le vrai battement.
        return std::exp(-0.5 * (octaves / 1.2) * (octaves / 1.2));
    };
    double bestBpm = 0.;
    double bestScore = 0.;
    double scoreSum = 0.;
    int scoreCount = 0;
    for (double bpm = 60.; bpm <= 180.; bpm += 0.5) {
        const double raw = gridScore(rate * 60. / bpm, nullptr);
        scoreSum += raw;
        ++scoreCount;
        if (raw * weight(bpm) > bestScore) {
            bestScore = raw * weight(bpm);
            bestBpm = bpm;
        }
    }
    if (bestBpm <= 0. || scoreCount == 0) {
        return result;
    }
    double refined = bestBpm;
    double refinedScore = 0.;
    for (double bpm = bestBpm - 0.5; bpm <= bestBpm + 0.5; bpm += 0.02) {
        const double raw = gridScore(rate * 60. / bpm, nullptr);
        if (raw > refinedScore) {
            refinedScore = raw;
            refined = bpm;
        }
    }
    // La confiance : de combien la meilleure grille dépasse une grille quelconque.
    const double contrast = refinedScore / qMax(1e-9, scoreSum / scoreCount);
    result.confidence = std::clamp((contrast - 1.3) / 1.2, 0., 1.);
    if (result.confidence < 0.2) {
        return result;
    }
    result.bpm = std::round(refined * 10.) / 10.;
    const double period = rate * 60. / refined;
    double phase = 0.;
    gridScore(period, &phase);
    for (double position = phase; position < n; position += period) {
        result.beats.append(position / rate);
    }
    return result;
}

QVector<double> clipLevels(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, bool *fromFile)
{
    if (fromFile) {
        *fromFile = false;
    }
    QVector<double> result;
    auto binClip = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(clipId));
    if (!binClip || !binClip->hasAudio()) {
        return result;
    }
    const int length = timeline->getClipPlaytime(clipId);
    const int in = timeline->getClipIn(clipId);
    const double speed = qAbs(timeline->getClipSpeed(clipId));
    const QMap<int, QString> streams = binClip->audioStreams();
    int stream = timeline->data(timeline->makeClipIndexFromID(clipId), TimelineModel::AudioStreamRole).toInt();
    if (!streams.contains(stream)) {
        stream = streams.isEmpty() ? -1 : streams.firstKey();
    }
    // 1. Les niveaux des formes d'onde, s'ils sont prêts.
    if (binClip->audioInfo() && stream >= 0) {
        const QVector<int16_t> cache = binClip->audioFrameCache(stream);
        const int channels = qMax(1, binClip->audioInfo()->channelsForStream(stream));
        const int block = AUDIOLEVELS_POINTS_PER_FRAME * channels;
        if (cache.size() >= block) {
            result.fill(-144., length);
            for (int t = 0; t < length; ++t) {
                const int source = int((in + t) * (speed > 0. ? speed : 1.));
                const int offset = source * block;
                if (offset < 0 || offset + block > cache.size()) {
                    break;
                }
                int peak = 0;
                for (int k = 0; k < block; ++k) {
                    peak = qMax(peak, qAbs(int(cache.at(offset + k))));
                }
                result[t] = KoraCutDucking::peakToDb(peak);
            }
            return result;
        }
    }
    // 2. Sinon, le fichier lui-même.
    const QString path = binClip->url();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return result;
    }
    const QVector<double> file = fileLevels(path, pCore->getCurrentFps(), stream);
    if (file.isEmpty()) {
        return result;
    }
    if (fromFile) {
        *fromFile = true;
    }
    result.fill(-144., length);
    for (int t = 0; t < length; ++t) {
        const int source = int((in + t) * (speed > 0. ? speed : 1.));
        if (source < 0 || source >= file.size()) {
            break;
        }
        result[t] = file.at(source);
    }
    return result;
}

QVector<float> clipEnvelope(const std::shared_ptr<TimelineItemModel> &timeline, int clipId)
{
    auto binClip = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(clipId));
    if (!binClip || !binClip->audioInfo()) {
        return {};
    }
    const QMap<int, QString> streams = binClip->audioStreams();
    if (streams.isEmpty()) {
        return {};
    }
    int stream = timeline->data(timeline->makeClipIndexFromID(clipId), TimelineModel::AudioStreamRole).toInt();
    if (!streams.contains(stream)) {
        stream = streams.firstKey();
    }
    return KoraCutSync::envelopeFromLevels(binClip->audioFrameCache(stream), binClip->audioInfo()->channelsForStream(stream));
}

QList<SyncOutcome> syncToReference(const std::shared_ptr<TimelineItemModel> &timeline, int referenceClip, const QList<int> &clips, double minConfidence,
                                   Fun &undo, Fun &redo, bool *ok)
{
    if (ok) {
        *ok = true;
    }
    QList<SyncOutcome> outcomes;
    const QVector<float> reference = clipEnvelope(timeline, referenceClip);
    if (reference.isEmpty()) {
        return outcomes;
    }
    const int points = AUDIOLEVELS_POINTS_PER_FRAME;
    const int refPos = timeline->getClipPosition(referenceClip);
    const int refIn = timeline->getClipIn(referenceClip);
    // On cherche jusqu'à la durée du plus long des deux : une caméra peut démarrer bien avant le micro.
    QList<QPair<int, int>> targets; // clip, position voulue
    for (int cid : clips) {
        if (cid == referenceClip) {
            continue;
        }
        SyncOutcome outcome;
        outcome.clipId = cid;
        const QVector<float> other = clipEnvelope(timeline, cid);
        if (other.isEmpty()) {
            outcomes << outcome;
            continue;
        }
        const KoraCutSync::Result r = KoraCutSync::align(reference, other, int(qMax(reference.size(), other.size())));
        outcome.confidence = r.confidence;
        if (r.confidence >= minConfidence) {
            // other[i] ressemble à reference[i + lag] (en points de fichier). En images de timeline :
            // position voulue = position de la référence + décalage - entrée de la référence + entrée du clip.
            const int lagFrames = int(std::lround(double(r.lag) / points));
            targets << qMakePair(cid, refPos + lagFrames - refIn + timeline->getClipIn(cid));
            outcome.moved = true;
        }
        outcomes << outcome;
    }
    // Personne avant zéro : si un clip devait y partir, tout glisse vers la droite d'autant.
    int shift = 0;
    for (const auto &t : std::as_const(targets)) {
        shift = qMax(shift, -t.second);
    }
    QList<QPair<int, int>> deltas;
    if (shift > 0) {
        deltas << qMakePair(referenceClip, shift);
    }
    for (const auto &t : std::as_const(targets)) {
        const int delta = t.second + shift - timeline->getClipPosition(t.first);
        deltas << qMakePair(t.first, delta);
        for (SyncOutcome &o : outcomes) {
            if (o.clipId == t.first) {
                o.deltaFrames = delta;
            }
        }
    }
    if (!TimelineFunctions::moveClipsBy(timeline, deltas, undo, redo)) {
        if (ok) {
            *ok = false;
        }
        for (SyncOutcome &o : outcomes) {
            o.moved = false;
            o.deltaFrames = 0;
        }
    }
    return outcomes;
}

}
