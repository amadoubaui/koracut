/*
SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QSet>
#include <QString>
#include <functional>
#include <memory>

class DocUndoStack;
class KActionCollection;
class KoraCutPorte;
class MainWindow;
class TimelineItemModel;

/** @brief Ce dont les méthodes de la porte ont besoin. Dans l'application tout vient de la fenêtre
 *  et de pCore ; dans les tests, la timeline, la pile d'annulation, le dossier du projet et les
 *  actions sont fournis à la main, et les méthodes qui exigent la fenêtre répondent `non_disponible`. */
struct KoraCutPorteContext
{
    MainWindow *window{nullptr};
    KActionCollection *actions{nullptr};
    std::function<std::shared_ptr<TimelineItemModel>()> timeline;
    std::function<std::shared_ptr<DocUndoStack>()> undoStack;
    /** @brief Le dossier du projet (celui du fichier .koracut) ; vide tant qu'il n'est pas enregistré. */
    std::function<QString()> projectFolder;
};

/** @brief Le catalogue v1 de `Agents in-app/PORTE.md`, méthode par méthode, branché sur la porte.
 *
 * Familles : app, actions, ui, projet, medias, fichiers, timeline, export, historique, evenements.
 * Chaque méthode qui touche le projet passe par les fonctions annulables de Kdenlive : une commande
 * = une étape d'annulation, et `historique.tour` en fait une seule pour tout un client.
 */
namespace KoraCutPorteMethodes {

void registerAll(KoraCutPorte *porte, const KoraCutPorteContext &context);

/** @brief Les outils d'agents qui demandent l'accord de la personne (`x_confirmation` dans
 *  Agents in-app/outils, fichiers json, quand la ressource est là, sinon la courte liste de repli). */
QSet<QString> confirmationTools();

/** @brief Vrai si @p path est dans @p folder (ou @p folder lui-même), liens symboliques résolus. */
bool isInsideFolder(const QString &folder, const QString &path);

} // namespace KoraCutPorteMethodes
