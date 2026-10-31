// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

/**
 * A command of a page, shown by the TopBar as a button. Pages list theirs in
 * `property list<QtObject> actions: [PageAction { ... }]`; `iconName` is a LineIcon name.
 */
QtObject {
    id: action

    property string text
    property string iconName
    property string tooltip
    property bool enabled: true
    property bool visible: true
    signal triggered()

    /// Runs the action, unless it is disabled.
    function trigger(): void {
        if (action.enabled) {
            action.triggered();
        }
    }
}
