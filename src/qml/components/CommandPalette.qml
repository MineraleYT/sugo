// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import io.github.mineraleyt.sugo

/**
 * Ctrl+K: type to find a page, a timer or a command, Enter to run it.
 * `commands` is an array of { text, hint, icon, timerIcon, run }.
 */
QQC2.Popup {
    id: commandPalette

    property var commands: []
    readonly property var matches: {
        const query = field.text.trim().toLowerCase();
        return commands.filter(c => query.length === 0 || c.text.toLowerCase().includes(query)
                                    || (c.hint || "").toLowerCase().includes(query));
    }

    function runAt(index: int): void {
        const command = matches[index];
        if (command) {
            close();
            command.run();
        }
    }

    parent: QQC2.Overlay.overlay
    x: (parent.width - width) / 2
    y: Theme.space4xl + Theme.spaceXl
    width: Math.min(Theme.contentMaxWidth * 0.75, parent.width - Theme.space2xl * 2)
    padding: 0
    modal: true
    focus: true
    onOpened: {
        field.text = "";
        list.currentIndex = 0;
        field.forceActiveFocus();
    }

    QQC2.Overlay.modal: Rectangle {
        color: Theme.scrim
    }
    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationShort }
    }

    background: Rectangle {
        radius: Theme.radiusLg
        color: Theme.raised
        border.width: Theme.hairline
        border.color: Theme.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spaceLg
            spacing: Theme.spaceMd
            LineIcon {
                name: "search"
                size: Theme.icon
                color: Theme.muted
            }
            TextInput {
                id: field
                Layout.fillWidth: true
                color: Theme.foreground
                font.pixelSize: Theme.fontTitle + 1
                selectionColor: Theme.alpha(Theme.brand, 0.35)
                Accessible.name: i18n("Search")
                Keys.onDownPressed: list.incrementCurrentIndex()
                Keys.onUpPressed: list.decrementCurrentIndex()
                Keys.onReturnPressed: commandPalette.runAt(list.currentIndex)
                Keys.onEnterPressed: commandPalette.runAt(list.currentIndex)
                onTextChanged: list.currentIndex = 0
                Text {
                    visible: field.text.length === 0
                    text: i18n("Search pages, timers and commands…")
                    color: Theme.subtle
                    font: field.font
                }
            }
            Kbd {
                text: i18nc("keyboard key", "Esc")
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.hairline
            color: Theme.border
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, Theme.navHeight * 9)
            Layout.margins: Theme.spaceXs
            clip: true
            model: commandPalette.matches
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: 0

            delegate: QQC2.AbstractButton {
                id: entry
                required property var modelData
                required property int index
                width: ListView.view.width
                implicitHeight: Theme.navHeight
                leftPadding: Theme.spaceMd
                rightPadding: Theme.spaceMd
                hoverEnabled: true
                text: modelData.text
                onHoveredChanged: if (hovered) list.currentIndex = index
                onClicked: commandPalette.runAt(index)
                background: Rectangle {
                    radius: Theme.radius
                    color: entry.ListView.isCurrentItem ? Theme.selected : "transparent"
                }
                contentItem: RowLayout {
                    spacing: Theme.spaceMd + Theme.hairline * 2
                    LineIcon {
                        visible: !entry.modelData.timerIcon
                        name: entry.modelData.icon || "chevron-right"
                        size: Theme.icon
                        color: Theme.muted
                    }
                    TimerIcon {
                        visible: !!entry.modelData.timerIcon
                        iconName: entry.modelData.timerIcon || ""
                        size: Theme.iconMd - Theme.hairline * 2
                    }
                    Text {
                        text: entry.modelData.text
                        color: Theme.foreground
                        font.pixelSize: Theme.fontTitle
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: entry.modelData.hint || ""
                        color: Theme.subtle
                        font.pixelSize: Theme.fontCaption
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: i18n("Nothing found")
                color: Theme.muted
                font.pixelSize: Theme.fontBody
            }
        }
        Item {
            Layout.fillWidth: true
            implicitHeight: list.count === 0 ? Theme.navHeight : Theme.spaceXs
        }
    }
}
