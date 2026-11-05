// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

/**
 * The list of the settings' categories, at the left of the settings and of a timer's page:
 * one row per category, the current one lit. Shown in a window wide enough; a narrow one has
 * a drop-down list of the categories at the top of the settings instead.
 */
Rectangle {
    id: nav

    /// The categories, as { id, text }.
    property var categories: []
    /// The id of the category shown ("" for none).
    property string current

    signal activated(string id)

    implicitWidth: Theme.settingsNavWidth
    color: Theme.background
    Accessible.role: Accessible.List
    Accessible.name: i18n("Settings")

    // The line between the list and the page.
    Rectangle {
        anchors.right: parent.right
        width: Theme.hairline
        height: parent.height
        color: Theme.border
    }

    Column {
        id: rows
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.spaceMd
        anchors.rightMargin: Theme.spaceMd + Theme.hairline
        anchors.topMargin: Theme.spaceXl
        spacing: Theme.hairline

        Repeater {
            model: nav.categories
            delegate: QQC2.AbstractButton {
                id: entry
                required property var modelData
                readonly property bool isCurrent: nav.current === modelData.id
                objectName: "settingsNavItem-" + modelData.id
                width: rows.width
                implicitHeight: Theme.settingsNavItemHeight
                leftPadding: Theme.spaceMd
                rightPadding: Theme.spaceMd
                hoverEnabled: true
                focusPolicy: Qt.TabFocus
                text: modelData.text
                Accessible.role: Accessible.ListItem
                Accessible.selected: isCurrent
                onClicked: nav.activated(modelData.id)

                background: Rectangle {
                    radius: Theme.radiusSm
                    color: entry.isCurrent ? Theme.selected : entry.hovered ? Theme.hover : "transparent"
                    border.width: entry.visualFocus ? Theme.hairline : 0
                    border.color: Theme.focusRing
                }
                contentItem: Text {
                    text: entry.text
                    color: entry.isCurrent || entry.hovered ? Theme.foreground : Theme.dim
                    font.pixelSize: Theme.fontBody
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }
    }
}
