/*
    SPDX-FileCopyrightText: 2016 Jean-Baptiste Mardelle <jb@kdenlive.org>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "definitions.h"

#include <QDir>
#include <QFuture>
#include <QMutex>
#include <QProcess>
#include <QSet>
#include <QTimer>
#include <QUuid>

class TimelineController;

namespace Mlt {
class Tractor;
class Playlist;
class Producer;
} // namespace Mlt

/** @class PreviewManager
    @brief Handles timeline preview.
    This manager creates an additional video track on top of the current timeline and renders
    chunks (small video files of 25 frames) that are added on this track when rendered.
    This allow us to get a preview with a smooth playback of our project.
    Only the preview zone is rendered. Once defined, a preview zone shows as a red line below
    the timeline ruler. As chunks are rendered, the zone turns to green.
 */
class PreviewManager : public QObject
{
    Q_OBJECT

public:
    friend class TimelineModel;
    friend class TimelineController;

    explicit PreviewManager(Mlt::Tractor *tractor, QUuid uuid, QObject *parent = nullptr);
    ~PreviewManager() override;
    /** @brief: initialize base variables, return false if error. */
    bool initialize();
    /** @brief: after a small  delay (some operations trigger several invalidatePreview calls), take care of these invalidated chunks. */
    void invalidatePreviews();
    /** @brief: user adds current timeline zone to the preview zone. */
    void addPreviewRange(const QPoint zone, bool add);
    /** @brief: Remove all existing previews. */
    void clearPreviewRange(bool resetZones);
    /** @brief: stops current rendering process. */
    void abortRendering();
    /** @brief: rendering parameters have changed, reload them. */
    bool loadParams();
    /** @brief: Create the preview track if not existing. */
    bool buildPreviewTrack();
    /** @brief: Delete the preview track. */
    void deletePreviewTrack();
    /** @brief: Whenever we save or render our project, we remove the preview track so it is not saved. */
    void reconnectTrack();
    /** @brief: After project save or render, re-add our preview track. */
    void disconnectTrack();
    /** @brief: Returns directory currently used to store the preview files. */
    const QDir getCacheDir() const;
    /** @brief: Load existing ruler chunks. */
    void loadChunks(QVariantList previewChunks, QVariantList dirtyChunks, Mlt::Playlist &playlist);
    int setOverlayTrack(Mlt::Playlist *overlay);
    /** @brief Remove the effect compare overlay track */
    void removeOverlayTrack();
    /** @brief The current preview chunk being processed, -1 if none */
    int workingPreview;
    /** @brief Returns the list of existing chunks */
    QPair<QStringList, QStringList> previewChunks();
    bool hasOverlayTrack() const;
    bool hasPreviewTrack() const;
    int addedTracks() const;
    /** @brief Returns true if a preview render range has already been defined */
    bool hasDefinedRange() const;
    /** @brief Returns true if the render process is still running */
    bool isRunning() const;

    // KoraCut, rendu d'aperçu (24 septembre 2026, chantier 10)
    /** @brief Les morceaux rendus et ceux qui restent à rendre (copies). */
    QVariantList renderedList() const;
    QVariantList dirtyList() const;
    /** @brief Avancement du rendu en cours, en millièmes. */
    int progressPermille() const;
    /** @brief Vrai si un rendu tourne ou va partir (minuterie armée). */
    bool isPending() const;
    /** @brief Taille des fichiers rendus de cette séquence, et plafond du cache du projet. */
    qint64 cacheBytes() const;
    qint64 cacheCapBytes() const;
    /** @brief Les paramètres d'encodage du rendu, tels que koracut_render les reçoit. */
    QString consumerParameters() const;
    /** @brief Rendu automatique : les morceaux qui portent des effets, de l'étalonnage, des
     * compositions ou du Motion. Ceux qui manquent sont mis en file, ceux qui n'en portent plus
     * sont retirés (et leur fichier effacé), puis le rendu part s'il ne tourne pas. */
    void setAutoChunks(const QList<int> &heavyChunks);
    /** @brief « Effacer les rendus » : tous les fichiers de la séquence partent, zones comprises.
     * Le rendu automatique ne les refait qu'au prochain changement. */
    void clearRenderFiles(int duration);
    /** @brief KoraCut : tous les morceaux rendus sont faux (rendus pendant que le décodage matériel
     * échouait). Leurs fichiers partent, annulation comprise, et ils sont à refaire. Sans étape d'annulation. */
    void koraCutDiscardAll();

private:
    Mlt::Tractor *m_tractor;
    QUuid m_uuid;
    Mlt::Playlist *m_previewTrack;
    Mlt::Playlist *m_overlayTrack;
    bool m_warnOnCrash;
    int m_previewTrackIndex;
    /** @brief: The kdenlive timeline preview process. */
    QProcess m_previewProcess;
    /** @brief: The directory used to store the preview files. */
    QDir m_cacheDir;
    /** @brief: The directory used to store undo history of preview files (child of m_cacheDir). */
    QDir m_undoDir;
    QMutex m_previewMutex;
    QStringList m_consumerParams;
    QString m_extension;
    /** @brief: Timer used to autostart preview rendering. */
    QTimer m_previewTimer;
    /** @brief: Since some timeline operations generate several invalidate calls, use a timer to get them all. */
    QTimer m_previewGatherTimer;
    bool m_initialized;
    QList<int> m_waitingThumbs;
    QFuture<void> m_previewThread;
    /** @brief: The count of chunks to process - to calculate job progress */
    int m_chunksToRender;
    /** @brief: The count of already processed chunks - to calculate job progress */
    int m_processedChunks;
    /** @brief: The render process output, useful in case of failure */
    QString m_errorLog;
    /** @brief: After an undo/redo, if we have preview history, use it. */
    void reloadChunks(const QVariantList &chunks);
    /** @brief: A chunk failed to render, abort. */
    void corruptedChunk(int workingPreview, const QString &fileName);
    /** @brief: Get a compressed list of chunks, like: "0-500,525,575". */
    const QStringList getCompressedList(const QVariantList items) const;

    // KoraCut : morceaux mis en file par le rendu automatique, et morceaux écartés (cache plein,
    // « Effacer les rendus ») que le rendu automatique ne remet pas en file avant un changement.
    QSet<int> m_autoChunks;
    QSet<int> m_evictedChunks;
    /** @brief Les morceaux demandés au dernier lancement de koracut_render. */
    QSet<int> m_lastRequested;
    /** @brief Octets du cache du projet, estimés au lancement puis tenus à jour morceau par morceau. */
    qint64 m_cacheEstimate{0};
    bool m_capWarned{false};
    /** @brief Le rendu se met en pause pendant la lecture et pendant un geste (souris enfoncée), puis reprend. */
    QTimer m_playbackWatch;
    bool m_suspended{false};
    /** @brief Vrai si la personne lit ou fait un geste : le rendu attend. */
    static bool userIsBusy();
    /** @brief Range un fichier rendu dans l'historique d'annulation, ou l'efface (réglage apercuEtapesAnnulation). */
    bool archiveChunkFile(const QString &fileName, int undoIndex);
    /** @brief Fait de la place avant un rendu : les plus vieux fichiers du projet partent d'abord. */
    void makeRoomFor(int chunkCount);
    /** @brief Choisit le profil de rendu KoraCut (H.264 tout-intra) pour un document qui n'en a pas. */
    bool selectKoraCutPreviewProfile();
    /** @brief Ordonne les morceaux à rendre depuis la tête de lecture. */
    QStringList orderedChunkList();

    /** @brief Compare two chunks for usage by std::sort
     * @returns true if @param c1 is less than @param c2
     */
    static bool chunkSort(const QVariant &c1, const QVariant &c2) { return c1.toInt() < c2.toInt(); };

private Q_SLOTS:
    /** @brief KoraCut : départ du rendu après la minuterie, sauf si la souris est enfoncée (un geste
     * en cours) ou, sur machine modeste, si la lecture tourne : on attend encore. */
    void slotAutoStart();
    /** @brief: To avoid filling the hard drive, remove preview undo history after 5 steps. */
    void doCleanupOldPreviews();
    /** @brief: Start the real rendering process. */
    void doPreviewRender(const QString &scene); // std::shared_ptr<Mlt::Producer> sourceProd);
    /** @brief: If user does an undo, then makes a new timeline operation, delete undo history of more recent stack . */
    void slotRemoveInvalidUndo(int ix);
    /** @brief: When the timer collecting invalid zones is done, process. */
    void slotProcessDirtyChunks();
    /** @brief: Process preview rendering output. */
    void receivedStderr();
    void processEnded(int exitCode, QProcess::ExitStatus status);

public Q_SLOTS:
    /** @brief: Prepare and start rendering. */
    void startPreviewRender();
    /** @brief: A chunk has been created, notify ruler. */
    void gotPreviewRender(int frame, const QString &file, int progress);
    /** @brief: a timeline operation caused changes to frames between startFrame and endFrame. */
    void invalidatePreview(int startFrame, int endFrame);

protected:
    QVariantList m_renderedChunks;
    QVariantList m_dirtyChunks;
    QList<int> m_dirtyChunksToRemove;
    mutable QMutex m_dirtyMutex;
    /** @brief: Re-enable timeline preview track. */
    void enable();
    /** @brief: Temporarily disable timeline preview track. */
    void disable();

Q_SIGNALS:
    void abortPreview();
    void cleanupOldPreviews();
    void previewRender(int frame, const QString &file, int progress);
    void dirtyChunksChanged();
    void renderedChunksChanged();
    void workingPreviewChanged();
};
