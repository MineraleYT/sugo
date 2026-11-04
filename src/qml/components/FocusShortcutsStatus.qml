// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import io.github.mineraleyt.sugo

/**
 * macOS: where "Install Shortcuts" got (AppSettings.installFocusShortcuts()), as a message for a
 * MessageSlot of the settings' Focus card or of the welcome tour. `active` while there is
 * something to say: from the click until both shortcuts are there, or after a failure.
 */
QtObject {
    /// Both Focus shortcuts are among the user's shortcuts.
    property bool found: false
    readonly property string installStatus: AppSettings.focusShortcutsInstallStatus

    readonly property bool active: installStatus.length > 0 && !(installStatus === "done" && found)
    readonly property string tone: installStatus === "failed" ? "danger" : "info"
    readonly property string text: {
        switch (installStatus) {
        case "installing":
            return i18n("Preparing the shortcuts…");
        case "waitingOn":
            return i18n("Shortcuts asks you to add \"%1\": click Add Shortcut.", AppSettings.focusOnShortcut);
        case "waitingOff":
            return i18n("Now add \"%1\" too: click Add Shortcut.", AppSettings.focusOffShortcut);
        case "done":
            return i18n("Both shortcuts are installed.");
        case "failed":
            return i18n("The shortcuts could not be installed (%1). Create them yourself in the Shortcuts app.",
                        AppSettings.focusShortcutsInstallDetail);
        }
        return "";
    }
}
