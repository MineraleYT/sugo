// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import io.github.mineraleyt.sugo

/// A keyboard shortcut shown next to the action it triggers, e.g. "Space" or "Ctrl 1".
Rectangle {
    id: kbd

    property string text
    property color textColor: Theme.muted

    implicitWidth: label.implicitWidth + Theme.spaceSm * 2
    implicitHeight: label.implicitHeight + Theme.hairline * 2
    radius: Theme.radiusSm - 1
    color: "transparent"
    border.width: Theme.hairline
    border.color: Theme.alpha(kbd.textColor, 0.35)
    Accessible.ignored: true

    Text {
        id: label
        anchors.centerIn: parent
        text: kbd.text
        color: kbd.textColor
        font.family: Theme.monoFamily
        font.pixelSize: Theme.fontMicro
    }
}
