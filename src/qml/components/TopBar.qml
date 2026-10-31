// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

/// The bar above the page: back (on sub-pages), the title, and the timer's state.
Rectangle {
    id: bar

    /// The page shown; its `title` appears here.
    property var page: null
    property bool canGoBack: false
    property bool showMenuButton: false
    /// Only the window controls (the welcome tour draws its own content).
    property bool minimal: false
    /// The state of the timer, in a pill (not on the timer page, which shows it in large).
    property bool showStatus: true
    property bool maximized: false
    /// The buttons the desktop puts on the left (when the sidebar, which has them otherwise, is hidden).
    property bool showLeftButtons: false
    signal backRequested()
    signal menuRequested()
    signal statusClicked()
    // The window has no system title bar: this bar moves it and carries its buttons.
    signal moveRequested()
    signal toggleMaximizeRequested()
    signal minimizeRequested()
    signal closeRequested()

    implicitHeight: Theme.topBarHeight
    color: Theme.background

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.hairline
        color: Theme.border
    }

    readonly property string statusText: {
        if (!TimerEngine.active) {
            return "";
        }
        const total = TimerEngine.remainingSeconds;
        const h = Math.floor(total / 3600);
        const m = Math.floor((total % 3600) / 60);
        const s = total % 60;
        const pad = n => String(n).padStart(2, "0");
        const clock = h > 0 ? h + ":" + pad(m) + ":" + pad(s) : pad(m) + ":" + pad(s);
        const phase = TimerEngine.phase === TimerEngine.Phase.Work ? i18n("Work")
                    : TimerEngine.phase === TimerEngine.Phase.ShortBreak ? i18n("Short break") : i18n("Long break");
        return TimerEngine.paused ? i18nc("phase · time", "%1 · paused at %2", phase, clock)
                                  : i18nc("phase · time", "%1 · %2 left", phase, clock);
    }

    DragHandler {
        target: null
        onActiveChanged: if (active) bar.moveRequested()
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onDoubleTapped: bar.toggleMaximizeRequested()
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spaceLg
        anchors.rightMargin: Theme.spaceLg
        spacing: Theme.spaceMd

        WindowButtons {
            visible: bar.showLeftButtons && DesktopStyle.leftButtons.length > 0
            Layout.rightMargin: Theme.spaceSm
            buttons: DesktopStyle.leftButtons
            maximized: bar.maximized
            onMinimizeRequested: bar.minimizeRequested()
            onMaximizeRequested: bar.toggleMaximizeRequested()
            onCloseRequested: bar.closeRequested()
        }
        SButton {
            visible: bar.showMenuButton && !bar.canGoBack && !bar.minimal
            kind: "ghost"
            iconOnly: true
            iconName: "menu"
            text: i18n("Menu")
            onClicked: bar.menuRequested()
        }
        SButton {
            visible: bar.canGoBack && !bar.minimal
            kind: "ghost"
            iconOnly: true
            iconName: "chevron-left"
            text: i18n("Back")
            onClicked: bar.backRequested()
        }

        // Where we are: "Timers / Pomodoro", or the page's title alone.
        Text {
            visible: !bar.minimal && !!bar.page && !!bar.page.section
            text: bar.page && bar.page.section ? bar.page.section : ""
            color: Theme.muted
            font.pixelSize: Theme.fontTitle
        }
        Text {
            visible: !bar.minimal && !!bar.page && !!bar.page.section
            text: "/"
            color: Theme.subtle
            font.pixelSize: Theme.fontTitle
        }
        Text {
            visible: !bar.minimal
            text: bar.page ? bar.page.title : ""
            color: Theme.foreground
            font.pixelSize: Theme.fontTitle
            font.weight: Font.Medium
            elide: Text.ElideRight
            Layout.maximumWidth: implicitWidth + 1
            Layout.fillWidth: true
        }

        StatusPill {
            visible: bar.statusText.length > 0 && !bar.minimal && bar.showStatus
            text: bar.statusText
            dotColor: TimerEngine.paused ? Theme.warning
                    : TimerEngine.phase === TimerEngine.Phase.Work ? Theme.brand : Theme.breakColor
            pulsing: TimerEngine.active && !TimerEngine.paused
            TapHandler {
                onTapped: bar.statusClicked()
            }
            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }
        }

        Item {
            Layout.fillWidth: true
        }

        // The window's own buttons on this side, as the desktop draws them.
        WindowButtons {
            visible: DesktopStyle.rightButtons.length > 0
            Layout.fillHeight: DesktopStyle.style === "windows"
            Layout.rightMargin: DesktopStyle.style === "windows" ? -Theme.spaceLg : 0
            buttons: DesktopStyle.rightButtons
            maximized: bar.maximized
            barHeight: bar.height - Theme.hairline
            onMinimizeRequested: bar.minimizeRequested()
            onMaximizeRequested: bar.toggleMaximizeRequested()
            onCloseRequested: bar.closeRequested()
        }
    }
}
