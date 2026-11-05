// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

/**
 * Minimize, maximize and close, drawn the way the desktop draws them (DesktopStyle): the icon
 * theme on KDE and GNOME (GNOME's round buttons), Windows' wide buttons with a red close,
 * macOS's coloured circles. `buttons` is one side of DesktopStyle's layout.
 */
RowLayout {
    id: controls

    property var buttons: []
    property bool maximized: false
    /// The height of the bar they sit in (Windows' buttons fill it).
    property real barHeight: Theme.titleBarHeight
    signal minimizeRequested()
    signal maximizeRequested()
    signal closeRequested()

    readonly property string style: DesktopStyle.style
    spacing: style === "windows" ? 0 : style === "macos" ? Theme.spaceMd : style === "gnome" ? Theme.spaceMd + Theme.hairline * 2 : Theme.spaceXs

    Repeater {
        model: controls.buttons

        QQC2.AbstractButton {
            id: button
            required property string modelData
            readonly property bool isClose: modelData === "close"
            readonly property string iconName: modelData === "minimize" ? "window-minimize"
                                              : modelData === "maximize" ? (controls.maximized ? "window-restore" : "window-maximize")
                                              : "window-close"
            readonly property string lineIcon: modelData === "minimize" ? "minimize"
                                              : modelData === "maximize" ? (controls.maximized ? "restore" : "maximize") : "close"

            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            text: modelData === "minimize" ? i18n("Minimize") : modelData === "maximize" ? (controls.maximized ? i18n("Restore") : i18n("Maximize")) : i18n("Close")
            Accessible.name: text
            onClicked: {
                if (modelData === "minimize") {
                    controls.minimizeRequested();
                } else if (modelData === "maximize") {
                    controls.maximizeRequested();
                } else {
                    controls.closeRequested();
                }
            }

            implicitWidth: controls.style === "windows" ? Theme.space4xl - Theme.hairline * 2
                         : controls.style === "macos" ? Theme.spaceLg + Theme.hairline
                         : controls.style === "gnome" ? Theme.iconMd + Theme.spaceXs
                         : Theme.iconMd + Theme.spaceXs
            implicitHeight: controls.style === "windows" ? controls.barHeight : implicitWidth

            background: Rectangle {
                radius: controls.style === "windows" ? 0 : width / 2
                color: {
                    switch (controls.style) {
                    case "macos":
                        return button.modelData === "close" ? Theme.macClose
                             : button.modelData === "minimize" ? Theme.macMinimize : Theme.macMaximize;
                    case "windows":
                        return button.isClose && button.hovered ? Theme.windowsClose
                             : button.down ? Theme.pressed : button.hovered ? Theme.hover : "transparent";
                    case "gnome":
                        return button.down ? Theme.borderStrong : button.hovered ? Theme.pressed : Theme.hover;
                    default:
                        return button.isClose && button.hovered ? Theme.windowsClose
                             : button.down ? Theme.pressed : button.hovered ? Theme.hover : "transparent";
                    }
                }
                Behavior on color { ColorAnimation { duration: Theme.durationShort } }
            }

            contentItem: Item {
                // macOS shows its glyphs only while the pointer is over the buttons.
                visible: controls.style !== "macos" || button.hovered
                ThemeIcon {
                    id: themeIcon
                    anchors.centerIn: parent
                    visible: (controls.style === "kde" || controls.style === "gnome") && valid
                    name: controls.style === "gnome" ? button.iconName + "-symbolic" : button.iconName
                    size: controls.style === "gnome" ? Theme.iconSm + Theme.hairline * 2 : Theme.icon
                    color: button.isClose && button.hovered && controls.style !== "gnome" ? Theme.brandText : Theme.foreground
                }
                LineIcon {
                    anchors.centerIn: parent
                    visible: !themeIcon.visible
                    name: button.lineIcon
                    size: controls.style === "macos" ? Theme.spaceSm + Theme.hairline * 2 : controls.style === "windows" ? Theme.iconSm - Theme.hairline * 2 : Theme.iconSm
                    color: controls.style === "macos" ? Theme.alpha(Theme.scrim, 1)
                         : button.isClose && button.hovered ? Theme.brandText : Theme.foreground
                }
            }
        }
    }
}
