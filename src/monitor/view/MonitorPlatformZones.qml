/*
    SPDX-FileCopyrightText: 2026 KoraCut contributors / PIXEWORK
    SPDX-License-Identifier: GPL-3.0-or-later
*/

import QtQuick
import org.kde.kdenlive as K

// KoraCut, document 20 : les zones que l'interface d'une plateforme recouvre (colonne des boutons,
// légende, barre du haut, écran de fin), en gris semi-transparent avec leur étiquette, et la zone
// sûre au milieu. Les mesures viennent de data/koracut-reperes-plateformes.json. Remplit l'image.
Item {
    id: zonesItem
    required property K.MonitorProxy controller
    required property double scalex
    required property double scaley
    // Pour l'écran de fin de YouTube : la zone n'existe que sur les dernières secondes.
    property int position: 0
    property int duration: 0
    visible: controller.platformZones.length > 0

    Repeater {
        model: zonesItem.controller.platformZones
        delegate: Rectangle {
            id: zone
            required property var modelData
            readonly property string kind: modelData.kind
            readonly property bool narrow: height > width * 2
            x: Math.round(modelData.x * zonesItem.scalex)
            y: Math.round(modelData.y * zonesItem.scaley)
            width: Math.round(modelData.w * zonesItem.scalex)
            height: Math.round(modelData.h * zonesItem.scaley)
            visible: modelData.lastFrames === 0 || (zonesItem.duration - zonesItem.position) <= modelData.lastFrames
            color: kind === "outside" ? Qt.rgba(0, 0, 0, 0.7) : kind === "zone" ? Qt.rgba(0.5, 0.5, 0.5, 0.45) : "transparent"
            border.color: kind === "safe" ? Qt.rgba(1, 1, 1, 0.5) : "transparent"
            border.width: kind === "safe" ? 1 : 0
            Rectangle {
                visible: modelData.label !== "" && zone.kind === "zone" && (zone.narrow ? zone.height > 90 : zone.width > 90)
                // Une bande collée en haut de l'image passerait sous la règle : son étiquette va en bas.
                x: zone.narrow ? (zone.width - width) / 2 : 6
                y: zone.narrow ? 6 : (zone.y <= 0 ? zone.height - height - 6 : 6)
                width: zone.narrow ? label.implicitHeight + 6 : Math.min(label.implicitWidth + 10, zone.width - 12)
                height: zone.narrow ? Math.min(label.implicitWidth + 10, zone.height - 12) : label.implicitHeight + 6
                radius: 3
                color: Qt.rgba(0, 0, 0, 0.65)
                clip: true
                Text {
                    id: label
                    text: zone.modelData.label
                    color: "#FFFFFF"
                    font.pixelSize: 10
                    anchors.centerIn: parent
                    rotation: zone.narrow ? -90 : 0
                    width: zone.narrow ? parent.height - 10 : parent.width - 10
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
    // Le nom de la plateforme, en haut à droite de l'image.
    Rectangle {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 6
        width: nameLabel.implicitWidth + 12
        height: nameLabel.implicitHeight + 6
        radius: 3
        color: Qt.rgba(0, 0, 0, 0.65)
        visible: zonesItem.controller.platformName !== ""
        Text {
            id: nameLabel
            anchors.centerIn: parent
            text: zonesItem.controller.platformName
            color: "#FFFFFF"
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }
    }
}
