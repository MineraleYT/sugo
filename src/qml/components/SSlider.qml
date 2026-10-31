// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

/// A slider: a thin track, filled in the brand colour up to a round handle.
QQC2.Slider {
    id: control

    implicitWidth: Theme.space4xl * 3.5
    implicitHeight: Theme.controlHeight
    hoverEnabled: true

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: Theme.spaceXs
        radius: height / 2
        color: Theme.track

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: height / 2
            color: control.enabled ? Theme.brand : Theme.subtle
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: Theme.spaceXl
        implicitHeight: Theme.spaceXl
        radius: width / 2
        color: Theme.foreground
        border.width: control.visualFocus ? Theme.hairline * 2 : 0
        border.color: Theme.focusRing
        scale: control.pressed ? 1.1 : 1
        Behavior on scale { NumberAnimation { duration: Theme.durationShort } }
    }
}
