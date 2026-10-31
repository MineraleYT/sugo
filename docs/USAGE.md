<!--
  SPDX-FileCopyrightText: 2026 Sugo contributors
  SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Sugo user guide

This guide describes how to use Sugo day to day. For installation see the [README](../README.md); to build or contribute see the [development guide](DEVELOPMENT.md).

**Contents**

1. [Timers](#timers)
2. [The tray icon and its menu](#the-tray-icon-and-its-menu)
3. [Keyboard shortcuts](#keyboard-shortcuts)
4. [Statistics](#statistics)
5. [Task notes](#task-notes)
6. [Calendar events](#calendar-events)
7. [KRunner](#krunner)
8. [D-Bus and scripting](#d-bus-and-scripting)
9. [Data, backup and privacy](#data-backup-and-privacy)
10. [Updates and diagnostics](#updates-and-diagnostics)
11. [Flatpak notes](#flatpak-notes)

---

## Timers

A *timer* (preset) bundles the durations of a work phase, a short break and a long break, how many work sessions come before a long break, and a few options. Sugo starts with one timer, **Pomodoro** (25/5, a 15-minute break after four sessions), which silences desktop notifications while you work; edit it, or add timers of your own with **New timer** in the sidebar.

- **Name, category and icon** appear in the timer list, the tray menu and the statistics. The category is also used as the label of the phase chip while you work.
- **Auto-start** decides what happens when a phase ends: *Manual* (nothing starts by itself), *Breaks only*, or *All phases*.
- **A long break of 0 minutes** means no long break: the set still ends there, with a short break instead. Set both breaks to 0 to go straight from one work session to the next.
- **Silence notifications while working**, **play a sound** and **show a notification** when a phase ends can be set per timer.
- The timer you pick stays selected the next time you start Sugo. You cannot switch timers while one is running.

Pausing keeps the remaining time; stopping resets the phase. A phase stopped early is recorded as *interrupted* in the statistics if it ran for at least five seconds.

Skipping a running work session counts it towards the long break. Skipping a work session that has not started yet (for example with the notification's **Skip** button) moves on to the short break without counting it, so the long break is only reached by working.

When the computer goes to sleep, a running phase is paused, and it stays paused after the computer wakes up until you resume it: the time asleep never counts as work or as a break.

---

## The tray icon and its menu

The tray icon shows a ring with the minutes left (red while working, green during breaks). Its context menu contains, from the top:

1. **A status line** (not clickable): the phase, the time left and the name of the timer, for example `Work · 12:30 · Pomodoro`.
2. **Start / Pause / Resume**, and **Stop** and **Skip**, which appear only while a phase is running or paused.
3. **Timer**: a submenu with all your timers; pick one to switch to it. It is disabled while a timer is running.
4. **Statistics** and **Settings**, which open the window on that page.
5. **Show Window / Hide Window** and **Quit**.

Closing the window keeps Sugo running in the tray if *Settings → System tray → Keep running in the tray when the window is closed* is on. If your desktop has no system tray, the tray options are disabled and closing the window quits the app.

---

## Keyboard shortcuts

| Shortcut | Action |
|:---|:---|
| `Space` | Start or pause the timer (on the Focus page, when nothing else has the focus) |
| `S` | Skip to the next phase (on the Focus page, while a phase runs) |
| `Ctrl` + `1` … `4` | Go to Focus, Statistics, Settings, About |
| `Ctrl` + `K` | Search pages, timers and commands |
| `Ctrl` + `N` | New timer |
| `Ctrl` + `,` | Open Settings |
| `Ctrl` + `S` / `Esc` | Save / drop the changes on a timer's page |
| `Ctrl` + `Q` | Quit |

For shortcuts that work from anywhere, bind the [D-Bus commands](#d-bus-and-scripting) in **System Settings → Shortcuts → Add Command**.

---

## Statistics

The **Statistics** page offers day, week, month and year views with:

- work and break time, completed and interrupted sessions;
- how the work time compares with the same stretch of the period before;
- your streak of days in a row; with *weekend streak protection* an inactive Saturday or Sunday does not break the streak;
- a chart of the period and a distribution by timer, category or task.

Sessions are counted on the day they started, in your time zone. The page follows the clock: after midnight it moves on to the new day.

---

## Task notes

When a work session ends, Sugo can ask what you worked on (**Settings → Focus → Streak and notes → Prompt for a task note when a work session finishes**). Type it in the notification's reply field or in the in-app dialog, which also suggests recent notes. Notes are stored with the session and shown in the "Task" view of *Where the time went* in the statistics.

---

## Calendar events

Sugo can add an event to your calendar every time you **finish a work session**, for example "Pomodoro, 25 minutes". Breaks and interrupted phases are not sent. It is **off by default** and set up in **Settings → Calendar**.

### With Nextcloud

1. Choose **Nextcloud** as the calendar service and type your server address (for example `https://cloud.example.org`).
2. Press **Log in** and authorize Sugo in the browser tab that opens. Sugo receives an *app password* by itself; you do not copy anything.
3. If you have several calendars, pick one in the **Calendar** list. The refresh button next to it reloads the list from the server.
4. Press **Test** to check the connection: a green check on the button means it works.

In Nextcloud (*Settings → Security → Devices & sessions*) the new entry is named **Sugo**. Delete it there to revoke Sugo's access, and press **Disconnect** in Sugo to forget the login.

### With another CalDAV calendar

Choose **Custom CalDAV** and enter the calendar's CalDAV address, your user name and a password (use an *app password* if your provider offers them). Press the refresh button to list the calendars at that address and pick one. Nextcloud and Custom CalDAV keep **separate settings**, so switching between them does not mix up addresses or passwords.

### Good to know

- **Subscription links do not work.** A calendar link ending in `.ics` is read-only: it lets you *see* events, not add them. Use the calendar's CalDAV address.
- **What is sent:** the timer name, its category and the real start and end times. The task note goes in the event description only if you turn on *Include the task note in the event description*.
- **Secure connections only:** addresses must be `https://` (plain `http://` is accepted only for `localhost`). Certificate errors are never ignored.
- **If the server is unreachable**, events wait in a queue (up to 100) and Sugo retries every few minutes while it is running. The queue is kept when you quit and sent at the next start. Each waiting event belongs to the account that was set up when it was recorded: if you sign in with another account, the old account's waiting events are dropped instead of being sent to the new one. Work sessions that end after you turn the calendar on but before you finish signing in also wait, and go to the account you then set up (unless it is another user name than the one you had entered).
- **Where the password is kept:** in your desktop keyring (KWallet on Plasma, or any other Secret Service keyring). Only when no keyring is available is it kept in `~/.config/sugo/sugo.conf`, a file readable only by you; it moves to the keyring by itself the next time one is available. It is never written to logs or to the diagnostics report.
- **Not supported:** Proton Calendar (to our knowledge it offers no way for other apps to add events), and calendars that only accept an OAuth sign-in, such as Google Calendar or Microsoft 365.

---

## KRunner

Sugo adds a runner to the Plasma launcher. Press `Alt` + `Space` (or `Meta`) and type:

| Query | What it does |
|:---|:---|
| `pomodoro`, `sugo`, `tomato` or `timer` | Shows the current status, the time left and quick actions |
| `pomodoro start` | Start or resume the timer |
| `pomodoro pause` / `pomodoro resume` | Pause or continue the countdown |
| `pomodoro stop` | Stop and reset |
| `pomodoro skip` | Go to the next phase |
| `pomodoro <timer name>` (or part of it) | Switch to that timer and start it |
| `pomodoro 25` (any number of minutes, 1–180) | Start a one-off work session of that length; your timer's own duration is not changed |

While a timer runs, the status result also offers buttons (select it, or press `Shift` + `Enter`): **Pause/Resume**, **Skip** and **Stop**.

Opening KRunner does **not** start Sugo; it starts (hidden in the tray) only when you type one of the words above. After installing or updating, restart KRunner once (`kquitapp6 krunner`) so that it reads the new plugin.

---

## D-Bus and scripting

Sugo exposes a D-Bus interface:

- **Service**: `io.github.mineraleyt.sugo`
- **Path**: `/Timer`
- **Interface**: `io.github.mineraleyt.sugo.Timer`

| Command | Action |
|:---|:---|
| `qdbus io.github.mineraleyt.sugo /Timer toggle` | Start or pause |
| `qdbus io.github.mineraleyt.sugo /Timer start` | Start or resume the active timer |
| `qdbus io.github.mineraleyt.sugo /Timer pause` | Pause |
| `qdbus io.github.mineraleyt.sugo /Timer stop` | Stop and reset |
| `qdbus io.github.mineraleyt.sugo /Timer skip` | Skip to the next phase |
| `qdbus io.github.mineraleyt.sugo /Timer status` | Print `state:phase:seconds` |
| `qdbus io.github.mineraleyt.sugo /Timer presets` | List the names of the timers, one per line |
| `qdbus io.github.mineraleyt.sugo /Timer selectPreset "Deep work"` | Choose a timer by name (not case-sensitive) or uuid |
| `qdbus io.github.mineraleyt.sugo /Timer nextPreset` | Choose the next timer in the list (wraps around) |
| `qdbus io.github.mineraleyt.sugo /Timer previousPreset` | Choose the previous timer (wraps around) |
| `qdbus io.github.mineraleyt.sugo /Timer startPreset "Deep work"` | Choose a timer and start it |

`status` returns three colon-separated fields, for example `running:work:1342`:

- **state**: `idle`, `running` or `paused`;
- **phase**: `work`, `short_break` or `long_break` (when idle, the phase that starts next);
- **seconds**: time left in the phase, in whole seconds.

These commands, and the KRunner ones, can be turned off in **Settings → Global Shortcuts and Automation → Allow other programs to control the timer**. They then answer with the error `io.github.mineraleyt.sugo.Error.ControlDisabled` and change nothing; `status` and `presets` keep working.

Changing the timer resets it, so while a phase is running or paused `selectPreset`, `nextPreset`, `previousPreset` and `startPreset` refuse with the error `io.github.mineraleyt.sugo.Error.TimerActive` and change nothing (stop the timer first). Choosing the timer that is already selected is always allowed, and `startPreset` then starts or resumes it. An unknown name or uuid gives `io.github.mineraleyt.sugo.Error.UnknownPreset`.

Depending on your distribution, `qdbus` may be called `qdbus6` or `qdbus-qt6`. If Sugo is not running, calling the service starts it hidden in the system tray (D-Bus activation). You can also start it that way yourself with `sugo --background`.

---

## Data, backup and privacy

| What | Where |
|:---|:---|
| Timers, sessions, notes | `~/.local/share/sugo/sugo.db` (SQLite) |
| Settings | `~/.config/sugo/sugo.conf` |

Inside the Flatpak the same files live under `~/.var/app/io.github.mineraleyt.sugo/`.

**Settings → Data management** can **back up** the database to a file, **restore** a backup (including backups made by older versions), and **clear the session history**. Restoring replaces your current timers and sessions, and is not possible while a timer is running. If the database file is damaged, Sugo moves it aside (`sugo.db.corrupt-…`) and starts a new one.

**What leaves your computer.** Nothing about you:

- **Updates**: the AppImage downloads a small file from GitHub every few hours to see whether a newer Sugo exists (see [Updates](#updates-and-diagnostics); you can turn this off), and the new version itself only when you click **Install update**. The other packages only look when you press **Check for Updates**. Either way the request carries only the program version.
- **Calendar sync**, if you turn it on, sends the information described [above](#good-to-know) to the server you chose.

There is no telemetry.

---

## Updates and diagnostics

- **The AppImage updates itself, when you say so.** A minute after it starts, and every six hours, it looks for a newer version on its channel; when there is one, the sidebar shows **Install update**. Nothing is downloaded before you click it. Sugo then downloads the new version next to itself, checks that the file is complete and intact (its size and SHA-256 checksum), and only then puts it in place of the old one. The sidebar then shows **Restart to update**; the new version starts when you click it (Sugo asks first if a timer is running) or the next time you open Sugo. A failed or damaged download changes nothing.
- **Settings → General → Updates** (AppImage only) chooses the channel: **Stable**, the tested releases, or **Nightly**, test builds of what is coming, newer and sometimes with new bugs. Going back from Nightly to Stable keeps the nightly build until a newer release comes out. Turn off **Check for updates automatically** and Sugo only looks when you ask, with **About → Check for Updates**.
- **About → Check for Updates** tells you whether a newer version exists; the AppImage can install it from there, the other packages link to the release (a Flatpak or a distribution's package is updated the way it was installed). A successful check with nothing new shows a green check on the button for a few seconds.
- **Settings → Advanced → Troubleshooting & Diagnostics** can copy or export a report with the version, your environment and the latest log lines. Your home directory, user name, passwords and addresses with credentials are replaced with placeholders. Attach it to a bug report.

---

## Flatpak notes

The Flatpak has network access (for the update check and calendar sync), the notification and screen-saver services (to silence notifications and keep the screen awake), the system tray, and audio. *Start at login* uses the desktop's background portal, which may ask for your permission the first time.
