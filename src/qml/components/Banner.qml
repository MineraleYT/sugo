// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import io.github.mineraleyt.sugo

/// A message inside a page: `tone` "info", "warning" or "danger"; buttons go in as children.
/// A message that may come, go or change belongs in a MessageSlot, which keeps its place.
Rectangle {
    id: banner

    property string text
    property string tone: "info"
    default property alias actions: actionRow.data
    /// Lines of text at most (the rest is cut, and shown in a tooltip); 0: as many as it needs.
    property int maximumLineCount: 0
    /// The height of the buttons, for MessageSlot.
    readonly property real actionsHeight: actionRow.implicitHeight

    readonly property color toneColor: tone === "danger" ? Theme.danger : tone === "warning" ? Theme.warning : Theme.muted

    implicitHeight: row.implicitHeight + Theme.spaceMd * 2
    radius: Theme.radius
    color: tone === "danger" ? Theme.dangerFill : tone === "warning" ? Theme.warningFill : Theme.hover
    border.width: Theme.hairline
    border.color: tone === "info" ? Theme.border : Theme.alpha(toneColor, 0.35)
    Accessible.role: Accessible.AlertMessage
    Accessible.name: text
    QQC2.ToolTip.text: text
    QQC2.ToolTip.visible: hover.hovered && label.truncated
    QQC2.ToolTip.delay: Theme.toolTipDelay

    HoverHandler {
        id: hover
    }

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
            id: label
            text: banner.text
            color: Theme.foreground
            font.pixelSize: Theme.fontCaption
            wrapMode: Text.WordWrap
            maximumLineCount: banner.maximumLineCount > 0 ? banner.maximumLineCount : 1000000
            elide: Text.ElideRight
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
