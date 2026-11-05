// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Layouts
import io.github.mineraleyt.sugo

/**
 * The timers to choose from, over the composer's timer button: a search field, the timers by
 * category (those without one under "Other"), then New timer and Edit timers…. Choosing a timer
 * makes it the one in use; its page does not open.
 *
 * `app` is the window (Main.qml), for chooseTimer(), newTimer() and openSettings().
 */
SMenu {
    id: timerMenu
    objectName: "timerMenu"

    property var app: null
    /// What the search field holds.
    readonly property string query: searchField.text.trim().toLowerCase()
    // Read again each time the menu opens (names and categories change while it is closed).
    property int revision: 0

    /// The rows: { header: true, text } before each category's timers, { uuid, text, index } for a timer.
    readonly property var entries: {
        void revision;
        void PresetModel.count;
        const groups = [];
        const byName = {};
        const other = [];
        let categorised = false;
        for (let i = 0; i < PresetModel.count; ++i) {
            const preset = PresetModel.get(PresetModel.uuidAt(i));
            const name = String(preset.name ?? "");
            const category = String(preset.category ?? "").trim();
            categorised = categorised || category.length > 0;
            if (query.length > 0 && !name.toLowerCase().includes(query)) {
                continue;
            }
            const timer = { header: false, uuid: preset.uuid, text: name, index: i };
            if (category.length === 0) {
                other.push(timer);
            } else {
                if (!(category in byName)) {
                    byName[category] = [];
                    groups.push(category);
                }
                byName[category].push(timer);
            }
        }
        // Headers only when some timer has a category: one group alone needs no name.
        const list = [];
        for (const category of groups) {
            list.push({ header: true, text: category });
            list.push(...byName[category]);
        }
        if (other.length > 0) {
            if (categorised) {
                list.push({ header: true, text: i18nc("the timers without a category", "Other") });
            }
            list.push(...other);
        }
        return list;
    }

    side: "top"
    align: "start"

    function choose(uuid: string): void {
        close();
        if (app) {
            app.chooseTimer(uuid);
        }
    }

    function highlightFirstMatch(): void {
        highlightedItem = null;
        moveHighlight(1);
    }

    onAboutToShow: {
        searchField.text = "";
        ++revision;
        // The rows were just made again: light the timer in use.
        const kids = rowsColumn.children;
        for (let i = 0; i < kids.length; ++i) {
            if (kids[i].isMenuItem === true && kids[i].checked) {
                highlightedItem = kids[i];
            }
        }
    }
    onOpened: searchField.forceActiveFocus()
    onQueryChanged: Qt.callLater(highlightFirstMatch)

    // The search field, across the top of the menu with a line under it.
    Item {
        implicitWidth: searchRow.implicitWidth + Theme.spaceMd * 2
        implicitHeight: Theme.menuRowHeight + Theme.spaceSm

        RowLayout {
            id: searchRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spaceMd
            anchors.rightMargin: Theme.spaceMd
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: -Theme.hairline * 2
            spacing: Theme.spaceMd
            LineIcon {
                name: "search"
                size: Theme.iconSm
                color: Theme.subtle
            }
            TextInput {
                id: searchField
                objectName: "timerMenuSearch"
                Layout.fillWidth: true
                color: Theme.foreground
                font.pixelSize: Theme.fontBody
                selectionColor: Theme.alpha(Theme.accent, 0.35)
                clip: true
                Accessible.role: Accessible.EditableText
                Accessible.name: i18n("Search timers")
                Keys.onUpPressed: timerMenu.moveHighlight(-1)
                Keys.onDownPressed: timerMenu.moveHighlight(1)
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (timerMenu.highlightedItem && timerMenu.highlightedItem.isMenuItem === true) {
                            timerMenu.highlightedItem.activate();
                        }
                        event.accepted = true;
                    }
                }
                Text {
                    visible: searchField.text.length === 0
                    text: i18n("Search timers")
                    color: Theme.subtle
                    font: searchField.font
                }
            }
        }
        // From one side of the menu's frame to the other, over the menu's padding.
        Rectangle {
            x: -(Theme.menuPadding - Theme.hairline)
            width: parent.width + (Theme.menuPadding - Theme.hairline) * 2
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spaceXs
            height: Theme.hairline
            color: Theme.border
        }
    }

    Repeater {
        model: timerMenu.entries

        // A category's caption, or a timer: for the menu (highlight, arrow keys, Enter) a timer's
        // row is this item, which passes everything on to its SMenuItem.
        Item {
            id: entry
            required property var modelData
            readonly property bool isMenuItem: !modelData.header
            readonly property bool checked: isMenuItem && modelData.uuid === PresetModel.currentUuid
            property var menu: null

            function activate(): void {
                row.activate();
            }

            implicitWidth: modelData.header ? header.implicitWidth : row.implicitWidth
            implicitHeight: modelData.header ? header.implicitHeight : row.implicitHeight

            SMenuHeader {
                id: header
                visible: entry.modelData.header
                width: parent.width
                text: entry.modelData.header ? entry.modelData.text : ""
            }
            SMenuItem {
                id: row
                objectName: entry.isMenuItem ? "timerMenuEntry-" + entry.modelData.index : ""
                visible: entry.isMenuItem
                width: parent.width
                text: entry.isMenuItem ? entry.modelData.text : ""
                checked: entry.checked
                highlighted: timerMenu.highlightedItem === entry
                Accessible.checkable: true
                onHoveredChanged: {
                    if (hovered) {
                        timerMenu.highlightedItem = entry;
                    }
                }
                onTriggered: timerMenu.choose(entry.modelData.uuid)
            }
        }
    }

    SMenuSeparator {}

    SMenuItem {
        objectName: "timerMenuNewTimer"
        iconName: "plus"
        text: i18n("New timer")
        onTriggered: {
            if (timerMenu.app) {
                timerMenu.app.newTimer();
            }
        }
    }
    SMenuItem {
        objectName: "timerMenuEditTimers"
        iconName: "edit"
        text: i18n("Edit timers…")
        onTriggered: {
            if (timerMenu.app) {
                timerMenu.app.openSettings("timers");
            }
        }
    }
}
