// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

/**
 * A modal question: a title, an explanation and two buttons. `confirmed()` follows the main
 * button; the other one (or Escape) just closes. Extra content goes between text and buttons.
 */
QQC2.Dialog {
    id: dialog

    property string text
    property string confirmText: i18n("OK")
    property string cancelText: i18n("Cancel")
    /// "primary" or "danger".
    property string confirmKind: "primary"
    property bool confirmEnabled: true
    default property alias content: extra.data
    signal confirmed()

    parent: QQC2.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    width: Math.min(Theme.sheetWidth, (parent ? parent.width : Theme.sheetWidth) - Theme.spaceLg * 2)
    padding: Theme.sheetPadding

    QQC2.Overlay.modal: Rectangle {
        color: Theme.scrim
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationShort }
        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.durationShort; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationShort }
    }

    background: Rectangle {
        radius: Theme.radiusXl
        color: Theme.card
        border.width: Theme.hairline
        border.color: Theme.border
    }

    header: null
    footer: null

    contentItem: ColumnLayout {
        spacing: Theme.spaceLg

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceXs - Theme.hairline
            Text {
                text: dialog.title
                color: Theme.foreground
                font.pixelSize: Theme.fontTitle + 1
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Text {
                visible: dialog.text.length > 0
                text: dialog.text
                color: Theme.muted
                font.pixelSize: Theme.fontBody
                lineHeight: 1.15
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        ColumnLayout {
            id: extra
            visible: children.length > 0
            Layout.fillWidth: true
            spacing: Theme.spaceLg
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spaceMd
            Item {
                Layout.fillWidth: true
            }
            SButton {
                kind: "ghost"
                text: dialog.cancelText
                onClicked: dialog.close()
            }
            SButton {
                kind: dialog.confirmKind
                text: dialog.confirmText
                enabled: dialog.confirmEnabled
                onClicked: dialog.confirmed()
            }
        }
    }
}
