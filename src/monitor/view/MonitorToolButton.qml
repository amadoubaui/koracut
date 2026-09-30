/*
    SPDX-FileCopyrightText: 2022 Julius Künzel <julius.kuenzel@kde.org>
    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

import QtQuick
import QtQuick.Controls


ToolButton {
    id: toolButton
    property alias iconName: toolButton.icon.name
    property string toolTipText
    width: parent.width
    height: width
    // KoraCut : taille par défaut de la charte pour les commandes des moniteurs
    icon.width: 16
    icon.height: 16
    focusPolicy: Qt.NoFocus
    ToolTip {
        visible: toolButton.hovered
        delay: Application.styleHints.mousePressAndHoldInterval
        text: toolButton.toolTipText
        timeout: 3000
    }
}
