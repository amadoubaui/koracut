/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

// Ce que les fichiers des outils d'agent partagent entre eux (koracutagenttools.cpp, ...montage.cpp,
// ...media.cpp). Rien ici n'est destiné au reste de l'application : l'entrée publique reste
// koracutagenttools.h.

#include "definitions.h"
#include "undohelper.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <functional>
#include <memory>
#include <vector>

class EffectItemModel;
class EffectStackModel;
class TimelineItemModel;

namespace KoraCutAgentToolsPrivate {

using Args = QJsonObject;
using Timeline = std::shared_ptr<TimelineItemModel>;
using Tool = std::function<QJsonObject(const Timeline &, const Args &)>;

double projectFps();
QJsonObject error(const QString &code, const QString &message, const QString &advice = QString());
QJsonObject time(int frames);
/** @brief Paramètre de temps obligatoire : -1 et une erreur `entree_invalide` s'il manque ou ne se lit pas. */
int requiredTime(const Args &args, const QString &key, QJsonObject &failure);
QString trackLabel(const Timeline &timeline, int trackId);
bool trackMasked(const Timeline &timeline, int trackId);
/** @brief Les pistes dans l'ordre de l'écran, de haut en bas. */
QList<int> tracksOnScreen(const Timeline &timeline);
/** @brief Les clips d'une piste, triés par position. */
QList<int> clipsOnTrack(const Timeline &timeline, int trackId);
QJsonObject clipIdCheck(const Timeline &timeline, int clipId);
QJsonObject trackIdCheck(const Timeline &timeline, int trackId, bool mustBeUnlocked = true);
/** @brief Durée du fondu d'entrée ou de sortie d'un clip, 0 s'il n'y en a pas. */
int currentFade(const Timeline &timeline, int clipId, bool fadeIn);
/** @brief Le type d'un média tel que lister_medias le nomme : video, audio, image, titre, couleur, sequence. */
QString mediaTypeName(ClipType::ProducerType type);

// ---- Les effets (koracutagenttoolsmontage.cpp) ------------------------------------------------

/** @brief Les effets d'une pile tels que les agents les voient : sans les fondus (ils vivent dans
 * `fondu`) ni les effets intégrés cachés. Le rang dans ce vecteur est `effet_index`. */
std::vector<std::shared_ptr<EffectItemModel>> visibleEffects(const std::shared_ptr<EffectStackModel> &stack);
/** @brief La pile visée par clip_id / piste_id (-1 : le master), ou une erreur. */
std::shared_ptr<EffectStackModel> stackFor(const Timeline &timeline, const Args &args, QJsonObject &failure, bool *isClip = nullptr, int *itemId = nullptr);
/** @brief Le tableau `fx` de l'état : `{ i, id, off? }`, plus `p` et `kf` au niveau complet.
 * @param itemIn le point d'entrée du clip, pour ramener les images clés au début du clip. */
QJsonArray effectsJson(const std::shared_ptr<EffectStackModel> &stack, bool complete, int itemIn);
/** @brief Tous les paramètres d'un effet, nom technique vers valeur texte. */
QJsonObject parametersJson(const std::shared_ptr<EffectItemModel> &effect);
/** @brief Un nombre en texte court (10 chiffres significatifs), le reste tel quel. */
QString variantText(const QVariant &value);
/** @brief Le rang visible d'un effet dans sa pile, -1 s'il n'y est plus. */
int effectRank(const std::shared_ptr<EffectStackModel> &stack, const std::shared_ptr<EffectItemModel> &effect);
/** @brief Les noms techniques des paramètres d'un effet. */
QStringList parameterNames(const std::shared_ptr<EffectItemModel> &effect);
/** @brief Le premier effet visible qui porte cet identifiant, ou nul. */
std::shared_ptr<EffectItemModel> findVisibleEffect(const std::shared_ptr<EffectStackModel> &stack, const QString &effectId);
/** @brief Pose l'effet avec ses paramètres dans undo/redo (un effet intégré caché du même identifiant
 * est réveillé au lieu d'être doublé). Nul si la pile le refuse. */
std::shared_ptr<EffectItemModel> appendEffect(const std::shared_ptr<EffectStackModel> &stack, const QString &effectId, const QMap<QString, QString> &params, Fun &undo,
                                              Fun &redo);
/** @brief Change des paramètres dans undo/redo (valeurs précédentes relues sur le modèle). */
void setParametersWithUndo(const std::shared_ptr<EffectItemModel> &effect, const QMap<QString, QString> &params, Fun &undo, Fun &redo);

// ---- L'étalonnage (koracutagenttoolsetalonnage.cpp) --------------------------------------------

/** @brief Les effets que copier_etalonnage et reinitialiser_etalonnage tiennent pour des effets de
 * couleur : ceux que posent les outils du Coloriste, plus ceux que la personne pose à la main. */
bool isColourEffect(const QString &effectId);

// ---- Les outils, par lot -------------------------------------------------------------------------

/** @brief Lot A (24 septembre 2026) : titre, transition, effets, images clés, transformation. */
QMap<QString, Tool> montageTools();
/** @brief Lot A (24 septembre 2026) : projet (marqueur, import, retrait, image, transcription,
 * silences) et audio (gains, piste, mesure, normalisation, ducking, traitements). */
QMap<QString, Tool> mediaTools();
/** @brief Lot B (24 septembre 2026) : les 10 outils d'étalonnage (etalonnage.json). */
QMap<QString, Tool> etalonnageTools();
/** @brief Document 22, lot 3 (28 septembre 2026) : les outils d'étalonnage par nœuds, etal_* (etalonnage.json). */
QMap<QString, Tool> gradeTools();
/** @brief Lot B (24 septembre 2026) : les 8 outils d'export (export.json). */
QMap<QString, Tool> exportTools();
/** @brief Chantier 10 (24 septembre 2026) : le rendu d'aperçu, rendre_apercu et etat_apercu. */
QMap<QString, Tool> apercuTools();
/** @brief Document 21 (24 septembre 2026) : le script du projet, script_lire, script_ecrire, script_modele. */
QMap<QString, Tool> scriptTools();
/** @brief Lot 5 du document 19 (24 septembre 2026) : les animations élément par élément. HyperFrames
 * (motion_element_seul, motion_transition, motion_ouvrir) et l'application seule (animer_entree,
 * animer_sortie, animer_texte, transition_app). */
QMap<QString, Tool> animationTools();
/** @brief Document 13 (24 septembre 2026) : couleur_clip et couleur_piste (koracutagenttoolscouleur.cpp). */
QMap<QString, Tool> couleurTools();
/** @brief Outils d'organisation (25 septembre 2026, koracutagenttoolsorganisation.cpp) : dossiers et noms du
 *  chutier, fichiers dans le dossier du projet, séquences, pistes et clips (organisation.json). */
QMap<QString, Tool> organisationTools();
/** @brief 25 septembre 2026 (koracutagenttoolsreglages.cpp) : le thème et les réglages de l'application
 *  (reglages.json). Ils n'ont pas besoin d'une séquence ouverte. */
QMap<QString, Tool> reglagesTools();
/** @brief Chantier 5 (29 septembre 2026, koracutagenttoolsvoix.cpp) : la voix off de synthèse,
 *  voix_off_generer, voix_off_voix, voix_off_installer (voix.json). */
QMap<QString, Tool> voixTools();
/** @brief Document 24 (30 septembre 2026, koracutagenttoolsoutils.cpp) : la section Outils de
 *  l'inspecteur, reduire_bruit, ameliorer_voix, effet_voix (audio.json) et stabiliser (montage.json). */
QMap<QString, Tool> outilsTools();
QStringList reglagesToolNames();

} // namespace KoraCutAgentToolsPrivate
