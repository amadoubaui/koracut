/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Document 13 (24 septembre 2026) : couleur_clip et couleur_piste. Contrat : montage.json.
// Les menus « Couleur du clip » et « Couleur de la piste » passent aussi par ici.

#include "koracutagenttools.h"
#include "koracutagenttools_p.h"
#include "koracuttimelinecolors.h"

#include "core.h"
#include "timeline2/model/timelinefunctions.hpp"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>

namespace KoraCutAgentToolsPrivate {

namespace {

QString readColor(const Args &args, QJsonObject &failure)
{
    bool ok = false;
    const QString color = KoraCutTimelineColors::normalize(args.value(QStringLiteral("couleur")).toString(), &ok);
    if (!ok || !args.value(QStringLiteral("couleur")).isString()) {
        failure = error(QStringLiteral("entree_invalide"), QStringLiteral("La couleur « %1 » ne se lit pas.").arg(args.value(QStringLiteral("couleur")).toString()),
                        QStringLiteral("Un nom de la palette (bleu, violet, vert, orange, rouge, jaune, rose, turquoise, olive, brun, bleu_nuit, gris), "
                                       "« #RRGGBB » ou « defaut »."));
    }
    return color;
}

QJsonObject couleurClip(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    const QString color = readColor(args, failure);
    if (!failure.isEmpty()) {
        return failure;
    }
    QList<int> ids;
    QJsonArray done;
    for (const QJsonValue &v : args.value(QStringLiteral("clip_ids")).toArray()) {
        const int cid = v.toInt(-1);
        // Pas clipIdCheck : un clip d'une piste verrouillée se colore aussi, comme dans Resolve.
        if (!timeline->isClip(cid)) {
            return error(QStringLiteral("id_inconnu"), QStringLiteral("Aucun clip n'a l'identifiant %1.").arg(cid),
                         QStringLiteral("Relis etat_timeline : les identifiants changent après une coupe ou une annulation."));
        }
        if (!ids.contains(cid)) {
            ids << cid;
            done.append(cid);
        }
    }
    if (ids.isEmpty()) {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("clip_ids est vide."), QStringLiteral("Lis les clips dans etat_timeline."));
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (!TimelineFunctions::koraCutSetClipsColor(timeline, ids, color, undo, redo)) {
        return error(QStringLiteral("echec"), QStringLiteral("La couleur n'a pas pu être posée."));
    }
    pCore->pushUndo(undo, redo, ids.size() > 1 ? i18nc("@action undo text", "Change clips color") : i18nc("@action undo text", "Change clip color"));
    const QString name = KoraCutTimelineColors::nameOf(color);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("clip_ids"), done}, {QStringLiteral("couleur"), name.isEmpty() ? QStringLiteral("defaut") : name}};
}

QJsonObject couleurPiste(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    const QString color = readColor(args, failure);
    if (!failure.isEmpty()) {
        return failure;
    }
    const int tid = args.value(QStringLiteral("piste_id")).toInt(-1);
    // Une étiquette de couleur ne touche pas au contenu : une piste verrouillée se colore aussi.
    if (const QJsonObject check = trackIdCheck(timeline, tid, false); !check.isEmpty()) {
        return check;
    }
    Fun undo = []() { return true; };
    Fun redo = []() { return true; };
    if (!TimelineFunctions::koraCutSetTrackColor(timeline, tid, color, undo, redo)) {
        return error(QStringLiteral("echec"), QStringLiteral("La couleur n'a pas pu être posée."));
    }
    pCore->pushUndo(undo, redo, i18nc("@action undo text", "Change track color"));
    const QString name = KoraCutTimelineColors::nameOf(color);
    return QJsonObject{{QStringLiteral("ok"), true}, {QStringLiteral("piste_id"), tid}, {QStringLiteral("couleur"), name.isEmpty() ? QStringLiteral("defaut") : name}};
}

} // namespace

QMap<QString, Tool> couleurTools()
{
    return {{QStringLiteral("couleur_clip"), couleurClip}, {QStringLiteral("couleur_piste"), couleurPiste}};
}

} // namespace KoraCutAgentToolsPrivate
