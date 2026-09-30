/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

import QtQuick
import QtQuick.Controls
import org.kde.kdenlive as K

// KoraCut, document 20 : les repères posés depuis les règles, des lignes bleues fines dans l'image.
// On les glisse pour les déplacer, on les ramène sur la règle pour les retirer, double-clic pour
// taper la position en pixels. Cet item remplit l'image (frame).
Item {
    id: guidesItem
    required property K.MonitorProxy controller
    required property double scalex
    required property double scaley
    // L'item des règles et leur épaisseur, pour reconnaître un repère ramené sur une règle.
    property Item rulersItem: null
    property int rulerSize: 18
    visible: controller.guidesVisible
    readonly property color accent: "#0D99FF"
    readonly property string fontFamily: K.UiUtils.fixedFont.family

    Repeater {
        model: guidesItem.controller.guides
        delegate: Item {
            id: guide
            required property var modelData
            required property int index
            readonly property bool vertical: modelData.vertical
            readonly property double pos: modelData.pos
            // Pendant un glisser, la ligne suit la souris sans toucher au modèle (le Repeater
            // reconstruirait la ligne sous le doigt). Le modèle est mis à jour au relâcher.
            property double dragPos: pos
            property bool dragging: false
            readonly property double shown: dragging ? dragPos : pos
            x: vertical ? Math.round(shown * guidesItem.scalex) : 0
            y: vertical ? 0 : Math.round(shown * guidesItem.scaley)
            width: vertical ? 1 : guidesItem.width
            height: vertical ? guidesItem.height : 1
            Rectangle {
                anchors.fill: parent
                color: guidesItem.accent
                opacity: guide.dragging ? 1 : 0.85
            }
            MouseArea {
                id: guideArea
                enabled: !guidesItem.controller.guidesLocked
                x: guide.vertical ? -3 : 0
                y: guide.vertical ? 0 : -3
                width: guide.vertical ? 7 : guide.width
                height: guide.vertical ? guide.height : 7
                hoverEnabled: true
                cursorShape: guide.vertical ? Qt.SplitHCursor : Qt.SplitVCursor
                onPressed: mouse => {
                    guide.dragPos = guide.pos
                    guide.dragging = true
                }
                onPositionChanged: mouse => {
                    if (!pressed) {
                        return
                    }
                    var p = mapToItem(guidesItem, mouse.x, mouse.y)
                    guide.dragPos = Math.round(guide.vertical ? p.x / guidesItem.scalex : p.y / guidesItem.scaley)
                }
                onReleased: mouse => {
                    guide.dragging = false
                    if (guidesItem.rulersItem && guidesItem.rulersItem.visible) {
                        var r = mapToItem(guidesItem.rulersItem, mouse.x, mouse.y)
                        if ((guide.vertical && r.x < guidesItem.rulerSize) || (!guide.vertical && r.y < guidesItem.rulerSize)) {
                            guidesItem.controller.removeGuide(guide.index)
                            return
                        }
                    }
                    if (guide.dragPos !== guide.pos) {
                        guidesItem.controller.moveGuide(guide.index, guide.dragPos)
                    }
                }
                onDoubleClicked: {
                    editor.guideIndex = guide.index
                    editor.text = String(Math.round(guide.pos))
                    editor.x = guide.vertical ? Math.min(guide.x + 6, guidesItem.width - editor.width) : 6
                    editor.y = guide.vertical ? 6 : Math.min(guide.y + 6, guidesItem.height - editor.height)
                    editor.visible = true
                    editor.selectAll()
                    editor.forceActiveFocus()
                }
            }
            // La position, pendant le survol et le glisser.
            Rectangle {
                visible: guideArea.containsMouse || guide.dragging
                x: guide.vertical ? 4 : Math.max(4, Math.min(guideArea.mouseX - width / 2, guide.width - width - 4))
                y: guide.vertical ? Math.max(4, Math.min(guideArea.mouseY - height / 2, guide.height - height - 4)) : 4
                width: posLabel.implicitWidth + 8
                height: posLabel.implicitHeight + 4
                radius: 3
                color: guidesItem.accent
                Text {
                    id: posLabel
                    anchors.centerIn: parent
                    text: Math.round(guide.shown) + " px"
                    color: "#FFFFFF"
                    font.family: guidesItem.fontFamily
                    font.pixelSize: 10
                }
            }
        }
    }

    // Taper la position d'un repère (double-clic).
    TextField {
        id: editor
        property int guideIndex: -1
        visible: false
        width: 72
        font.family: guidesItem.fontFamily
        font.pixelSize: 11
        validator: IntValidator { bottom: -100000; top: 100000 }
        onAccepted: {
            guidesItem.controller.moveGuide(guideIndex, parseInt(text))
            visible = false
        }
        onActiveFocusChanged: {
            if (!activeFocus) {
                visible = false
            }
        }
        Keys.onEscapePressed: visible = false
    }
}
