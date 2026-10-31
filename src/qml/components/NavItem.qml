// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

/// An entry of the sidebar: an icon (a LineIcon, or a timer's tile), the name, and a hint on the right.
QQC2.AbstractButton {
    id: item

    property string iconName
    /// A timer's icon instead of a line icon.
    property string timerIconName
    property bool current: false
    property bool muted: false
    /// A short text on the right, e.g. "25m".
    property string trailingText
    /// A dot on the right (the timer in use, an update); transparent for none.
    property color dotColor: "transparent"

    hoverEnabled: true
    // Keyboard focus by Tab only: a click must not leave Space bound to this entry.
    focusPolicy: Qt.TabFocus
    implicitHeight: Theme.navHeight
    leftPadding: Theme.spaceMd + Theme.hairline * 2
    rightPadding: Theme.spaceMd + Theme.hairline * 2
    Accessible.role: Accessible.PageTab
    Accessible.name: text
    Accessible.checked: current

    background: Rectangle {
        radius: Theme.radius
        color: item.current ? Theme.selected : (item.down ? Theme.pressed : (item.hovered ? Theme.hover : "transparent"))
        border.width: item.current || item.visualFocus ? Theme.hairline : 0
        border.color: item.visualFocus ? Theme.focusRing : Theme.borderStrong
        Behavior on color { ColorAnimation { duration: Theme.durationShort } }
    }

    contentItem: RowLayout {
        spacing: Theme.spaceMd + Theme.hairline * 2
        opacity: item.enabled ? 1 : 0.45
        LineIcon {
            visible: item.timerIconName.length === 0 && item.iconName.length > 0
            name: item.iconName
            size: Theme.icon
            color: item.current ? Theme.foreground : Theme.muted
        }
        TimerIcon {
            visible: item.timerIconName.length > 0
            iconName: item.timerIconName
            size: Theme.iconMd - Theme.hairline * 2
        }
        Text {
            text: item.text
            color: item.current ? Theme.foreground : (item.muted ? Theme.muted : Theme.alpha(Theme.foreground, 0.82))
            font.pixelSize: Theme.fontTitle
            font.weight: item.current ? Font.Medium : Font.Normal
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        Text {
            visible: item.trailingText.length > 0 && item.dotColor.a === 0
            text: item.trailingText
            color: Theme.subtle
            font.pixelSize: Theme.fontCaption
            font.features: { "tnum": 1 }
        }
        Rectangle {
            visible: item.dotColor.a > 0
            implicitWidth: Theme.spaceSm + Theme.hairline
            implicitHeight: implicitWidth
            radius: width / 2
            color: item.dotColor
        }
    }
}
