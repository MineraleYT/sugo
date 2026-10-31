// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

/// An on/off switch in the brand colour.
QQC2.Switch {
    id: control

    padding: 0
    spacing: 0
    hoverEnabled: true
    implicitWidth: indicator.implicitWidth
    implicitHeight: indicator.implicitHeight

    indicator: Rectangle {
        x: 0
        y: (control.height - height) / 2
        implicitWidth: Theme.space3xl + Theme.spaceXs
        implicitHeight: Theme.spaceXl + Theme.spaceXs
        radius: height / 2
        color: !control.enabled ? Theme.track : control.checked ? Theme.brand : Theme.borderStrong
        opacity: control.enabled ? 1 : 0.6
        border.width: control.visualFocus ? Theme.hairline : 0
        border.color: Theme.focusRing
        Behavior on color { ColorAnimation { duration: Theme.durationShort } }

        Rectangle {
            readonly property real inset: Theme.hairline * 2
            x: control.checked ? parent.width - width - inset : inset
            y: inset
            width: parent.height - inset * 2
            height: width
            radius: width / 2
            color: control.checked ? Theme.brandText : Theme.muted
            Behavior on x { NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: null
}
