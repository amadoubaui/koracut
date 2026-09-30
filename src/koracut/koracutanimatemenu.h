/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QObject>

#include <memory>

class QAction;
class QMenu;
class QWidget;
class TimelineItemModel;

/** @class KoraCutAnimateMenu
 * @brief Les gestes d'animation au clic droit sur un clip de la timeline (document 19, lot 5, suite) :
 * « Animer » (entrée, sortie, titre machine à écrire) et « Transition vers le plan suivant » (celles de
 * l'application et les styles HyperFrames). Chaque choix appelle l'outil des agents correspondant
 * (animer_entree, animer_sortie, animer_texte, transition_app, motion_transition) : une étape d'annulation.
 */
class KoraCutAnimateMenu : public QObject
{
    Q_OBJECT

public:
    explicit KoraCutAnimateMenu(QWidget *parent);
    QMenu *animateMenu() const { return m_animate; }
    QMenu *transitionMenu() const { return m_transition; }
    /** @brief À appeler juste avant d'ouvrir le menu du clip @p cid : ce qui ne s'applique pas est masqué ou grisé. */
    void prepare(const std::shared_ptr<TimelineItemModel> &timeline, int cid);
    /** @brief Le choix @p action appliqué au clip préparé (le menu le fait au clic ; les tests et les captures aussi). */
    QJsonObject apply(QAction *action);
    /** @brief L'action du menu qui appelle @p tool avec @p style, nullptr sinon. */
    QAction *find(const QString &tool, const QString &style) const;

private:
    QMenu *m_animate;
    QMenu *m_transition;
    QAction *m_typewriter{nullptr};
    std::weak_ptr<TimelineItemModel> m_timeline;
    int m_clip{-1};
    QAction *add(QMenu *menu, const QString &label, const QString &tool, const QString &style);
};
