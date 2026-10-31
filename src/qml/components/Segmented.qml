// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

/// A row of choices where one is on: `model` is an array of { id, text }.
Rectangle {
    id: segmented

    property var model: []
    property string current
    signal activated(string id)

    implicitWidth: row.implicitWidth + Theme.hairline * 6
    implicitHeight: Theme.controlHeight + Theme.hairline * 2
    radius: Theme.radius + 2
    color: "transparent"
    border.width: Theme.hairline
    border.color: Theme.border
    Accessible.role: Accessible.PageTabList

    RowLayout {
        id: row
        anchors.centerIn: parent
        spacing: Theme.hairline * 2

        Repeater {
            model: segmented.model
            delegate: QQC2.AbstractButton {
                id: choice
                required property var modelData
                readonly property bool on: segmented.current === modelData.id
                implicitHeight: Theme.controlHeight - Theme.hairline * 4
                leftPadding: Theme.spaceLg
                rightPadding: Theme.spaceLg
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                text: modelData.text
                Accessible.role: Accessible.PageTab
                Accessible.checked: on
                onClicked: segmented.activated(modelData.id)
                background: Rectangle {
                    radius: Theme.radius
                    color: choice.on ? Theme.selected : (choice.hovered ? Theme.hover : "transparent")
                    border.width: choice.on || choice.visualFocus ? Theme.hairline : 0
                    border.color: choice.visualFocus ? Theme.focusRing : Theme.borderStrong
                }
                contentItem: Text {
                    text: choice.text
                    color: choice.on ? Theme.foreground : Theme.muted
                    font.pixelSize: Theme.fontBody
                    font.weight: choice.on ? Font.Medium : Font.Normal
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }
}
