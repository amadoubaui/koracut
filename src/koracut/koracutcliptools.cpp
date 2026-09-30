/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutcliptools.h"

#include "core.h"
#include "effects/effectsrepository.hpp"
#include "effects/effectstack/model/effectitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"
#include "timeline2/model/timelineitemmodel.hpp"
#include "undohelper.hpp"

#include <KLocalizedString>

#include <cmath>

namespace KoraCutClipTools {

namespace {

const char *kToolProperty = "kdenlive:koracut_outil";
const char *kVariantProperty = "kdenlive:koracut_variante";

using Params = QMap<QString, QString>;

// Passe-haut et passe-bas : la pente de Butterworth (Q 0,707), écrite en largeur de bande (Hz),
// l'unité par défaut du fichier d'effet : Q = f / largeur, donc largeur = f × 1,414. Les valeurs
// par défaut du fichier (707 Hz ou 0,707 Hz) donnent une pente molle ou une résonance forte, et
// une largeur en Q (0,707) serait affichée au minimum du curseur (100) dans la pile d'effets.
QString butterworthWidth(int hz)
{
    return QString::number(qRound(hz * 1.41421356));
}

Step highpass(int hz)
{
    return {QStringLiteral("avfilter.highpass"), {{QStringLiteral("av.f"), QString::number(hz)}, {QStringLiteral("av.t"), QStringLiteral("h")},
                                                  {QStringLiteral("av.w"), butterworthWidth(hz)}, {QStringLiteral("av.p"), QStringLiteral("2")}}};
}

Step lowpass(int hz)
{
    return {QStringLiteral("avfilter.lowpass"), {{QStringLiteral("av.f"), QString::number(hz)}, {QStringLiteral("av.t"), QStringLiteral("h")},
                                                 {QStringLiteral("av.w"), butterworthWidth(hz)}, {QStringLiteral("av.p"), QStringLiteral("2")}}};
}

// Égaliseur en cloche, largeur en octaves.
Step bell(int hz, double octaves, double gainDb)
{
    return {QStringLiteral("avfilter.equalizer"), {{QStringLiteral("av.f"), QString::number(hz)}, {QStringLiteral("av.t"), QStringLiteral("o")},
                                                   {QStringLiteral("av.w"), QString::number(octaves)}, {QStringLiteral("av.g"), QString::number(gainDb)}}};
}

// Compresseur : seuil linéaire (0,1 = -20 dB), détection RMS.
Step compressor(double threshold, double ratio, double makeup, int attackMs = 10, int releaseMs = 150)
{
    return {QStringLiteral("avfilter.acompressor"),
            {{QStringLiteral("av.threshold"), QString::number(threshold)}, {QStringLiteral("av.ratio"), QString::number(ratio)},
             {QStringLiteral("av.makeup"), QString::number(makeup)}, {QStringLiteral("av.attack"), QString::number(attackMs)},
             {QStringLiteral("av.release"), QString::number(releaseMs)}, {QStringLiteral("av.detection"), QStringLiteral("1")}}};
}

// Porte : sous le seuil, le son baisse de « range » (0,25 = -12 dB, 0,05 = -26 dB).
Step gate(double threshold, double range, int releaseMs)
{
    return {QStringLiteral("avfilter.agate"),
            {{QStringLiteral("av.threshold"), QString::number(threshold)}, {QStringLiteral("av.range"), QString::number(range)},
             {QStringLiteral("av.ratio"), QStringLiteral("4")}, {QStringLiteral("av.attack"), QStringLiteral("5")},
             {QStringLiteral("av.release"), QString::number(releaseMs)}, {QStringLiteral("av.detection"), QStringLiteral("1")}}};
}

// Le suppresseur de bruit pour la voix (RNNoise, réseau de neurones de Xiph, greffon LADSPA de
// werman). Seuil de détection de voix à 0 : le réseau travaille seul, sans couper entre les mots, et
// le retard reste de 10 ms (mesuré ; avec un seuil, il monte à 50 ms et plus : image et son décalés).
Step rnnoise()
{
    return {QStringLiteral("ladspa.9354877"), {{QStringLiteral("2"), QStringLiteral("0")}, {QStringLiteral("3"), QStringLiteral("200")}, {QStringLiteral("4"), QStringLiteral("0")}}};
}

// Écho à plusieurs retards (ms) : sert aussi de réverbération légère quand les retards sont serrés.
Step echo(double inGain, double outGain, const QString &delays, const QString &decays)
{
    return {QStringLiteral("avfilter.aecho"), {{QStringLiteral("av.in_gain"), QString::number(inGain)}, {QStringLiteral("av.out_gain"), QString::number(outGain)},
                                               {QStringLiteral("av.delays"), delays}, {QStringLiteral("av.decays"), decays}}};
}

// Hauteur de la voix en demi-tons, durée inchangée (Rubber Band).
Step pitch(double semitones)
{
    return {QStringLiteral("rbpitch"), {{QStringLiteral("pitchscale"), QString::number(std::pow(2., semitones / 12.), 'f', 4)}, {QStringLiteral("stretch"), QStringLiteral("0")}}};
}

Step softclip(const QString &type, double threshold)
{
    return {QStringLiteral("avfilter.asoftclip"), {{QStringLiteral("av.type"), type}, {QStringLiteral("av.threshold"), QString::number(threshold)}}};
}

int rank(const QString &key)
{
    if (key == QLatin1String("bruit")) {
        return 0;
    }
    if (key == QLatin1String("voix")) {
        return 1;
    }
    if (key == QLatin1String("effet_voix")) {
        return 2;
    }
    return -1;
}

QString propertyOf(const std::shared_ptr<EffectItemModel> &effect, const char *name)
{
    return QString::fromUtf8(effect->filter().get(name));
}

} // namespace

QString toolKey(Tool tool)
{
    switch (tool) {
    case Tool::Denoise:
        return QStringLiteral("bruit");
    case Tool::Enhance:
        return QStringLiteral("voix");
    case Tool::VoiceEffect:
        return QStringLiteral("effet_voix");
    }
    return QString();
}

QList<Choice> denoiseLevels()
{
    return {
        {QStringLiteral("leger"), i18nc("@item noise reduction level", "Light"), QString(),
         i18nc("@info:tooltip", "Removes the low rumble and softens the noise between words. Keeps the room sound.")},
        {QStringLiteral("moyen"), i18nc("@item noise reduction level", "Medium"), QString(),
         i18nc("@info:tooltip", "Removes the background noise under the voice (fan, street, hiss). Recommended.")},
        {QStringLiteral("fort"), i18nc("@item noise reduction level", "Strong"), QString(),
         i18nc("@info:tooltip", "Same, and also closes the sound between sentences. For very noisy places.")},
    };
}

QList<Choice> voiceEffects()
{
    return {
        {QStringLiteral("megaphone"), i18nc("@item voice effect", "Megaphone"), QStringLiteral("koracut-voix-megaphone"),
         i18nc("@info:tooltip", "A loud, narrow voice, like through a megaphone.")},
        {QStringLiteral("micro"), i18nc("@item voice effect", "Old mic"), QStringLiteral("koracut-voix-micro"),
         i18nc("@info:tooltip", "The warm, slightly worn sound of an old microphone.")},
        {QStringLiteral("telephone"), i18nc("@item voice effect", "Phone"), QStringLiteral("koracut-voix-telephone"),
         i18nc("@info:tooltip", "The voice heard on a phone call.")},
        {QStringLiteral("radio"), i18nc("@item voice effect", "Radio"), QStringLiteral("koracut-voix-radio"),
         i18nc("@info:tooltip", "A tight, present voice, like on the radio.")},
        {QStringLiteral("echo"), i18nc("@item voice effect", "Echo"), QStringLiteral("koracut-voix-echo"),
         i18nc("@info:tooltip", "The voice comes back two times.")},
        {QStringLiteral("salle"), i18nc("@item voice effect", "Big room"), QStringLiteral("koracut-voix-salle"),
         i18nc("@info:tooltip", "The voice in a large hall.")},
        {QStringLiteral("grotte"), i18nc("@item voice effect", "Cave"), QStringLiteral("koracut-voix-grotte"),
         i18nc("@info:tooltip", "Long, dark echoes, like in a cave.")},
        {QStringLiteral("grave"), i18nc("@item voice effect", "Deep voice"), QStringLiteral("koracut-voix-grave"),
         i18nc("@info:tooltip", "Four semitones lower, same speed.")},
        {QStringLiteral("aigue"), i18nc("@item voice effect", "High voice"), QStringLiteral("koracut-voix-aigue"),
         i18nc("@info:tooltip", "Five semitones higher, same speed.")},
        {QStringLiteral("robot"), i18nc("@item voice effect", "Robot"), QStringLiteral("koracut-voix-robot"),
         i18nc("@info:tooltip", "A metallic voice, like a robot.")},
    };
}

QString choiceLabel(Tool tool, const QString &id)
{
    if (tool == Tool::Enhance) {
        return id.isEmpty() ? QString() : i18nc("@item", "Enhance voice");
    }
    const QList<Choice> list = tool == Tool::Denoise ? denoiseLevels() : voiceEffects();
    for (const Choice &c : list) {
        if (c.id == id) {
            return c.label;
        }
    }
    return QString();
}

QList<Step> recipe(Tool tool, const QString &variant)
{
    switch (tool) {
    case Tool::Denoise:
        // Mesures (document 24) sur une voix avec bruit rose et ronflement à 50 Hz : bruit entre les
        // mots -44 dB avant ; léger -51, moyen -67, fort -67 et silence entre les phrases.
        if (variant == QLatin1String("leger")) {
            return {highpass(80), gate(0.02, 0.25, 200)};
        }
        if (variant == QLatin1String("moyen")) {
            return {highpass(80), rnnoise()};
        }
        if (variant == QLatin1String("fort")) {
            return {highpass(90), rnnoise(), gate(0.008, 0.05, 250)};
        }
        return {};
    case Tool::Enhance:
        if (variant != QLatin1String("standard")) {
            return {};
        }
        // Une chaîne de studio radio : ronflement retiré, boue dégagée, présence, sifflantes adoucies,
        // niveau resserré, crêtes plafonnées à -1 dB. Retard total : 5 ms (le limiteur).
        return {highpass(80),
                bell(300, 1., -3.),
                bell(3500, 1.2, 3.),
                {QStringLiteral("avfilter.deesser"), {{QStringLiteral("av.i"), QStringLiteral("0.4")}, {QStringLiteral("av.m"), QStringLiteral("0.5")},
                                                      {QStringLiteral("av.f"), QStringLiteral("0.5")}, {QStringLiteral("av.s"), QStringLiteral("o")}}},
                compressor(0.1, 3., 2.),
                {QStringLiteral("avfilter.alimiter"), {{QStringLiteral("av.limit"), QStringLiteral("0.89")}, {QStringLiteral("av.level"), QStringLiteral("0")},
                                                       {QStringLiteral("av.attack"), QStringLiteral("5")}, {QStringLiteral("av.release"), QStringLiteral("50")}}}};
    case Tool::VoiceEffect:
        if (variant == QLatin1String("megaphone")) {
            return {highpass(500), lowpass(3500), bell(1800, 1., 8.), compressor(0.05, 8., 4., 5, 100), softclip(QStringLiteral("tanh"), 0.5)};
        }
        if (variant == QLatin1String("micro")) {
            return {highpass(150), lowpass(6000), bell(1200, 1., 4.), compressor(0.1, 3., 2.5), softclip(QStringLiteral("atan"), 0.7)};
        }
        if (variant == QLatin1String("telephone")) {
            return {highpass(300), highpass(300), lowpass(3400), lowpass(3400), compressor(0.1, 4., 2.)};
        }
        if (variant == QLatin1String("radio")) {
            return {highpass(200), lowpass(5000), bell(2500, 1., 4.), compressor(0.08, 4., 1.6)};
        }
        if (variant == QLatin1String("echo")) {
            return {echo(0.8, 0.85, QStringLiteral("350|700"), QStringLiteral("0.4|0.2"))};
        }
        if (variant == QLatin1String("salle")) {
            return {echo(0.8, 0.8, QStringLiteral("40|70|110|160|230|320"), QStringLiteral("0.45|0.4|0.35|0.28|0.2|0.12"))};
        }
        if (variant == QLatin1String("grotte")) {
            return {echo(0.8, 0.75, QStringLiteral("180|370|560|780|1000"), QStringLiteral("0.55|0.45|0.35|0.25|0.15")), lowpass(4500)};
        }
        if (variant == QLatin1String("grave")) {
            return {pitch(-4.)};
        }
        if (variant == QLatin1String("aigue")) {
            return {pitch(5.)};
        }
        if (variant == QLatin1String("robot")) {
            return {{QStringLiteral("avfilter.tremolo"), {{QStringLiteral("av.f"), QStringLiteral("50")}, {QStringLiteral("av.d"), QStringLiteral("0.9")}}},
                    {QStringLiteral("avfilter.flanger"),
                     {{QStringLiteral("av.delay"), QStringLiteral("5")}, {QStringLiteral("av.depth"), QStringLiteral("0")}, {QStringLiteral("av.regen"), QStringLiteral("70")},
                      {QStringLiteral("av.speed"), QStringLiteral("0.1")}}},
                    compressor(0.1, 3., 3.)};
        }
        return {};
    }
    return {};
}

int audioTarget(const std::shared_ptr<TimelineItemModel> &timeline, int clipId)
{
    if (!timeline || !timeline->isClip(clipId)) {
        return -1;
    }
    if (timeline->isAudioTrack(timeline->getClipTrackId(clipId))) {
        return clipId;
    }
    const int partner = timeline->getClipSplitPartner(clipId);
    if (partner > -1 && timeline->isAudioTrack(timeline->getClipTrackId(partner))) {
        return partner;
    }
    return -1;
}

int videoTarget(const std::shared_ptr<TimelineItemModel> &timeline, int clipId)
{
    if (!timeline || !timeline->isClip(clipId)) {
        return -1;
    }
    int cid = clipId;
    if (timeline->isAudioTrack(timeline->getClipTrackId(cid))) {
        cid = timeline->getClipSplitPartner(clipId);
        if (cid < 0 || timeline->isAudioTrack(timeline->getClipTrackId(cid))) {
            return -1;
        }
    }
    const auto state = timeline->getClipState(cid);
    // Une image animée seulement : une couleur, un titre, une image fixe ne tremblent pas.
    if (state.second != ClipType::AV && state.second != ClipType::Video && state.second != ClipType::Playlist) {
        return -1;
    }
    return cid;
}

QString current(const std::shared_ptr<TimelineItemModel> &timeline, int audioClipId, Tool tool)
{
    if (!timeline || !timeline->isClip(audioClipId)) {
        return QString();
    }
    const auto stack = timeline->getClipEffectStackModel(audioClipId);
    if (!stack) {
        return QString();
    }
    const QString key = toolKey(tool);
    for (int row = 0; row < stack->rowCount(); ++row) {
        auto effect = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (effect && propertyOf(effect, kToolProperty) == key) {
            const QString variant = propertyOf(effect, kVariantProperty);
            return variant.isEmpty() ? QStringLiteral("standard") : variant;
        }
    }
    return QString();
}

Result apply(const std::shared_ptr<TimelineItemModel> &timeline, int audioClipId, Tool tool, const QString &variant)
{
    Result result;
    if (!timeline || !timeline->isClip(audioClipId) || !timeline->isAudioTrack(timeline->getClipTrackId(audioClipId))) {
        result.code = QStringLiteral("mauvais_type_de_piste");
        result.message = i18nc("@info", "This clip has no sound.");
        return result;
    }
    const auto stack = timeline->getClipEffectStackModel(audioClipId);
    if (!stack) {
        result.code = QStringLiteral("non_disponible");
        result.message = i18nc("@info", "The effects of this clip cannot be changed.");
        return result;
    }
    QList<Step> steps;
    if (!variant.isEmpty()) {
        steps = recipe(tool, variant);
        if (steps.isEmpty()) {
            result.code = QStringLiteral("entree_invalide");
            result.message = i18nc("@info", "Unknown choice: %1.", variant);
            return result;
        }
    }
    const QString key = toolKey(tool);
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };

    // 1. Retirer ce que l'outil avait posé (du bas vers le haut, pour garder les rangs valables).
    bool removed = false;
    for (int row = stack->rowCount() - 1; row >= 0; --row) {
        auto effect = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (effect && propertyOf(effect, kToolProperty) == key) {
            QString name;
            stack->removeEffectWithUndo(effect, name, undo, redo);
            removed = true;
        }
    }

    // 2. Poser la nouvelle chaîne, puis la ranger avant les outils qui viennent après elle.
    int insertRow = -1;
    for (int row = 0; row < stack->rowCount(); ++row) {
        auto effect = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(row));
        if (effect && rank(propertyOf(effect, kToolProperty)) > rank(key)) {
            insertRow = row;
            break;
        }
    }
    for (const Step &step : std::as_const(steps)) {
        if (!EffectsRepository::get()->exists(step.effect)) {
            result.missing << step.effect;
            continue;
        }
        Params params = step.params;
        params.insert(QString::fromLatin1(kToolProperty), key);
        params.insert(QString::fromLatin1(kVariantProperty), variant);
        const int rowsBefore = stack->rowCount();
        if (!stack->appendEffectWithParams(step.effect, params, undo, redo) || stack->rowCount() <= rowsBefore) {
            result.missing << step.effect;
            continue;
        }
        auto added = std::dynamic_pointer_cast<EffectItemModel>(stack->getEffectStackRow(stack->rowCount() - 1));
        if (!added) {
            continue;
        }
        // Le modèle de l'effet doit connaître les réglages posés sur le filtre (pile d'effets).
        for (auto it = step.params.constBegin(); it != step.params.constEnd(); ++it) {
            added->setParameter(it.key(), it.value(), false);
        }
        if (insertRow > -1) {
            stack->moveEffectWithUndo(insertRow, added, undo, redo);
            ++insertRow;
        }
        result.effects << step.effect;
    }
    if (!variant.isEmpty() && result.effects.isEmpty()) {
        undo();
        result.code = QStringLiteral("non_disponible");
        result.message = i18nc("@info", "The effects this tool needs are missing on this computer: %1.", result.missing.join(QStringLiteral(", ")));
        return result;
    }
    if (!removed && variant.isEmpty()) {
        // Rien à retirer : pas d'étape d'annulation vide.
        result.ok = true;
        return result;
    }
    QString text;
    switch (tool) {
    case Tool::Denoise:
        text = variant.isEmpty() ? i18nc("@action undo text", "Remove noise reduction") : i18nc("@action undo text", "Reduce noise");
        break;
    case Tool::Enhance:
        text = variant.isEmpty() ? i18nc("@action undo text", "Remove voice enhancement") : i18nc("@action undo text", "Enhance voice");
        break;
    case Tool::VoiceEffect:
        text = variant.isEmpty() ? i18nc("@action undo text", "Remove voice effect") : i18nc("@action undo text", "Voice effect: %1", choiceLabel(tool, variant));
        break;
    }
    pCore->pushUndo(undo, redo, text);
    result.ok = true;
    return result;
}

} // namespace KoraCutClipTools
