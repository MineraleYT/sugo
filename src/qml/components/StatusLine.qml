// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import io.github.mineraleyt.sugo

/// A state in a few words after a small icon, e.g. "✓ Up to date"; the icon turns while `spinning`.
Item {
    id: line

    property string text
    /// A LineIcon name.
    property string iconName: "check"
    property color iconColor: Theme.muted
    property bool spinning: false
    /// Every text the line takes in turn ("Syncing", "Up to date"): it is as wide as with the
    /// widest, so nothing beside it moves when the text changes.
    property var labels: []
    /// With labels, the room of the text: the widest of them (and the text), in whole pixels
    /// plus one, as the text's own width differs from the measured one by a fraction of a pixel.
    readonly property real labelsWidth: {
        if (labels.length === 0) {
            return 0;
        }
        let widest = labelMetrics.font.pixelSize > 0 ? labelMetrics.advanceWidth(text) : 0;
        for (const label of labels) {
            widest = Math.max(widest, labelMetrics.advanceWidth(label));
        }
        return Math.ceil(widest) + 1;
    }

    implicitWidth: icon.width + Theme.spaceSm + (labels.length > 0 ? labelsWidth : label.implicitWidth)
    implicitHeight: Math.max(icon.height, label.implicitHeight)
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    FontMetrics {
        id: labelMetrics
        font: label.font
    }

    LineIcon {
        id: icon
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        name: line.iconName
        size: Theme.iconSm
        color: line.iconColor
        RotationAnimation on rotation {
            running: line.spinning
            loops: Animation.Infinite
            from: 0
            to: 360
            duration: Theme.durationLong * 4
        }
    }
    // Upright again once it stops turning.
    onSpinningChanged: if (!spinning) icon.rotation = 0

    Text {
        id: label
        anchors.left: icon.right
        anchors.leftMargin: Theme.spaceSm
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: line.text
        color: Theme.muted
        font.pixelSize: Theme.fontBody
        elide: Text.ElideRight
    }
}
