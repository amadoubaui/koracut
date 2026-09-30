/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "koracutanimatemenu.h"

#include "bin/projectclip.h"
#include "bin/projectitemmodel.h"
#include "core.h"
#include "koracut/koracutagenttools.h"
#include "timeline2/model/timelineitemmodel.hpp"

#include <KLocalizedString>

#include <QAction>
#include <QMenu>
#include <QWidget>

namespace {
// Un titre de groupe dans un menu : grisé, sans effet (les sections de Qt n'affichent pas leur texte ici).
void header(QMenu *menu, const QString &text)
{
    QAction *action = menu->addAction(text);
    action->setEnabled(false);
}
} // namespace

KoraCutAnimateMenu::KoraCutAnimateMenu(QWidget *parent)
    : QObject(parent)
    , m_animate(new QMenu(i18nc("@title:menu timeline clip", "Animate"), parent))
    , m_transition(new QMenu(i18nc("@title:menu timeline clip", "Transition to Next Shot"), parent))
{
    // Les gestes de l'application : l'effet Transformer du clip, rien à rendre.
    header(m_animate, i18nc("@title:menu clip animation", "Entrance"));
    add(m_animate, i18nc("@action clip animation", "Slide"), QStringLiteral("animer_entree"), QStringLiteral("glisse_court"));
    add(m_animate, i18nc("@action clip animation", "Zoom"), QStringLiteral("animer_entree"), QStringLiteral("zoom_avant"));
    add(m_animate, i18nc("@action clip animation", "Fade"), QStringLiteral("animer_entree"), QStringLiteral("fondu"));
    add(m_animate, i18nc("@action clip animation", "Bounce"), QStringLiteral("animer_entree"), QStringLiteral("rebond"));
    m_animate->addSeparator();
    header(m_animate, i18nc("@title:menu clip animation", "Exit"));
    add(m_animate, i18nc("@action clip animation", "Slide"), QStringLiteral("animer_sortie"), QStringLiteral("glisse_court"));
    add(m_animate, i18nc("@action clip animation", "Zoom"), QStringLiteral("animer_sortie"), QStringLiteral("zoom_arriere"));
    add(m_animate, i18nc("@action clip animation", "Fade"), QStringLiteral("animer_sortie"), QStringLiteral("fondu"));
    add(m_animate, i18nc("@action clip animation", "Bounce"), QStringLiteral("animer_sortie"), QStringLiteral("rebond"));
    m_animate->addSeparator();
    m_typewriter = add(m_animate, i18nc("@action clip animation", "Typewriter Title"), QStringLiteral("animer_texte"), QStringLiteral("machine_a_ecrire"));

    // Les transitions de l'application, puis celles de HyperFrames (rendues, posées sur la piste « Transitions »).
    add(m_transition, i18nc("@action transition", "Cross Fade"), QStringLiteral("transition_app"), QStringLiteral("fondu_enchaine"));
    add(m_transition, i18nc("@action transition", "Wipe"), QStringLiteral("transition_app"), QStringLiteral("volet"));
    add(m_transition, i18nc("@action transition", "Slide"), QStringLiteral("transition_app"), QStringLiteral("glissement"));
    add(m_transition, i18nc("@action transition", "Fade to Black"), QStringLiteral("transition_app"), QStringLiteral("fondu_noir"));
    add(m_transition, i18nc("@action transition", "Zoom"), QStringLiteral("transition_app"), QStringLiteral("zoom"));
    add(m_transition, i18nc("@action transition", "Push"), QStringLiteral("transition_app"), QStringLiteral("pousse"));
    add(m_transition, i18nc("@action transition", "Shake"), QStringLiteral("transition_app"), QStringLiteral("secousse"));
    m_transition->addSeparator();
    header(m_transition, i18nc("@title:menu transition", "HyperFrames"));
    add(m_transition, i18nc("@action transition", "Cut Strings"), QStringLiteral("motion_transition"), QStringLiteral("cordes"));
    add(m_transition, i18nc("@action transition", "32° Sweep"), QStringLiteral("motion_transition"), QStringLiteral("balayage"));
    add(m_transition, i18nc("@action transition", "Mist"), QStringLiteral("motion_transition"), QStringLiteral("voile"));
    add(m_transition, i18nc("@action transition", "Flash"), QStringLiteral("motion_transition"), QStringLiteral("eclair"));
}

QAction *KoraCutAnimateMenu::add(QMenu *menu, const QString &label, const QString &tool, const QString &style)
{
    QAction *action = menu->addAction(label);
    action->setData(QStringList{tool, style});
    connect(action, &QAction::triggered, this, [this, action]() { apply(action); });
    return action;
}

QAction *KoraCutAnimateMenu::find(const QString &tool, const QString &style) const
{
    for (QMenu *menu : {m_animate, m_transition}) {
        for (QAction *action : menu->actions()) {
            if (action->data().toStringList() == QStringList{tool, style}) {
                return action;
            }
        }
    }
    return nullptr;
}

void KoraCutAnimateMenu::prepare(const std::shared_ptr<TimelineItemModel> &timeline, int cid)
{
    m_timeline = timeline;
    m_clip = cid;
    const bool clip = timeline && timeline->isClip(cid);
    const bool video = clip && !timeline->isAudioTrack(timeline->getClipTrackId(cid));
    m_animate->menuAction()->setVisible(video);
    m_transition->menuAction()->setVisible(video);
    if (!video) {
        return;
    }
    // La machine à écrire ne vaut que pour un titre de KoraCut.
    const auto bin = pCore->projectItemModel() ? pCore->projectItemModel()->getClipByBinID(timeline->getClipBinId(cid)) : nullptr;
    m_typewriter->setVisible(bin && bin->clipType() == ClipType::Text);
    // Une transition se pose sur une coupe : un plan doit suivre, collé, sur la même piste.
    const int end = timeline->getClipPosition(cid) + timeline->getClipPlaytime(cid);
    const int next = timeline->getClipByPosition(timeline->getClipTrackId(cid), end);
    m_transition->setEnabled(next != -1 && next != cid && timeline->isClip(next) && timeline->getClipPosition(next) == end);
}

QJsonObject KoraCutAnimateMenu::apply(QAction *action)
{
    auto timeline = m_timeline.lock();
    if (!action || !timeline || !timeline->isClip(m_clip)) {
        return {};
    }
    const QStringList data = action->data().toStringList();
    const QString tool = data.value(0);
    const QString style = data.value(1);
    QJsonObject args{{QStringLiteral("style"), style}};
    if (tool == QLatin1String("transition_app") || tool == QLatin1String("motion_transition")) {
        args.insert(QStringLiteral("clip_avant"), m_clip);
        // Le rendu HyperFrames se fait en fond : le clip arrive sur la piste « Transitions » quand il est prêt.
        args.insert(QStringLiteral("attendre"), false);
    } else {
        args.insert(QStringLiteral("clip_id"), m_clip);
    }
    const QJsonObject result = KoraCutAgentTools::call(timeline, tool, args);
    if (result.value(QStringLiteral("ok")).toBool()) {
        if (tool == QLatin1String("motion_transition")) {
            pCore->displayMessage(i18nc("@info:status", "Transition added. Its animation is being rendered, it will show on the Transitions track."),
                                  InformationMessage, 4000);
        }
        return result;
    }
    const QJsonObject failure = result.value(QStringLiteral("erreur")).toObject();
    const QString code = failure.value(QStringLiteral("code")).toString();
    QString text;
    if (code == QLatin1String("pas_de_matiere")) {
        text = i18nc("@info:status", "Not enough footage around the cut for this transition. Try Zoom, Push or Fade to Black, which need none.");
    } else if (code == QLatin1String("entree_invalide") && failure.value(QStringLiteral("message")).toString().contains(QLatin1String("plus long"))) {
        text = i18nc("@info:status", "This clip is too short for this animation or transition.");
    } else {
        text = i18nc("@info:status", "This animation could not be applied. %1", failure.value(QStringLiteral("message")).toString());
    }
    pCore->displayMessage(text, ErrorMessage);
    return result;
}
