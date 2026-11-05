// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import stream.sugo.app

/// A line between two groups of rows in an SMenu, across the whole menu.
Item {
    implicitHeight: Theme.menuPadding * 2 + Theme.hairline
    Accessible.role: Accessible.Separator

    // From one side of the menu's frame to the other, over the menu's padding.
    Rectangle {
        x: -(Theme.menuPadding - Theme.hairline)
        width: parent.width + (Theme.menuPadding - Theme.hairline) * 2
        y: Theme.menuPadding
        height: Theme.hairline
        color: Theme.border
    }
}
