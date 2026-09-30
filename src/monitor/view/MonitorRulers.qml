/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

import QtQuick
import org.kde.kdenlive as K

// KoraCut, document 20 : deux règles graduées en pixels du projet, en haut et à gauche du
// moniteur (⇧R). Glisser depuis une règle pose un repère ; les graduations suivent le zoom.
Item {
    id: rulers
    required property K.MonitorProxy controller
    // L'image (frame) dans le repère de cet item, et son échelle en pixels d'écran par pixel du projet.
    required property Item frameItem
    required property double scalex
    required property double scaley
    property int size: 18
    visible: controller.rulersVisible

    readonly property color bg: Qt.rgba(0.118, 0.118, 0.118, 0.94)
    readonly property color tickColor: "#6E6E6E"
    readonly property color textColor: "#A0A0A0"
    readonly property color accent: "#0D99FF"
    readonly property string fontFamily: K.UiUtils.fixedFont.family

    property double step: controller.rulerStep(scalex)
    // Un repère en cours de pose : sa position en pixels d'écran dans ce repère.
    property bool placing: false
    property bool placingVertical: false
    property double placingPos: 0

    function origin() {
        return frameItem.mapToItem(rulers, 0, 0)
    }
    function repaint() {
        hRuler.requestPaint()
        vRuler.requestPaint()
    }
    onScalexChanged: repaint()
    onScaleyChanged: repaint()
    onWidthChanged: repaint()
    onHeightChanged: repaint()
    onVisibleChanged: repaint()
    Connections {
        target: rulers.frameItem
        function onXChanged() { rulers.repaint() }
        function onYChanged() { rulers.repaint() }
        function onWidthChanged() { rulers.repaint() }
        function onHeightChanged() { rulers.repaint() }
    }

    // Graduations : un chiffre tous les « step » pixels du projet, cinq petits traits entre deux.
    function paintAxis(ctx, horizontal, length, thickness) {
        ctx.clearRect(0, 0, horizontal ? length : thickness, horizontal ? thickness : length)
        ctx.fillStyle = rulers.bg
        ctx.fillRect(0, 0, horizontal ? length : thickness, horizontal ? thickness : length)
        var o = rulers.origin()
        var scale = horizontal ? rulers.scalex : rulers.scaley
        var start = (horizontal ? o.x : o.y) - rulers.size
        var extent = horizontal ? rulers.frameItem.width : rulers.frameItem.height
        // L'étendue de l'image, un peu plus claire.
        ctx.fillStyle = Qt.rgba(1, 1, 1, 0.05)
        if (horizontal) {
            ctx.fillRect(start, 0, extent, thickness)
        } else {
            ctx.fillRect(0, start, thickness, extent)
        }
        var s = rulers.step
        var minor = s / 5
        var first = Math.floor(-start / scale / minor) * minor
        var last = (length - start) / scale
        ctx.strokeStyle = rulers.tickColor
        ctx.fillStyle = rulers.textColor
        ctx.lineWidth = 1
        ctx.font = "9px \"" + rulers.fontFamily + "\""
        ctx.textBaseline = "alphabetic"
        for (var v = first; v <= last; v += minor) {
            var px = Math.round(v / minor) * minor
            var screen = Math.round(start + px * scale) + 0.5
            var major = Math.abs(px / s - Math.round(px / s)) < 0.001
            var len = major ? thickness : (Math.abs(px / (s / 2) - Math.round(px / (s / 2))) < 0.001 ? thickness * 0.5 : thickness * 0.3)
            ctx.beginPath()
            if (horizontal) {
                ctx.moveTo(screen, thickness)
                ctx.lineTo(screen, thickness - len)
            } else {
                ctx.moveTo(thickness, screen)
                ctx.lineTo(thickness - len, screen)
            }
            ctx.stroke()
            if (major) {
                var label = String(Math.round(px))
                if (horizontal) {
                    ctx.fillText(label, screen + 3, 9)
                } else {
                    ctx.save()
                    ctx.translate(9, screen - 3)
                    ctx.rotate(-Math.PI / 2)
                    ctx.fillText(label, 0, 0)
                    ctx.restore()
                }
            }
        }
    }

    // Règle du haut.
    Canvas {
        id: hRuler
        x: rulers.size
        y: 0
        width: Math.max(0, rulers.width - rulers.size)
        height: rulers.size
        onPaint: {
            var ctx = getContext("2d")
            rulers.paintAxis(ctx, true, width, height)
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SplitVCursor
            onPressed: mouse => {
                if (rulers.controller.guidesLocked) {
                    return
                }
                rulers.placing = true
                rulers.placingVertical = false
                rulers.placingPos = mapToItem(rulers, mouse.x, mouse.y).y
            }
            onPositionChanged: mouse => {
                if (rulers.placing) {
                    rulers.placingPos = mapToItem(rulers, mouse.x, mouse.y).y
                }
            }
            onReleased: mouse => {
                if (!rulers.placing) {
                    return
                }
                rulers.placing = false
                var p = mapToItem(rulers.frameItem, mouse.x, mouse.y)
                if (mapToItem(rulers, mouse.x, mouse.y).y > rulers.size) {
                    rulers.controller.addGuide(false, p.y / rulers.scaley)
                }
            }
        }
    }
    // Règle de gauche.
    Canvas {
        id: vRuler
        x: 0
        y: rulers.size
        width: rulers.size
        height: Math.max(0, rulers.height - rulers.size)
        onPaint: {
            var ctx = getContext("2d")
            rulers.paintAxis(ctx, false, height, width)
        }
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SplitHCursor
            onPressed: mouse => {
                if (rulers.controller.guidesLocked) {
                    return
                }
                rulers.placing = true
                rulers.placingVertical = true
                rulers.placingPos = mapToItem(rulers, mouse.x, mouse.y).x
            }
            onPositionChanged: mouse => {
                if (rulers.placing) {
                    rulers.placingPos = mapToItem(rulers, mouse.x, mouse.y).x
                }
            }
            onReleased: mouse => {
                if (!rulers.placing) {
                    return
                }
                rulers.placing = false
                var p = mapToItem(rulers.frameItem, mouse.x, mouse.y)
                if (mapToItem(rulers, mouse.x, mouse.y).x > rulers.size) {
                    rulers.controller.addGuide(true, p.x / rulers.scalex)
                }
            }
        }
    }
    // Le coin, et le repère en cours de pose.
    Rectangle {
        x: 0
        y: 0
        width: rulers.size
        height: rulers.size
        color: rulers.bg
    }
    Rectangle {
        visible: rulers.placing
        color: rulers.accent
        x: rulers.placingVertical ? Math.round(rulers.placingPos) : rulers.size
        y: rulers.placingVertical ? rulers.size : Math.round(rulers.placingPos)
        width: rulers.placingVertical ? 1 : rulers.width - rulers.size
        height: rulers.placingVertical ? rulers.height - rulers.size : 1
    }
}
