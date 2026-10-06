/*
    SPDX-FileCopyrightText: 2017 Nicolas Carion
    SPDX-FileCopyrightText: 2025 Jean-Baptiste Mardelle <jb@kdenlive.org>

    SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import org.kde.ki18n

Window {
    id: splash
    property string version
    property var urls: []
    property var fileNames: []
    property var fileDates: []
    property var profileIds: []
    property var profileNames: []
    property bool crashRecovery: false
    property bool actionsEnabled: false
    property bool wasUpgraded: false
    property int border: 10

    signal timeout()
    signal openBlank()
    signal openOtherFile()
    signal closeApp()
    signal openFile(string url)
    signal openLink(string url)
    signal openTemplate(string templateId)
    signal showWelcome(bool show)
    signal resetConfig()
    signal clearHistory()
    signal clearProfiles()
    signal forgetFile(string url)
    signal forgetProfile(string id)

    visible: true
    color: "transparent"
    modality: Qt.WindowModal
    flags: Qt.FramelessWindowHint
    x: (Screen.width - width) / 2
    y: (Screen.height - height) / 2
    minimumWidth: splashContent.implicitWidth
    minimumHeight: splashContent.implicitHeight
    width: splashContent.implicitWidth
    height: splashContent.implicitHeight

    onClosing: function(close) {
      close.accepted = true
      splash.closeApp()
    }

    function fade()
    {
        fadeAnimation.start()
    }

    function enableActions()
    {
        actionsEnabled = true
    }

    function displayProgress(message)
    {
        loadingLabel.text = message
        loadingBox.visible = message.length > 0
        recentProjects.enabled = false
    }

    Component.onCompleted: {
        if (urls.length === 0) {
            tlistView.forceActiveFocus();
        } else {
            listView.forceActiveFocus();
        }
        visible = true;
    }

    SystemPalette {
        id: activePalette
    }

    Rectangle {
        id: splashContent
        anchors.fill: parent
        implicitHeight: splash.crashRecovery || splash.wasUpgraded  ? newProjectButton.height * 20 : newProjectButton.height * 18
        implicitWidth: newProjectButton.height * 17
        radius: 5
        border.width: 2
        border.color: "#2B1AD6" /* KoraCut: Violet Kora, seul moment de marque dans l'application */
        color: activePalette.window
        clip: true
        focus: true
        NumberAnimation on opacity {
            id: fadeAnimation
            running: false
            duration: 1000
            easing.type: Easing.OutCubic
            from: 1
            to: 0
        }
        Keys.enabled: splash.actionsEnabled
        Keys.onDownPressed: {
            if (listView.activeFocus) {
                if (listView.currentIndex < splash.urls.length)
                    listView.currentIndex += 1;

            } else if (tlistView.activeFocus) {
                if (tlistView.currentIndex < splash.urls.length)
                    tlistView.currentIndex += 1;

            }
        }
        Keys.onUpPressed: {
            if (listView.activeFocus) {
                if (listView.currentIndex > 0)
                    listView.currentIndex -= 1;

            } else if (tlistView.activeFocus) {
                if (tlistView.currentIndex > 0)
                    tlistView.currentIndex -= 1;

            }
        }
        Keys.onReturnPressed: {
            if (listView.activeFocus) {
                if (listView.currentIndex >= 0 && listView.currentIndex < splash.urls.length) {
                    splashContent.enabled = false
                    splash.fade()
                    splash.openFile(splash.urls[listView.currentIndex]);
                }

            } else if (tlistView.activeFocus) {
                if (tlistView.currentIndex >= 0 && tlistView.currentIndex < splash.profileIds.length)
                    splash.openTemplate(splash.profileIds[tlistView.currentIndex]);
            }
        }
        Keys.onEnterPressed: {
            if (listView.activeFocus) {
                if (listView.currentIndex >= 0 && listView.currentIndex < splash.urls.length) {
                    splashContent.enabled = false
                    splash.fade()
                    splash.openFile(splash.urls[listView.currentIndex]);
                }

            } else if (tlistView.activeFocus) {
                if (tlistView.currentIndex >= 0 && tlistView.currentIndex < splash.profileIds.length)
                    splash.openTemplate(splash.profileIds[tlistView.currentIndex]);

            }
        }
        Keys.onEscapePressed: {
            splash.openBlank()
        }

        Shortcut {
            sequences: [StandardKey.Quit]
            context: Qt.ApplicationShortcut
            onActivated: splash.closeApp()
        }

        Shortcut {
            sequences: [StandardKey.Open]
            context: Qt.ApplicationShortcut
            onActivated: splash.openOtherFile()
        }

        Item {
            id: header
            height: splashContent.height / 3
            anchors.left: parent.left
            anchors.right: parent.right

            Image {
                id: background
                anchors.fill: parent
                anchors.margins: 5
                anchors.bottomMargin: 10
                source: "qrc:/pics/splash-background.webp"
                verticalAlignment: Image.AlignTop
                fillMode: Image.PreserveAspectCrop
            }
            // rounded corners for image

            Rectangle {
                anchors.fill: parent
                anchors.margins: 2
                color: "transparent"
                border.color: splashContent.color
                border.width: 4
                radius: 5
            }

            Rectangle {
                anchors.bottom: background.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 5
                anchors.rightMargin: 5
                height: 5
                color: splashContent.color
            }

        }

        Item {
            id: recentProjects
            anchors.top: header.bottom
            anchors.left: splashContent.left
            anchors.leftMargin: splash.border
            anchors.right: splashContent.right
            anchors.rightMargin: splash.border
            anchors.bottom: splashContent.bottom
            RowLayout {
                id: recentListContainer
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                height: childrenRect.height
                spacing: 20

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height

                    Button {
                        id: openButton

                        anchors.top: parent.top
                        anchors.left: parent.left
                        icon.name: "document-open"
                        text: KI18n.i18n("Open Project…")
                        onClicked: splash.openOtherFile()
                        KeyNavigation.tab: newProjectButton
                    }

                    Item {
                        //Layout.fillHeight: true
                        anchors.top: openButton.bottom
                        anchors.topMargin: 10
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: childrenRect.height
                        visible: splash.urls.length > 0

                        Rectangle {
                            id: recentSeparator
                            anchors.top: parent.top
                            anchors.margins: 10
                            height: recentLabel.height + 6
                            width: parent.width
                            radius: height / 4
                            color: Qt.darker(splashContent.color, 1.5)

                            Label {
                                id: recentLabel
                                anchors.verticalCenter: parent.verticalCenter
                                leftPadding: 10
                                text: KI18n.i18n("Open Recent")
                            }
                            ToolButton {
                                id: clearHistoryButton
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                icon.name: "edit-clear-history"
                                hoverEnabled: true
                                ToolTip.text: KI18n.i18n("Clear History of Recent Projects")
                                ToolTip.delay: 1000
                                ToolTip.visible: hovered
                                KeyNavigation.tab: templatesClearButton
                                onClicked: {
                                    splash.clearHistory()
                                    splash.urls = []
                                    splash.fileNames = []
                                    splash.fileDates = []
                                    listView.model = splash.urls
                                }
                            }

                        }

                        ScrollView {
                            id: recentListView
                            anchors.top: recentSeparator.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.topMargin: 10
                            height: newProjectButton.height * (splash.crashRecovery || splash.wasUpgraded ? 4 : 5)
                            ScrollBar.vertical.policy: ScrollBar.AsNeeded
                            clip: true

                            ListView {
                                id: listView
                                property int hoveredIndex: -1
                                property int itemHeight: recentLabel.font.pixelSize * 2.2
                                focus: true
                                model: splash.urls
                                KeyNavigation.right: tlistView
                                KeyNavigation.tab: tlistView

                                delegate: Item {
                                    id: fileItem
                                    width: parent.width
                                    height: listView.itemHeight
                                    required property url modelData
                                    required property int index

                                    ToolTip.text: modelData
                                    ToolTip.visible: index == listView.hoveredIndex
                                    ToolTip.delay: Application.styleHints.mousePressAndHoldInterval
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 4
                                        color: activePalette.highlight
                                        visible: fileItem.index == listView.currentIndex && listView.activeFocus
                                        opacity: 0.5
                                    }

                                    Label {
                                        id: listLabel
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.leftMargin: 4
                                        anchors.right: datesLabel.left
                                        leftPadding: 4
                                        text: splash.fileNames[fileItem.index]
                                        elide: Text.ElideRight
                                        color: labelArea.containsMouse || listButton.hovered ? activePalette.highlight : activePalette.text
                                        opacity: recentProjects.enabled ? 1 : 0.5
                                    }
                                    Label {
                                        id: datesLabel
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.right: parent.right
                                        anchors.rightMargin: 4
                                        text: splash.fileDates[fileItem.index]
                                        color: activePalette.text
                                        opacity: recentProjects.enabled ? 0.7 : 0.4
                                    }

                                    MouseArea {
                                        id: labelArea
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        enabled: splash.actionsEnabled
                                        cursorShape: Qt.PointingHandCursor
                                        onPositionChanged: {
                                            if (fileItem.index != listView.hoveredIndex)
                                                listView.hoveredIndex = fileItem.index;
                                        }
                                        onExited: {
                                            listView.hoveredIndex = -1;
                                        }
                                        onClicked: {
                                            splashContent.enabled = false
                                            splash.fade()
                                            splash.openFile(fileItem.modelData);
                                        }
                                    }
                                    Rectangle {
                                        anchors.fill: listButton
                                        radius: 2
                                        color: splashContent.color
                                        visible: listButton.visible
                                    }

                                    ToolButton {
                                        id: listButton
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        height: listLabel.height
                                        width: height
                                        icon.name: "list-remove"
                                        enabled: splash.actionsEnabled
                                        hoverEnabled: true
                                        ToolTip.text: KI18n.i18n("Remove file from list")
                                        ToolTip.delay: 1000
                                        ToolTip.visible: hovered
                                        visible: labelArea.containsMouse || hovered
                                        onClicked: {
                                            splash.forgetFile(fileItem.modelData)
                                            splash.fileNames.splice(fileItem.index, 1)
                                            splash.urls.splice(fileItem.index, 1)
                                            listView.model = splash.urls
                                        }
                                    }

                                }

                            }

                        }

                    }

                }

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height

                    Button {
                        id: newProjectButton
                        text: KI18n.i18n("New Project…")
                        enabled: splash.actionsEnabled
                        icon.name: "document-new"
                        onClicked: splash.openTemplate("")
                        anchors.top: parent.top
                        anchors.left: parent.left
                        KeyNavigation.tab: clearHistoryButton
                    }

                    Item {
                        anchors.top: newProjectButton.bottom
                        anchors.topMargin: 10
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: childrenRect.height

                        Rectangle {
                            id: templateSeparator
                            anchors.top: parent.top
                            anchors.margins: 10
                            height: templatesLabel.height + 6
                            width: parent.width
                            radius: height / 4
                            color: Qt.darker(splashContent.color, 1.5)
                            Label {
                                id: templatesLabel
                                anchors.verticalCenter: parent.verticalCenter
                                leftPadding: 10
                                text: KI18n.i18n("New From Profile")
                            }
                            ToolButton {
                                id: templatesClearButton
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                icon.name: "edit-clear-history"
                                enabled: splash.actionsEnabled
                                hoverEnabled: true
                                ToolTip.text: KI18n.i18n("Clear History of Recent Profiles")
                                ToolTip.delay: 1000
                                ToolTip.visible: hovered
                                KeyNavigation.tab: showBox
                                onClicked: {
                                    splash.clearProfiles()
                                    splash.profileIds = []
                                    splash.profileNames = []
                                    tlistView.model = splash.profileIds
                                }
                            }

                        }

                        ScrollView {
                            anchors.top: templateSeparator.bottom
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: 10
                            height: recentListView.height
                            ScrollBar.vertical.policy: ScrollBar.AsNeeded
                            clip: true

                            ListView {
                                id: tlistView

                                property int hoveredIndex: -1
                                focus: true
                                model: splash.profileIds
                                KeyNavigation.left: listView
                                KeyNavigation.tab: openButton

                                delegate: Item {
                                    id: templateItem
                                    width: parent.width
                                    height: listView.itemHeight
                                    required property string modelData
                                    required property int index
                                    ToolTip.text: modelData
                                    ToolTip.visible: index == tlistView.hoveredIndex
                                    ToolTip.delay: Application.styleHints.mousePressAndHoldInterval
                                    Rectangle {
                                        anchors.fill: parent
                                        radius: 4
                                        color: activePalette.highlight
                                        visible: templateItem.index == tlistView.currentIndex && tlistView.activeFocus
                                        opacity: 0.5
                                    }

                                    Label {
                                        id: tlistLabel
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.leftMargin: 4
                                        anchors.right: parent.right
                                        anchors.rightMargin: 4
                                        leftPadding: 4
                                        text: splash.profileNames[templateItem.index]
                                        color: templatesArea.containsMouse || tlistButton.hovered ? activePalette.highlight : activePalette.text
                                        elide: Text.ElideMiddle
                                        opacity: recentProjects.enabled ? 1 : 0.5
                                    }

                                    MouseArea {
                                        id: templatesArea

                                        anchors.fill: parent
                                        hoverEnabled: true
                                        enabled: splash.actionsEnabled
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            tlistView.currentIndex = templateItem.index;
                                            tlistView.forceActiveFocus();
                                            splash.openTemplate(templateItem.modelData);

                                        }
                                        onPositionChanged: {
                                            if (templateItem.index != tlistView.hoveredIndex)
                                                tlistView.hoveredIndex = templateItem.index;

                                        }
                                        onExited: {
                                            tlistView.hoveredIndex = -1;
                                        }
                                    }
                                    ToolButton {
                                        id: tlistButton
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        height: tlistLabel.height
                                        width: height
                                        enabled: splash.actionsEnabled
                                        icon.name: "list-remove"
                                        ToolTip.text: KI18n.i18n("Remove profile from list")
                                        ToolTip.delay: 1000
                                        ToolTip.visible: hovered
                                        hoverEnabled: true
                                        visible: templatesArea.containsMouse || hovered
                                        onClicked: {
                                            splash.forgetProfile(templateItem.modelData)
                                            splash.profileNames.splice(templateItem.index, 1)
                                            splash.profileIds.splice(templateItem.index, 1)
                                            tlistView.model = splash.profileIds
                                        }
                                    }

                                }

                            }

                        }

                    }

                }

            }
            Item {
                id: buttonBar

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: showBox.height + 8
                anchors.margins: 10

                CheckBox {
                    id: showBox

                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    checked: true
                    text: KI18n.i18n("Show on Startup")
                    onCheckStateChanged: {
                        splash.showWelcome(checked)
                    }
                }

                Label {
                    // KoraCut : le bas de l'écran de démarrage, d'après la charte. Rangé contre le numéro
                    // de version : centré, il passait sous « Afficher lors du démarrage » (4 octobre 2026).
                    anchors.right: kdenliveid.left
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: 0.62
                    text: KI18n.i18nc("@info écran de démarrage", "Free software · GPL-3.0 · koracut.org")
                }
                Label {
                    id: kdenliveid
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: KI18n.i18n("KoraCut") + " " + splash.version
                }

            }
            Item {
                // Crash recovery
                visible: splash.crashRecovery
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: buttonBar.top
                anchors.margins: 10
                height: Math.max(restartButton.height, restartLabel.height) + 10
                Rectangle {
                    anchors.fill: parent
                    color: "#22FF0000"
                    radius: 5
                }
                Label {
                    id: restartLabel
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.right: restartButton.left
                    text: KI18n.i18n("KoraCut crashed on last start. Reset config and restart?")
                    Layout.alignment: Qt.AlignVCenter
                    wrapMode: Text.Wrap
                }
                Button {
                    id: restartButton
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    text: KI18n.i18n("Reset")
                    icon.name: "view-refresh"
                    enabled: splash.actionsEnabled
                    onClicked: splash.resetConfig()
                    KeyNavigation.tab: listView
                }
            }
            Item {
                // Upgrade notice
                id: upgradeBox
                visible: splash.wasUpgraded && !splash.crashRecovery
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: buttonBar.top
                anchors.margins: 10
                height: Math.max(notesButton.height, upgradedLabel.height) + 10
                Rectangle {
                    anchors.fill: parent
                    color: "#2200FF00"
                    radius: 5
                }
                Label {
                    id: upgradedLabel
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 10
                    anchors.right: notesButton.left
                    text: KI18n.i18n("KoraCut has just been updated.")
                    wrapMode: Text.Wrap
                    onLinkActivated: (link)=> splash.openLink(link)
                    textFormat: Text.RichText
                    HoverHandler {
                        enabled: upgradedLabel.hoveredLink
                        cursorShape: Qt.PointingHandCursor
                    }
                }
                Button {
                    id: notesButton
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    enabled: splash.actionsEnabled
                    text: KI18n.i18n("What's New")
                    icon.name: "help-contents"
                    onClicked: splash.openLink("https://koracut.org/blog")
                    KeyNavigation.tab: listView
                }
            }

            // Loading progress info
            Rectangle {
                id: loadingBox
                visible: false
                color: activePalette.highlight
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: buttonBar.top
                radius: 5
                height: Math.max(loadingLabel.height, notesButton.height) + 10
                opacity: 0.5
            }
            Label {
                id: loadingLabel
                anchors.verticalCenter: loadingBox.verticalCenter
                anchors.left: loadingBox.left
                anchors.margins: 5
                wrapMode: Text.Wrap
                color: activePalette.highlightedText
            }
            // KoraCut, 23 septembre 2026 : barre de chargement Violet Kora de 3 px (charte, écran de
            // démarrage). Kdenlive ne donne que des messages, pas d'avancement : la barre est donc
            // indéterminée, un segment qui va et vient pendant que le message s'affiche.
            Rectangle {
                id: koracutProgress
                visible: loadingBox.visible
                anchors.left: loadingBox.left
                anchors.right: loadingBox.right
                anchors.bottom: loadingBox.top
                anchors.bottomMargin: 2
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

        Button {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: 8
            anchors.rightMargin: 8
            icon.name: "window-close"
            enabled: splash.actionsEnabled
            onClicked: splash.closeApp()
            opacity: 0.6
        }

    }

}
