/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutloudness.h"

#include "core.h"
#include "doc/kdenlivedoc.h"
#include "project/projectmanager.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>

#include <QDebug>
#include <QEventLoop>
#include <QLocale>
#include <QThread>

#include <algorithm>
#include <functional>
#include <cmath>
#include <memory>
#include <mlt++/Mlt.h>

namespace KoraCutLoudness {

namespace {

// Le plafond de crête demandé par les plateformes (YouTube, Spotify) est -1 dBTP. L'encodage AAC fait
// remonter les crêtes de quelques dixièmes (mesuré -0,8 dBTP sur la promo de 83 s le 24 septembre 2026) :
// le limiteur vise -1,5 pour que le fichier livré reste sous -1.
constexpr double kCeilingDbtp = -1.5;
// Le plafond du limiteur, en crête d'échantillon : un limiteur d'échantillons laisse la crête vraie
// passer au-dessus de lui (+1,1 dB mesuré), et l'AAC en ajoute encore. -2,5 dBFS donne -1,4 dBTP dans
// le fichier livré (mesuré le 28 septembre 2026 sur la V1 d'AFEBABA sans voix, gain +9,9 dB).
constexpr double kLimiterDbfs = -2.5;
// Un gain au-delà de ça n'est plus une correction, c'est une erreur de mesure.
constexpr double kMaxGainDb = 40.;

QString number(double value, int decimals = 1)
{
    return QLocale::c().toString(value, 'f', decimals);
}

// Le tractor principal de la scène : celui que la racine désigne, sinon le dernier.
QDomElement mainTractor(QDomDocument &doc)
{
    const QString rootId = doc.documentElement().attribute(QStringLiteral("producer"));
    QDomNodeList tractors = doc.elementsByTagName(QStringLiteral("tractor"));
    for (int i = 0; i < tractors.count(); ++i) {
        QDomElement t = tractors.at(i).toElement();
        if (t.attribute(QStringLiteral("id")) == rootId) {
            return t;
        }
    }
    if (tractors.count() > 0) {
        return tractors.at(tractors.count() - 1).toElement();
    }
    return {};
}

// Pose un filtre MLT (id, service, propriétés) sur un élément.
void appendFilter(QDomDocument &doc, QDomElement &target, const QString &id, const QString &service, const QList<QPair<QString, QString>> &properties)
{
    QDomElement filter = doc.createElement(QStringLiteral("filter"));
    filter.setAttribute(QStringLiteral("id"), id);
    const auto property = [&doc, &filter](const QString &name, const QString &value) {
        QDomElement p = doc.createElement(QStringLiteral("property"));
        p.setAttribute(QStringLiteral("name"), name);
        p.appendChild(doc.createTextNode(value));
        filter.appendChild(p);
    };
    property(QStringLiteral("mlt_service"), service);
    for (const auto &pair : properties) {
        property(pair.first, pair.second);
    }
    target.appendChild(filter);
}

// L'ancien nivellement, adaptatif (fenêtre glissante de 3 s) : le repli quand on n'a pas pu mesurer.
void appendDynamicFilter(QDomDocument &doc, QDomElement &target, int targetLufs)
{
    appendFilter(doc, target, QStringLiteral("koracut_loudness"), QStringLiteral("dynamic_loudness"),
                 {{QStringLiteral("target_loudness"), QString::number(targetLufs)},
                  // Un rendu est un flux continu : pas de remise à zéro de la mesure à chaque plan.
                  {QStringLiteral("discontinuity_reset"), QStringLiteral("0")},
                  {QStringLiteral("window"), QStringLiteral("3")}});
}

// Le limiteur disponible sur cette machine : alimiter d'abord, acompressor en mode limiteur sinon, rien
// sinon. Testé une fois par session.
QString limiterService()
{
    static QString service;
    static bool probed = false;
    if (!probed) {
        probed = true;
        for (const char *candidate : {"avfilter.alimiter", "avfilter.acompressor"}) {
            Mlt::Filter probe(pCore->getProjectProfile(), candidate);
            if (probe.is_valid()) {
                service = QString::fromLatin1(candidate);
                break;
            }
        }
    }
    return service;
}

// Les réglages du limiteur. Surtout pas `av.latency` (compensation du retard) : l'enveloppe avfilter de
// MLT ne la gère pas, la sortie du limiteur est décalée et les crêtes passent telles quelles (+8 dBFS
// sur la V1 d'AFEBABA sans voix, 28 septembre 2026). Sans elle, le son est en retard de l'attaque
// (5 ms), bien moins qu'une image.
QList<QPair<QString, QString>> limiterParams(const QString &service)
{
    const QString ceiling = number(std::pow(10., kLimiterDbfs / 20.), 4);
    if (service == QLatin1String("avfilter.alimiter")) {
        return {{QStringLiteral("av.limit"), ceiling},
                // Pas de remontée automatique du niveau : le gain mesuré doit rester tel quel.
                {QStringLiteral("av.level"), QStringLiteral("0")},
                {QStringLiteral("av.attack"), QStringLiteral("5")},
                {QStringLiteral("av.release"), QStringLiteral("50")}};
    }
    // acompressor en limiteur : seuil au plafond, rapport 20:1, attaque courte.
    return {{QStringLiteral("av.threshold"), ceiling},
            {QStringLiteral("av.ratio"), QStringLiteral("20")},
            {QStringLiteral("av.attack"), QStringLiteral("1")},
            {QStringLiteral("av.release"), QStringLiteral("50")},
            {QStringLiteral("av.knee"), QStringLiteral("1")},
            {QStringLiteral("av.makeup"), QStringLiteral("1")}};
}

void appendLimiter(QDomDocument &doc, QDomElement &target, const QString &service)
{
    appendFilter(doc, target, QStringLiteral("koracut_limiter"), service, limiterParams(service));
}

// Une mesure en cours à la fois : un second appel pendant l'attente mesure sans fil.
bool s_measuring = false;

} // namespace

namespace {
Measure measureImpl(const QString &xml, double fps, int start, int endInclusive, const QSet<int> &mltTracks, bool muteMixer, double gainDb, bool limiter);
}

Measure measure(const QString &xml, double fps, int start, int endInclusive, const QSet<int> &mltTracks, bool muteMixer)
{
    return measureImpl(xml, fps, start, endInclusive, mltTracks, muteMixer, 0., false);
}

Measure measureTreated(const QString &xml, double fps, int start, int endInclusive, double gainDb, bool limiter)
{
    return measureImpl(xml, fps, start, endInclusive, {}, false, gainDb, limiter);
}

namespace {
Measure measureImpl(const QString &xml, double fps, int start, int endInclusive, const QSet<int> &mltTracks, bool muteMixer, double gainDb, bool limiter)
{
    Measure m;
    if (xml.isEmpty() || fps <= 0 || endInclusive < start) {
        return m;
    }
    Mlt::Producer producer(pCore->getProjectProfile(), "xml-string", xml.toUtf8().constData());
    if (!producer.is_valid()) {
        return m;
    }
    Mlt::Service service(producer.get_service());
    Mlt::Tractor tractor(service);
    if (!tractor.is_valid()) {
        return m;
    }
    const int count = tractor.count();
    for (int i = 1; i < count; ++i) {
        std::unique_ptr<Mlt::Producer> track(tractor.track(i));
        if (!track || !track->is_valid()) {
            continue;
        }
        if (!mltTracks.isEmpty() && !mltTracks.contains(i)) {
            track->set("hide", 3);
            continue;
        }
        if (muteMixer) {
            Mlt::Service trackService(track->get_service());
            for (int f = 0; f < trackService.filter_count(); ++f) {
                std::unique_ptr<Mlt::Filter> filter(trackService.filter(f));
                if (filter && filter->is_valid() && filter->get_int("internal_added") == 237) {
                    filter->set("disable", 1);
                }
            }
        }
    }
    // Le traitement de l'export, dans le même ordre que sur le tractor du rendu : gain, puis limiteur.
    std::unique_ptr<Mlt::Filter> gain;
    std::unique_ptr<Mlt::Filter> limit;
    if (!qFuzzyIsNull(gainDb)) {
        gain = std::make_unique<Mlt::Filter>(pCore->getProjectProfile(), "volume");
        gain->set("level", number(gainDb, 2).toUtf8().constData());
        producer.attach(*gain);
    }
    if (limiter && !limiterService().isEmpty()) {
        limit = std::make_unique<Mlt::Filter>(pCore->getProjectProfile(), limiterService().toUtf8().constData());
        if (limit->is_valid()) {
            for (const auto &pair : limiterParams(limiterService())) {
                limit->set(pair.first.toUtf8().constData(), pair.second.toUtf8().constData());
            }
            producer.attach(*limit);
        }
    }
    Mlt::Filter meter(pCore->getProjectProfile(), "loudness_meter");
    if (!meter.is_valid()) {
        return m;
    }
    producer.attach(meter);
    const int frequency = 48000;
    double sumSquares = 0.;
    qint64 sampleCount = 0;
    double shortTermMax = kSilenceDb;
    const int last = std::min(endInclusive, producer.get_length() - 1);
    for (int i = std::max(0, start); i <= last; ++i) {
        producer.seek(i);
        std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
        if (!frame || !frame->is_valid()) {
            break;
        }
        mlt_audio_format format = mlt_audio_f32le;
        int freq = frequency;
        int channels = 2;
        int samples = mlt_audio_calculate_frame_samples(float(fps), frequency, i);
        const auto *data = static_cast<const float *>(frame->get_audio(format, freq, channels, samples));
        if (data) {
            for (int k = 0; k < samples * channels; ++k) {
                sumSquares += double(data[k]) * double(data[k]);
            }
            sampleCount += samples * channels;
        }
        const double shortTerm = meter.get_double("shortterm");
        if (std::isfinite(shortTerm) && shortTerm > kSilenceDb) {
            shortTermMax = std::max(shortTermMax, shortTerm);
        }
        ++m.frames;
    }
    m.ok = true;
    const double program = meter.get_double("program");
    if (std::isfinite(program) && program > kSilenceDb && sampleCount > 0) {
        m.silent = false;
        m.lufs = program;
        m.shortTermMax = shortTermMax;
        const double range = meter.get_double("range");
        m.range = std::isfinite(range) && range >= 0 ? range : 0.;
        const double peak = meter.get_double("max_true_peak");
        m.truePeak = std::isfinite(peak) ? peak : kSilenceDb;
        const double rms = std::sqrt(sumSquares / double(sampleCount));
        m.rms = rms > 0 ? 20. * std::log10(rms) : kSilenceDb;
    }
    producer.detach(meter);
    if (limit && limit->is_valid()) {
        producer.detach(*limit);
    }
    if (gain) {
        producer.detach(*gain);
    }
    return m;
}
} // namespace

Plan prepare(QDomDocument &doc, int in, int out, int targetLufs)
{
    Plan plan;
    plan.target = targetLufs;
    QDomElement target = mainTractor(doc);
    if (target.isNull() || targetLufs == 0) {
        plan.note = QStringLiteral("Pas de tractor principal dans la scène du rendu : rien posé.");
        return plan;
    }

    // La scène de la timeline courante, lue ici, dans le fil principal.
    QString xml;
    double fps = 0.;
    int last = out;
    if (auto timeline = pCore->projectManager()->getTimeline()) {
        xml = timeline->sceneList(QString());
        fps = pCore->getCurrentFps();
        if (last < 0 || last >= timeline->duration()) {
            last = timeline->duration() - 1;
        }
    }
    const int first = std::max(0, in);

    // Une mesure dans un fil, l'interface reste vivante (une seule à la fois).
    const auto run = [](const std::function<Measure()> &work) {
        if (s_measuring) {
            return work();
        }
        s_measuring = true;
        Measure result;
        QEventLoop loop;
        QThread *thread = QThread::create([&result, work]() { result = work(); });
        QObject::connect(thread, &QThread::finished, &loop, &QEventLoop::quit);
        thread->start();
        loop.exec();
        thread->wait();
        delete thread;
        s_measuring = false;
        return result;
    };

    Measure m;
    if (!xml.isEmpty() && last >= first) {
        pCore->displayMessage(i18nc("@info:status", "Measuring sound level…"), ProcessingJobMessage);
        m = run([xml, fps, first, last]() { return measure(xml, fps, first, last); });
    }
    if (!m.ok || m.silent) {
        // Repli : l'ancien filtre adaptatif, et on le dit.
        plan.fallback = true;
        appendDynamicFilter(doc, target, targetLufs);
        plan.note = !m.ok ? QStringLiteral("La mesure du niveau sonore n'a pas pu être faite (module MLT loudness_meter absent, ou timeline illisible) : "
                                           "nivellement adaptatif à la place, moins exact sur un extrait court.")
                          : QStringLiteral("Le mixage est silencieux sur la zone rendue : nivellement adaptatif à la place.");
        qWarning() << "KoraCut, niveau sonore :" << plan.note;
        pCore->displayMessage(i18nc("@info:status", "Sound level could not be measured, adaptive levelling used instead"), InformationMessage);
        return plan;
    }

    plan.measured = true;
    plan.lufs = m.lufs;
    plan.truePeak = m.truePeak;
    plan.gainDb = std::clamp(double(targetLufs) - m.lufs, -kMaxGainDb, kMaxGainDb);
    plan.lufsAfter = m.lufs + plan.gainDb;
    plan.truePeakAfter = m.truePeak + plan.gainDb;

    // Le plafond de crête : seulement si le gain fait dépasser -1 dBTP.
    if (m.truePeak + plan.gainDb > kCeilingDbtp) {
        const QString service = limiterService();
        if (service.isEmpty()) {
            plan.note = QStringLiteral("La crête après gain dépasse -1 dBTP (%1 dBTP) et aucun limiteur MLT n'est disponible (avfilter.alimiter ou "
                                       "avfilter.acompressor) : pas de plafond de crête.")
                            .arg(number(m.truePeak + plan.gainDb));
            qWarning() << "KoraCut, niveau sonore :" << plan.note;
        } else {
            plan.limiter = true;
            plan.limiterService = service;
            // Le limiteur retient les crêtes, donc un peu du niveau : un mixage plein de sons courts (pops,
            // whooshes) poussé de +10 dB sortait 1,4 LU sous la cible. On mesure ce que le fichier aura,
            // et on corrige le gain une fois si l'écart dépasse un demi-point.
            // Deux corrections au plus (la seconde par la pente mesurée), et jamais plus de +6 dB au-delà
            // du gain calculé : au-delà, le limiteur écraserait le son.
            const double target = double(targetLufs);
            const double gain0 = plan.gainDb;
            const double gainMax = std::min(kMaxGainDb, gain0 + 6.);
            Measure after = run([xml, fps, first, last, gain0]() { return measureTreated(xml, fps, first, last, gain0, true); });
            double previousGain = gain0;
            double previousLufs = after.lufs;
            // 28 septembre 2026 : le seuil passe d'un demi-point à un dixième (les fichiers sortaient à -14,4 ou
            // -14,5 LUFS pour -14), avec une troisième correction au besoin.
            for (int step = 0; step < 3 && after.ok && !after.silent && target - after.lufs > 0.1 && plan.gainDb < gainMax; ++step) {
                double next = plan.gainDb + (target - after.lufs) * 1.2;
                if (step > 0 && after.lufs - previousLufs > 0.05) {
                    next = plan.gainDb + (target - after.lufs) * (plan.gainDb - previousGain) / (after.lufs - previousLufs);
                }
                next = std::clamp(next, plan.gainDb + 0.05, gainMax);
                const Measure again = run([xml, fps, first, last, next]() { return measureTreated(xml, fps, first, last, next, true); });
                if (!again.ok || again.silent || std::abs(target - again.lufs) >= std::abs(target - after.lufs)) {
                    break;
                }
                previousGain = plan.gainDb;
                previousLufs = after.lufs;
                plan.gainDb = next;
                after = again;
            }
            if (after.ok && !after.silent) {
                plan.verified = true;
                plan.lufsAfter = after.lufs;
                plan.truePeakAfter = after.truePeak;
                if (target - after.lufs > 1.) {
                    plan.note = QStringLiteral("Le limiteur retient trop de son pour atteindre %1 LUFS : le fichier sera à %2 LUFS, crête sous -1 dBTP. "
                                               "Des sons courts très forts (pops, whooshes) portent le niveau : les baisser, ou viser moins fort.")
                                    .arg(targetLufs)
                                    .arg(number(after.lufs));
                }
            }
        }
    }
    appendFilter(doc, target, QStringLiteral("koracut_loudness"), QStringLiteral("volume"), {{QStringLiteral("level"), number(plan.gainDb, 2)}});
    if (plan.limiter) {
        appendLimiter(doc, target, plan.limiterService);
    }
    pCore->displayMessage(planText(plan), OperationCompletedMessage);
    qDebug() << "KoraCut, niveau sonore :" << planText(plan);
    return plan;
}

QJsonObject planJson(const Plan &plan)
{
    QJsonObject o{{QStringLiteral("cible_lufs"), plan.target}, {QStringLiteral("methode"), plan.measured ? QStringLiteral("gain_mesure") : QStringLiteral("adaptatif")}};
    if (plan.measured) {
        o.insert(QStringLiteral("lufs_mesure"), std::round(plan.lufs * 10.) / 10.);
        o.insert(QStringLiteral("crete_vraie_dbtp"), std::round(plan.truePeak * 10.) / 10.);
        o.insert(QStringLiteral("gain_db"), std::round(plan.gainDb * 10.) / 10.);
        o.insert(QStringLiteral("limiteur"), plan.limiter ? plan.limiterService : QStringLiteral("aucun"));
        // Ce que le fichier aura : mesuré avec le gain et le limiteur quand un limiteur est posé.
        o.insert(QStringLiteral("lufs_prevu"), std::round(plan.lufsAfter * 10.) / 10.);
        o.insert(QStringLiteral("crete_prevue_dbtp"), std::round(plan.truePeakAfter * 10.) / 10.);
        o.insert(QStringLiteral("prevision_mesuree"), plan.verified);
    }
    if (!plan.note.isEmpty()) {
        o.insert(QStringLiteral("note"), plan.note);
    }
    return o;
}

QString planText(const Plan &plan)
{
    if (!plan.measured) {
        return plan.note;
    }
    QString text = i18nc("@info:status LUFS measured, gain applied, target", "Sound level: %1 LUFS measured, gain %2 dB towards %3 LUFS", number(plan.lufs),
                         (plan.gainDb >= 0 ? QStringLiteral("+") : QString()) + number(plan.gainDb), plan.target);
    if (plan.limiter) {
        text += i18nc("@info:status appended to the sound level message", ", peak limiter at -1 dBTP");
    }
    return text;
}

} // namespace KoraCutLoudness
