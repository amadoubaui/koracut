/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <memory>

class TimelineItemModel;

/** @brief Les outils du son d'un plan, en un clic : réduire le bruit, améliorer la voix, effets de voix.
 *
 * Document 24 (30 septembre 2026). Chaque outil pose une petite chaîne d'effets ordinaires de la
 * bibliothèque (FFmpeg par MLT, et le suppresseur de bruit RNNoise en LADSPA), marqués par la
 * propriété `kdenlive:koracut_outil` (et `kdenlive:koracut_variante`) : ils restent réglables dans la
 * pile d'effets comme n'importe quel effet, et l'outil sait les retrouver pour les changer ou les
 * retirer. Poser, changer ou retirer un outil = une seule étape d'annulation.
 *
 * Les recettes sont choisies pour être légères (filtres temps réel, aucun fichier écrit) et ont été
 * mesurées sur une voix bruitée (document 24, « Mesures »).
 */
namespace KoraCutClipTools {

/** Un outil du son. L'ordre compte : c'est celui de la chaîne (le bruit d'abord, les effets à la fin). */
enum class Tool { Denoise, Enhance, VoiceEffect };

/** @brief Le nom de l'outil tel qu'il est écrit dans le projet et dans les outils des agents
 *  (`bruit`, `voix`, `effet_voix`). */
QString toolKey(Tool tool);

/** Un choix d'un outil : niveau de réduction de bruit, effet de voix, niveau de stabilisation. */
struct Choice
{
    QString id;    ///< identifiant stable (leger, moyen, fort, megaphone…)
    QString label; ///< texte à l'écran, traduit
    QString icon;  ///< nom d'icône du thème
    QString hint;  ///< une phrase d'aide, traduite
};

/** @brief Les trois niveaux de réduction de bruit : leger, moyen (recommandé), fort. */
QList<Choice> denoiseLevels();
/** @brief Les effets de voix, dans l'ordre des cartes. */
QList<Choice> voiceEffects();
/** @brief Le libellé d'un choix (vide s'il est inconnu). */
QString choiceLabel(Tool tool, const QString &id);

/** Une étape d'une recette : un effet de la bibliothèque et ses réglages. */
struct Step
{
    QString effect;
    QMap<QString, QString> params;
};

/** @brief La chaîne d'effets d'un outil pour ce choix (vide si le choix est inconnu). `Enhance`
 *  n'a qu'une variante, `standard`. */
QList<Step> recipe(Tool tool, const QString &variant);

/** @brief Le clip qui porte le son d'un plan : lui-même s'il est sur une piste audio, sinon sa
 *  partie son groupée ; -1 s'il n'y en a pas. */
int audioTarget(const std::shared_ptr<TimelineItemModel> &timeline, int clipId);
/** @brief Le clip qui porte l'image d'un plan : lui-même s'il est sur une piste vidéo (image
 *  animée, pas une couleur ni un titre), sinon sa partie image groupée ; -1 sinon. */
int videoTarget(const std::shared_ptr<TimelineItemModel> &timeline, int clipId);

/** @brief La variante posée sur ce clip audio (vide : outil absent). */
QString current(const std::shared_ptr<TimelineItemModel> &timeline, int audioClipId, Tool tool);

struct Result
{
    bool ok = false;
    QString code;        ///< code d'erreur pour les agents (id_inconnu, mauvais_type_de_piste…)
    QString message;     ///< en français simple
    QStringList effects; ///< identifiants des effets posés, dans l'ordre
    QStringList missing; ///< effets de la recette absents de cette machine (sautés)
};

/** @brief Pose l'outil avec ce choix sur le clip audio, en remplaçant ce que l'outil avait posé.
 *  @p variant vide : retire l'outil. Une seule étape d'annulation. */
Result apply(const std::shared_ptr<TimelineItemModel> &timeline, int audioClipId, Tool tool, const QString &variant);

} // namespace KoraCutClipTools
