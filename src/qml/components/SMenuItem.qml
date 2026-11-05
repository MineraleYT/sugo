// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import stream.sugo.app

/**
 * A row of an SMenu: an optional leading icon (`iconName`, a LineIcon), colour dot (`swatch`) or
 * item of its own (`leading`), the label cut with "…" when too long, an optional second line
 * (`description`, the row is then taller) and a check on the right when `checked`.
 * `triggered()` follows a click or Enter; the menu then closes, unless `keepOpen`.
 */
QQC2.AbstractButton {
    id: row

    /// A LineIcon name before the label.
    property string iconName
    /// A colour dot before the label (the accent colours); none while transparent.
    property color swatch: "transparent"
    /// Any other item before the label (e.g. a TimerIcon), drawn at its implicit size.
    property Component leading
    /// A second, quieter line under the label.
    property string description
    /// Leaves the menu open after the row is chosen.
    property bool keepOpen: false
    /// Set by the SMenu holding the row.
    property var menu: null
    /// Lit: under the mouse or chosen with the arrow keys (an SMenu follows its own highlight; a
    /// combo box's list sets it from its highlighted index).
    property bool highlighted: menu !== null && menu.highlightedItem === row
    readonly property bool isMenuItem: true

    signal triggered()

    readonly property bool hasLeading: iconName.length > 0 || swatch.a > 0 || leading !== null

    implicitHeight: description.length > 0 ? Theme.menuRowHeightTwoLine : Theme.menuRowHeight
    // The check's room is always kept, so the row does not widen when the check moves to it.
    implicitWidth: leftPadding + rightPadding + implicitContentWidth
    leftPadding: Theme.spaceMd
    rightPadding: Theme.spaceMd
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    focusPolicy: Qt.NoFocus
    font.pixelSize: Theme.fontBody

    Accessible.role: Accessible.MenuItem
    Accessible.name: text
    Accessible.description: description
    Accessible.checkable: menu !== null && menu.hasChecks
    Accessible.checked: checked

    /// Chooses the row, as a click does.
    function activate(): void {
        if (!enabled) {
            return;
        }
        triggered();
        if (menu !== null && !keepOpen) {
            menu.close();
        }
    }

    onClicked: activate()
    onHoveredChanged: {
        if (hovered && enabled && menu !== null) {
            menu.highlightedItem = row;
        }
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: row.highlighted && row.enabled ? Theme.selected : "transparent"
    }

    contentItem: RowLayout {
        spacing: Theme.spaceMd + Theme.hairline

        Item {
            id: leadingBox
            visible: row.hasLeading
            implicitWidth: row.leading !== null ? leadingLoader.implicitWidth : Theme.swatchSize
            implicitHeight: row.leading !== null ? leadingLoader.implicitHeight : Theme.swatchSize
            Layout.alignment: Qt.AlignVCenter

            LineIcon {
                visible: row.iconName.length > 0 && row.leading === null
                anchors.centerIn: parent
                name: row.iconName
                size: Theme.iconSm
                color: row.enabled ? Theme.muted : Theme.subtle
            }
            Rectangle {
                visible: row.iconName.length === 0 && row.leading === null && row.swatch.a > 0
                anchors.fill: parent
                radius: width / 2
                color: row.swatch
                border.width: Theme.hairline
                border.color: Theme.borderStrong
            }
            Loader {
                id: leadingLoader
                anchors.centerIn: parent
                sourceComponent: row.leading
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 0
            Text {
                id: label
                Layout.fillWidth: true
                text: row.text
                font: row.font
                color: !row.enabled ? Theme.subtle : row.highlighted || row.checked ? Theme.foreground : Theme.dim
                elide: Text.ElideRight
                maximumLineCount: 1
            }
            Text {
                id: second
                visible: row.description.length > 0
                Layout.fillWidth: true
                text: row.description
                font.pixelSize: Theme.fontSmall
                color: Theme.muted
                elide: Text.ElideRight
                maximumLineCount: 1
            }
        }

        Item {
            implicitWidth: Theme.iconSm
            implicitHeight: Theme.iconSm
            Layout.leftMargin: Theme.spaceLg - (Theme.spaceMd + Theme.hairline)
            Layout.alignment: Qt.AlignVCenter
            LineIcon {
                visible: row.checked
                anchors.fill: parent
                name: "check"
                size: Theme.iconSm
                color: Theme.muted
            }
        }
    }
}
