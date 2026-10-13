/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Chantier 3, lot A (24 septembre 2026) : les outils de projet (marqueur, import, retrait, image,
// transcription, silences) et les outils audio (gains, piste, mesure, normalisation, baisse
// automatique, traitements). Contrat : projet.json, audio.json et le banc (banc/src/outils/projet.ts
// et audio.ts). Règle : un appel = une étape d'annulation, un outil ne fait jamais les choses à moitié.

#include "koracutagenttools.h"
#include "koracutagenttools_p.h"
#include "koracut/koracutperformance.h"
#include "koracutautocut.h"
#include "koracutducking.h"
#include "koracutloudness.h"
#include "koracuttimelineaudio.h"
#include "koracuttranscriber.h"

#include "bin/clipcreator.hpp"
#include "bin/model/markerlistmodel.hpp"
#include "bin/bin.h"
#include "bin/projectclip.h"
#include "bin/projectfolder.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "doc/kdenlivedoc.h"
#include "doc/kthumb.h"
#include "effects/effectsrepository.hpp"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "profiles/profilemodel.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "utils/thumbnailcache.hpp"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mlt++/Mlt.h>

using namespace KoraCutAgentToolsPrivate;

namespace {

constexpr double kSilenceDb = -70.;

double round1(double v)
{
    return std::round(v * 10.) / 10.;
}

double round2(double v)
{
    return std::round(v * 100.) / 100.;
}

// dB vers amplitude linéaire (0 dB = 1), comme l'attendent acompressor, agate et alimiter.
double dbToLinear(double db)
{
    return std::round(std::pow(10., db / 20.) * 1000.) / 1000.;
}

QString numberText(double v)
{
    return QLocale::c().toString(v, 'f', 3).remove(QRegularExpression(QStringLiteral("\\.?0+$")));
}

// Le clip est-il du son ? Sinon, une erreur qui montre le clip audio du plan s'il existe.
QJsonObject audioClipCheck(const Timeline &timeline, int cid)
{
    if (timeline->isAudioTrack(timeline->getClipTrackId(cid))) {
        return {};
    }
    const int partner = timeline->getClipSplitPartner(cid);
    return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("Ce clip est une image, pas un son."),
                 partner > -1 ? QStringLiteral("Le son de ce plan est le clip %1.").arg(partner)
                              : QStringLiteral("Ce plan a été posé sans son. Choisis un clip dont le champ k vaut \"a\"."));
}

QJsonObject audioTrackCheck(const Timeline &timeline, int tid, const QString &role)
{
    if (QJsonObject e = trackIdCheck(timeline, tid, false); !e.isEmpty()) {
        return e;
    }
    if (!timeline->isAudioTrack(tid)) {
        return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("%1 : %2 est une piste vidéo.").arg(role, trackLabel(timeline, tid)),
                     QStringLiteral("Choisis une piste dont le champ t vaut \"a\"."));
    }
    return {};
}

// Le premier effet visible d'un identifiant donné dans une pile, ou nul.
std::shared_ptr<EffectItemModel> findEffect(const std::shared_ptr<EffectStackModel> &stack, const QString &id)
{
    for (const auto &e : visibleEffects(stack)) {
        if (e->getAssetId() == id) {
            return e;
        }
    }
    return nullptr;
}

int rankOf(const std::shared_ptr<EffectStackModel> &stack, const std::shared_ptr<EffectItemModel> &effect)
{
    const auto effects = visibleEffects(stack);
    for (size_t i = 0; i < effects.size(); ++i) {
        if (effects.at(i) == effect) {
            return int(i);
        }
    }
    return -1;
}

// Pose un effet avec ses paramètres dans undo/redo (voir appendEffect du fichier montage : ici la
// version courte, sans effet intégré à réveiller, suffit aux effets de son).
std::shared_ptr<EffectItemModel> appendAudioEffect(const std::shared_ptr<EffectStackModel> &stack, const QString &effectId, const QMap<QString, QString> &params, Fun &undo,
                                                   Fun &redo)
{
    for (int row = 0; row < stack->rowCount(); ++row) {
        auto effect = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (effect && effect->isBuiltIn() && !effect->isAssetEnabled() && effect->getAssetId() == effectId) {
            effect->markEnabled(true, undo, redo);
            QVector<QPair<QString, QVariant>> before, after;
            for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
                before.append({it.key(), effect->getParamFromName(it.key())});
                after.append({it.key(), it.value()});
            }
            Fun local_redo = [effect, after]() {
                effect->setParameters(after);
                return true;
            };
            Fun local_undo = [effect, before]() {
                effect->setParameters(before);
                return true;
            };
            local_redo();
            UPDATE_UNDO_REDO_NOLOCK(local_redo, local_undo, undo, redo);
            return effect;
        }
    }
    const int rowsBefore = stack->rowCount();
    if (!stack->appendEffectWithParams(effectId, params, undo, redo) || stack->rowCount() <= rowsBefore) {
        return nullptr;
    }
    std::shared_ptr<EffectItemModel> added;
    for (int row = stack->rowCount() - 1; row >= 0; --row) {
        auto effect = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (effect && effect->getAssetId() == effectId) {
            added = effect;
            break;
        }
    }
    if (added) {
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            added->setParameter(it.key(), it.value());
        }
    }
    return added;
}

void setParamsWithUndo(const std::shared_ptr<EffectItemModel> &effect, const QMap<QString, QString> &params, Fun &undo, Fun &redo)
{
    QVector<QPair<QString, QVariant>> before, after;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        before.append({it.key(), effect->getParamFromName(it.key())});
        after.append({it.key(), it.value()});
    }
    std::weak_ptr<EffectItemModel> weak = effect;
    Fun local_redo = [weak, after]() {
        if (auto e = weak.lock()) {
            e->setParameters(after);
        }
        return true;
    };
    Fun local_undo = [weak, before]() {
        if (auto e = weak.lock()) {
            e->setParameters(before);
        }
        return true;
    };
    local_redo();
    UPDATE_UNDO_REDO_NOLOCK(local_redo, local_undo, undo, redo);
}

// Le gain fixe d'une valeur de volume : « -6 » ou, animée, la valeur de sa première image clé.
double constantLevel(const QString &value)
{
    const QString first = value.section(QLatin1Char(';'), 0, 0);
    const QString number = first.contains(QLatin1Char('=')) ? first.section(QLatin1Char('='), 1, 1) : first;
    return QLocale::c().toDouble(number.trimmed());
}

// ---------------------------------------------------------------------------------------------
// La mesure du son : la brique partagée KoraCutLoudness (une copie MLT de la timeline, un mètre de sonie),
// la même que celle de l'export.

using Measure = KoraCutLoudness::Measure;

// @param tracks les pistes entendues (vide : toutes), @param muteMixer coupe le gain et le panoramique de
// piste (la tranche du mixeur) pour mesurer un clip seul.
Measure measureTimeline(const Timeline &timeline, int start, int endInclusive, const QList<int> &tracks, bool muteMixer)
{
    QSet<int> keep;
    for (int tid : tracks) {
        keep.insert(timeline->getTrackMltIndex(tid));
    }
    return KoraCutLoudness::measure(timeline->sceneList(QString()), projectFps(), start, endInclusive, keep, muteMixer);
}

// Un clip seul, sur sa durée.
Measure measureClip(const Timeline &timeline, int cid)
{
    const int start = timeline->getClipPosition(cid);
    return measureTimeline(timeline, start, start + timeline->getClipPlaytime(cid) - 1, {timeline->getClipTrackId(cid)}, true);
}

QJsonObject measureJson(const Measure &m)
{
    QJsonObject o{{QStringLiteral("lufs_integre"), round1(m.lufs)},
                  {QStringLiteral("lufs_court_terme_max"), round1(m.shortTermMax)},
                  {QStringLiteral("plage_dynamique_lu"), round1(m.range)},
                  {QStringLiteral("crete_vraie_dbtp"), round1(m.truePeak)},
                  {QStringLiteral("rms_db"), round1(m.rms)},
                  {QStringLiteral("duree"), time(m.frames)}};
    if (m.silent) {
        o.insert(QStringLiteral("silence"), true);
    }
    return o;
}

// Les bornes debut / fin (fin comprise) d'une plage, ou une erreur.
bool rangeArgs(const Args &args, int defaultEnd, int &start, int &end, QJsonObject &failure)
{
    const double fps = projectFps();
    start = 0;
    end = defaultEnd;
    if (args.contains(QStringLiteral("debut"))) {
        start = KoraCutAgentTools::parseTime(args.value(QStringLiteral("debut")), fps);
        if (start < 0) {
            failure = error(QStringLiteral("entree_invalide"), QStringLiteral("debut ne se lit pas."), QStringLiteral("Donne un nombre d'images ou un timecode HH:MM:SS:II."));
            return false;
        }
    }
    if (args.contains(QStringLiteral("fin"))) {
        end = KoraCutAgentTools::parseTime(args.value(QStringLiteral("fin")), fps);
        if (end < 0) {
            failure = error(QStringLiteral("entree_invalide"), QStringLiteral("fin ne se lit pas."), QStringLiteral("Donne un nombre d'images ou un timecode HH:MM:SS:II."));
            return false;
        }
    }
    if (end < start) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("La plage à mesurer est vide : fin est avant debut."), QStringLiteral("Inverse les deux valeurs."));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// mesurer_audio

QJsonObject mesurerAudio(const Timeline &timeline, const Args &args)
{
    const QString target = args.value(QStringLiteral("cible")).toString();
    if (target != QLatin1String("clip") && target != QLatin1String("piste") && target != QLatin1String("mixage")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("cible vaut clip, piste ou mixage."));
    }
    QJsonObject failure;
    int start = 0;
    int end = 0;
    if (!rangeArgs(args, std::max(0, timeline->duration() - 1), start, end, failure)) {
        return failure;
    }
    Measure m;
    if (target == QLatin1String("clip")) {
        if (!args.contains(QStringLiteral("clip_id"))) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("clip_id est requis quand cible vaut \"clip\"."), QStringLiteral("Ajoute clip_id."));
        }
        const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
        if (!timeline->isClip(cid)) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun clip n'a l'identifiant %1.").arg(cid), QStringLiteral("Relis etat_timeline."));
        }
        if (QJsonObject e = audioClipCheck(timeline, cid); !e.isEmpty()) {
            return e;
        }
        const int clipStart = timeline->getClipPosition(cid);
        const int clipEnd = clipStart + timeline->getClipPlaytime(cid) - 1;
        start = std::max(start, clipStart);
        end = std::min(end, clipEnd);
        if (end < start) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("La plage ne touche pas le clip %1 (%2 à %3).").arg(cid).arg(clipStart).arg(clipEnd));
        }
        m = measureTimeline(timeline, start, end, {timeline->getClipTrackId(cid)}, true);
    } else if (target == QLatin1String("piste")) {
        if (!args.contains(QStringLiteral("piste_id"))) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("piste_id est requis quand cible vaut \"piste\"."), QStringLiteral("Ajoute piste_id."));
        }
        const int tid = args.value(QStringLiteral("piste_id")).toInt(-1);
        if (QJsonObject e = audioTrackCheck(timeline, tid, QStringLiteral("piste_id")); !e.isEmpty()) {
            return e;
        }
        m = measureTimeline(timeline, start, end, {tid}, false);
    } else {
        m = measureTimeline(timeline, start, end, {}, false);
    }
    if (!m.ok) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("La mesure du son n'a pas pu être faite (module MLT loudness_meter absent, ou timeline illisible)."));
    }
    QJsonObject o = measureJson(m);
    o.insert(QStringLiteral("ok"), true);
    return o;
}

// ---------------------------------------------------------------------------------------------
// regler_gain_clip

QJsonObject reglerGainClip(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    if (QJsonObject e = audioClipCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    if (!args.contains(QStringLiteral("gain_db")) || !args.value(QStringLiteral("gain_db")).isDouble()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("gain_db manque ou n'est pas un nombre."));
    }
    const double wanted = args.value(QStringLiteral("gain_db")).toDouble();
    const bool relative = args.value(QStringLiteral("relatif")).toBool(false);
    const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(cid);
    std::shared_ptr<EffectItemModel> effect = findEffect(stack, QStringLiteral("volume"));
    const QString current = effect ? effect->getParamFromName(QStringLiteral("level")).toString() : QStringLiteral("0");
    const double base = constantLevel(current);
    const double gain = round2(relative ? base + wanted : wanted);
    if (gain < -60 || gain > 24) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le gain final serait de %1 dB, hors de la plage -60 à +24.").arg(gain),
                     QStringLiteral("Au-delà de +12 dB, c'est la prise de son qu'il faut revoir."));
    }
    QString value = numberText(gain);
    QString keyframesNote;
    if (current.contains(QLatin1Char('='))) {
        if (relative) {
            // Chaque image clé bouge du même gain.
            QStringList parts;
            const QStringList keys = current.split(QLatin1Char(';'), Qt::SkipEmptyParts);
            for (const QString &key : keys) {
                const QString pos = key.section(QLatin1Char('='), 0, 0);
                const double v = QLocale::c().toDouble(key.section(QLatin1Char('='), 1, 1).trimmed());
                parts << QStringLiteral("%1=%2").arg(pos, numberText(round2(v + wanted)));
            }
            value = parts.join(QLatin1Char(';'));
            keyframesNote = QStringLiteral("décalées du même gain");
        } else {
            keyframesNote = QStringLiteral("retirées (valeur fixe sur tout le clip)");
        }
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (effect) {
        setParamsWithUndo(effect, {{QStringLiteral("level"), value}}, undo, redo);
    } else {
        effect = appendAudioEffect(stack, QStringLiteral("volume"), {{QStringLiteral("level"), value}}, undo, redo);
        if (!effect) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet Volume n'a pas pu être posé sur le clip %1.").arg(cid));
        }
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Set clip gain to %1 dB", value));
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), cid}, {QStringLiteral("effet_index"), rankOf(stack, effect)}, {QStringLiteral("gain_db"), gain}};
    if (!keyframesNote.isEmpty()) {
        result.insert(QStringLiteral("images_cles"), keyframesNote);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// regler_piste_audio : la tranche du mixeur (filtres volume et panner posés sur la piste).

// Le service MLT d'une piste (-1 : le master), ou nul.
std::unique_ptr<Mlt::Service> trackService(const Timeline &timeline, int tid)
{
    if (tid == -1) {
        return std::make_unique<Mlt::Service>(timeline->tractor()->get_service());
    }
    std::unique_ptr<Mlt::Producer> track(timeline->tractor()->track(timeline->getTrackMltIndex(tid)));
    if (!track || !track->is_valid()) {
        return nullptr;
    }
    return std::make_unique<Mlt::Service>(track->get_service());
}

// Le filtre du mixeur d'un service (« volume » ou « panner »), créé s'il manque.
std::unique_ptr<Mlt::Filter> mixerFilter(Mlt::Service &service, const char *name, bool create)
{
    for (int i = 0; i < service.filter_count(); ++i) {
        std::unique_ptr<Mlt::Filter> filter(service.filter(i));
        if (filter && filter->is_valid() && filter->get_int("internal_added") == 237 && qstrcmp(filter->get("mlt_service"), name) == 0) {
            return filter;
        }
    }
    if (!create) {
        return nullptr;
    }
    auto filter = std::make_unique<Mlt::Filter>(pCore->getProjectProfile(), name);
    if (!filter->is_valid()) {
        return nullptr;
    }
    filter->set("internal_added", 237);
    filter->set("disable", 1);
    if (qstrcmp(name, "panner") == 0) {
        filter->set("start", 0.5);
    }
    service.attach(*filter);
    return filter;
}

double trackGainDb(const Timeline &timeline, int tid)
{
    auto service = trackService(timeline, tid);
    if (!service) {
        return 0.;
    }
    auto filter = mixerFilter(*service, "volume", false);
    if (!filter || filter->get_int("disable") == 1) {
        return 0.;
    }
    const double level = filter->get_double("level");
    return level < -999 ? filter->get_double("kdenlive:muted_level") : level;
}

double trackPan(const Timeline &timeline, int tid)
{
    auto service = trackService(timeline, tid);
    if (!service) {
        return 0.;
    }
    auto filter = mixerFilter(*service, "panner", false);
    if (!filter || filter->get_int("disable") == 1) {
        return 0.;
    }
    return round2(filter->get_double("start") * 2. - 1.);
}

QJsonObject reglerPisteAudio(const Timeline &timeline, const Args &args)
{
    const int tid = args.value(QStringLiteral("piste_id")).toInt(-2);
    const bool master = tid == -1;
    if (!master) {
        if (QJsonObject e = audioTrackCheck(timeline, tid, QStringLiteral("piste_id")); !e.isEmpty()) {
            return e;
        }
        if (timeline->trackIsLocked(tid)) {
            return error(QStringLiteral("piste_verrouillee"), QStringLiteral("La piste %1 est verrouillée.").arg(trackLabel(timeline, tid)),
                         QStringLiteral("Déverrouille-la avec regler_piste, ou demande à la personne."));
        }
    } else if (args.contains(QStringLiteral("panoramique")) || args.contains(QStringLiteral("muet")) || args.contains(QStringLiteral("solo"))) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Sur la tranche master (-1), seul gain_db s'applique."), QStringLiteral("Retire panoramique, muet et solo."));
    }
    if (args.value(QStringLiteral("solo")).toBool(false)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le solo n'est pas encore piloté par les agents."),
                     QStringLiteral("Coupe les autres pistes avec muet: true, et remets-les avant de rendre la main."));
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    std::weak_ptr<TimelineItemModel> weak = timeline;
    bool changed = false;
    if (args.contains(QStringLiteral("gain_db"))) {
        const double gain = std::clamp(args.value(QStringLiteral("gain_db")).toDouble(), -60., 24.);
        const double before = trackGainDb(timeline, tid);
        auto apply = [weak, tid](double value) {
            auto t = weak.lock();
            if (!t) {
                return false;
            }
            auto service = trackService(t, tid);
            if (!service) {
                return false;
            }
            auto filter = mixerFilter(*service, "volume", true);
            if (!filter) {
                return false;
            }
            filter->set("level", value);
            filter->set("disable", qFuzzyIsNull(value) ? 1 : 0);
            return true;
        };
        if (!apply(gain)) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le gain de la piste n'a pas pu être réglé."));
        }
        Fun local_redo = [apply, gain]() { return apply(gain); };
        Fun local_undo = [apply, before]() { return apply(before); };
        UPDATE_UNDO_REDO_NOLOCK(local_redo, local_undo, undo, redo);
        changed = true;
    }
    if (args.contains(QStringLiteral("panoramique"))) {
        const double pan = std::clamp(args.value(QStringLiteral("panoramique")).toDouble(), -1., 1.);
        const double before = trackPan(timeline, tid);
        auto apply = [weak, tid](double value) {
            auto t = weak.lock();
            if (!t) {
                return false;
            }
            auto service = trackService(t, tid);
            if (!service) {
                return false;
            }
            auto filter = mixerFilter(*service, "panner", true);
            if (!filter) {
                return false;
            }
            filter->set("start", (value + 1.) / 2.);
            filter->set("disable", qFuzzyIsNull(value) ? 1 : 0);
            return true;
        };
        if (!apply(pan)) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le panoramique de la piste n'a pas pu être réglé."));
        }
        Fun local_redo = [apply, pan]() { return apply(pan); };
        Fun local_undo = [apply, before]() { return apply(before); };
        UPDATE_UNDO_REDO_NOLOCK(local_redo, local_undo, undo, redo);
        changed = true;
    }
    if (args.contains(QStringLiteral("muet"))) {
        const QString before = timeline->getTrackProperty(tid, QStringLiteral("hide")).toString();
        const QString after = args.value(QStringLiteral("muet")).toBool() ? QStringLiteral("3") : QStringLiteral("1");
        Fun local_redo = [weak, tid, after]() {
            if (auto t = weak.lock()) {
                t->setTrackProperty(tid, QStringLiteral("hide"), after);
            }
            return true;
        };
        Fun local_undo = [weak, tid, before]() {
            if (auto t = weak.lock()) {
                t->setTrackProperty(tid, QStringLiteral("hide"), before);
            }
            return true;
        };
        local_redo();
        UPDATE_UNDO_REDO_NOLOCK(local_redo, local_undo, undo, redo);
        changed = true;
    }
    if (changed) {
        pCore->pushUndo(undo, redo, i18nc("@action undo text", "Adjust audio track"));
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("piste_id"), tid},
                       {QStringLiteral("gain_db"), round2(trackGainDb(timeline, tid))},
                       {QStringLiteral("panoramique"), master ? 0. : trackPan(timeline, tid)},
                       {QStringLiteral("muet"), master ? false : trackMasked(timeline, tid)},
                       {QStringLiteral("solo"), false}};
}

// ---------------------------------------------------------------------------------------------
// detecter_silences

using Interval = KoraCutDucking::Interval;

// Les silences d'un clip, en images de timeline ([début, fin[), au seuil et à la durée minimale donnés.
QList<Interval> clipSilences(const Timeline &timeline, int cid, double thresholdDb, int minFrames, bool *unreadable)
{
    QList<Interval> zones;
    const QVector<double> levels = KoraCutTimelineAudio::clipLevels(timeline, cid);
    if (levels.isEmpty()) {
        if (unreadable) {
            *unreadable = true;
        }
        return zones;
    }
    const double fps = projectFps();
    KoraCutAutoCut::SilenceSettings settings;
    settings.autoThreshold = false;
    settings.thresholdDb = thresholdDb;
    settings.minSilenceMs = int(std::lround(minFrames * 1000. / fps));
    settings.minSpeechMs = 0;
    settings.padBeforeMs = 0;
    settings.padAfterMs = 0;
    const int position = timeline->getClipPosition(cid);
    const QList<Interval> found = KoraCutAutoCut::silenceIntervals(levels, settings, fps);
    for (const Interval &in : found) {
        if (in.second - in.first >= minFrames) {
            zones << Interval(position + in.first, position + in.second);
        }
    }
    return zones;
}

QJsonObject detecterSilences(const Timeline &timeline, const Args &args)
{
    const bool hasClip = args.contains(QStringLiteral("clip_id"));
    const bool hasTrack = args.contains(QStringLiteral("piste_id"));
    if (hasClip == hasTrack) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Il faut donner clip_id ou piste_id, et un seul des deux."),
                     QStringLiteral("Pour une interview : le clip audio de l'interview."));
    }
    const double threshold = args.contains(QStringLiteral("seuil_db")) ? std::clamp(args.value(QStringLiteral("seuil_db")).toDouble(), -80., -10.) : -40.;
    int minFrames = 20;
    if (args.contains(QStringLiteral("duree_mini"))) {
        minFrames = KoraCutAgentTools::parseTime(args.value(QStringLiteral("duree_mini")), projectFps());
        if (minFrames < 1) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("duree_mini fait au moins une image."));
        }
    }
    QList<int> clips;
    if (hasClip) {
        const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
        if (!timeline->isClip(cid)) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun clip n'a l'identifiant %1.").arg(cid), QStringLiteral("Relis etat_timeline."));
        }
        if (QJsonObject e = audioClipCheck(timeline, cid); !e.isEmpty()) {
            return e;
        }
        clips << cid;
    } else {
        const int tid = args.value(QStringLiteral("piste_id")).toInt(-1);
        if (QJsonObject e = audioTrackCheck(timeline, tid, QStringLiteral("piste_id")); !e.isEmpty()) {
            return e;
        }
        clips = clipsOnTrack(timeline, tid);
    }
    QList<Interval> zones;
    int unreadable = 0;
    for (int cid : std::as_const(clips)) {
        bool bad = false;
        zones += clipSilences(timeline, cid, threshold, minFrames, &bad);
        if (bad) {
            ++unreadable;
        }
    }
    if (hasClip && unreadable > 0) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le son de ce clip n'a pas pu être lu."), QStringLiteral("Vérifie que le fichier est bien sur le disque."));
    }
    std::sort(zones.begin(), zones.end());
    int total = 0;
    for (const Interval &z : std::as_const(zones)) {
        total += z.second - z.first;
    }
    const bool truncated = zones.size() > 200;
    if (truncated) {
        std::sort(zones.begin(), zones.end(), [](const Interval &a, const Interval &b) { return (b.second - b.first) < (a.second - a.first); });
        zones = zones.mid(0, 200);
    }
    QJsonArray list;
    for (const Interval &z : std::as_const(zones)) {
        list.append(QJsonObject{{QStringLiteral("debut"), time(z.first)}, {QStringLiteral("fin"), time(z.second)}, {QStringLiteral("duree"), time(z.second - z.first)}});
    }
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("silences"), list}, {QStringLiteral("total_silence"), time(total)}, {QStringLiteral("seuil_db"), threshold}};
    if (truncated) {
        result.insert(QStringLiteral("tronque"), true);
    }
    if (unreadable > 0) {
        result.insert(QStringLiteral("remarque"), QStringLiteral("%1 clip(s) de la piste n'ont pas de son lisible : ils ne sont pas comptés.").arg(unreadable));
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// temps_musique (12 octobre 2026) : le tempo et les temps d'un morceau posé sur la timeline, pour poser les
// coupes dessus. Lecture seule. Le fichier est relu à cent mesures par seconde (les niveaux par image sont
// trop grossiers pour un tempo), sur la seule partie que le clip joue.

QJsonObject tempsMusique(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (!timeline->isClip(cid)) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun clip n'a l'identifiant %1.").arg(cid), QStringLiteral("Relis etat_timeline."));
    }
    if (QJsonObject e = audioClipCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    auto binClip = pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(cid));
    const QString path = binClip ? binClip->url() : QString();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le son de ce clip n'a pas pu être lu."), QStringLiteral("Vérifie que le fichier est bien sur le disque."));
    }
    const QMap<int, QString> streams = binClip->audioStreams();
    int stream = timeline->data(timeline->makeClipIndexFromID(cid), TimelineModel::AudioStreamRole).toInt();
    if (!streams.contains(stream)) {
        stream = streams.isEmpty() ? -1 : streams.firstKey();
    }
    const double fps = projectFps();
    const double speed = qMax(0.01, qAbs(timeline->getClipSpeed(cid)));
    const int position = timeline->getClipPosition(cid);
    const int length = timeline->getClipPlaytime(cid);
    const double sourceStart = timeline->getClipIn(cid) * speed / fps;
    const double sourceEnd = (timeline->getClipIn(cid) + length) * speed / fps;
    const double rate = 100.;
    const QVector<float> whole = KoraCutTimelineAudio::fileEnergy(path, stream, qMin(sourceEnd + 1., 900.));
    const int from = std::clamp(int(sourceStart * rate), 0, int(whole.size()));
    const int to = std::clamp(int(sourceEnd * rate), from, int(whole.size()));
    if (to - from < int(rate * 4.)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Il faut au moins quatre secondes de son pour trouver un tempo."),
                     QStringLiteral("Pose un passage plus long du morceau, ou repère les temps forts avec mesurer_audio."));
    }
    const KoraCutTimelineAudio::Beats found = KoraCutTimelineAudio::analyseBeats(whole.mid(from, to - from), rate);
    const auto frameOf = [&](double seconds) { return position + int(std::lround(seconds * fps / speed)); };
    QJsonArray beats;
    bool truncated = false;
    for (double t : found.beats) {
        const int frame = frameOf(t);
        if (frame >= position + length) {
            break;
        }
        if (beats.size() >= 600) {
            truncated = true;
            break;
        }
        beats.append(time(frame));
    }
    QJsonArray hits;
    for (const auto &hit : found.hits) {
        const int frame = frameOf(hit.first);
        if (frame < position + length) {
            hits.append(QJsonObject{{QStringLiteral("position"), time(frame)}, {QStringLiteral("force"), round2(hit.second)}});
        }
    }
    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("tempo_bpm"), found.bpm > 0. ? QJsonValue(found.bpm) : QJsonValue()},
                       {QStringLiteral("confiance"), round2(found.confidence)},
                       {QStringLiteral("temps"), beats},
                       {QStringLiteral("temps_forts"), hits}};
    if (found.bpm > 0.) {
        result.insert(QStringLiteral("intervalle_images"), round2(60. / found.bpm * fps / speed));
        result.insert(QStringLiteral("premier_temps"), beats.isEmpty() ? QJsonValue() : beats.first());
        if (found.confidence < 0.6) {
            result.insert(QStringLiteral("remarque"), QStringLiteral("Tempo incertain (morceau ternaire, swing ou battue peu marquée) : vérifie que les temps tombent sur des temps_forts avant de caler toutes les coupes dessus."));
        }
    } else {
        result.insert(QStringLiteral("remarque"), QStringLiteral("Aucun tempo régulier ne ressort (parole, ambiance, morceau sans battue ou qui change de tempo) : appuie-toi sur temps_forts."));
    }
    if (truncated) {
        result.insert(QStringLiteral("tronque"), true);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// appliquer_ducking

QJsonObject appliquerDucking(const Timeline &timeline, const Args &args)
{
    const int music = args.value(QStringLiteral("piste_musique_id")).toInt(-1);
    const int voice = args.value(QStringLiteral("piste_voix_id")).toInt(-1);
    if (QJsonObject e = audioTrackCheck(timeline, music, QStringLiteral("piste_musique_id")); !e.isEmpty()) {
        return e;
    }
    if (QJsonObject e = audioTrackCheck(timeline, voice, QStringLiteral("piste_voix_id")); !e.isEmpty()) {
        return e;
    }
    if (music == voice) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("La piste de musique et la piste de voix sont la même."), QStringLiteral("Donne deux pistes différentes."));
    }
    if (timeline->trackIsLocked(music)) {
        return error(QStringLiteral("piste_verrouillee"), QStringLiteral("La piste %1 est verrouillée.").arg(trackLabel(timeline, music)),
                     QStringLiteral("Déverrouille-la avec regler_piste, ou demande à la personne."));
    }
    const double fps = projectFps();
    const double reduction = std::clamp(args.value(QStringLiteral("reduction_db")).toDouble(9.), 1., 30.);
    const double threshold = std::clamp(args.value(QStringLiteral("seuil_db")).toDouble(-35.), -70., -10.);
    const int down = std::clamp(args.value(QStringLiteral("descente_images")).toInt(10), 1, 100);
    const int up = std::clamp(args.value(QStringLiteral("remontee_images")).toInt(25), 1, 200);
    const int minSilence = std::clamp(args.value(QStringLiteral("silence_mini_images")).toInt(int(std::lround(fps))), 1, 500);

    // 1. Là où il y a de la voix : le niveau de chaque clip de la piste voix, au-dessus du seuil.
    QVector<double> voiceDb(std::max(1, timeline->duration()), -144.);
    for (int cid : clipsOnTrack(timeline, voice)) {
        const QVector<double> levels = KoraCutTimelineAudio::clipLevels(timeline, cid);
        const int position = timeline->getClipPosition(cid);
        for (int t = 0; t < levels.size() && position + t < voiceDb.size(); ++t) {
            voiceDb[position + t] = std::max(voiceDb.at(position + t), levels.at(t));
        }
    }
    KoraCutDucking::Settings settings;
    settings.duckDb = reduction;
    settings.thresholdDb = threshold;
    settings.anticipationMs = 0;
    settings.holdMs = 0;
    settings.fadeMs = 0;
    settings.minVoiceMs = 0;
    // Deux zones trop proches pour laisser la musique remonter puis redescendre n'en font qu'une.
    settings.mergeGapMs = int(std::lround(std::max(minSilence, down + up) * 1000. / fps));
    const QList<Interval> zones = KoraCutDucking::duckedIntervals(voiceDb, settings, fps);

    // 2. Sur chaque clip de musique : des images clés de volume. Relancer remplace les précédentes.
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    int posed = 0;
    QJsonArray modified;
    for (int cid : clipsOnTrack(timeline, music)) {
        const int position = timeline->getClipPosition(cid);
        const int length = timeline->getClipPlaytime(cid);
        const int end = position + length;
        QList<Interval> concerned;
        for (const Interval &z : zones) {
            if (z.first < end + down && z.second > position - up) {
                concerned << z;
            }
        }
        if (concerned.isEmpty()) {
            continue;
        }
        const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(cid);
        std::shared_ptr<EffectItemModel> effect = findEffect(stack, QStringLiteral("volume"));
        const double base = effect ? constantLevel(effect->getParamFromName(QStringLiteral("level")).toString()) : 0.;
        QMap<int, double> keys;
        auto pose = [&](int frame, double value) { keys.insert(std::clamp(frame - position, 0, length - 1), value); };
        pose(position, base);
        pose(end - 1, base);
        for (const Interval &z : std::as_const(concerned)) {
            pose(z.first - down, base);
            pose(z.first, base - reduction);
            pose(z.second, base - reduction);
            pose(z.second + up, base);
        }
        QList<QPair<int, double>> list;
        for (auto it = keys.constBegin(); it != keys.constEnd(); ++it) {
            list << qMakePair(it.key(), round2(it.value()));
        }
        const QString animation = KoraCutDucking::animationString(list, timeline->getClipIn(cid));
        if (effect) {
            setParamsWithUndo(effect, {{QStringLiteral("level"), animation}}, undo, redo);
        } else {
            effect = appendAudioEffect(stack, QStringLiteral("volume"), {{QStringLiteral("level"), animation}, {QStringLiteral("kdenlive:koracut_ducking"), QStringLiteral("1")}}, undo,
                                       redo);
            if (!effect) {
                continue;
            }
        }
        posed += list.size();
        modified.append(cid);
    }
    if (!modified.isEmpty()) {
        pCore->pushUndo(undo, redo, i18nc("@action nom de l'étape d'annulation", "Automatic Music Ducking"));
    }
    QJsonArray zoneList;
    for (const Interval &z : zones) {
        zoneList.append(QJsonObject{{QStringLiteral("debut"), time(z.first)}, {QStringLiteral("fin"), time(z.second)}});
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("zones"), zoneList},
                       {QStringLiteral("keyframes_poses"), posed},
                       {QStringLiteral("clips_modifies"), modified},
                       {QStringLiteral("reduction_db"), reduction}};
}

// ---------------------------------------------------------------------------------------------
// normaliser_loudness : l'effet « loudness » de MLT (deux passes), avec le résultat de la première
// passe mesuré ici (« L: <LUFS>\tR: <plage>\tP <crête> »), donc appliqué tout de suite.

bool isNormalisation(const QString &id)
{
    return id == QLatin1String("loudness") || id == QLatin1String("avfilter.loudnorm") || id == QLatin1String("dynamic_loudness");
}

QString loudnessResults(const Measure &m)
{
    const QLocale c = QLocale::c();
    return QStringLiteral("L: %1\tR: %2\tP %3").arg(c.toString(m.lufs, 'f', 2), c.toString(m.range, 'f', 2), c.toString(m.truePeak, 'f', 2));
}

// Le limiteur que normaliser_loudness pose après le gain du mixage (marqué, pour être remplacé au
// prochain appel sans toucher à un limiteur posé à la main).
constexpr const char *kNormLimiter = "kdenlive:koracut_limiteur_norm";

// Retire les normalisations déjà posées dans une pile (dans undo/redo).
void removeNormalisations(const std::shared_ptr<EffectStackModel> &stack, Fun &undo, Fun &redo)
{
    for (const auto &effect : visibleEffects(stack)) {
        Mlt::Properties *asset = effect->getAsset();
        const bool ours = asset && asset->get_int(kNormLimiter) == 1;
        if (isNormalisation(effect->getAssetId()) || ours) {
            QString name;
            stack->removeEffectWithUndo(effect, name, undo, redo);
        }
    }
}

QJsonObject normaliserLoudness(const Timeline &timeline, const Args &args)
{
    const QString target = args.value(QStringLiteral("cible")).toString();
    if (target != QLatin1String("clip") && target != QLatin1String("mixage")) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("cible vaut clip ou mixage."));
    }
    if (!args.contains(QStringLiteral("lufs_cible")) || !args.value(QStringLiteral("lufs_cible")).isDouble()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("lufs_cible manque ou n'est pas un nombre."));
    }
    const double wanted = std::clamp(args.value(QStringLiteral("lufs_cible")).toDouble(), -50., -10.);
    const double ceiling = std::clamp(args.value(QStringLiteral("crete_max_dbtp")).toDouble(-1.), -9., 0.);
    if (!EffectsRepository::get()->exists(QStringLiteral("loudness"))) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet de normalisation (loudness) n'est pas dans la bibliothèque d'effets."));
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (target == QLatin1String("clip")) {
        if (!args.contains(QStringLiteral("clip_id"))) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("clip_id est requis quand cible vaut \"clip\"."), QStringLiteral("Ajoute clip_id."));
        }
        const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
        if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
            return e;
        }
        if (QJsonObject e = audioClipCheck(timeline, cid); !e.isEmpty()) {
            return e;
        }
        const Measure before = measureClip(timeline, cid);
        if (!before.ok) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("La mesure du son n'a pas pu être faite."));
        }
        if (before.silent) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("Le clip %1 est silencieux : il n'y a rien à normaliser.").arg(cid));
        }
        const std::shared_ptr<EffectStackModel> stack = timeline->getClipEffectStackModel(cid);
        removeNormalisations(stack, undo, redo);
        const Measure raw = measureClip(timeline, cid);
        // Le gain : atteindre la cible, sans dépasser le plafond de crête.
        const double gain = std::min(wanted - raw.lufs, ceiling - raw.truePeak);
        const QString program = numberText(round2(raw.lufs + gain));
        const auto effect = appendAudioEffect(stack, QStringLiteral("loudness"), {{QStringLiteral("program"), program}, {QStringLiteral("results"), loudnessResults(raw)}}, undo, redo);
        if (!effect) {
            undo();
            return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet de normalisation n'a pas pu être posé sur le clip %1.").arg(cid));
        }
        pCore->pushUndo(undo, redo, i18nc("@action undo text", "Normalize clip to %1 LUFS", numberText(wanted)));
        const Measure after = measureClip(timeline, cid);
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("lufs_avant"), round1(before.lufs)},
                           {QStringLiteral("lufs_apres"), round1(after.ok && !after.silent ? after.lufs : raw.lufs + gain)},
                           {QStringLiteral("gain_applique_db"), round1(gain)},
                           {QStringLiteral("crete_vraie_dbtp"), round1(after.ok && !after.silent ? after.truePeak : raw.truePeak + gain)},
                           {QStringLiteral("effet_index"), rankOf(stack, effect)}};
    }
    const int last = std::max(0, timeline->duration() - 1);
    const Measure before = measureTimeline(timeline, 0, last, {}, false);
    if (!before.ok) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("La mesure du son n'a pas pu être faite."));
    }
    if (before.silent) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le mixage est silencieux : il n'y a rien à normaliser."),
                     QStringLiteral("Vérifie qu'il y a du son sur la timeline et qu'aucune piste n'est restée muette."));
    }
    const std::shared_ptr<EffectStackModel> master = timeline->getMasterEffectStackModel();
    removeNormalisations(master, undo, redo);
    const Measure raw = measureTimeline(timeline, 0, last, {}, false);
    // Pose la normalisation et, si le gain fait dépasser le plafond, le limiteur juste derrière.
    // Le plafond de crête : si le gain fait dépasser crete_max_dbtp (des pops, des sons courts sur une
    // voix déjà forte), un limiteur le tient. Sans lui, le mixage sortait à +5 dBTP (constaté le
    // 25 septembre 2026 sur les vidéos AFEBABA) : de la saturation à l'export.
    const auto place = [&](double program, Fun &u, Fun &r, bool &limitedOut) {
        const auto posed = appendAudioEffect(master, QStringLiteral("loudness"), {{QStringLiteral("program"), numberText(round2(program))}, {QStringLiteral("results"), loudnessResults(raw)}}, u, r);
        limitedOut = false;
        if (posed && raw.truePeak + (program - raw.lufs) > ceiling && EffectsRepository::get()->exists(QStringLiteral("avfilter.alimiter"))) {
            // Une marge de 1,5 dB : le limiteur tient les échantillons, la crête vraie passe environ 1,1 dB
            // au-dessus, et l'AAC en ajoute (mesuré le 28 septembre 2026 ; la marge de 0,5 dB laissait -0,4 dBTP).
            // Jamais `av.latency` : l'enveloppe avfilter de MLT ne la gère pas et les crêtes passent.
            limitedOut = bool(appendAudioEffect(master, QStringLiteral("avfilter.alimiter"),
                                                {{QStringLiteral("av.limit"), numberText(std::clamp(dbToLinear(ceiling - 1.5), 0.06, 1.))},
                                                 {QStringLiteral("av.level"), QStringLiteral("0")},
                                                 {QStringLiteral("av.attack"), QStringLiteral("5")},
                                                 {QStringLiteral("av.release"), QStringLiteral("50")},
                                                 {QString::fromLatin1(kNormLimiter), QStringLiteral("1")}},
                                                u, r));
        }
        return posed;
    };
    // Le limiteur retire un peu de niveau : le mixage sortait à -14,4 ou -14,5 LUFS pour -14 (relevé le
    // 28 septembre 2026). Un essai mesuré, puis la cible corrigée de l'écart (deux essais au plus), et
    // seulement quand le limiteur a travaillé : sinon le gain est déjà exact.
    double program = wanted;
    for (int attempt = 0; attempt < 2; ++attempt) {
        Fun tryUndo = []() { return true; };
        Fun tryRedo = []() { return true; };
        bool tryLimited = false;
        if (!place(program, tryUndo, tryRedo, tryLimited) || !tryLimited) {
            tryUndo();
            break;
        }
        const Measure trial = measureTimeline(timeline, 0, last, {}, false);
        tryUndo();
        if (!trial.ok || trial.silent || std::abs(trial.lufs - wanted) <= 0.05) {
            break;
        }
        program += wanted - trial.lufs;
    }
    const double gain = program - raw.lufs;
    bool limited = false;
    const auto effect = place(program, undo, redo, limited);
    if (!effect) {
        undo();
        return error(QStringLiteral("non_disponible"), QStringLiteral("L'effet de normalisation n'a pas pu être posé sur le master."));
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Normalize mix to %1 LUFS", numberText(wanted)));
    const Measure after = measureTimeline(timeline, 0, last, {}, false);
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("lufs_avant"), round1(before.lufs)},
                       {QStringLiteral("lufs_apres"), round1(after.ok && !after.silent ? after.lufs : wanted)},
                       {QStringLiteral("gain_applique_db"), round1(gain)},
                       {QStringLiteral("crete_vraie_dbtp"), round1(after.ok && !after.silent ? after.truePeak : std::min(raw.truePeak + gain, ceiling))},
                       {QStringLiteral("limiteur"), limited},
                       {QStringLiteral("effet_index"), rankOf(master, effect)}};
}

// ---------------------------------------------------------------------------------------------
// ajouter_effet_audio

QJsonObject ajouterEffetAudio(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    bool isClip = false;
    int itemId = -1;
    const std::shared_ptr<EffectStackModel> stack = stackFor(timeline, args, failure, &isClip, &itemId);
    if (!stack) {
        return failure;
    }
    if (isClip) {
        if (QJsonObject e = audioClipCheck(timeline, itemId); !e.isEmpty()) {
            return e;
        }
    } else if (itemId != -1 && !timeline->isAudioTrack(itemId)) {
        return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("La piste %1 est une piste vidéo.").arg(trackLabel(timeline, itemId)),
                     QStringLiteral("Un traitement sonore va sur une piste audio, un clip audio, ou le master (piste_id -1)."));
    }
    const QString treatment = args.value(QStringLiteral("traitement")).toString();
    struct Recipe
    {
        QString id;
        QStringList fields;
    };
    static const QMap<QString, Recipe> recipes = {
        {QStringLiteral("passe_haut"), {QStringLiteral("avfilter.highpass"), {QStringLiteral("frequence_hz")}}},
        {QStringLiteral("passe_bas"), {QStringLiteral("avfilter.lowpass"), {QStringLiteral("frequence_hz")}}},
        {QStringLiteral("egaliseur"), {QStringLiteral("avfilter.equalizer"), {QStringLiteral("frequence_hz"), QStringLiteral("gain_db"), QStringLiteral("largeur_hz")}}},
        {QStringLiteral("compresseur"), {QStringLiteral("avfilter.acompressor"), {QStringLiteral("seuil_db"), QStringLiteral("ratio"), QStringLiteral("attaque_ms"), QStringLiteral("relachement_ms")}}},
        {QStringLiteral("limiteur"), {QStringLiteral("avfilter.alimiter"), {QStringLiteral("seuil_db"), QStringLiteral("attaque_ms"), QStringLiteral("relachement_ms")}}},
        {QStringLiteral("porte"), {QStringLiteral("avfilter.agate"), {QStringLiteral("seuil_db"), QStringLiteral("attaque_ms"), QStringLiteral("relachement_ms")}}},
        {QStringLiteral("de_esseur"), {QStringLiteral("avfilter.deesser"), {}}},
        {QStringLiteral("echo"), {QStringLiteral("avfilter.aecho"), {}}},
    };
    if (!recipes.contains(treatment)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("traitement vaut passe_haut, passe_bas, egaliseur, compresseur, limiteur, porte, de_esseur ou echo."));
    }
    const Recipe recipe = recipes.value(treatment);
    if (!EffectsRepository::get()->exists(recipe.id)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le traitement %1 (%2) n'est pas dans la bibliothèque d'effets de cette machine.").arg(treatment, recipe.id),
                     QStringLiteral("Dis-le à la personne : le module avfilter de MLT manque."));
    }
    QMap<QString, QString> params;
    auto has = [&args](const char *key) { return args.contains(QLatin1String(key)) && args.value(QLatin1String(key)).isDouble(); };
    auto num = [&args](const char *key) { return args.value(QLatin1String(key)).toDouble(); };
    auto times = [&]() {
        if (has("attaque_ms")) {
            params.insert(QStringLiteral("av.attack"), numberText(num("attaque_ms")));
        }
        if (has("relachement_ms")) {
            params.insert(QStringLiteral("av.release"), numberText(num("relachement_ms")));
        }
    };
    if (treatment == QLatin1String("passe_haut") || treatment == QLatin1String("passe_bas")) {
        if (has("frequence_hz")) {
            params.insert(QStringLiteral("av.f"), numberText(num("frequence_hz")));
        }
    } else if (treatment == QLatin1String("egaliseur")) {
        if (has("frequence_hz")) {
            params.insert(QStringLiteral("av.f"), numberText(num("frequence_hz")));
        }
        if (has("largeur_hz")) {
            params.insert(QStringLiteral("av.w"), numberText(num("largeur_hz")));
        }
        if (has("gain_db")) {
            params.insert(QStringLiteral("av.g"), numberText(num("gain_db")));
        }
    } else if (treatment == QLatin1String("compresseur")) {
        // Le seuil d'acompressor est linéaire (0 à 1), pas en dB : conversion ici (README, point 5).
        if (has("seuil_db")) {
            params.insert(QStringLiteral("av.threshold"), numberText(std::max(0.001, dbToLinear(num("seuil_db")))));
        }
        if (has("ratio")) {
            params.insert(QStringLiteral("av.ratio"), numberText(num("ratio")));
        }
        times();
    } else if (treatment == QLatin1String("limiteur")) {
        params.insert(QStringLiteral("av.limit"), numberText(std::clamp(dbToLinear(has("seuil_db") ? num("seuil_db") : -1.), 0.06, 1.)));
        times();
    } else if (treatment == QLatin1String("porte")) {
        if (has("seuil_db")) {
            params.insert(QStringLiteral("av.threshold"), numberText(dbToLinear(num("seuil_db"))));
        }
        times();
    } else if (treatment == QLatin1String("de_esseur")) {
        // Posé tel quel, avfilter.deesser a une intensité nulle et ne fait rien (relevé le 6 octobre 2026) :
        // on lui donne la recette de « Améliorer la voix » (koracutcliptools.cpp), qui adoucit les sifflantes.
        params.insert(QStringLiteral("av.i"), QStringLiteral("0.4"));
        params.insert(QStringLiteral("av.m"), QStringLiteral("0.5"));
        params.insert(QStringLiteral("av.f"), QStringLiteral("0.5"));
        params.insert(QStringLiteral("av.s"), QStringLiteral("o"));
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    const auto effect = appendAudioEffect(stack, recipe.id, params, undo, redo);
    if (!effect) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("Le traitement %1 n'a pas pu être posé ici.").arg(treatment));
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Add audio effect %1", EffectsRepository::get()->getName(recipe.id)));
    QJsonArray ignored;
    for (const char *key : {"frequence_hz", "gain_db", "largeur_hz", "seuil_db", "ratio", "attaque_ms", "relachement_ms"}) {
        if (args.contains(QLatin1String(key)) && !recipe.fields.contains(QLatin1String(key))) {
            ignored.append(QLatin1String(key));
        }
    }
    QJsonObject result{{QStringLiteral("ok"), true},
                       {QStringLiteral("effet_index"), rankOf(stack, effect)},
                       {QStringLiteral("effet_id"), recipe.id},
                       {QStringLiteral("parametres"), parametersJson(effect)}};
    if (!ignored.isEmpty()) {
        result.insert(QStringLiteral("champs_ignores"), ignored);
    }
    return result;
}

// ---------------------------------------------------------------------------------------------
// ajouter_marqueur (projet.json)

QJsonObject ajouterMarqueur(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    const int position = requiredTime(args, QStringLiteral("position"), failure);
    if (position < 0) {
        return failure;
    }
    const int category = args.value(QStringLiteral("categorie")).toInt(0);
    if (!pCore->markerTypes.contains(category)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("La catégorie %1 n'existe pas dans ce projet.").arg(category),
                     QStringLiteral("Catégories disponibles : %1.").arg([]() {
                         QStringList l;
                         for (int k : pCore->markerTypes.keys()) {
                             l << QString::number(k);
                         }
                         return l.join(QLatin1String(", "));
                     }()));
    }
    std::shared_ptr<MarkerListModel> model;
    if (args.contains(QStringLiteral("media_id"))) {
        const QString mediaId = args.value(QStringLiteral("media_id")).toString();
        const auto clip = pCore->projectItemModel()->getClipByBinID(mediaId);
        if (mediaId.isEmpty() || !clip) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun média ne porte l'id « %1 ».").arg(mediaId), QStringLiteral("Appelle lister_medias et prends le champ id."));
        }
        if (clip->frameDuration() > 0 && size_t(position) >= clip->frameDuration()) {
            return error(QStringLiteral("hors_media"), QStringLiteral("%1 ne dure que %2 images.").arg(clip->clipName()).arg(clip->frameDuration()),
                         QStringLiteral("Donne une position à l'intérieur du média."));
        }
        model = clip->markerModel();
    } else {
        model = timeline->getGuideModel();
    }
    if (!model) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucune liste de marqueurs ici."));
    }
    const double fps = projectFps();
    const GenTime pos(position, fps);
    if (args.value(QStringLiteral("supprimer")).toBool(false)) {
        if (!model->hasMarker(pos)) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Pas de marqueur à l'image %1.").arg(position), QStringLiteral("Relis etat_timeline pour voir les marqueurs."));
        }
        bool ok = false;
        const CommentedTime removed = model->getMarker(pos, &ok);
        if (!model->removeMarker(pos)) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le marqueur n'a pas pu être retiré."));
        }
        return QJsonObject{{QStringLiteral("ok"), true},
                           {QStringLiteral("position"), time(position)},
                           {QStringLiteral("commentaire"), removed.comment()},
                           {QStringLiteral("categorie"), removed.markerType()},
                           {QStringLiteral("supprime"), true}};
    }
    const QString comment = args.value(QStringLiteral("commentaire")).toString().left(80);
    if (!model->addMarker(pos, comment, category)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Le marqueur n'a pas pu être posé."));
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("position"), time(position)}, {QStringLiteral("commentaire"), comment}, {QStringLiteral("categorie"), category}};
}

// ---------------------------------------------------------------------------------------------
// importer_media

bool readableMedia(const QString &path)
{
    const QString mime = QMimeDatabase().mimeTypeForFile(path).name();
    return mime.startsWith(QLatin1String("video/")) || mime.startsWith(QLatin1String("audio/")) || mime.startsWith(QLatin1String("image/")) ||
           mime == QLatin1String("application/mxf") || mime == QLatin1String("application/x-matroska");
}

QJsonObject importerMedia(const Timeline &, const Args &args)
{
    auto model = pCore->projectItemModel();
    if (!model || !pCore->currentDoc()) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucun projet ouvert."));
    }
    const QJsonArray paths = args.value(QStringLiteral("chemins")).toArray();
    if (paths.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("chemins est vide."));
    }
    QJsonArray refused;
    QJsonArray medias;
    QStringList files; // à importer, absolus
    static const QRegularExpression scheme(QStringLiteral("^[a-zA-Z][a-zA-Z0-9+.-]*://"));
    for (const QJsonValue &v : paths) {
        const QString given = v.toString();
        QString path = given;
        if (path.startsWith(QLatin1String("~/"))) {
            path = QDir::homePath() + path.mid(1);
        }
        if (scheme.match(given).hasMatch()) {
            refused.append(QJsonObject{{QStringLiteral("chemin"), given}, {QStringLiteral("raison"), QStringLiteral("C'est une adresse internet : l'outil n'importe que des fichiers déjà sur le disque.")}});
            continue;
        }
        if (!QDir::isAbsolutePath(path)) {
            refused.append(QJsonObject{{QStringLiteral("chemin"), given}, {QStringLiteral("raison"), QStringLiteral("Le chemin doit être absolu.")}});
            continue;
        }
        const QFileInfo info(path);
        if (!info.exists()) {
            refused.append(QJsonObject{{QStringLiteral("chemin"), given}, {QStringLiteral("raison"), QStringLiteral("Fichier introuvable sur le disque.")}});
            continue;
        }
        if (info.isDir()) {
            const QFileInfoList entries = QDir(path).entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
            int found = 0;
            for (const QFileInfo &entry : entries) {
                if (readableMedia(entry.absoluteFilePath())) {
                    files << entry.absoluteFilePath();
                    ++found;
                }
            }
            if (found == 0) {
                refused.append(QJsonObject{{QStringLiteral("chemin"), given}, {QStringLiteral("raison"), QStringLiteral("Ce dossier ne contient aucun fichier lisible.")}});
            }
            continue;
        }
        if (!readableMedia(path)) {
            refused.append(QJsonObject{{QStringLiteral("chemin"), given}, {QStringLiteral("raison"), QStringLiteral("Format .%1 non reconnu.").arg(info.suffix())}});
            continue;
        }
        files << info.absoluteFilePath();
    }
    files.removeDuplicates();
    // Un fichier déjà dans le chutier n'est pas ajouté deux fois : son id est renvoyé.
    QStringList toImport;
    for (const QString &file : std::as_const(files)) {
        const QStringList existing = model->getClipByUrl(QFileInfo(file));
        if (!existing.isEmpty()) {
            if (const auto clip = model->getClipByBinID(existing.first())) {
                medias.append(QJsonObject{{QStringLiteral("id"), existing.first()},
                                          {QStringLiteral("nom"), clip->clipName()},
                                          {QStringLiteral("type"), mediaTypeName(clip->clipType())},
                                          {QStringLiteral("duree"), time(int(clip->frameDuration()))},
                                          {QStringLiteral("deja_present"), true}});
                continue;
            }
        }
        toImport << file;
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    QString folderId = model->getRootFolder()->clipId();
    const QString folderName = args.value(QStringLiteral("dossier_chutier")).toString().trimmed();
    if (!toImport.isEmpty() && !folderName.isEmpty()) {
        const QString existing = model->getFolderIdByName(folderName);
        if (!existing.isEmpty() && model->getFolderByBinId(existing)) {
            folderId = existing;
        } else {
            QString newId;
            if (!model->requestAddFolder(newId, folderName, folderId, undo, redo)) {
                return error(QStringLiteral("non_disponible"), QStringLiteral("Le dossier « %1 » n'a pas pu être créé dans le chutier.").arg(folderName));
            }
            folderId = newId;
        }
    }
    // L'import, puis l'attente que chaque clip soit prêt (60 s au plus). Aucune fenêtre : les doublons
    // sont déjà écartés, et le contrôle du profil à l'import est laissé de côté pendant l'appel.
    QSet<QString> ready;
    QMap<QString, QString> idByPath;
    auto onReady = [&ready](const QString &binId) { ready.insert(binId); };
    for (const QString &file : std::as_const(toImport)) {
        const QString id = ClipCreator::createClipFromFile(file, folderId, model, undo, redo, onReady);
        if (id != QLatin1String("-1") && !id.isEmpty()) {
            idByPath.insert(file, id);
        } else {
            refused.append(QJsonObject{{QStringLiteral("chemin"), file}, {QStringLiteral("raison"), QStringLiteral("Le fichier n'a pas pu être lu.")}});
        }
    }
    if (pCore->bin()) {
        pCore->bin()->shouldCheckProfile = false;
    }
    if (idByPath.isEmpty() && medias.isEmpty()) {
        undo();
        QStringList reasons;
        for (const QJsonValue &r : std::as_const(refused)) {
            reasons << QStringLiteral("%1 : %2").arg(r.toObject().value(QStringLiteral("chemin")).toString(), r.toObject().value(QStringLiteral("raison")).toString());
        }
        return error(QStringLiteral("fichier_introuvable"), QStringLiteral("Aucun fichier importé. %1").arg(reasons.join(QLatin1Char(' '))),
                     QStringLiteral("N'invente pas de chemin : demande à la personne où sont ses fichiers."));
    }
    if (!idByPath.isEmpty()) {
        pCore->pushUndo(undo, redo, i18ncp("@action undo text", "Import %1 file", "Import %1 files", idByPath.size()));
        QElapsedTimer timer;
        timer.start();
        while (ready.size() < idByPath.size() && timer.elapsed() < 60000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
    }
    for (auto it = idByPath.constBegin(); it != idByPath.constEnd(); ++it) {
        const auto clip = model->getClipByBinID(it.value());
        if (!clip || !clip->statusReady()) {
            refused.append(QJsonObject{{QStringLiteral("chemin"), it.key()}, {QStringLiteral("raison"), QStringLiteral("Le fichier n'a pas pu être lu dans le temps imparti.")}});
            continue;
        }
        medias.append(QJsonObject{{QStringLiteral("id"), it.value()},
                                  {QStringLiteral("nom"), clip->clipName()},
                                  {QStringLiteral("type"), mediaTypeName(clip->clipType())},
                                  {QStringLiteral("duree"), time(int(clip->frameDuration()))},
                                  {QStringLiteral("deja_present"), false}});
    }
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("medias"), medias}, {QStringLiteral("refuses"), refused}};
}

// ---------------------------------------------------------------------------------------------
// retirer_media

QJsonObject retirerMedia(const Timeline &timeline, const Args &args)
{
    auto model = pCore->projectItemModel();
    const QJsonArray ids = args.value(QStringLiteral("media_ids")).toArray();
    if (ids.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("media_ids est vide."));
    }
    QStringList mediaIds;
    for (const QJsonValue &v : ids) {
        const QString id = v.toString();
        if (id.isEmpty() || !model->hasClip(id)) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun média ne porte l'id « %1 » dans le chutier.").arg(id), QStringLiteral("Appelle lister_medias et prends le champ id."));
        }
        if (!mediaIds.contains(id)) {
            mediaIds << id;
        }
    }
    // Tout vérifier avant de toucher : un clip posé sur une piste verrouillée bloque tout le lot.
    int timelineClips = 0;
    for (const QString &id : std::as_const(mediaIds)) {
        for (int tid : tracksOnScreen(timeline)) {
            for (int cid : clipsOnTrack(timeline, tid)) {
                if (timeline->getClipBinId(cid) == id) {
                    if (timeline->trackIsLocked(tid)) {
                        return error(QStringLiteral("piste_verrouillee"), QStringLiteral("Un de ces médias est posé sur la piste verrouillée %1.").arg(trackLabel(timeline, tid)),
                                     QStringLiteral("Laisse ce média dans le projet, ou demande à la personne."));
                    }
                    ++timelineClips;
                }
            }
        }
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    for (const QString &id : std::as_const(mediaIds)) {
        const auto clip = model->getClipByBinID(id);
        if (!clip || !model->requestBinClipDeletion(clip, undo, redo)) {
            undo();
            return error(QStringLiteral("non_disponible"), QStringLiteral("Le média %1 n'a pas pu être retiré du chutier.").arg(id));
        }
    }
    pCore->pushUndo(undo, redo, i18ncp("@action undo text", "Remove %1 media from project", "Remove %1 media from project", mediaIds.size()));
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("retires"), QJsonArray::fromStringList(mediaIds)},
                       {QStringLiteral("clips_timeline_retires"), timelineClips},
                       {QStringLiteral("fichiers_sur_le_disque"), QStringLiteral("intacts")}};
}

// ---------------------------------------------------------------------------------------------
// voir_image : une PNG du chemin rendu ; l'envoi au modèle est l'affaire de la session.

QJsonObject voirImage(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    const int position = requiredTime(args, QStringLiteral("position"), failure);
    if (position < 0) {
        return failure;
    }
    const int width = std::clamp(args.value(QStringLiteral("largeur_px")).toInt(384), 128, 512);
    int projectWidth = 1920;
    int projectHeight = 1080;
    if (const auto &profile = pCore->getCurrentProfile()) {
        projectWidth = profile->width();
        projectHeight = profile->height();
    }
    // Les vignettes vont dans le cache du projet (13 octobre 2026) : rangées au même endroit pour toutes les
    // KoraCut ouvertes, « media-4-3.png » ou « timeline-100.png » d'un projet écrasait celle d'un autre, et un
    // agent pouvait regarder l'image d'un autre montage. Elles partent aussi avec le cache du projet.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/vignettes");
    if (pCore->currentDoc()) {
        bool ok = false;
        const QDir base = pCore->currentDoc()->getCacheDir(CacheBase, &ok);
        if (ok) {
            dir = base.absoluteFilePath(QStringLiteral("vignettes"));
        }
    }
    QDir().mkpath(dir);
    QImage image;
    QString path;
    if (args.contains(QStringLiteral("media_id"))) {
        const QString mediaId = args.value(QStringLiteral("media_id")).toString();
        const auto clip = pCore->projectItemModel()->getClipByBinID(mediaId);
        if (mediaId.isEmpty() || !clip) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun média ne porte l'id « %1 ».").arg(mediaId), QStringLiteral("Appelle lister_medias et prends le champ id."));
        }
        if (!clip->hasVideo() && clip->clipType() != ClipType::Image) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("%1 est un son : il n'y a rien à regarder.").arg(clip->clipName()), QStringLiteral("Choisis un média avec de l'image."));
        }
        const int frame = std::clamp(position, 0, std::max(0, int(clip->frameDuration()) - 1));
        const QSize size = clip->getFrameSize();
        const int height = size.width() > 0 ? std::max(1, width * size.height() / size.width()) : width * projectHeight / projectWidth;
        if (!clip->url().isEmpty() && QFileInfo::exists(clip->url())) {
            const KoraCutPerformance::DecoderCacheRoom room(4);
            Mlt::Producer producer(pCore->getProjectProfile(), clip->url().toUtf8().constData());
            if (producer.is_valid()) {
                image = KThumb::getFrame(producer, frame, width, height);
            }
        }
        if (image.isNull()) {
            // Un média sans fichier (couleur, titre, séquence) : son producteur du chutier.
            if (const std::shared_ptr<Mlt::Producer> producer = clip->originalProducer(); producer && producer->is_valid()) {
                image = KThumb::getFrame(*producer, frame, width, height);
            }
        }
        if (image.isNull()) {
            image = ThumbnailCache::get()->getThumbnail(mediaId, frame);
            if (!image.isNull()) {
                image = image.scaledToWidth(width, Qt::SmoothTransformation);
            }
        }
        path = QStringLiteral("%1/media-%2-%3.png").arg(dir, mediaId).arg(frame);
    } else {
        if (position >= timeline->duration()) {
            return error(QStringLiteral("hors_clip"), QStringLiteral("La timeline dure %1 images : l'image %2 n'existe pas.").arg(timeline->duration()).arg(position));
        }
        const QString xml = timeline->sceneList(QString());
        const KoraCutPerformance::DecoderCacheRoom room(xml);
        // L'image regardée est celle de l'export : l'étalonnage revient même s'il est en pause dans l'aperçu.
        const KoraCutPerformance::GradeBypass graded;
        Mlt::Producer producer(pCore->getProjectProfile(), "xml-string", xml.toUtf8().constData());
        if (producer.is_valid()) {
            image = KThumb::getFrame(producer, position, width, std::max(1, width * projectHeight / projectWidth));
        }
        // Une image par séquence : la même position n'est pas la même image dans deux séquences du projet.
        path = QStringLiteral("%1/timeline-%2-%3.png").arg(dir, timeline->uuid().toString(QUuid::WithoutBraces).left(8)).arg(position);
    }
    if (image.isNull() || !image.save(path, "PNG")) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucune image n'a pu être tirée à cette position."));
    }
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("position"), time(position)},
                       {QStringLiteral("l"), image.width()},
                       {QStringLiteral("h"), image.height()},
                       {QStringLiteral("chemin"), path}};
}

// ---------------------------------------------------------------------------------------------
// transcrire_media : le Whisper de la coupe automatique, avec son cache. L'appel attend la fin.

QJsonObject transcrireMedia(const Timeline &, const Args &args)
{
    const QString mediaId = args.value(QStringLiteral("media_id")).toString();
    const auto clip = pCore->projectItemModel()->getClipByBinID(mediaId);
    if (mediaId.isEmpty() || !clip) {
        return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun média ne porte l'id « %1 ».").arg(mediaId), QStringLiteral("Appelle lister_medias et prends le champ id."));
    }
    if (!clip->hasAudio() || clip->url().isEmpty() || !QFileInfo::exists(clip->url())) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("%1 n'a pas de son à transcrire.").arg(clip->clipName()), QStringLiteral("Choisis un média avec du son."));
    }
    QString language = args.value(QStringLiteral("langue")).toString(QStringLiteral("auto"));
    if (language == QLatin1String("auto")) {
        language.clear(); // Whisper devine
    }
    const double fps = projectFps();
    int start = 0;
    int end = std::max(0, int(clip->frameDuration()));
    if (args.contains(QStringLiteral("debut"))) {
        start = KoraCutAgentTools::parseTime(args.value(QStringLiteral("debut")), fps);
    }
    if (args.contains(QStringLiteral("fin"))) {
        end = KoraCutAgentTools::parseTime(args.value(QStringLiteral("fin")), fps);
    }
    if (start < 0 || end < 0 || end < start) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("La plage debut / fin ne se lit pas."));
    }
    const bool wordsLevel = args.value(QStringLiteral("niveau")).toString(QStringLiteral("mots")) != QLatin1String("segments");
    const QString file = clip->url();
    if (KoraCutTranscriber::chooseModel().isEmpty()) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("Aucun modèle de reconnaissance vocale n'est installé sur cette machine."),
                     QStringLiteral("Dis-le à la personne : Préférences > Reconnaissance vocale (modèle « base » ou « small »). Appuie-toi sur detecter_silences en attendant."));
    }
    const QString cacheFile = KoraCutTranscriber::cacheFileFor(file, language);
    bool fromCache = QFileInfo::exists(cacheFile);
    if (!fromCache) {
        KoraCutTranscriber transcriber;
        QEventLoop loop;
        QString failure;
        bool finished = false;
        QObject::connect(&transcriber, &KoraCutTranscriber::finished, &loop, [&](const QList<KoraCutAutoCut::Word> &) {
            finished = true;
            loop.quit();
        });
        QObject::connect(&transcriber, &KoraCutTranscriber::failed, &loop, [&](const QString &message) {
            failure = message;
            loop.quit();
        });
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(20 * 60 * 1000);
        transcriber.startFile(file, language);
        if (!finished && failure.isEmpty()) {
            loop.exec();
        }
        if (!failure.isEmpty()) {
            return error(QStringLiteral("non_disponible"), failure, QStringLiteral("La reconnaissance vocale s'installe dans Préférences > Reconnaissance vocale."));
        }
        if (!finished) {
            transcriber.cancel();
            return error(QStringLiteral("non_disponible"), QStringLiteral("La transcription n'a pas fini en vingt minutes."), QStringLiteral("Transcris une portion plus courte avec debut et fin."));
        }
    }
    QFile cached(cacheFile);
    if (!cached.open(QIODevice::ReadOnly)) {
        return error(QStringLiteral("non_disponible"), QStringLiteral("La transcription n'a pas pu être relue."));
    }
    const QJsonObject json = QJsonDocument::fromJson(cached.readAll()).object();
    const QList<KoraCutAutoCut::Word> words = KoraCutAutoCut::parseWords(QJsonDocument(json).toJson(QJsonDocument::Compact));
    // Les mots en phrases : une ponctuation finale, une pause de 0,7 s ou 25 mots ferment un segment.
    QJsonArray segments;
    QList<KoraCutAutoCut::Word> current;
    auto flush = [&]() {
        if (current.isEmpty()) {
            return;
        }
        const int first = int(std::lround(current.first().start * fps));
        const int last = int(std::lround(current.last().end * fps));
        if (last > start && first < end) {
            QStringList text;
            QJsonArray list;
            for (const KoraCutAutoCut::Word &w : std::as_const(current)) {
                text << w.text;
                if (wordsLevel) {
                    list.append(QJsonObject{{QStringLiteral("d"), int(std::lround(w.start * fps))}, {QStringLiteral("f"), int(std::lround(w.end * fps))}, {QStringLiteral("m"), w.text}});
                }
            }
            QJsonObject segment{{QStringLiteral("debut_i"), first}, {QStringLiteral("fin_i"), last}, {QStringLiteral("texte"), text.join(QLatin1Char(' '))}};
            if (wordsLevel) {
                segment.insert(QStringLiteral("mots"), list);
            }
            segments.append(segment);
        }
        current.clear();
    };
    for (const KoraCutAutoCut::Word &w : words) {
        if (!current.isEmpty() && w.start - current.last().end > 0.7) {
            flush();
        }
        current << w;
        const QString t = w.text.trimmed();
        if (t.endsWith(QLatin1Char('.')) || t.endsWith(QLatin1Char('?')) || t.endsWith(QLatin1Char('!')) || current.size() >= 25) {
            flush();
        }
    }
    flush();
    return QJsonObject{{QStringLiteral("ok"), true},
                       {QStringLiteral("langue"), json.value(QStringLiteral("langue")).toString(language.isEmpty() ? QStringLiteral("auto") : language)},
                       {QStringLiteral("segments"), segments},
                       {QStringLiteral("depuis_cache"), fromCache}};
}

} // namespace

namespace KoraCutAgentToolsPrivate {

QMap<QString, Tool> mediaTools()
{
    return {
        {QStringLiteral("ajouter_marqueur"), ajouterMarqueur},     {QStringLiteral("importer_media"), importerMedia},
        {QStringLiteral("retirer_media"), retirerMedia},           {QStringLiteral("voir_image"), voirImage},
        {QStringLiteral("transcrire_media"), transcrireMedia},     {QStringLiteral("detecter_silences"), detecterSilences},
        {QStringLiteral("temps_musique"), tempsMusique},
        {QStringLiteral("regler_gain_clip"), reglerGainClip},      {QStringLiteral("regler_piste_audio"), reglerPisteAudio},
        {QStringLiteral("mesurer_audio"), mesurerAudio},           {QStringLiteral("normaliser_loudness"), normaliserLoudness},
        {QStringLiteral("appliquer_ducking"), appliquerDucking},   {QStringLiteral("ajouter_effet_audio"), ajouterEffetAudio},
    };
}

} // namespace KoraCutAgentToolsPrivate
