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

## macOS: Keychain

Secrets (the calendar password; the sync's token and key once the sync is in) are items of the login Keychain, label "Sugo", service `io.github.mineraleyt.sugo`. Each item trusts the build that wrote it, and an ad hoc signed build is known by its code hash, so every update is a new build for the Keychain. Use two different builds of the app (a release and the next nightly, or two local builds of different commits), each replacing the other at the same path, as "Install update" does.

- [ ] With build 1, save a calendar password (Settings → Calendar) and restart: no question from macOS.
- [ ] Replace it with build 2 and start it: macOS asks once per stored secret to let Sugo use it. Note what the dialog asks for (Allow / Always Allow, or the login password) and that Sugo's window does not respond until it is answered. Choose "Always Allow", restart: no question.
- [ ] With build 2, change the calendar password, restart: no question (the item is rewritten as build 2's). Go back to build 1: it asks again, as for any update.
- [ ] After an update, answer "Deny" (start Sugo from a terminal, `Sugo.app/Contents/MacOS/Sugo`, to see its messages): Sugo goes on without the password (the calendar shows it as missing), nothing hangs, and the terminal shows the OSStatus of the refusal once (`Cannot read "calendar-caldav" from the Keychain, OSStatus -128`: "Deny" gave -128 on macOS 27; note it if it differs) followed by "Access to … was refused". Then finish two work sessions with the calendar on and use "Test connection": macOS does not ask again. Enter the password again: it is saved without a question and the queued sessions go out. Quit and start again: macOS asks once more (a new start asks again).
- [ ] Without a usable Keychain (start `Sugo.app/Contents/MacOS/Sugo` from an SSH session, which has no login session: every Keychain call fails at once with OSStatus -25308, no dialog): Sugo starts normally; once the sync is in, Settings → Sync says it needs the system keyring and stays off (sync-plan S4), the same as on a Linux desktop without Secret Service.
- [ ] Once the sync is in: sign in, update the app (build 1 → 2), let a sync run: at most one question per secret, then syncing goes on; "Sign out" removes Sugo's sync items from Keychain Access.

## No layout shift

- [ ] On every page, nothing moves when a message appears, changes or goes away: the welcome tour's setup step (a login entry error), the timer page (paused by the screen lock: the banner appears over the clock), About (check for updates, an update found, downloading, ready, an error), Settings → Calendar (log in, a failed test), Settings → Advanced (export the diagnostics: a toast; "Copy to Clipboard" → "Copied" keeps the buttons in place), the statistics (periods with and without interrupted sessions), and the window-wide "cannot be saved" warning (laid over the top of the page).
