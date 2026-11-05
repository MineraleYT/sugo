// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import io.github.mineraleyt.sugo

/// A rounded label with a coloured dot, e.g. "Work · 16:12 left".
Rectangle {
    id: pill

    property string text
    property color dotColor: Theme.brand
    property bool pulsing: false
    /// Every text the pill takes in turn ("Syncing", "Up to date"): it is as wide as with the
    /// widest, so nothing beside it moves when the text changes.
    property var labels: []
    /// With labels, the room of the text: the widest of them (and the text), in whole pixels
    /// plus one, as the text's own width differs from the measured one by a fraction of a pixel.
    /// It leaves out the current text's width, which would make the pill a pixel wider or
    /// narrower with each text.
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

    implicitWidth: (labels.length > 0 ? dot.implicitWidth + row.spacing + labelsWidth : row.implicitWidth)
                   + Theme.spaceLg * 2
    /// The narrowest it gets, the dot alone: given less than its implicit width, the text is cut.
    readonly property real compactWidth: Theme.spaceSm + Theme.spaceLg * 2
    implicitHeight: row.implicitHeight + Theme.spaceXs * 2
    radius: height / 2
    color: "transparent"
    border.width: Theme.hairline
    border.color: Theme.border
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    FontMetrics {
        id: labelMetrics
        font: label.font
    }

    RowLayout {
        id: row
        anchors.centerIn: parent
        width: Math.max(0, Math.min(implicitWidth, pill.width - Theme.spaceLg * 2))
        spacing: Theme.spaceSm
        Rectangle {
            id: dot
            implicitWidth: Theme.spaceSm
            implicitHeight: Theme.spaceSm
            radius: width / 2
            color: pill.dotColor
            SequentialAnimation on opacity {
                running: pill.pulsing
                loops: Animation.Infinite
                alwaysRunToEnd: true
                NumberAnimation { to: 0.35; duration: Theme.durationLong * 4; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1; duration: Theme.durationLong * 4; easing.type: Easing.InOutSine }
            }
        }
        Text {
            id: label
            text: pill.text
            color: Theme.muted
            font.pixelSize: Theme.fontCaption
            font.features: { "tnum": 1 }
            elide: Text.ElideRight
            Layout.minimumWidth: 0
            Layout.maximumWidth: implicitWidth + 1
            Layout.fillWidth: true
        }
    }
}
