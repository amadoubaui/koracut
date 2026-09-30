/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QColor>
#include <QList>
#include <QObject>
#include <QString>

#include <memory>

class QAction;
class QMenu;
class QWidget;
class TimelineItemModel;

/** @brief La couleur des clips et des pistes de la timeline (document 13, 24 septembre 2026),
 * comme les couleurs de clip de DaVinci Resolve et les étiquettes de Premiere Pro. */
namespace KoraCutTimelineColors {

struct Swatch
{
    /** @brief Le nom qu'emploient les outils d'agent (« bleu », « bleu_nuit »…). */
    QString id;
    QColor color;
};

/** @brief Les douze couleurs de la palette : les quatre de la charte, puis huit autres lisibles sur #1E1E1E. */
const QList<Swatch> &palette();
/** @brief Le libellé traduit d'une couleur de la palette. */
QString label(const QString &id);
/** @brief Un nom de la palette ou « #RRGGBB » en « #RRGGBB » ; « defaut » (ou « piste ») et vide en vide.
 *  @p ok faux si le texte ne se lit pas. */
QString normalize(const QString &input, bool *ok);
/** @brief Le nom de la palette d'une couleur « #RRGGBB », sinon la couleur elle-même ; vide reste vide. */
QString nameOf(const QString &hex);

} // namespace KoraCutTimelineColors

/** @class KoraCutColorMenu
 * @brief Les deux sous-menus « Couleur du clip » (clic droit sur un clip) et « Couleur de la piste »
 * (clic droit sur une tête de piste). Chaque choix passe par les outils couleur_clip et couleur_piste,
 * comme les agents et la porte : une étape d'annulation. Les pastilles sont dessinées une fois. */
class KoraCutColorMenu : public QObject
{
    Q_OBJECT

public:
    explicit KoraCutColorMenu(QWidget *parent);
    QMenu *clipMenu() const { return m_clip; }
    QMenu *trackMenu() const { return m_track; }
    /** @brief Avant d'ouvrir le menu du clip @p cid : la sélection s'il en fait partie, sinon lui seul. */
    void prepareClip(const std::shared_ptr<TimelineItemModel> &timeline, int cid);
    /** @brief Avant d'ouvrir le menu de la piste @p tid. */
    void prepareTrack(const std::shared_ptr<TimelineItemModel> &timeline, int tid);
    /** @brief Les clips visés par le prochain choix (tests et captures). */
    QList<int> preparedClips() const { return m_clips; }

private:
    QMenu *m_clip;
    QMenu *m_track;
    std::weak_ptr<TimelineItemModel> m_timeline;
    QList<int> m_clips;
    int m_trackId{-1};
    void fill(QMenu *menu, bool forClip);
    void apply(bool forClip, const QString &id);
    void check(QMenu *menu, const QString &current);
};
