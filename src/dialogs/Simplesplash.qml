/*
    SPDX-FileCopyrightText: 2025 Jean-Baptiste Mardelle <jb@kdenlive.org>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import QtQuick.Layouts

import org.kde.ki18n

Window {
    id: splash
    visible: true
    color: "transparent"
    title: "Splash Screen"
    SystemPalette { id: activePalette }
    modality: Qt.WindowModal
    flags: crashRecovery ? Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint : Qt.SplashScreen | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    property int timeoutInterval: 5000
    property string version
    property bool crashRecovery: false
    property bool wasUpgraded: false
    signal timeout
    x: (Screen.width - splashContent.width) / 2
    y: (Screen.height - splashContent.height) / 2
    width: splashContent.width
    height: splashContent.height
    signal openLink(string url)
    signal openBlank()
    signal resetConfig()

    property int border: 10

    function fade()
    {
        fadeAnimation.start()
    }

    function activate()
    {
        // Hack to ensure the qml Window stays on top of our QWidget MainWindow while loading
        splash.requestActivate()
        if (splash.crashRecovery) {
            normalStartButton.forceActiveFocus();
        } else if (splash.wasUpgraded) {
            notesStartButton.forceActiveFocus();
        } else {
            splashContent.forceActiveFocus()
        }
    }

    function displayProgress(message)
    {
        loadingLabel.text = message
        loadingBox.visible = message.length > 0
    }

    Rectangle {
        id: splashContent
        height: splash.crashRecovery || splash.wasUpgraded ? kdenliveid.height * 15 : kdenliveid.height * 13
        width: height * 2
        radius: 8
        border.width: 2
        border.color: "#2B1AD6" /* KoraCut: Violet Kora, seul moment de marque dans l'application */
        color: activePalette.window
        clip: true
        NumberAnimation on opacity {
            id: fadeAnimation
            running: false
            duration: 600
            easing.type: Easing.InCubic
            from: 1
            to: 0
        }
        onActiveFocusChanged: {
            if (!activeFocus) {
                splash.activate()
            }
        }
        Keys.onEscapePressed: {
            console.log('ESC PRESSED!!!')
            if (splash.wasUpgraded) {
                notesStartButton.animateClick()
            } else if (splash.crashRecovery) {
                normalStartButton.animateClick()
            }
        }

        Item {
            id: header
            anchors.fill: parent
            Image {
                id: background
                anchors.fill: parent
                anchors.margins: 5
                source: "qrc:/pics/splash-background.webp"
                verticalAlignment: Image.AlignTop
                fillMode: Image.PreserveAspectCrop
            }
            Rectangle { // rounded corners for image
                anchors.fill: parent
                anchors.margins: 2
                color: "transparent"
                border.color: splashContent.color
                border.width: 4
                radius: 8
            }
        }
        Rectangle {
            id: buttonBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 2
            height: 1.7 * kdenliveid.height
            color: splashContent.color
            opacity: 0.5
        }
        Label {
            // KoraCut : le bas de l'écran de démarrage, d'après la charte.
            anchors.left: buttonBar.left
            anchors.leftMargin: 10
            anchors.verticalCenter: buttonBar.verticalCenter
            opacity: 0.62
            text: KI18n.i18nc("@info écran de démarrage", "Free software · GPL-3.0 · koracut.org")
        }
        Label {
            id: kdenliveid
            anchors.right: buttonBar.right
            anchors.rightMargin: 10
            anchors.verticalCenter: buttonBar.verticalCenter
            text: KI18n.i18n("KoraCut") + " " + splash.version
        }
        MouseArea {
            anchors.fill: parent
            enabled: !splash.crashRecovery && !splash.wasUpgraded
            acceptedButtons: Qt.NoButton
            onClicked: {
                splashContent.visible = false
            }
        }
        Rectangle {
            id: loadingBox
            visible: false
            color: activePalette.window
            opacity: 0.85
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: splashContent.border.width + 5
            anchors.rightMargin: splashContent.border.width + 5
            anchors.bottom: buttonBar.top
            height: Math.max(loadingLabel.height, restartButton.height + 15)
            Label {
                id: loadingLabel
                anchors.fill: parent
                anchors.margins: 10
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
            }
            // KoraCut, 23 septembre 2026 : barre de chargement Violet Kora de 3 px (charte, écran de
            // démarrage). Kdenlive ne donne que des messages, pas d'avancement : la barre est donc
            // indéterminée, un segment qui va et vient pendant que le message s'affiche.
            Rectangle {
                id: koracutProgress
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 3
                color: "transparent"
                clip: true
                Rectangle {
                    id: koracutProgressSegment
                    width: parent.width * 0.3
                    height: parent.height
                    color: "#2B1AD6"
                    SequentialAnimation on x {
                        running: koracutProgress.visible
                        loops: Animation.Infinite
                        NumberAnimation { from: -koracutProgressSegment.width; to: koracutProgress.width; duration: 1400; easing.type: Easing.InOutQuad }
                    }
                }
            }
        }
        Rectangle {
            // Crash recovery
            id: resetBox
            visible: splash.crashRecovery
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: buttonBar.top
            anchors.leftMargin: splashContent.border.width
            anchors.rightMargin: splashContent.border.width
            height: restartButton.height + 15
            color: activePalette.window
            Rectangle {
                anchors.fill: parent
                color: "#11FF0000"
            }
            Label {
                id: restartLabel
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: restartButton.left
                text: KI18n.i18n("KoraCut crashed on last start.")
                Layout.alignment: Qt.AlignVCenter
                wrapMode: Text.Wrap
            }
            Button {
                id: restartButton
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: normalStartButton.left
                anchors.rightMargin: 10
                text: KI18n.i18n("Reset Configuration")
                icon.name: "view-refresh"
                property bool buttonPressed: false
                onClicked: {
                    loadingLabel.text = KI18n.i18n("Starting…")
                    resetBox.visible = false
                    loadingBox.visible = true
                    console.log('--------resetting config--.------')
                    splash.resetConfig()
                }
                Keys.onPressed: (event)=> {
                    if (event.key === Qt.Key_Return) {
                        buttonPressed = true
                    }
                }
                Keys.onReleased: {
                    if (buttonPressed) {
                        animateClick()
                    }
                    buttonPressed = false
                }
            }
            Button {
                id: normalStartButton
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 10
                property bool buttonPressed: false
                text: KI18n.i18n("Start Normally")
                icon.name: "go-next"
                focus: true
                onClicked: {
                    console.log('--------normal start--.------')
                    resetBox.visible = false
                    loadingBox.visible = true
                    loadingLabel.text = KI18n.i18n("Starting…")
                    splash.openBlank()
                }
                Keys.onPressed: (event)=> {
                    if (event.key === Qt.Key_Return) {
                        buttonPressed = true
                    }
                }
                Keys.onReleased: {
                    if (buttonPressed) {
                        animateClick()
                    }
                    buttonPressed = false
                }
            }
        }
        Rectangle {
            // Upgrade notice
            id: upgradeBox
            visible: splash.wasUpgraded && !splash.crashRecovery
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: splashContent.border.width + 5
            anchors.rightMargin: splashContent.border.width + 5
            anchors.bottom: buttonBar.top
            height: Math.max(upgradedLabel.height, notesButton.height) + 15
            color: activePalette.window
            Rectangle {
                anchors.fill: parent
                color: "#2200FF00"
            }
            Label {
                id: upgradedLabel
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.right: notesButton.left
                textFormat: Text.RichText
                text: KI18n.i18n("KoraCut has just been updated.")
                wrapMode: Text.Wrap
                HoverHandler {
                    enabled: upgradedLabel.hoveredLink
                    cursorShape: Qt.PointingHandCursor
                }
                onLinkActivated: (link) => splash.openLink(link)
            }
            Button {
                id: notesButton
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: notesStartButton.left
                anchors.rightMargin: 10
                property bool buttonPressed: false
                text: KI18n.i18n("What's New")
                icon.name: "help-contents"
                onClicked: splash.openLink("https://koracut.org/blog")
                Keys.onPressed: (event)=> {
                    if (event.key === Qt.Key_Return) {
                        buttonPressed = true
                    }
                }
                Keys.onReleased: {
                    if (buttonPressed) {
                        animateClick()
                    }
                    buttonPressed = false
                }

            }
            Button {
                id: notesStartButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.rightMargin: 10
                property bool buttonPressed: false
                text: KI18n.i18n("Continue")
                icon.name: "go-next"
                onClicked: {
                    loadingLabel.text = KI18n.i18n("Starting…")
                    upgradeBox.visible = false
                    loadingBox.visible = true
                    splash.openBlank()
                }
                Keys.onPressed: (event)=> {
                    if (event.key === Qt.Key_Return) {
                        buttonPressed = true
                    }
                }
                Keys.onReleased: {
                    if (buttonPressed) {
                        animateClick()
                    }
                    buttonPressed = false
                }
            }
        }
    }
    Component.onCompleted: {
        visible = true;
        if (splash.crashRecovery) {
            normalStartButton.forceActiveFocus();
        } else if (splash.wasUpgraded) {
            notesStartButton.forceActiveFocus();
        } else {
            splashContent.forceActiveFocus()
        }
    }
}
