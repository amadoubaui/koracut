/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Document 24 (30 septembre 2026) : les outils de la section Outils de l'inspecteur, pour les agents.
// reduire_bruit, ameliorer_voix, effet_voix (audio.json) et stabiliser (montage.json). Ils passent par
// les mêmes fonctions que les boutons : ce que l'agent fait, la personne peut le refaire à la main.

#include "koracutagenttools.h"
#include "koracutagenttools_p.h"
#include "koracutcliptools.h"
#include "koracutstabilizer.h"

#include "timeline2/model/timelineitemmodel.hpp"

#include <QJsonArray>

using namespace KoraCutAgentToolsPrivate;
using SoundTool = KoraCutClipTools::Tool;

namespace {

QJsonArray toArray(const QStringList &list)
{
    QJsonArray a;
    for (const QString &s : list) {
        a.append(s);
    }
    return a;
}

/** Le clip audio d'un plan, ou une erreur. */
int audioOf(const Timeline &timeline, const Args &args, QJsonObject &failure)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
        failure = e;
        return -1;
    }
    const int audio = KoraCutClipTools::audioTarget(timeline, cid);
    if (audio < 0) {
        failure = error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("Le plan %1 n'a pas de son.").arg(cid),
                        QStringLiteral("Choisis un clip dont le champ k vaut \"a\", ou un plan posé avec son son."));
    }
    return audio;
}

QJsonObject soundTool(const Timeline &timeline, int audio, SoundTool tool, const QString &variant, const QString &field, const QJsonValue &value)
{
    const KoraCutClipTools::Result r = KoraCutClipTools::apply(timeline, audio, tool, variant);
    if (!r.ok) {
        return error(r.code, r.message);
    }
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), audio}, {field, value}, {QStringLiteral("effets"), toArray(r.effects)}};
    if (!r.missing.isEmpty()) {
        result.insert(QStringLiteral("effets_sautes"), toArray(r.missing));
    }
    return result;
}

QJsonObject reduireBruit(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    const int audio = audioOf(timeline, args, failure);
    if (audio < 0) {
        return failure;
    }
    const QString level = args.value(QStringLiteral("niveau")).toString(QStringLiteral("moyen"));
    if (!QStringList{QStringLiteral("leger"), QStringLiteral("moyen"), QStringLiteral("fort"), QStringLiteral("aucun")}.contains(level)) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("niveau vaut leger, moyen, fort ou aucun."));
    }
    return soundTool(timeline, audio, SoundTool::Denoise, level == QLatin1String("aucun") ? QString() : level, QStringLiteral("niveau"), level);
}

QJsonObject ameliorerVoix(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    const int audio = audioOf(timeline, args, failure);
    if (audio < 0) {
        return failure;
    }
    const bool on = args.value(QStringLiteral("actif")).toBool(true);
    return soundTool(timeline, audio, SoundTool::Enhance, on ? QStringLiteral("standard") : QString(), QStringLiteral("actif"), on);
}

QJsonObject effetVoix(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    const int audio = audioOf(timeline, args, failure);
    if (audio < 0) {
        return failure;
    }
    const QString effect = args.value(QStringLiteral("effet")).toString();
    if (effect != QLatin1String("aucun") && KoraCutClipTools::recipe(SoundTool::VoiceEffect, effect).isEmpty()) {
        QStringList ids;
        for (const auto &c : KoraCutClipTools::voiceEffects()) {
            ids << c.id;
        }
        return error(QStringLiteral("entree_invalide"), QStringLiteral("L'effet « %1 » n'existe pas.").arg(effect),
                     QStringLiteral("Effets : %1, ou aucun.").arg(ids.join(QStringLiteral(", "))));
    }
    return soundTool(timeline, audio, SoundTool::VoiceEffect, effect == QLatin1String("aucun") ? QString() : effect, QStringLiteral("effet"), effect);
}

QJsonObject stabiliser(const Timeline &timeline, const Args &args)
{
    const int cid = args.value(QStringLiteral("clip_id")).toInt(-1);
    if (QJsonObject e = clipIdCheck(timeline, cid); !e.isEmpty()) {
        return e;
    }
    const int video = KoraCutClipTools::videoTarget(timeline, cid);
    if (video < 0) {
        return error(QStringLiteral("mauvais_type_de_piste"), QStringLiteral("Le plan %1 n'a pas d'image animée à stabiliser.").arg(cid),
                     QStringLiteral("Choisis un clip vidéo (champ k \"v\") tiré d'une vidéo, pas d'une image, d'une couleur ou d'un titre."));
    }
    const QString level = args.value(QStringLiteral("niveau")).toString(QStringLiteral("moyen"));
    auto *stab = KoraCutStabilizer::instance();
    QJsonObject result{{QStringLiteral("ok"), true}, {QStringLiteral("clip_id"), video}, {QStringLiteral("niveau"), level}};
    if (level == QLatin1String("aucun")) {
        const auto s = stab->remove(timeline, video);
        if (!s.ok) {
            return error(s.code, s.message);
        }
        result.insert(QStringLiteral("etat"), QStringLiteral("retire"));
        return result;
    }
    // Déjà à ce niveau, ou analyse en cours pour ce niveau : on répond où on en est.
    if (stab->currentLevel(timeline, video) == level && stab->progress(timeline, video) < 0) {
        result.insert(QStringLiteral("etat"), QStringLiteral("fait"));
        return result;
    }
    if (stab->pendingLevel(timeline, video) == level) {
        result.insert(QStringLiteral("etat"), QStringLiteral("en_cours"));
        result.insert(QStringLiteral("pourcent"), stab->progress(timeline, video));
        return result;
    }
    const auto s = stab->start(timeline, video, level);
    if (!s.ok) {
        return error(s.code, s.message);
    }
    if (s.running) {
        result.insert(QStringLiteral("etat"), QStringLiteral("en_cours"));
        result.insert(QStringLiteral("pourcent"), std::max(0, stab->progress(timeline, video)));
        result.insert(QStringLiteral("conseil"), QStringLiteral("Rappelle stabiliser avec le même niveau pour savoir où en est l'analyse."));
    } else {
        result.insert(QStringLiteral("etat"), QStringLiteral("fait"));
    }
    return result;
}

} // namespace

namespace KoraCutAgentToolsPrivate {

QMap<QString, Tool> outilsTools()
{
    return {
        {QStringLiteral("reduire_bruit"), reduireBruit},
        {QStringLiteral("ameliorer_voix"), ameliorerVoix},
        {QStringLiteral("effet_voix"), effetVoix},
        {QStringLiteral("stabiliser"), stabiliser},
    };
}

} // namespace KoraCutAgentToolsPrivate
