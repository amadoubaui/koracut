/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>
#include <memory>

class KoraCutMotionMirror;
class TimelineItemModel;

/** @brief Les outils que les agents appellent pour monter, exécutés dans l'application.
 *
 * Chantier 3, lot 1 (23 septembre 2026). Les noms, les paramètres et la forme des réponses sont
 * ceux de `Agents in-app/outils/*.json`, qui font foi ; l'architecture est celle du document
 * « 5 - Agents IA - architecture.md » : le service d'agents décide, l'application exécute ici.
 *
 * Chaque outil passe par les fonctions annulables de la timeline et répond
 * `{ "ok": true, ... }` ou `{ "ok": false, "erreur": { "code", "message", "conseil" } }`.
 * Les temps entrent en images ou en timecode « HH:MM:SS:II » et sortent sous les deux formes
 * `{ "i": 150, "tc": "00:00:06:00" }`. Convention in/out des outils : `out` exclu ; Kdenlive
 * garde un `out` inclus, la conversion se fait ici et nulle part ailleurs.
 *
 * La demande d'accord des outils à confirmation (`x_confirmation`) n'est pas faite ici : c'est au
 * client du service de l'intercaler avant l'appel.
 */
namespace KoraCutAgentTools {

/** @brief Appelle l'outil @p name avec ses arguments. Outil inconnu : erreur `outil_inconnu`. */
QJsonObject call(const std::shared_ptr<TimelineItemModel> &timeline, const QString &name, const QJsonObject &args);

/** @brief Les outils déjà écrits dans l'application. */
QStringList implementedTools();

/** @brief Un temps donné en images (entier) ou en timecode « HH:MM:SS:II » ; -1 si illisible. */
int parseTime(const QJsonValue &value, double fps);

/** @brief Un nombre d'images en timecode « HH:MM:SS:II » (sans compensation de 29,97). */
QString timecode(int frames, double fps);

/** @brief Le fichier d'un média a été renommé ou déplacé sur le disque : le clip du chutier et les
 *  producteurs de piste de ses clips dans les séquences ouvertes prennent le nouveau chemin, sans
 *  rechargement. Sert aux outils d'organisation et à la porte (fichiers.renommer, fichiers.deplacer). */
int relocateMedia(const QString &binId, const QString &from, const QString &to);

/** @brief Le miroir Motion derrière les outils `motion_*` (document 19, lot 3). Le miroir s'inscrit
 * lui-même à sa construction ; sans miroir, ces outils répondent `non_disponible`. */
void setMotionMirror(KoraCutMotionMirror *mirror);
KoraCutMotionMirror *motionMirror();

} // namespace KoraCutAgentTools
