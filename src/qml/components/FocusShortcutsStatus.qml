// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import io.github.mineraleyt.sugo

/**
 * macOS: where "Install Shortcuts" got (AppSettings.installFocusShortcuts()), for the Focus card
 * of the settings and the welcome tour. Hidden until the button was clicked, and once both
 * shortcuts are found (`found`): the page says so itself.
 */
Banner {
    id: status

    /// Both Focus shortcuts are among the user's shortcuts.
    property bool found: false
    readonly property string installStatus: AppSettings.focusShortcutsInstallStatus

    objectName: "focusShortcutsInstallStatus"
    visible: installStatus.length > 0 && !(found && installStatus === "opened")
    tone: installStatus === "failed" ? "danger" : installStatus === "needsICloud" ? "warning" : "info"
    text: installStatus === "installing"
          ? i18n("Preparing the shortcuts…")
          : installStatus === "opened"
            ? i18n("The Shortcuts app asks you to add each of the two shortcuts: click Add Shortcut. Sugo finds them when you come back.")
            : installStatus === "needsICloud"
              ? i18n("Shortcuts can only add shortcuts from another app when this Mac is signed into iCloud. Sign in in System Settings, then try again, or create the two shortcuts yourself.")
              : installStatus === "failed"
                ? i18n("The shortcuts could not be installed (%1). Create them yourself in the Shortcuts app.", AppSettings.focusShortcutsInstallDetail)
                : ""
}
