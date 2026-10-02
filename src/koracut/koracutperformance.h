/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "utils/machineprofile.h"

#include <KConfigGroup>
#include <QJsonObject>
#include <QString>
#include <functional>

namespace Mlt {
class Tractor;
}

/** @brief Les réglages de performance selon la machine (chantier 10, document 6, section 5).
 *
 * MachineProfile mesure et recommande ; ce fichier pose les réglages dans KdenliveSettings et
 * dans koracutrc, groupe [KoraCut], et les relit là où le moteur s'en sert. Clés [KoraCut] :
 * - profilMachine : « modeste », « moyen », « confortable », vide = détecté (Préférences, Performances)
 * - profilApplique : le profil dont les défauts ont été posés (témoin du premier lancement)
 * - apercuAdaptatif : plancher de l'aperçu adaptatif (cran previewScaling, 8 = 360p), 0 = coupé
 * - dropDiviseur : images sautées tolérées par seconde = i/s ÷ dropDiviseur (2 sur machine modeste)
 * - proxyLourdLargeur : largeur à partir de laquelle un HEVC, AV1, VP9 ou 10 bits reçoit un proxy, -1 = non
 * - effetsLourdsSimplifies : aperçu simplifié des effets lourds pendant la lecture
 * - cacheProducteursMax : décodeurs gardés ouverts par MLT, 0 = calcul de Kdenlive
 * - decodageMaterielEssai : résultat du dernier essai du décodage matériel (texte court)
 *
 * KORACUT_PROFIL=modeste|moyen|confortable force un profil sans rien écrire (banc d'essai).
 */
namespace KoraCutPerformance {

KConfigGroup group();

/** @brief Le profil en service (voir MachineProfile::effectiveLevel). */
MachineProfile::Level level();
/** @brief Les réglages recommandés pour @p level sur cette machine. */
MachineProfile::Recommendation recommendation(MachineProfile::Level level);

/** @brief Pose les défauts de @p level dans KdenliveSettings et [KoraCut], et les applique aux
 * moniteurs ouverts. Le rendu automatique garde sa propre règle (KoraCutRender). */
void applyProfile(MachineProfile::Level level);
/** @brief Enregistre le choix de la personne (vide = détecté) et pose ses défauts. */
void chooseProfile(const QString &id);
/** @brief Premier lancement : défauts du profil, puis essai du décodage matériel en tâche de
 * fond. Ne fait rien sur une configuration existante, sauf si KORACUT_PROFIL force un autre profil. */
void applyFirstLaunchDefaults(bool firstRun);

int adaptiveFloor();
int dropDivisor();
int proxyHeavyMinWidth();
bool simplifiedHeavyEffects();
int producerCacheMax();

/** @brief Garde de la place dans le cache des décodeurs de MLT tant qu'une copie de la scène vit.
 *
 * Ce cache (« producer_avformat ») est commun à tout le processus. Une copie XML de la timeline ouvre
 * un décodeur par producteur : dans un cache plein, elle évince ceux du moniteur alors que ses images
 * les tiennent encore. Quand le moniteur les rouvre, MLT 7.41 se trompe dans ses comptes
 * (mlt_cache_put garde le même mlt_cache_item pour l'ancien et le nouveau décodeur) et ferme le
 * nouveau décodeur pendant que le fil de rendu décode avec : plantages du 2 octobre 2026 (document 6).
 * À déclarer AVANT le producteur de la copie, pour être détruit après lui (sa fermeture retire ses
 * entrées du cache, puis la taille d'origine revient).
 */
class DecoderCacheRoom
{
public:
    /** @p producers : le nombre de producteurs que la copie va ouvrir. */
    explicit DecoderCacheRoom(int producers);
    /** La place est comptée dans le XML de la scène. */
    explicit DecoderCacheRoom(const QString &sceneXml);
    ~DecoderCacheRoom();
    DecoderCacheRoom(const DecoderCacheRoom &) = delete;
    DecoderCacheRoom &operator=(const DecoderCacheRoom &) = delete;

    /** @brief Le nombre de producteurs décrits par un XML MLT, avec une marge pour les fichiers imbriqués. */
    static int producersIn(const QString &sceneXml);
    /** @brief La taille que le cache aurait sans aucune garde en cours. */
    static int baseSize();

private:
    int m_extra{0};
};

/** @brief Ce que le Diagnostic et les Préférences affichent pour le décodage : la puce, le processeur,
 * ou le processeur pour ce projet parce qu'un clip ne passe pas par la puce. @p tooltip reçoit les clips en cause. */
QString decodingLabel(QString *tooltip = nullptr);
/** @brief Essaie les décodeurs matériels du système sur une courte vidéo de test (ffmpeg), en
 * tâche de fond : le premier qui décode sans erreur et au moins aussi vite que le logiciel est
 * posé dans hwDecoding ; sinon hwDecoding reste vide (décodage logiciel). @p done reçoit le
 * décodeur retenu (vide sinon) et un compte rendu d'une ligne. */
void testHwDecoding(const std::function<void(const QString &decoder, const QString &report)> &done = {});

/** @brief Ce qui est en service, pour le panneau Diagnostic et la porte. */
QJsonObject stateJson();

/** @brief Aperçu simplifié des effets lourds (document 6, section 3g).
 *
 * Pendant la lecture du moniteur Programme, sur un profil où effetsLourdsSimplifies est vrai, les
 * filtres de data/heavy_effects.txt actifs dans la séquence sont mis en pause (disable=1, marqués
 * _koracut_simplifie pour ne rendre que ceux-là), et reviennent à la pause. Toute écriture du projet
 * en XML (enregistrement, sauvegarde automatique, rendu d'aperçu, export) passe par SaveGuard, qui
 * remet les effets le temps de l'écriture : un effet mis en pause ne peut pas finir dans un fichier. */
namespace HeavyEffects {
/** @brief Les identifiants de service MLT de la liste (lue une fois). */
const QStringList &list();
bool isHeavy(const QString &service);
/** @brief Met en pause les effets lourds de @p tractor ; rend leur nombre. */
int simplify(Mlt::Tractor *tractor);
/** @brief Même chose pour la séquence affichée dans la timeline. */
int simplifyCurrentTimeline();
/** @brief Remet tous les effets mis en pause. */
void restore();
/** @brief Nombre d'effets en pause en ce moment. */
int count();

class SaveGuard
{
public:
    SaveGuard();
    ~SaveGuard();

private:
    bool m_active{false};
};
} // namespace HeavyEffects

} // namespace KoraCutPerformance
