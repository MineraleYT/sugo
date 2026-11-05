// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import stream.sugo.app

/// A short message at the bottom of the window that goes away by itself.
Rectangle {
    id: toast

    function show(message: string, timeout: var): void {
        label.text = message;
        hideTimer.interval = timeout === "long" ? 7000 : (typeof timeout === "number" ? timeout : 3500);
        hideTimer.restart();
        shown = true;
    }

    property bool shown: false
    /// The message shown last (smoke checks read it).
    readonly property string message: label.text

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: shown ? Theme.space2xl : -height
    width: Math.min(label.implicitWidth + Theme.spaceXl * 2, parent.width - Theme.space2xl * 2)
    height: label.implicitHeight + Theme.spaceMd * 2 + Theme.spaceXs
    radius: Theme.radius
    color: Theme.raised
    border.width: Theme.hairline
    border.color: Theme.borderStrong
    opacity: shown ? 1 : 0
    visible: opacity > 0
    z: 1000
    Behavior on opacity { NumberAnimation { duration: Theme.durationLong } }
    Behavior on anchors.bottomMargin { NumberAnimation { duration: Theme.durationLong; easing.type: Easing.OutCubic } }
    Accessible.role: Accessible.AlertMessage
    Accessible.name: label.text

    Text {
        id: label
        anchors.fill: parent
        anchors.leftMargin: Theme.spaceXl
        anchors.rightMargin: Theme.spaceXl
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.foreground
        font.pixelSize: Theme.fontBody
    }

    Timer {
        id: hideTimer
        onTriggered: toast.shown = false
    }

    MouseArea {
        anchors.fill: parent
        onClicked: toast.shown = false
    }
}
