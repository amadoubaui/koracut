/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

// Chantier 10 (24 septembre 2026) : les deux outils du rendu d'aperçu. Contrat : montage.json.
// Le rendu est celui du bouton « Rendu » de la timeline (PreviewManager, koracutrender.cpp) : des
// morceaux rendus sur une piste cachée, pour que les passages chargés se lisent sans saccade.

#include "koracutagenttools.h"
#include "koracutagenttools_p.h"
#include "koracutrender.h"

#include "core.h"
#include "kdenlivesettings.h"
#include "mainwindow.h"
#include "timeline2/model/timelineitemmodel.hpp"
#include "timeline2/view/timelinecontroller.h"
#include "timeline2/view/timelinewidget.h"

namespace KoraCutAgentToolsPrivate {

namespace {

TimelineController *controllerFor(const Timeline &timeline, QJsonObject &failure)
{
    TimelineWidget *widget = pCore && pCore->window() ? pCore->window()->getCurrentTimeline() : nullptr;
    if (!widget || !widget->controller() || widget->controller()->getModel() != timeline) {
        failure = error(QStringLiteral("non_disponible"), QStringLiteral("Le rendu ne vise que la séquence ouverte à l'écran."),
                        QStringLiteral("Active la séquence d'abord (projet sequence faire=activer)."));
        return nullptr;
    }
    return widget->controller();
}

QJsonObject rendreApercu(const Timeline &timeline, const Args &args)
{
    QJsonObject failure;
    TimelineController *controller = controllerFor(timeline, failure);
    if (!controller) {
        return failure;
    }
    const int duration = timeline->duration();
    if (duration <= 1) {
        return error(QStringLiteral("pas_de_matiere"), QStringLiteral("La séquence est vide."));
    }
    QPoint zone;
    QString mode = args.value(QStringLiteral("zone")).toString(QStringLiteral("zone"));
    if (args.contains(QStringLiteral("in")) || args.contains(QStringLiteral("out"))) {
        const int in = requiredTime(args, QStringLiteral("in"), failure);
        if (in < 0) {
            return failure;
        }
        const int out = requiredTime(args, QStringLiteral("out"), failure);
        if (out < 0) {
            return failure;
        }
        if (out <= in) {
            return error(QStringLiteral("entree_invalide"), QStringLiteral("« out » doit venir après « in »."), QStringLiteral("out est exclu : une seconde à 25 i/s, c'est in 0, out 25."));
        }
        zone = QPoint(in, std::min(out, duration) - 1);
        mode = QStringLiteral("plage");
    } else if (mode == QLatin1String("selection")) {
        zone = controller->selectionZone();
        if (zone.x() < 0) {
            return error(QStringLiteral("non_disponible"), QStringLiteral("Aucun clip n'est sélectionné."), QStringLiteral("Sélectionne d'abord (timeline selection clip_ids=[…])."));
        }
    } else if (mode == QLatin1String("tout")) {
        zone = QPoint(0, duration - 1);
    } else if (mode == QLatin1String("zone")) {
        zone = QPoint(controller->zoneIn(), controller->zoneOut());
        if (zone.x() < 0 || zone.y() <= zone.x()) {
            zone = QPoint(0, duration - 1);
            mode = QStringLiteral("tout");
        }
    } else {
        return error(QStringLiteral("entree_invalide"), QStringLiteral("zone vaut « zone », « selection » ou « tout »."));
    }
    if (!controller->renderRange(zone)) {
        return error(QStringLiteral("rendu_echoue"), QStringLiteral("Le rendu n'a pas pu démarrer (dossier de cache ou profil de rendu)."),
                     QStringLiteral("Lis le journal (Console) : un message dit ce qui manque."));
    }
    const bool wait = args.value(QStringLiteral("attendre")).toBool(false);
    bool finished = false;
    if (wait) {
        // Au plus 10 minutes ; la boucle d'événements continue de tourner, l'interface reste vivante.
        finished = KoraCutRender::waitForRender(600000);
    }
    QJsonObject result = KoraCutRender::stateJson();
    result.insert(QStringLiteral("demande"), QJsonObject{{QStringLiteral("mode"), mode}, {QStringLiteral("in"), time(zone.x())}, {QStringLiteral("out"), time(zone.y() + 1)}});
    result.insert(QStringLiteral("attendu"), wait);
    if (wait) {
        result.insert(QStringLiteral("termine"), finished);
    }
    return result;
}

QJsonObject etatApercu(const Timeline &timeline, const Args &)
{
    QJsonObject failure;
    if (!controllerFor(timeline, failure)) {
        return failure;
    }
    return KoraCutRender::stateJson();
}

} // namespace

QMap<QString, Tool> apercuTools()
{
    return {
        {QStringLiteral("rendre_apercu"), rendreApercu},
        {QStringLiteral("etat_apercu"), etatApercu},
    };
}

} // namespace KoraCutAgentToolsPrivate
