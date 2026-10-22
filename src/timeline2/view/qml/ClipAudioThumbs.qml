/*
    SPDX-FileCopyrightText: 2018 Jean-Baptiste Mardelle <jb@kdenlive.org>
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

pragma ComponentBehavior: Bound

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQml.Models 2.15

import org.kde.kdenlive as K

Row {
    id: waveform
    required property var parentClip
    required property color audioColor
    required property real timeScale

    readonly property int maxWidth: 2048
    property int totalChunks: 0
    property bool usesOffset: false
    property int offset: -1

    opacity: parentClip.clipState === K.PlaylistState.Disabled ? 0.2 : 1
    anchors.fill: parent

    // KoraCut, 22 octobre 2026 (KC-011) : pendant un zoom, la forme d'onde déjà peinte est étirée (une
    // transformation, rien à repeindre) ; elle n'est repeinte qu'au zoom posé, 150 ms après le dernier cran
    // (root.koraThumbScale). Avant, chaque cran repeignait chaque forme d'onde visible, en logiciel.
    readonly property real paintScale: root.koraThumbScale > 0 ? root.koraThumbScale : waveform.timeScale
    readonly property real stretch: waveform.timeScale / waveform.paintScale
    readonly property real paintWidth: waveform.width / waveform.stretch
    transform: Scale { xScale: waveform.stretch }

    Timer {
        id: waveTimer
        interval: 50; running: false; repeat: false
        onTriggered: waveform.processReload()
    }

    onHeightChanged: {
        waveTimer.start()
    }

    function reload(reset) {
        if (reset === 0) {
            waveform.offset = 0
            waveform.totalChunks = 0
            waveformRepeater.model = 0
        }
        waveTimer.start()
    }

    onPaintScaleChanged: {
        processReload()
    }

    function processReload() {
        // This is needed to make the model have the correct count.
        // Model as a property expression is not working in all cases.
        if (!waveform.visible || !K.KdenliveSettings.audiothumbnails) {
            return
        }
        var total = Math.ceil(waveform.paintWidth / waveform.maxWidth)
        var chunks = total
        var updatedOffset = 0
        if (chunks > 10) {
            // Having too many chunks causes major slowdowns. In this case, we use an offset and only allow up to 10 chunks
            waveform.usesOffset = true
            updatedOffset = Math.max(0, Math.floor(waveform.parentClip.scrollStart / waveform.stretch / waveform.maxWidth - 2))
            if (updatedOffset < waveform.offset || updatedOffset > (waveform.offset + 5) || total != waveform.totalChunks) {
                // Enforce repaint
                //waveformRepeater.model = 0
            } else {
                // All required audio thumbs chunks are already painted
                return
            }
            chunks = Math.min(10, total - updatedOffset)
        } else {
            waveform.usesOffset = false
            updatedOffset = 0
        }
        waveform.offset = updatedOffset
        waveform.totalChunks = total
        if (waveformRepeater.model === undefined || chunks !== waveformRepeater.model.length) {
            waveformRepeater.model = chunks
        }
    }
    Item {
        // Placeholder item for the offset
        width: waveform.usesOffset ? waveform.offset * waveform.maxWidth : 0
        height: waveform.height
    }

    Repeater {
        id: waveformRepeater
        K.TimelineWaveform {
            required property int index
            width: waveform.maxWidth < waveform.paintWidth ? (index + waveform.offset == waveform.totalChunks - 1 ? waveform.paintWidth % waveform.maxWidth : waveform.maxWidth) : Math.round(waveform.paintWidth)
            height: waveform.height
            channels: waveform.parentClip.audioChannels
            binId: waveform.parentClip.binId
            audioStream: waveform.parentClip.audioStream
            isOpaque: true
            scaleFactor: waveform.paintScale
            format: K.KdenliveSettings.displayallchannels
            normalize: K.KdenliveSettings.normalizechannels
            speed: waveform.parentClip.speed
            property int aWaveInPoint: Math.round((waveform.parentClip.inPoint + ((index + waveform.offset) * waveform.maxWidth / scaleFactor)) * Math.abs(waveform.parentClip.speed))
            waveInPoint: aWaveInPoint
            waveOutPoint: aWaveInPoint + Math.round(width / scaleFactor * Math.abs(waveform.parentClip.speed))
            bgColorEven: waveform.parentClip.selected ? waveform.audioColor : waveform.audioColor.darker(1.5)
            bgColorOdd: waveform.parentClip.selected ? waveform.audioColor : waveform.audioColor.darker(1.5)
            // KoraCut : le tracé passe au blanc à 55 %, sur le fond de la piste. Il était vert sur
            // vert (`thumbColor1` et `thumbColor2`, deux réglages hérités) : une couleur de plus,
            // qui n'est dans aucune charte, pour dire la même chose que la couleur du clip dit déjà.
            fgColorEven: Qt.rgba(1, 1, 1, waveform.parentClip.selected ? 0.55 : 0.42)
            fgColorOdd: Qt.rgba(1, 1, 1, waveform.parentClip.selected ? 0.55 : 0.42)
            drawChannelNames: false //(index + waveform.offset) == 0
        }
    }
}
