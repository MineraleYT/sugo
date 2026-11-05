// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import stream.sugo.app

/// A small caption over a group of rows in an SMenu ("Start automatically").
Item {
    id: header

    property string text

    implicitWidth: caption.implicitWidth + Theme.spaceMd * 2
    implicitHeight: Theme.menuHeaderHeight
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    Text {
        id: caption
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spaceMd
        anchors.rightMargin: Theme.spaceMd
        anchors.verticalCenter: parent.verticalCenter
        text: header.text
        font.pixelSize: Theme.fontSmall
        font.weight: Font.Medium
        color: Theme.muted
        elide: Text.ElideRight
    }
}
