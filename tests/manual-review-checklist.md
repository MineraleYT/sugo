<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Sugo manual review checklist

Run these checks in a fresh user profile where practical. Record the application version, desktop environment, package format, and any failure with each review.

## Timer and presets

- [ ] Create a timer with a name, category, icon, work duration, break durations, and long-break interval. Save it and confirm the displayed values.
- [ ] Edit a user timer, duplicate it, select each resulting timer, and delete the duplicate.
- [ ] Confirm built-in timers cannot be deleted and can be reset to their defaults.
- [ ] Start, pause, resume, skip, and stop a work phase. Confirm the remaining time and phase label follow each action.
- [ ] Use short durations to cross a work-to-break boundary and a long-break boundary. Confirm the session count and next phase.
- [ ] Check automatic phase start in each supported mode, Manual, Breaks only and All phases.
- [ ] Try the minimum and maximum supported durations, zero-length breaks, and a long-break interval of zero.

## Notifications and desktop integration

- [ ] Confirm phase-end notifications and sounds follow the selected timer's settings.
- [ ] Enable notification inhibition and screen-sleep inhibition during work, then verify both are released on pause, stop, and phase completion.
- [ ] Test the focus sound and its volume, then disable playback and confirm it stops.
- [ ] Open the system-tray menu and verify timer controls, current phase, and show/hide behavior.
- [ ] Enable login startup and verify the desktop entry is created. Confirm the setting is reported as unavailable in Flatpak.
- [ ] Call the documented D-Bus `status`, `start`, `pause`, `skip`, `stop`, and `toggle` methods, and the timer ones (`presets`, `selectPreset`, `nextPreset`, `previousPreset`, `startPreset`), including the refusal while a timer is running.
- [ ] Launch a second instance and confirm it activates the existing window instead of opening another database-backed instance.

## Data and statistics

- [ ] Complete and interrupt sessions; confirm completed and interrupted records appear with the correct durations.
- [ ] Check daily totals, streaks, previous/next period navigation, and chart ranges across a day boundary.
- [ ] Back up the database, change presets and history, restore the backup, and confirm both are restored.
- [ ] Try restoring an invalid or newer-schema backup; confirm it is rejected without replacing current data.
- [ ] Clear session history and confirm presets remain intact.

## Packaging and first run

- [ ] Install from the native build and from the Flatpak. Confirm the desktop entry, icon, About links, notifications, tray, and audio behavior.
- [ ] Launch with an existing database and confirm presets and settings remain available.
- [ ] Launch Flatpak with Wayland and X11 where available; verify audio, notifications, tray registration, and screen-sleep inhibition.

## macOS: Focus shortcuts

- [ ] On a Mac (signed into iCloud or not), click "Install Focus Shortcuts" in the welcome tour's setup step (Settings → Advanced → Show the Welcome Tour). Shortcuts shows the "Add Shortcut" sheet of "Sugo Focus On"; the step says to add it. Only after clicking Add Shortcut does the sheet of "Sugo Focus Off" appear (not lost behind the first); after adding it the row says the shortcuts are installed. Nothing on the step moves while the messages change.
- [ ] Settings → Focus while working: with the shortcuts deleted in Shortcuts, "Install Shortcuts" does the same, with the status in the row's description (the card keeps its height, the text stays centred); with one already there, only the other one's sheet appears; with both, the button is off.
- [ ] Close a sheet without adding the shortcut: after three minutes the row says installing failed.
- [ ] With a timer whose "Silence notifications while working" is on, start it: Do Not Disturb turns on; pause or stop it: it turns off. With the option off, nothing happens.
- [ ] Run each installed shortcut once in Shortcuts: "On" turns on Do Not Disturb until turned off, "Off" turns it off.

## No layout shift

- [ ] On every page, nothing moves when a message appears, changes or goes away: the welcome tour's setup step (a login entry error), the timer page (paused by the screen lock: the banner appears over the clock), About (check for updates, an update found, downloading, ready, an error), Settings → Calendar (log in, a failed test), Settings → Advanced (export the diagnostics: a toast; "Copy to Clipboard" → "Copied" keeps the buttons in place), the statistics (periods with and without interrupted sessions), and the window-wide "cannot be saved" warning (laid over the top of the page).

## Window and dialogs

- [ ] Start a work phase with "Pause timer when screen is locked" on, lock and unlock the screen: "Screen Unlocked" asks to resume. Resume from the tray menu (or the global shortcut, or KRunner): the dialog closes by itself. With a task note being asked for when the screen is unlocked, the resume question comes only after the note dialog is closed.
- [ ] Make the window narrow, open the menu (the sidebar slides in), then maximise or widen the window: the drawer closes and only the sidebar is shown, with the page not dimmed.
- [ ] With a timer running, open Statistics and double-click the status pill in the top bar: the timer page opens and the window is neither maximised nor restored. A double-click on an empty part of the top bar still maximises and restores it.
- [ ] With eight or more timers and an update found (AppImage, or the macOS app), make the window as short as it goes: the timer list shrinks and scrolls, and Settings and About stay inside the window, in the sidebar and in the menu of a narrow window.
- [ ] On a timer's page and in Settings, Tab into a number field (e.g. "Work") and press Shift+Tab: the focus goes back to the control before it and is not kept in the field.
