// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import stream.sugo.app

/// An on/off switch: filled with the accent colour when on, an outlined track when off.
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
        implicitWidth: Theme.switchWidth
        implicitHeight: Theme.switchHeight
        radius: height / 2
        color: control.checked && control.enabled ? Theme.accent : Theme.track
        opacity: control.enabled ? 1 : 0.6
        border.width: Theme.hairline
        border.color: control.visualFocus ? Theme.focusRing : control.checked && control.enabled ? Theme.accent : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.durationShort } }

        Rectangle {
            readonly property real inset: (parent.height - height) / 2
            x: control.checked ? parent.width - width - inset : inset
            y: inset
            width: Theme.switchKnob
            height: width
            radius: width / 2
            color: control.checked && control.enabled ? Theme.accentText : Theme.muted
            Behavior on x { NumberAnimation { duration: Theme.durationShort; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: null
}
