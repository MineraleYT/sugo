// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import io.github.mineraleyt.sugo

/**
 * The smoke run "inventory" (SUGO_SMOKE_PAGE=inventory, tests/check_platform_parity.py): goes
 * through every screen and lists what the user can see and use there. That covers the rows of
 * settings, buttons, switches, choices, fields and sliders, the entries of the search, and what
 * the platform says it supports. The list goes to Diagnostics.saveInventory(). The parity test
 * compares it with tests/platform-parity.json, so a feature that appears or goes away on one
 * platform only is noticed.
 *
 * Main.qml's smoke timer calls step() every 100 ms until it returns true.
 */
QtObject {
    id: inventory

    /// Main.qml's window: navigate(), pageStack, paletteCommands().
    property var app: null

    property var screens: []
    property int index: -1
    property int ticks: 0
    property var collected: ({})

    function plan(): var {
        const list = [
            { name: "timer", open: () => app.navigate("timer") },
            { name: "timer-page", open: () => app.navigate("preset", PresetModel.currentUuid) },
            { name: "new-timer", open: () => app.navigate("preset", "") },
            { name: "stats", open: () => app.navigate("stats") },
            { name: "about", open: () => app.navigate("about") }
        ];
        for (const category of ["general", "sounds", "focus", "calendar", "advanced"]) {
            list.push({ name: "settings/" + category, open: () => {
                app.navigate("settings");
                app.pageStack.currentItem.category = category;
            } });
        }
        for (let step = 0; step < 4; ++step) {
            list.push({ name: "welcome/" + step, open: () => {
                if (app.currentDestination !== "welcome") {
                    app.navigate("welcome");
                }
                app.pageStack.currentItem.current = step;
            } });
        }
        return list;
    }

    /// What a control is called: its name for screen readers, else its text.
    function nameOf(item: var): string {
        const accessible = item.Accessible ? item.Accessible.name : "";
        const text = typeof item.text === "string" ? item.text : "";
        return String(accessible || text || "").replace(/\s+/g, " ").trim();
    }

    /// One entry for what the user can use, or "" for anything else.
    function entryOf(item: var): string {
        // A row of settings or of the welcome tour: its title.
        if (item.descriptionVariants !== undefined && typeof item.title === "string" && item.title.length > 0) {
            return "row: " + item.title;
        }
        // A setting's switch: by the setting it changes, which does not depend on the wording.
        if (typeof item.setting === "string" && item.setting.length > 0) {
            return "setting: " + item.setting;
        }
        if (item.popup !== undefined && item.currentIndex !== undefined && item.model !== undefined) {
            return "choice: " + nameOf(item);
        }
        if (item.stepSize !== undefined && item.from !== undefined && item.to !== undefined) {
            return "slider: " + nameOf(item);
        }
        if (item.echoMode !== undefined && item.readOnly !== undefined) {
            return "field: " + (nameOf(item) || item.placeholderText || "");
        }
        if (item.checkable !== undefined && item.pressed !== undefined && typeof item.toggle === "function") {
            const name = nameOf(item);
            return name.length > 0 ? (item.checkable ? "switch: " : "button: ") + name : "";
        }
        return "";
    }

    function collect(root: var, into: var): void {
        if (!root || !root.visible) {
            return;
        }
        const entry = entryOf(root);
        if (entry.length > 0 && into.indexOf(entry) < 0) {
            into.push(entry);
        }
        const kids = root.children || [];
        for (let i = 0; i < kids.length; ++i) {
            collect(kids[i], into);
        }
    }

    /// One step of the run; true once the inventory is saved.
    function step(): bool {
        if (index < 0) {
            screens = plan();
            index = 0;
            ticks = 0;
            screens[0].open();
            return false;
        }
        // Three ticks for the page to load and lay out.
        if (++ticks < 3) {
            return false;
        }
        const entries = [];
        collect(app.pageStack.currentItem, entries);
        entries.sort();
        collected[screens[index].name] = entries;
        ticks = 0;
        if (++index < screens.length) {
            screens[index].open();
            return false;
        }
        // The search's commands; a keyboard shortcut is noted, not its keys (Ctrl or Cmd).
        collected["search"] = app.paletteCommands().map(command => "command: " + command.text + (command.hint ? " (shortcut)" : "")).sort();
        const report = {
            platform: DesktopStyle.macos ? "macos" : "linux",
            capabilities: {
                autostart: AppSettings.autostartSupported,
                externalControl: AppSettings.externalControlSupported,
                focusShortcuts: AppSettings.focusShortcutsSupported,
                updateInstall: UpdateChecker.canInstall,
                nativeTitleBar: DesktopStyle.nativeChrome
            },
            screens: collected
        };
        if (!Diagnostics.saveInventory(JSON.stringify(report, null, 1))) {
            console.error("Cannot save the inventory: is SUGO_INVENTORY set?");
            Qt.exit(48);
        }
        return true;
    }
}
