<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# Sugo manual review checklist

Run these checks in a fresh user profile where practical. Record the application version, desktop environment, package format, and any failure with each review.

## Timer and presets

- [ ] Create a timer with a name, category, icon, work duration, break durations, and long-break interval. Save it and confirm the displayed values.
- [ ] Edit a user timer, duplicate it, select each resulting timer, and delete the duplicate.
- [ ] Confirm built-in timers cannot be deleted and can be reset to their defaults.
- [ ] On a timer's page, hover the disabled Start while another timer runs, then with unsaved changes: a tooltip says why each time. Start and stop that timer: Start/Running keeps its width, and Duplicate and Delete stay in place.
- [ ] Start, pause, resume, skip, and stop a work phase. Confirm the remaining time and phase label follow each action.
- [ ] Give a timer a category of 100 characters and start it in a narrow window: "Focusing · …" is cut with an ellipsis inside its pill, which is never wider than the cycle track.
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
- [ ] Record only breaks in a day (skip or interrupt the work under the minimum): the day's chart shows the green break bars, and "No focus time in this period yet." stays hidden.
- [ ] Back up the database, change presets and history, restore the backup, and confirm both are restored.
- [ ] Try restoring an invalid or newer-schema backup; confirm it is rejected without replacing current data.
- [ ] Clear session history and confirm presets remain intact.
- [ ] Quit, overwrite `sugo.db` with junk, start again: a warning names the `sugo.db.corrupt-*` file it kept, "Open folder" shows it, the close button hides the warning, and the page under it does not move.

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

## Sugo Sync (dev server)

Run the sync server locally (`sugo-sync/worker`: `npm run db:migrate:local`, `npx wrangler dev`, with `CLERK_DEV_BYPASS=1` in `.dev.vars`) and start each copy of Sugo with `SUGO_SYNC_URL=http://localhost:8787` and its own `SUGO_DATA_DIR`. Approve a sign-in code with `curl -X POST localhost:8787/v1/auth/device/approve -H 'Authorization: Bearer dev:user_local' -H 'Content-Type: application/json' -d '{"user_code":"…"}'`. Two copies on one Linux desktop need separate session buses (one Sugo per bus), each with a keyring.

- [ ] Signed out, Settings → Sync shows the description, the device name (the host name) and Sign in. Sign in: the code appears large; "Open in browser" opens the verification page with the code, "Copy" copies it and reads "Copied" for a few seconds without moving anything; Cancel goes back.
- [ ] Approve the code: the first device shows "Keep your recovery code"; "Done" stays off until the asked group is typed (any case, O for 0); then the tab shows "Up to date" and the last sync, the device as "This device", the recovery code and Sign out.
- [ ] Sign in a second copy: it shows a 6-digit code. The first copy asks "Approve …?" with the same code (with its window hidden in the tray, a notification comes first; clicking it opens Settings → Sync). Approve: the second copy becomes "Up to date". "Not Now" leaves it waiting until the next start.
- [ ] A third copy, with the free plan's 2 devices taken: after approving its code on the site it says to disconnect a device on sugo.stream, and goes on by itself once one is disconnected.
- [ ] The second copy uses the recovery code instead of an approval: a wrong code says so; the right one opens the timers.
- [ ] Change, add and delete a timer on one copy: after "Sync now" (or within a minute of activating the window) the other copy shows the change; a deleted timer stays deleted.
- [ ] Stop the server: "Sync now" shows "Offline" with a message in its place, nothing moves, and the timers keep working; start it again and sync.
- [ ] Without a keyring (Linux without Secret Service, `DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null`; an unsigned Mac from SSH): Sign in is refused with "Sugo Sync needs the system keyring" and the button turns off; nothing is sent to the server.
- [ ] On macOS, after updating an ad hoc signed build while linked: one Keychain question for the sync (its single item), a few seconds after the start; while signed out, starting never asks.
- [ ] Show the recovery code and copy it; "Create a new one…" warns that the old code stops working and then asks to confirm the new one.
- [ ] Disconnect the other device: it asks first, and the other copy is signed out with "This device was disconnected". Sign out asks first and says the timers stay; with the server stopped, a toast says to disconnect the device on sugo.stream. The timers are still there afterwards.
- [ ] In a narrow window (about 400 px) and in light and dark colours, every state of the Sync tab fits, and the category tabs scroll sideways.
- [ ] Settings → Advanced → diagnostics: the Sync section has the state, error, keyring, linked, last sync, device count and server (default/custom), and no names, ids, codes or tokens.

## No layout shift

- [ ] On every page, nothing moves when a message appears, changes or goes away: the welcome tour's setup step (a login entry error), the timer page (paused by the screen lock: the banner appears over the clock), About (check for updates, an update found, downloading, ready, an error), Settings → Calendar (log in, a failed test), Settings → Sync (no keyring, the device limit, offline), Settings → Advanced (export the diagnostics: a toast; "Copy to Clipboard" → "Copied" keeps the buttons in place), the statistics (periods with and without interrupted sessions), and the window-wide "cannot be saved" warning (laid over the top of the page).
- [ ] On a timer's page, make silencing notifications available or unavailable while the page is open (macOS: install or remove the Focus shortcuts; Linux: start or stop the notification inhibitor service): the "Silence notifications while working" row and those below do not move.

## Window and dialogs

- [ ] Start a work phase with "Pause timer when screen is locked" on, lock and unlock the screen: "Screen Unlocked" asks to resume. Resume from the tray menu (or the global shortcut, or KRunner): the dialog closes by itself. With a task note being asked for when the screen is unlocked, the resume question comes only after the note dialog is closed.
- [ ] Make the window narrow, open the menu (the sidebar slides in), then maximise or widen the window: the drawer closes and only the sidebar is shown, with the page not dimmed.
- [ ] With a timer running, open Statistics and double-click the status pill in the top bar: the timer page opens and the window is neither maximised nor restored. A double-click on an empty part of the top bar still maximises and restores it.
- [ ] With eight or more timers and an update found (AppImage, or the macOS app), make the window as short as it goes: the timer list shrinks and scrolls, and Settings and About stay inside the window, in the sidebar and in the menu of a narrow window.
- [ ] On a timer's page and in Settings, Tab into a number field (e.g. "Work") and press Shift+Tab: the focus goes back to the control before it and is not kept in the field.
- [ ] Maximise the window, minimise it, then click the tray icon (or launch Sugo again): the window comes back maximised.
- [ ] macOS: minimise the window (Cmd+M), then choose Window › Sugo in the menu bar: the window comes out of the Dock, maximised (zoomed) again if it was.
- [ ] macOS: start a work phase, close the window (Cmd+W), then press Cmd+Q (or choose Sugo › Quit Sugo): the window comes back with "Quit Sugo?". Do the same with the window minimised.
