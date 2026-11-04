// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import io.github.mineraleyt.sugo

/**
 * A row of a FormSection: the title and a description on the left, the control on the right.
 * With `stacked` the control takes the full width under the text (an icon grid, a long field).
 */
Item {
    id: row

    property string title
    property string description
    property bool stacked: false
    /// No line above it (the first row of a section).
    property bool first: false
    /// Every text `description` can show, when it changes with a state: the row keeps the
    /// height of the tallest, so nothing moves when the description changes.
    property var descriptionVariants: []
    default property alias control: controlHolder.data

    Layout.fillWidth: true
    implicitHeight: layout.implicitHeight + Theme.spaceLg * 2
    Accessible.role: Accessible.Grouping
    Accessible.name: title

    Rectangle {
        visible: !row.first
        width: parent.width
        height: Theme.hairline
        color: Theme.border
    }

    GridLayout {
        id: layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        columns: row.stacked ? 1 : 2
        columnSpacing: Theme.spaceXl
        rowSpacing: Theme.spaceMd

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.hairline * 2
            Text {
                text: row.title
                color: row.enabled ? Theme.foreground : Theme.subtle
                font.pixelSize: Theme.fontBody + 1
                font.weight: Font.Medium
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Text {
                id: descriptionText
                visible: row.description.length > 0
                text: row.description
                color: Theme.muted
                font.pixelSize: Theme.fontCaption + 1
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.minimumHeight: {
                    let height = 0;
                    for (let i = 0; i < descriptionMeasures.count; ++i) {
                        const measure = descriptionMeasures.itemAt(i);
                        if (measure) {
                            height = Math.max(height, measure.implicitHeight);
                        }
                    }
                    return height;
                }
                verticalAlignment: Text.AlignTop

                // The variants, never shown: their heights at this width.
                Repeater {
                    id: descriptionMeasures
                    model: row.descriptionVariants
                    delegate: Text {
                        required property string modelData
                        visible: false
                        width: descriptionText.width
                        text: modelData
                        font: descriptionText.font
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
        RowLayout {
            id: controlHolder
            Layout.alignment: row.stacked ? Qt.AlignLeft : (Qt.AlignRight | Qt.AlignVCenter)
            Layout.fillWidth: row.stacked
            spacing: Theme.spaceSm
        }
    }
}
