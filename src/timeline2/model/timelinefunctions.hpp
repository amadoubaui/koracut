
/*
SPDX-FileCopyrightText: 2017 Jean-Baptiste Mardelle <jb@kdenlive.org>
This file is part of Kdenlive. See www.kdenlive.org.

SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include "definitions.h"
#include "undohelper.hpp"
#include <memory>
#include <unordered_set>
#include <unordered_map>

#include <QDir>

class TimelineItemModel;

/** @namespace TimelineFunction
    @brief This namespace contains a list of static methods for advanced timeline editing features
    based on timelinemodel methods
 */
struct TimelineFunctions
{
    /** @brief Cuts a clip at given position
       If the clip is part of the group, all clips of the groups are cut at the same position. The group structure is then preserved for clips on both sides
       Returns true on success
       @param timeline : ptr to the timeline model
       @param clipId: Id of the clip to split
       @param position: position (in frames from the beginning of the timeline) where to cut
    */

    struct TimelineTracksInfo {
        QList<int> audioIds;
        QList<int> videoIds;
    };

    static bool requestClipCut(std::shared_ptr<TimelineItemModel> timeline, int clipId, int position);
    /** @brief This is the same function, except that it accumulates undo/redo */
    static bool requestClipCut(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, int position, Fun &undo, Fun &redo);
    /** @brief This is the same function, except that it accumulates undo/redo and do not deal with groups. Do not call directly */
    static bool processClipCut(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, int position, int &newId, Fun &undo, Fun &redo);

    /** @brief Cuts all clips at given position */
    static bool requestClipCutAll(std::shared_ptr<TimelineItemModel> timeline, int position);

    /** @brief Makes a perfect clone of a given clip, but do not insert it */
    static bool cloneClip(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, int &newId, PlaylistState::ClipState state, int audioStream,
                          Fun &undo, Fun &redo);

    /** @brief Creates a string representation of the given clips, that can then be pasted using pasteClips(). Return an empty string on failure */
    static QString copyClips(const std::shared_ptr<TimelineItemModel> &timeline, const std::unordered_set<int> &itemIds, int mainClip = -1);

    /** @brief Paste the clips as described by the string. Returns true on success*/
    static bool pasteClips(const std::shared_ptr<TimelineItemModel> &timeline, const QString &pasteString, int trackId, int position,
                           QString historyText = QString(), bool select = false);
    static bool pasteClips(const std::shared_ptr<TimelineItemModel> &timeline, const QString &pasteString, int trackId, int position, Fun &undo, Fun &redo,
                           int inPos = 0, int duration = -1, bool select = false);
    static bool pasteClipsWithUndo(const std::shared_ptr<TimelineItemModel> &timeline, const QString &pasteString, int trackId, int position, Fun &undo,
                                   Fun &redo);
    static bool pasteTimelineClips(const std::shared_ptr<TimelineItemModel> &timeline, const QDomDocument &copiedItems, int position, int inPos = 0,
                                   int duration = -1);
    static bool pasteTimelineClips(const std::shared_ptr<TimelineItemModel> &timeline, QDomDocument copiedItems, int position, Fun &timeline_undo,
                                   Fun &timeline_redo, bool pushToStack, int inPos = 0, int duration = -1, bool select = false);

    /** @brief Request the addition of multiple clips to the timeline
     * If the addition of any of the clips fails, the entire operation is undone.
     * @returns true on success, false otherwise.
     * @param binIds the list of bin ids to be inserted
     * @param trackId the track where the insertion should happen
     * @param position the position at which the clips should be inserted
     * @param clipIds a return parameter with the ids assigned to the clips if success, empty otherwise
     */
    static bool requestMultipleClipsInsertion(const std::shared_ptr<TimelineItemModel> &timeline, const QStringList &binIds, int trackId, int position,
                                              QList<int> &clipIds, bool logUndo, bool refreshView);

    /** @brief KoraCut : crée une piste du type du média (un son : audio ; le reste : vidéo) et y pose le clip,
     * en une seule étape d'annulation. La piste audio va tout en bas ; la piste vidéo en bas des pistes
     * vidéo, juste au-dessus de l'audio (document 13, § 10.3).
     * @param newTrack reçoit l'identifiant de la piste créée
     * @return l'identifiant du clip, -1 en cas d'échec (rien n'est alors changé). */
    /** @brief KoraCut : retire une liste de zones sur toutes les pistes non verrouillées à la fois, de la
     * dernière à la première, en refermant les trous (ou en les laissant si @p liftOnly). Tout va dans
     * @p undo / @p redo : l'appelant pousse une seule étape d'annulation. Coupe automatique,
     * « 15 - Coupe automatique (type AutoCut).md ».
     * @param zones intervalles [début, fin[ en images, triés et disjoints
     * @param moveGuides les guides suivent : ceux d'une zone retirée disparaissent, ceux d'après reculent.
     *        Choix explicite, indépendant du réglage « verrouiller les guides » (vrai par défaut), fait
     *        pour le déplacement d'espace : après une coupe automatique, un guide resté en place
     *        désignerait un autre moment du podcast.
     * @return faux si une zone n'a pas pu être retirée ; l'annulation accumulée est alors déjà rejouée. */
    static bool removeZonesOnAllTracks(const std::shared_ptr<TimelineItemModel> &timeline, const QList<QPair<int, int>> &zones, bool liftOnly,
                                       bool moveGuides, Fun &undo, Fun &redo);

    /** @brief KoraCut : déplace des clips dans le temps, chacun de son décalage (en images), avec leur
     * groupe (la vidéo d'une caméra suit son son). Dans une macro d'annulation. Les clips qui reculent
     * sont déplacés du premier au dernier, ceux qui avancent du dernier au premier, pour qu'ils ne se
     * heurtent pas en chemin. Synchronisation de la coupe automatique, document 15, lot 4.
     * @return faux si un déplacement est refusé ; l'annulation accumulée est alors rejouée. */
    static bool moveClipsBy(const std::shared_ptr<TimelineItemModel> &timeline, const QList<QPair<int, int>> &clipDeltas, Fun &undo, Fun &redo);

    /** @brief KoraCut : monte les caméras d'un podcast (document 15, lot 5), sans rien supprimer. Les
     * pistes vidéo des caméras sont coupées à chaque changement de plan, puis chaque morceau est activé
     * s'il est la caméra choisie pour ce plan, désactivé sinon. On réactive un plan à la main pour le
     * changer. Dans une macro d'annulation.
     * @param shots les plans : [début, fin[ en images, et l'intervenant montré (-1 : plan large)
     * @param cameraForSpeaker piste vidéo de la caméra de chaque intervenant, dans l'ordre
     * @param wideTrack piste vidéo du plan large, -1 s'il n'y en a pas */
    static bool applyCameraPlan(const std::shared_ptr<TimelineItemModel> &timeline, const QList<QPair<QPair<int, int>, int>> &shots,
                                const QList<int> &cameraForSpeaker, int wideTrack, Fun &undo, Fun &redo);

    static int requestClipOnNewTrack(const std::shared_ptr<TimelineItemModel> &timeline, const QString &data, int position, int &newTrack);

    /** @brief This function will find the blank located in the given track at the given position and remove it
        @returns true on success, false otherwise
        @param trackId id of the track to search in
        @param position of the blank
        @param affectAllTracks if true, the same blank will be removed from all tracks. Note that all the tracks must have a blank at least that big in that
       position
    */
    static bool requestDeleteBlankAt(const std::shared_ptr<TimelineItemModel> &timeline, int trackId, int position, bool affectAllTracks);

    /** @brief This function will delete all blanks on the given track after the given position
        @returns true on success, false otherwise
        @param trackId id of the track to search in
        @param position of the blank
    */
    static bool requestDeleteAllBlanksFrom(const std::shared_ptr<TimelineItemModel> &timeline, int trackId, int position);

    /** @brief Adds guide markers at the start of every gap between clips on video tracks
        @returns true if at least one gap marker was added
        @param timeline TimelineItemModel to scan for gaps
    */
    static bool addMarkersAtGaps(const std::shared_ptr<TimelineItemModel> &timeline);

    /** @brief Adds guide markers at the start of every gap between clips on a specific track
        @returns true if at least one gap marker was added
        @param timeline TimelineItemModel to scan for gaps
        @param trackId Id of the track to scan for gaps
    */
    static bool addMarkersAtGapsOnTrack(const std::shared_ptr<TimelineItemModel> &timeline, int trackId);

    /** @brief This function will delete all clips on the given track after the given position
        @returns true on success, false otherwise
        @param trackId id of the track to search in
        @param position start position for the operation
    */
    static bool requestDeleteAllClipsFrom(const std::shared_ptr<TimelineItemModel> &timeline, int trackId, int position);

    /** @brief Starts a spacer operation. Should be used together with requestSpacerEndOperation
        @returns {clipId, {max blank duration before, max_blank duration after}} of the position-wise first clip in the temporary group
        @param timeline TimelineItemModel where the operation should be performed on
        @param trackId
        @param position
        @param ignoreMultiTrackGroups
        @param allowGroupBreaking Whether independent move of grouped items is allowed
        @see requestSpacerEndOperation
    */
    static std::pair<int, int> requestSpacerStartOperation(const std::shared_ptr<TimelineItemModel> &timeline, int trackId, int position,
                                                           bool ignoreMultiTrackGroups = false, bool allowGroupBreaking = false);
    /**
        @see requestSpacerStartOperation
        @param moveGuidesPosition if > -1, all guides after this position will be moved as well in the operation
     */
    static bool requestSpacerEndOperation(const std::shared_ptr<TimelineItemModel> &timeline, int itemId, int startPosition, int endPosition, int affectedTrack,
                                          int moveGuidesPosition, Fun &undo, Fun &redo, bool pushUndo = true);
    static bool extractZone(const std::shared_ptr<TimelineItemModel> &timeline, const QVector<int> &tracks, QPoint zone, bool liftOnly, int clipToUnGroup = -1,
                            std::unordered_set<int> clipsToRegroup = {});
    static bool extractZoneWithUndo(const std::shared_ptr<TimelineItemModel> &timeline, const QVector<int> &tracks, QPoint zone, bool liftOnly,
                                    int clipToUnGroup, std::unordered_set<int> clipsToRegroup, Fun &undo, Fun &redo);
    static bool liftZone(const std::shared_ptr<TimelineItemModel> &timeline, int trackId, QPoint zone, Fun &undo, Fun &redo);
    static bool removeSpace(const std::shared_ptr<TimelineItemModel> &timeline, QPoint zone, Fun &undo, Fun &redo,
                            const QVector<int> &allowedTracks = QVector<int>(), bool useTargets = true);

    /** @brief This function will insert a blank space starting at zone.x, and ending at zone.y. This will affect all the tracks
        @returns true on success, false otherwise
    */
    static bool requestInsertSpace(const std::shared_ptr<TimelineItemModel> &timeline, QPoint zone, Fun &undo, Fun &redo,
                                   const QVector<int> &allowedTracks = QVector<int>());
    static bool insertZone(const std::shared_ptr<TimelineItemModel> &timeline, const QList<int> &trackIds, const QString &binId, int insertFrame, QPoint zone,
                           bool overwrite, bool useTargets = true);
    static bool insertZone(const std::shared_ptr<TimelineItemModel> &timeline, QList<int> trackIds, const QString &binId, int insertFrame, QPoint zone,
                           bool overwrite, bool useTargets, Fun &undo, Fun &redo);

    static bool requestItemCopy(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, int trackId, int position);
    static void showClipKeyframes(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, bool value);
    static void showCompositionKeyframes(const std::shared_ptr<TimelineItemModel> &timeline, int compoId, bool value);

    /** @brief If the clip is activated, disable, otherwise enable
     * @param timeline: pointer to the timeline that we modify
     * @param clipId: Id of the clip to modify
     * @param status: target status of the clip
     This function creates an undo object and returns true on success
     */
    static bool switchEnableState(const std::shared_ptr<TimelineItemModel> &timeline, std::unordered_set<int> selection);
    /** @brief change the clip state and accumulates for undo/redo
     */
    static bool changeClipState(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, PlaylistState::ClipState status, Fun &undo, Fun &redo);

    static bool requestSplitAudio(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, QList<int> targetTracks);
    static bool requestSplitVideo(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, int videoTarget);
    static void setCompositionATrack(const std::shared_ptr<TimelineItemModel> &timeline, int cid, int aTrack);
    static bool setCompositionATrack(const std::shared_ptr<TimelineItemModel> &timeline, int cid, int aTrack, Fun &undo, Fun &redo, bool pushUndo);
    static QStringList enableMultitrackView(const std::shared_ptr<TimelineItemModel> &timeline, bool enable, bool refresh);
    static void saveTimelineSelection(const std::shared_ptr<TimelineItemModel> &timeline, const std::unordered_set<int> &selection, const QDir &targetDir,
                                      int duration);
    /** @brief returns the number of same type tracks between 2 tracks
     */
    static int getTrackOffset(const std::shared_ptr<TimelineItemModel> &timeline, int startTrack, int destTrack);
    /** @brief returns an offset track id
     */
    static int getOffsetTrackId(const std::shared_ptr<TimelineItemModel> &timeline, int startTrack, int offset, bool audioOffset);
    /** @brief returns a list of ids for all audio tracks and all video tracks
     */
    static TimelineTracksInfo getAVTracksIds(const std::shared_ptr<TimelineItemModel> &timeline);

    /** @brief This function breaks group is an item in the zone is grouped with an item outside of selected tracks
     */
    static bool breakAffectedGroups(const std::shared_ptr<TimelineItemModel> &timeline, const QVector<int> &tracks, QPoint zone, Fun &undo, Fun &redo);

    /** @brief This function extracts the content of an xml playlist file and converts it to json paste format
     */
    static QDomDocument extractClip(const std::shared_ptr<TimelineItemModel> &timeline, int cid, const QString &binId);

    static int spacerMinPos();
    static int spacerMaxPos();

    /** @brief KoraCut : la couleur propre d'un clip de la timeline (« #RRGGBB »), vide s'il suit sa piste. */
    static QString koraCutClipColor(const std::shared_ptr<TimelineItemModel> &timeline, int clipId);
    /** @brief KoraCut : pose la même couleur sur ces clips (vide : ils reprennent celle de leur piste),
     *  dans undo/redo. Faux si un identifiant n'est pas un clip. */
    static bool koraCutSetClipsColor(const std::shared_ptr<TimelineItemModel> &timeline, const QList<int> &clipIds, const QString &color, Fun &undo, Fun &redo);
    /** @brief KoraCut : pose la couleur d'une piste (vide : par défaut), dans undo/redo. */
    static bool koraCutSetTrackColor(const std::shared_ptr<TimelineItemModel> &timeline, int trackId, const QString &color, Fun &undo, Fun &redo);
    /** @brief KoraCut : le nom propre d'un clip de la timeline (vide : il porte le nom de son média),
     *  dans undo/redo. Faux si l'identifiant n'est pas un clip. */
    static bool koraCutSetClipName(const std::shared_ptr<TimelineItemModel> &timeline, int clipId, const QString &name, Fun &undo, Fun &redo);
    /** @brief KoraCut : le fichier d'un média a été renommé ou déplacé sur le disque. Les producteurs de
     *  piste de ses clips (ceux que le projet enregistre) prennent le nouveau chemin, sans rechargement.
     *  Rend le nombre de producteurs mis à jour. */
    static int koraCutRelocateMedia(const std::shared_ptr<TimelineItemModel> &timeline, const QString &binId, const QString &from, const QString &to);

private:
    static bool getUsedTracks(const QDomNodeList &clips, const QDomNodeList &compositions, int masterSourceTrack, int &topAudioMirror, TimelineTracksInfo &allTracks, QList<int> &singleAudioTracks, std::unordered_map<int, int> &audioMirrors);
};
