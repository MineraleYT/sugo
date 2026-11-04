// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

/// The column on the left: search, the pages, and the timers (the only place to choose one).
Rectangle {
    id: sidebar

    /// The destination shown: "timer", "stats", "settings", "about" or "preset".
    property string current
    /// The timer whose page is open ("" for none).
    property string openTimer
    signal navigateRequested(string name)
    signal timerRequested(string uuid)
    signal newTimerRequested()
    signal searchRequested()
    /// "Restart to update": an update has been installed and waits for the restart.
    signal restartRequested()
    /// The top of the sidebar moves the window, which has no system title bar; on desktops that
    /// put window buttons on the left, they are here.
    signal moveRequested()
    property bool maximized: false
    signal minimizeRequested()
    signal toggleMaximizeRequested()
    signal closeRequested()

    implicitWidth: Theme.sidebarWidth
    color: Theme.sidebar

    Rectangle {
        anchors.right: parent.right
        width: Theme.hairline
        height: parent.height
        color: Theme.border
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spaceMd
        anchors.rightMargin: Theme.spaceMd + Theme.hairline
        spacing: Theme.hairline * 2

        // The window buttons the desktop puts on the left.
        WindowButtons {
            visible: DesktopStyle.leftButtons.length > 0
            Layout.leftMargin: Theme.spaceSm
            Layout.topMargin: Theme.spaceXs
            Layout.bottomMargin: Theme.spaceSm
            buttons: DesktopStyle.leftButtons
            maximized: sidebar.maximized
            onMinimizeRequested: sidebar.minimizeRequested()
            onMaximizeRequested: sidebar.toggleMaximizeRequested()
            onCloseRequested: sidebar.closeRequested()
        }

        // The logo and the name; dragging them moves the window.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.topBarHeight - Theme.spaceMd * 2
            // On macOS the system's window buttons sit in the corner, left of the logo.
            Layout.leftMargin: Theme.spaceSm + (DesktopStyle.nativeChrome ? Theme.trafficLightsWidth : 0)
            Layout.bottomMargin: Theme.spaceSm
            spacing: Theme.spaceMd + Theme.hairline * 2
            DragHandler {
                target: null
                onActiveChanged: if (active) sidebar.moveRequested()
            }
            TapHandler {
                onDoubleTapped: sidebar.toggleMaximizeRequested()
            }
            Image {
                source: "qrc:/icons/io.github.mineraleyt.sugo.svg"
                sourceSize.width: Theme.iconLg * 2
                sourceSize.height: Theme.iconLg * 2
                Layout.preferredWidth: Theme.iconLg - Theme.spaceXs
                Layout.preferredHeight: Theme.iconLg - Theme.spaceXs
                smooth: true
                mipmap: true
            }
            Text {
                text: i18n("Sugo")
                color: Theme.foreground
                font.pixelSize: Theme.fontTitle + 2
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
        }

        // Search: pages, timers and commands (Ctrl+K).
        QQC2.AbstractButton {
            id: search
            Layout.fillWidth: true
            implicitHeight: Theme.navHeight
            Layout.bottomMargin: Theme.spaceMd
            hoverEnabled: true
            focusPolicy: Qt.TabFocus
            leftPadding: Theme.spaceMd + Theme.hairline * 2
            rightPadding: Theme.spaceSm
            text: i18n("Search")
            onClicked: sidebar.searchRequested()
            background: Rectangle {
                radius: Theme.radius
                color: search.hovered ? Theme.pressed : Theme.hover
                border.width: Theme.hairline
                border.color: search.visualFocus ? Theme.focusRing : Theme.border
            }
            contentItem: RowLayout {
                spacing: Theme.spaceMd + Theme.hairline * 2
                LineIcon {
                    name: "search"
                    size: Theme.icon
                    color: Theme.muted
                }
                Text {
                    text: search.text
                    color: Theme.muted
                    font.pixelSize: Theme.fontTitle
                    Layout.fillWidth: true
                }
                Kbd {
                    text: DesktopStyle.shortcutText("Ctrl+K")
                }
            }
        }

        NavItem {
            Layout.fillWidth: true
            text: i18n("Focus")
            iconName: "timer"
            current: sidebar.current === "timer"
            onClicked: sidebar.navigateRequested("timer")
        }
        NavItem {
            Layout.fillWidth: true
            text: i18n("Statistics")
            iconName: "stats"
            current: sidebar.current === "stats"
            onClicked: sidebar.navigateRequested("stats")
        }

        Text {
            text: i18n("Timers")
            color: Theme.muted
            font.pixelSize: Theme.fontCaption + 1
            Layout.leftMargin: Theme.spaceMd + Theme.hairline * 2
            Layout.topMargin: Theme.spaceXl
            Layout.bottomMargin: Theme.spaceXs
        }

        ListView {
            id: timerList
            Layout.fillWidth: true
            Layout.preferredHeight: contentHeight
            Layout.maximumHeight: Math.max(Theme.navHeight * 3, sidebar.height - Theme.navHeight * 9)
            clip: true
            model: PresetModel
            spacing: Theme.hairline * 2
            boundsBehavior: Flickable.StopAtBounds
            QQC2.ScrollBar.vertical: QQC2.ScrollBar { policy: QQC2.ScrollBar.AsNeeded }

            delegate: NavItem {
                id: timerEntry
                required property int index
                required property string uuid
                required property string name
                required property string iconName
                required property int workMinutes
                readonly property bool inUse: PresetModel.currentUuid === uuid

                objectName: "timerEntry-" + index
                width: ListView.view.width
                text: name
                timerIconName: iconName
                current: sidebar.current === "preset" && sidebar.openTimer === uuid
                trailingText: i18nc("minutes, short", "%1m", workMinutes)
                dotColor: inUse ? (TimerEngine.phase === TimerEngine.Phase.Work ? Theme.brand : Theme.breakColor) : "transparent"
                Accessible.role: Accessible.RadioButton
                Accessible.checked: inUse
                onClicked: sidebar.timerRequested(uuid)
            }
        }

        NavItem {
            Layout.fillWidth: true
            text: i18n("New timer")
            iconName: "plus"
            muted: true
            current: sidebar.current === "preset" && sidebar.openTimer === ""
            onClicked: sidebar.newTimerRequested()
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }

        // The update of the AppImage or the macOS app, one step at a time: found, downloading, installed.
        NavItem {
            readonly property bool found: UpdateChecker.installable
            readonly property bool downloading: UpdateChecker.status === UpdateChecker.Downloading
            readonly property bool installed: UpdateChecker.status === UpdateChecker.ReadyToRestart
            Layout.fillWidth: true
            visible: found || downloading || installed
            enabled: !downloading
            text: installed ? i18n("Restart to update")
                  : downloading ? i18n("Downloading update… %1%", Math.round(UpdateChecker.progress * 100))
                  : i18n("Install update")
            iconName: installed ? "refresh" : "download"
            dotColor: downloading ? "transparent" : Theme.brand
            Accessible.description: !installed ? i18n("Sugo %1 is available.", UpdateChecker.latestVersion)
                                    : DesktopStyle.macos ? i18n("Sugo %1 is ready; it replaces this version when Sugo restarts.", UpdateChecker.latestVersion)
                                    : i18n("Sugo %1 is installed; it starts after the restart.", UpdateChecker.latestVersion)
            onClicked: installed ? sidebar.restartRequested() : UpdateChecker.installUpdate()
        }
        NavItem {
            Layout.fillWidth: true
            text: i18n("Settings")
            iconName: "settings"
            current: sidebar.current === "settings"
            onClicked: sidebar.navigateRequested("settings")
        }
        NavItem {
            Layout.fillWidth: true
            text: i18n("About")
            iconName: "info"
            dotColor: UpdateChecker.hasUpdate ? Theme.brand : "transparent"
            Accessible.description: UpdateChecker.hasUpdate ? i18n("Update available") : ""
            current: sidebar.current === "about"
            onClicked: sidebar.navigateRequested("about")
        }
    }
}
