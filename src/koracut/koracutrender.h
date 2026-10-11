/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QToolButton>
#include <functional>

class QMenu;

/** @brief Le rendu d'aperçu de la timeline, côté KoraCut (chantier 10, 24 septembre 2026).
 *
 * Le moteur est celui de Kdenlive (PreviewManager : des morceaux de 25 images rendus par
 * koracut_render sur une piste cachée). Ce fichier ajoute ce qui manquait pour que la
 * personne s'en serve sans le chercher : les réglages par défaut selon la machine, le plafond
 * du cache, le nettoyage des vieux projets, et la mesure de la lecture.
 *
 * Réglages dans koracutrc, groupe [KoraCut] :
 * - apercuAutoInitialise : vrai une fois le défaut du rendu automatique posé
 * - apercuPlafondMo : plafond du cache de rendu d'un projet, 0 = automatique (5 Go ou 10 % du libre)
 * - apercuEtapesAnnulation : nombre d'étapes d'annulation dont on garde les rendus, 0 par défaut
 * - apercuNettoyageJours : âge au-delà duquel les rendus d'un projet non ouvert partent, 30 par défaut
 */
namespace KoraCutRender {

/** @brief Vrai si la machine est classée modeste (MachineProfile), mesuré une fois. */
bool isModestMachine();
/** @brief Premier lancement : rendu automatique actif sur les machines moyennes et confortables,
 * proposé (message dans le chutier) sur les machines modestes. */
void applyFirstLaunchDefaults();
/** @brief Plafond du cache de rendu d'un projet, en octets, pour le dossier @p previewRoot. */
qint64 cacheCapBytes(const QString &previewRoot, qint64 currentSize);
/** @brief Taille des morceaux rendus sous @p dir (sous-dossiers compris). */
qint64 folderSize(const QString &dir);
/** @brief Étapes d'annulation dont on garde les rendus (0 : un rendu invalidé part tout de suite). */
int undoStepsKept();
/** @brief Efface, en tâche de fond et au plus une fois par jour, les rendus des projets du cache
 * système non ouverts depuis apercuNettoyageJours. Les proxys et les vignettes ne sont pas touchés. */
void cleanupOldProjects(const QString &currentDocumentId);
/** @brief Un clip du projet ne se décode pas avec la puce (document 6, journal du 1er octobre 2026).
 * S'il était déjà dans le projet (@p legacy), ses vignettes et les rendus d'aperçu ont peut-être été
 * faits pendant que le décodage échouait : ils sont effacés une fois, puis refaits en logiciel.
 * Le dossier de cache du projet garde la liste des fichiers déjà soignés. */
void healAfterHardwareRefusal(const QString &path, bool legacy);
/** @brief Pose le témoin « projet ouvert » dans le dossier de rendu d'un projet. */
void touchOpenedMarker(const QString &previewRoot);
/** Le nom de ce témoin. Son point de tête le cache sous macOS et Linux, pas sous Windows : qui liste le
 *  dossier du rendu d'aperçu doit donc l'écarter par son nom. */
QString openedMarkerName();

/** @brief Lit la séquence dans le moniteur Programme pendant @p seconds secondes depuis @p from
 * et compte les images affichées. Rend { images_affichees, images_attendues, images_sautees,
 * retard, ips } à @p done. */
void measurePlayback(int from, int seconds, const std::function<void(const QJsonObject &)> &done);
/** @brief Mesure le temps de réponse aux gestes (2 octobre 2026, document 6) : sauts de la tête de lecture,
 * départ de la lecture, rognage et déplacement d'un clip, réglage d'étalonnage. Chaque geste est chronométré
 * jusqu'à l'image affichée qui le montre. Modifie la séquence (rogne puis remet) : banc d'essai seulement.
 * @p dragOnly > 0 : seulement le glisser de la tête de lecture, ce nombre de fois (une réponse par passe). */
void measureGestures(const std::function<void(const QJsonObject &)> &done, int dragOnly = 0);

/** @brief Attend (en faisant tourner la boucle d'événements) que le rendu d'aperçu de la
 * séquence courante soit fini, au plus @p timeoutMs. Vrai s'il est fini. */
bool waitForRender(int timeoutMs);

/** @brief L'état du rendu de la séquence courante, pour les outils et la porte. */
QJsonObject stateJson();

} // namespace KoraCutRender

/** @class KoraCutRenderButton
 * @brief Le bouton « Rendu » de la barre d'outils de la timeline : un clic rend la zone (ou
 * arrête le rendu en cours), la flèche ouvre le menu. Le libellé dit l'état : « Rendu »,
 * « Rendu 42 % », « Rendu à jour ». */
class KoraCutRenderButton : public QToolButton
{
    Q_OBJECT
public:
    explicit KoraCutRenderButton(QWidget *parent = nullptr);
    /** @brief L'action du clic quand rien ne tourne, et celle qui arrête le rendu. */
    void setActions(QAction *render, QAction *stop);

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void refresh();
    QTimer m_poll;
    QPointer<QAction> m_render;
    QPointer<QAction> m_stop;
    int m_state{-1};
    int m_percent{-1};
};
