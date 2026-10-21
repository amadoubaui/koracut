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
                // All frames : autant d'images que la place en tient
                return Math.ceil(thumbRow.width / thumbRow.thumbWidth);
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
            width: thumbRepeater.imageWidth
            height: thumbRow.height
            // Première et dernière image aux deux bouts ; sinon une case après l'autre.
            x: thumbRepeater.count < 3 && index > 0 ? thumbRow.width - width : index * width
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: thumbRow.enableCache
            property int currentThumbFrame: thumbRow.fixedThumbs
                                       ? 0
                                       : thumbRepeater.count < 3
                                         ? (index === 0 ? thumbRepeater.thumbStartFrame : thumbRepeater.thumbEndFrame)
                                         : Math.floor(clipRoot.inPoint * thumbRow.initialSpeed + Math.round((index) * width / timeline.scaleFactor)* clipRoot.speed)
            source: thumbRepeater.count < 3
                    ? (thumbRow.parentClip.baseThumbPath + currentThumbFrame)
                    : (index * width < thumbRow.parentClip.scrollStart - width || index * width > thumbRow.parentClip.scrollStart + scrollView.width)
                      ? '' : thumbRow.parentClip.baseThumbPath + currentThumbFrame
            onStatusChanged: {
                if (status === Image.Ready && (index === 0  || index === thumbRepeater.count - 1)) {
                    thumbPlaceholder.source = source
                }
            }
            Image {
                // L'image d'avant reste affichée pendant que la nouvelle se charge.
                id: thumbPlaceholder
                visible: parent.status != Image.Ready && (thumbImage.index === 0  || thumbImage.index === thumbRepeater.count - 1)
                anchors.fill: parent
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
        }
    }
}
