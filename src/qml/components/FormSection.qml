// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import io.github.mineraleyt.sugo

/// A group of settings: a bold heading, an optional line under it, and its rows in one card.
ColumnLayout {
    id: section

    property string title
    property string description
    /// Next to the heading (a button, say).
    property alias headerExtra: extraRow.data
    default property alias rows: rowColumn.data

    Layout.fillWidth: true
    spacing: Theme.spaceMd

    RowLayout {
        Layout.fillWidth: true
        visible: section.title.length > 0
        spacing: Theme.spaceMd
        Text {
            text: section.title
            color: Theme.foreground
            font.pixelSize: Theme.fontSection
            font.weight: Font.DemiBold
        }
        Item {
            Layout.fillWidth: true
        }
        RowLayout {
            id: extraRow
            spacing: Theme.spaceXs
        }
    }
    Text {
        visible: section.description.length > 0
        text: section.description
        color: Theme.muted
        font.pixelSize: Theme.fontBody
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Layout.topMargin: -Theme.spaceXs
    }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: rowColumn.implicitHeight
        radius: Theme.radiusLg
        color: Theme.card
        border.width: Theme.hairline
        border.color: Theme.border

        ColumnLayout {
            id: rowColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spaceXl
            anchors.rightMargin: Theme.spaceXl
            spacing: 0
        }
    }
}
