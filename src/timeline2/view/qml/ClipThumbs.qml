/*
    SPDX-FileCopyrightText: 2018 Jean-Baptiste Mardelle <jb@kdenlive.org>
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

pragma ComponentBehavior: Bound

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQml.Models 2.15

import org.kde.kdenlive as K

// KoraCut, 25 septembre 2026 : des vignettes « comme un film ». Chaque image remplit sa case,
// recadrée au centre plutôt que réduite (plus de bandes noires quand le média n'a pas la forme
// du projet), sans séparateur entre deux cases. Une case est au moins carrée : sur un projet
// vertical, une case de 9:16 ne montrait qu'une tranche illisible.
Item {
    id: thumbRow
    anchors.fill: parent
    visible: !isAudio
    clip: true
    // On Qt 6.10.x, using Clip type creates a dependency loop between Clip and ClipThumbs.
    // When increasing the Qt dependency, we can revert to use :
    // required property Clip parentClip
    required property var parentClip
    property real initialSpeed: 1
    opacity: clipState === K.PlaylistState.Disabled ? 0.2 : 1
    property bool fixedThumbs: parentClip.itemType === K.ClipType.Image || parentClip.itemType === K.ClipType.Text || parentClip.itemType === K.ClipType.TextTemplate
    property int thumbWidth: Math.max(1, Math.round(thumbRow.height * Math.max(K.Core.getCurrentDar(), 1.0)))
    property bool enableCache: parentClip.itemType === K.ClipType.Video || parentClip.itemType === K.ClipType.AV
    // KoraCut, 22 octobre 2026 (KC-011) : en mode « toutes les images », seules les cases visibles existent (plus une de
    // chaque côté), au lieu d'une par case sur toute la largeur du plan, et leurs images sont calées sur une grille qui
    // ne dépend que du zoom posé : un cran de zoom ne recrée plus toutes les cases ni ne redemande toutes les images.
    readonly property bool allFrames: parentTrack.trackThumbsFormat === 1
    readonly property int totalSlots: Math.ceil(thumbRow.width / thumbRow.thumbWidth)
    readonly property bool inOutLayout: !allFrames || totalSlots < 3
    readonly property int firstSlot: Math.max(0, Math.floor(Math.max(0, thumbRow.parentClip.scrollStart) / thumbRow.thumbWidth) - 1)
    readonly property int lastSlot: Math.min(totalSlots - 1, Math.ceil((Math.max(0, thumbRow.parentClip.scrollStart) + scrollView.width) / thumbRow.thumbWidth) + 1)
    // Le pas de la grille : une puissance de deux au moins égale au nombre d'images de média par case, au zoom posé.
    readonly property int frameStep: Math.pow(2, Math.max(0, Math.ceil(Math.log(Math.max(1, thumbRow.thumbWidth / root.koraThumbScale)) / Math.LN2)))

    Repeater {
        id: thumbRepeater
        // switching the model allows one to have different view modes.
        // We set the model to the number of frames we want to show
        // Des « return » plutôt que la valeur du switch : qmllint de Qt 6.12 refuse une expression sans effet.
        model: {
            switch (parentTrack.trackThumbsFormat) {
            case 0:
                // in/out : 2 montre l'image de début et de fin, 1 seulement la première si la place manque
                return thumbRow.width > 2 * thumbRow.thumbWidth ? 2 : 1;
            case 1:
                // All frames : les cases visibles seulement (un plan étroit garde son début et sa fin)
                return thumbRow.inOutLayout ? (thumbRow.width > 2 * thumbRow.thumbWidth ? 2 : 1) : Math.max(0, thumbRow.lastSlot - thumbRow.firstSlot + 1);
            case 2:
                // In frame only
                return 1;
            case 3:
            default:
                // No thumbs
                return 0;
            }
        }
        property int startFrame: thumbRow.parentClip.inPoint
        property int endFrame: thumbRow.parentClip.outPoint
        // Des cases de largeur entière, collées : pas de jour entre deux images.
        property real imageWidth: thumbRow.thumbWidth
        property int thumbStartFrame: thumbRow.fixedThumbs ? 0 :
                                                    (thumbRow.parentClip.speed >= 0)
                                                    ? Math.round(thumbRow.parentClip.inPoint * thumbRow.initialSpeed)
                                                    : Math.round((thumbRow.parentClip.maxDuration - thumbRow.parentClip.inPoint) * -thumbRow.initialSpeed - 1)
        property int thumbEndFrame: thumbRow.fixedThumbs ? 0 :
                                                  (thumbRow.parentClip.speed >= 0)
                                                  ? Math.round(thumbRow.parentClip.outPoint * thumbRow.initialSpeed)
                                                  : Math.round((thumbRow.parentClip.maxDuration - thumbRow.parentClip.outPoint) * -thumbRow.initialSpeed - 1)

        Image {
            id: thumbImage
            required property int index
            // La case de cette image dans le plan : son rang parmi les cases visibles, plus la première visible.
            readonly property int slot: thumbRow.inOutLayout ? index : thumbRow.firstSlot + index
            width: thumbRepeater.imageWidth
            height: thumbRow.height
            // Première et dernière image aux deux bouts ; sinon une case après l'autre.
            x: thumbRow.inOutLayout && index > 0 ? thumbRow.width - width : slot * width
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: thumbRow.enableCache
            property int currentThumbFrame: thumbRow.fixedThumbs
                                       ? 0
                                       : thumbRow.inOutLayout
                                         ? (index === 0 ? thumbRepeater.thumbStartFrame : thumbRepeater.thumbEndFrame)
                                         : Math.max(thumbRepeater.thumbStartFrame, thumbRow.frameStep * Math.floor(Math.floor(clipRoot.inPoint * thumbRow.initialSpeed + Math.round(slot * width / root.koraThumbScale) * clipRoot.speed) / thumbRow.frameStep))
            source: thumbRow.parentClip.baseThumbPath + currentThumbFrame
            onStatusChanged: {
                if (status === Image.Ready && thumbRow.inOutLayout && (index === 0  || index === thumbRepeater.count - 1)) {
                    thumbPlaceholder.source = source
                }
            }
            Image {
                // L'image d'avant reste affichée pendant que la nouvelle se charge.
                id: thumbPlaceholder
                visible: parent.status != Image.Ready && thumbRow.inOutLayout && (thumbImage.index === 0  || thumbImage.index === thumbRepeater.count - 1)
                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
        }
    }
}
