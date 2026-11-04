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

- [ ] On a Mac signed into iCloud, click "Install Shortcuts" (Settings → Focus while working, or the welcome tour's setup step). Shortcuts shows an "Add Shortcut" sheet for "Sugo Focus On" and then one for "Sugo Focus Off" (check the second sheet appears after the first is confirmed, not lost). After adding both and returning to Sugo, the card says both were found.
- [ ] Run each installed shortcut once in Shortcuts: "On" turns on Do Not Disturb until turned off, "Off" turns it off.
- [ ] On a Mac not signed into iCloud, "Install Shortcuts" says to sign into iCloud, and "Create them yourself" opens with the "Open Shortcuts" button.
