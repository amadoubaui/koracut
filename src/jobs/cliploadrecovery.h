/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QString>

/** @brief Ce qu'on fait d'un clip dont la tâche de chargement s'arrête sans résultat.
 *
 * Écrit le 21 septembre 2026 pour le faux « fichier introuvable » : une tâche interrompue
 * (annulation, changement de profil du projet) marquait le clip introuvable sans jamais
 * regarder si le fichier était encore sur le disque. Un média valide se retrouvait barré
 * d'un cadre rouge et d'une croix, définitivement.
 *
 * La règle est ici, à part et sans dépendance, pour qu'elle se lise et se teste seule.
 */
namespace ClipLoadRecovery {

enum class Decision {
    Ignore,      /**< ne rien changer : quelqu'un d'autre s'occupe de ce clip */
    Retry,       /**< remettre en attente et relancer le chargement */
    Delete,      /**< retirer le clip du chutier : l'utilisateur vient d'ajouter un média illisible */
    MarkMissing, /**< marquer le clip introuvable : le fichier a vraiment disparu */
};

/** @brief Les faits qu'on connaît au moment où la tâche s'arrête. */
struct Facts {
    bool softDelete = false;        /**< le clip est en cours de suppression */
    bool thumbOnly = false;         /**< tâche de vignette, elle ne juge pas du média */
    bool managerBlocked = false;    /**< on ferme le projet */
    bool otherLoadJobPending = false; /**< une tâche de chargement plus récente est en route */
    bool clipAlreadyReady = false;  /**< le clip est déjà chargé, donc valide */
    bool loadFailed = false;        /**< le média lui-même a refusé de s'ouvrir */
    bool reloading = false;         /**< le clip existait déjà et se rechargeait */
    bool canceled = false;          /**< la tâche a été annulée */
    bool sourceOnDisk = false;      /**< la ressource est bien un fichier présent */
    int retriesDone = 0;            /**< relances déjà tentées pour ce clip */
    int maxRetries = 3;             /**< plafond, pour ne jamais tourner en boucle */
};

/** @brief Vrai si la ressource est bien un fichier présent sur le disque.
 *  Un clip sans fichier (couleur, titre, séquence) rend faux : on ne peut rien vérifier. */
bool sourceIsThere(const QString &path);

/** @brief La décision, à partir des seuls faits. */
Decision decide(const Facts &facts);

} // namespace ClipLoadRecovery
