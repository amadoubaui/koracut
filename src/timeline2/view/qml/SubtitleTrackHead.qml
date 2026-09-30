/*
    SPDX-FileCopyrightText: 2017-2021 Jean-Baptiste Mardelle <jb@kdenlive.org>
    SPDX-FileCopyrightText: 2021 Julius Künzel <julius.kuenzel@kde.org>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

pragma ComponentBehavior: Bound

import QtQuick 2.15
import QtQuick.Controls 2.15

import org.kde.ki18n

import org.kde.kdenlive as K


Rectangle {
    id: trackHeader
    border.color: frameColor
    border.width: 1

    required property K.TimelineController timeline
    required property K.TimelineItemModel controller
    required property int collapsedHeight
    required property bool collapsed
    required property bool isDisabled
    required property bool isLocked

    SystemPalette { id: activePalette }

    function animateLock() {
        flashLock.restart();
    }

    visible: height > 0
    color: (controller && controller.isSubtitleTrack(timeline.activeTrack)) ? Qt.tint(getTrackColor(false, false), selectedTrackColor) : getTrackColor(false, false)
    MouseArea {
        anchors.fill: parent
        onClicked: {
            trackHeader.timeline.activeTrack = -2
        }
    }
    ToolButton {
        id: expandSubButton
        focusPolicy: Qt.NoFocus
        anchors.left: parent.left
        anchors.leftMargin: 1.5 * K.UiUtils.baseSizeMedium
        width: trackHeader.collapsedHeight
        height: trackHeader.collapsedHeight
        icon.name: trackHeader.collapsed ? "go-next" : "go-down"
        // KoraCut : taille de la charte, sinon le style prend PM_SmallIconSize et les icônes grossissent
        icon.width: 14
        icon.height: 14
        onClicked: {
            if (subtitleTrack.height > trackHeader.collapsedHeight) {
                subtitleTrack.height = trackHeader.collapsedHeight
            } else {
                subtitleTrack.height = K.UiUtils.baseSizeMedium * 2.5 * ((maxSubLayer == 0)? 2: (maxSubLayer + 1))
            }
        }
    }
    ComboBox {
        id: subLabel
        model: trackHeader.timeline.subtitlesList
        property int subIndex: trackHeader.timeline.activeSubPosition
        onSubIndexChanged: {
            subLabel.currentIndex = subIndex
        }
        // KoraCut, 25 septembre 2026 : le nom de la piste sur la première ligne, comme les autres
        // pistes. Il était sur la deuxième, et seulement piste dépliée : repliée, la piste de
        // sous-titres n'avait aucun nom, dépliée elle avait une ligne de boutons sans nom.
        anchors.right: subButtonsRow.left
        anchors.rightMargin: 4
        anchors.top: parent.top
        height: trackHeader.collapsedHeight
        anchors.left: subtitleLayerIndicator.right
        // Même cran que le nom des autres pistes, et moins de marge : « Sous-titres » tient.
        font: K.UiUtils.smallestReadableFont
        leftPadding: 4
        visible: subtitleTrack.visible
        flat: true
        onActivated: index => {
            trackHeader.timeline.subtitlesMenuActivatedAsync(index)
        }
    }

    Row {
        id: subButtonsRow
        width: childrenRect.width
        x: Math.max(2 * trackHeader.collapsedHeight + 2, parent.width - width - 4)
        spacing: 0
        ToolButton {
            id: warningButton
            visible: subtitlesWarning
            focusPolicy: Qt.NoFocus
            icon.name: "data-warning"
            // KoraCut : taille de la charte, sinon le style prend PM_SmallIconSize et les icônes grossissent
            icon.width: 14
            icon.height: 14
            width: trackHeader.collapsedHeight
            height: trackHeader.collapsedHeight
            onClicked: trackHeader.timeline.subtitlesWarningDetails()
            ToolTip {
                visible: warningButton.hovered
                font: K.UiUtils.smallestReadableFont
                delay: 1500
                timeout: 5000
                background: Rectangle {
                    color: activePalette.alternateBase
                    border.color: activePalette.light
                }
                contentItem: Label {
                    color: activePalette.text
                    text: KI18n.i18n("Click to see details")
                }
            }
        }
        ToolButton {
            id: analyseButton
            // KoraCut, 25 septembre 2026 : en-tête étroit, le nom de la piste passe avant ce
            // raccourci (la reconnaissance vocale reste dans le menu Séquence).
            visible: trackHeader.width >= 9 * trackHeader.collapsedHeight
            focusPolicy: Qt.NoFocus
            icon.name: "text-speak"
            // KoraCut : taille de la charte, sinon le style prend PM_SmallIconSize et les icônes grossissent
            icon.width: 14
            icon.height: 14
            width: trackHeader.collapsedHeight
            height: trackHeader.collapsedHeight
            onClicked: K.Core.triggerAction('audio_recognition')
            ToolTip.visible: hovered
            ToolTip.delay: 1500
            ToolTip.timeout: 5000
            ToolTip.text: KI18n.i18n("Speech recognition")
        }
        ToolButton {
            id: muteButton
            focusPolicy: Qt.NoFocus
            icon.name: trackHeader.isDisabled ? "view-hidden" : "view-visible"
            // KoraCut : taille de la charte, sinon le style prend PM_SmallIconSize et les icônes grossissent
            icon.width: 14
            icon.height: 14
            width: trackHeader.collapsedHeight
            height: trackHeader.collapsedHeight
            onClicked: K.Core.triggerAction('disable_subtitle')
            ToolTip.visible: hovered
            ToolTip.delay: 1500
            ToolTip.timeout: 5000
            ToolTip.text: trackHeader.isDisabled ? KI18n.i18n("Show") : KI18n.i18n("Hide")
        }

        ToolButton {
            id: lockButton
            width: trackHeader.collapsedHeight
            height: trackHeader.collapsedHeight
            focusPolicy: Qt.NoFocus
            icon.name: trackHeader.isLocked ? "lock" : "unlock"
            // KoraCut : taille de la charte, sinon le style prend PM_SmallIconSize et les icônes grossissent
            icon.width: 14
            icon.height: 14
            onClicked: K.Core.triggerAction('lock_subtitle')
            ToolTip.visible: hovered
            ToolTip.delay: 1500
            ToolTip.timeout: 5000
            ToolTip.text: trackHeader.isLocked ? KI18n.i18n("Unlock track") : KI18n.i18n("Lock track")
            SequentialAnimation {
                id: flashLock
                loops: 3
                ParallelAnimation {
                    ScaleAnimator {target: lockButton; from: 1; to: 1.2; duration: 120}
                }
                ParallelAnimation {
                    ScaleAnimator {target: lockButton; from: 1.6; to: 1; duration: 120}
                }
            }
        }
    }
    Column {
        id: subtitleLayerIndicator
        width: root.trackTagWidth
        height: trackHeader.height
        anchors.left: expandSubButton.right
        anchors.top: trackHeader.top
        anchors.bottom: trackHeader.bottom
        property color bgColor: Qt.darker(getTrackColor(false, true), 0.55)
        visible: maxSubLayer > 0 && subtitleTrack.visible && subtitleTrack.height !== trackHeader.collapsedHeight

        Repeater {
            model: subtitleLayerIndicator.visible ? maxSubLayer + 1 : 0
            id: subLayerRepeater
            delegate: Rectangle {
                id: layerLabel
                required property int index
                height: trackHeader.height / subLayerRepeater.count
                width: subtitleLayerIndicator.width
                color: subtitleLayerIndicator.bgColor
                border.color: root.frameColor
                Text {
                    id: name
                    font: K.UiUtils.smallestReadableFont
                    text: "S" + layerLabel.index
                    color: activePalette.text
                    anchors.centerIn: layerLabel
                }
            }
        }
    }
}
