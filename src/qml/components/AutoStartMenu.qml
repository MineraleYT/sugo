// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import stream.sugo.app

/// How the phases of the timer in use start, over the composer's button: by hand, the breaks by
/// themselves, or all of them. The choice is the timer's option "Start automatically".
SMenu {
    id: autoStartMenu
    objectName: "autoStartMenu"

    /// The timer's autoStartMode: 0 Manual, 1 Breaks only, 2 All phases.
    property int mode: 0

    side: "top"
    align: "start"
    fixedWidth: Theme.menuAutoStartWidth

    function choose(value: int): void {
        PresetModel.setOption(PresetModel.currentUuid, "autoStartMode", value);
    }

    SMenuHeader {
        text: i18n("Start automatically")
    }
    SMenuItem {
        objectName: "autoStartEntry-0"
        text: i18n("Manual")
        description: i18nc("the start mode Manual", "You start every phase.")
        checked: autoStartMenu.mode === 0
        onTriggered: autoStartMenu.choose(0)
    }
    SMenuItem {
        objectName: "autoStartEntry-1"
        text: i18n("Breaks only")
        description: i18nc("the start mode Breaks only", "Breaks start by themselves.")
        checked: autoStartMenu.mode === 1
        onTriggered: autoStartMenu.choose(1)
    }
    SMenuItem {
        objectName: "autoStartEntry-2"
        text: i18n("All phases")
        description: i18nc("the start mode All phases", "Work and breaks follow each other.")
        checked: autoStartMenu.mode === 2
        onTriggered: autoStartMenu.choose(2)
    }
}
