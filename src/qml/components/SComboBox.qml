// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

/// A drop-down list in the style of the other controls.
QQC2.ComboBox {
    id: combo

    implicitWidth: Math.max(Theme.space4xl * 3.5, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Theme.controlHeight
    leftPadding: Theme.spaceMd + Theme.hairline * 2
    rightPadding: Theme.controlHeight
    font.pixelSize: Theme.fontBody
    hoverEnabled: true

    background: Rectangle {
        radius: Theme.radius
        // hover and pressed are translucent: lay them over the field's colour, not in place of it.
        color: combo.down || combo.popup.visible ? Qt.tint(Theme.background, Theme.pressed)
                                                 : (combo.hovered ? Qt.tint(Theme.background, Theme.hover) : Theme.background)
        border.width: Theme.hairline
        border.color: combo.visualFocus ? Theme.focusRing : Theme.border
    }

    contentItem: Text {
        text: combo.displayText
        font: combo.font
        color: combo.enabled ? Theme.foreground : Theme.subtle
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: LineIcon {
        x: combo.width - width - Theme.spaceMd
        y: (combo.height - height) / 2
        name: "chevron-down"
        size: Theme.iconSm
        color: Theme.muted
    }

    delegate: QQC2.ItemDelegate {
        id: entry
        required property var model
        required property int index
        width: ListView.view.width
        implicitHeight: Theme.controlHeight
        highlighted: combo.highlightedIndex === index
        hoverEnabled: true
        contentItem: Text {
            text: entry.model[combo.textRole] ?? entry.model.modelData ?? ""
            color: Theme.foreground
            font: combo.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Theme.radiusSm
            color: entry.highlighted || entry.hovered ? Theme.hover : "transparent"
            LineIcon {
                visible: combo.currentIndex === entry.index
                anchors.right: parent.right
                anchors.rightMargin: Theme.spaceMd
                anchors.verticalCenter: parent.verticalCenter
                name: "check"
                size: Theme.iconSm
                color: Theme.foreground
            }
        }
    }

    popup: QQC2.Popup {
        y: combo.height + Theme.spaceXs
        width: Math.max(combo.width, Theme.space4xl * 4)
        padding: Theme.spaceXs
        implicitHeight: Math.min(contentItem.implicitHeight + padding * 2, Theme.contentMaxWidth / 2)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: combo.popup.visible ? combo.delegateModel : null
            currentIndex: combo.highlightedIndex
            QQC2.ScrollIndicator.vertical: QQC2.ScrollIndicator {}
        }
        background: Rectangle {
            radius: Theme.radiusMd
            color: Theme.raised
            border.width: Theme.hairline
            border.color: Theme.borderStrong
        }
    }
}
