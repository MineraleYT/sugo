// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import io.github.mineraleyt.sugo

/// A message inside a page: `tone` "info", "warning" or "danger"; buttons go in as children.
Rectangle {
    id: banner

    property string text
    property string tone: "info"
    default property alias actions: actionRow.data

    readonly property color toneColor: tone === "danger" ? Theme.danger : tone === "warning" ? Theme.warning : Theme.muted

    implicitHeight: row.implicitHeight + Theme.spaceMd * 2
    radius: Theme.radius
    color: tone === "danger" ? Theme.dangerFill : tone === "warning" ? Theme.warningFill : Theme.hover
    border.width: Theme.hairline
    border.color: tone === "info" ? Theme.border : Theme.alpha(toneColor, 0.35)
    Accessible.role: Accessible.AlertMessage
    Accessible.name: text

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: Theme.spaceLg
        anchors.rightMargin: Theme.spaceSm
        spacing: Theme.spaceMd
        LineIcon {
            name: "info"
            size: Theme.iconSm
            color: banner.toneColor
            Layout.alignment: Qt.AlignVCenter
        }
        Text {
            text: banner.text
            color: Theme.foreground
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceXs
            Layout.bottomMargin: Theme.spaceXs
        }
        RowLayout {
            id: actionRow
            spacing: Theme.spaceXs
        }
    }
}
