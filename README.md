<!--
  SPDX-FileCopyrightText: 2026 Sugo contributors
  SPDX-License-Identifier: CC-BY-SA-4.0
-->

<div align="center">

  <img src="data/icons/hicolor/scalable/apps/io.github.mineraleyt.sugo.svg" alt="Sugo logo" width="128" height="128" />

  # Sugo

  **A modern, distraction-free Pomodoro timer for Linux desktops, at home on KDE Plasma.**

  [![CI](https://github.com/MineraleYT/sugo/actions/workflows/ci.yml/badge.svg)](https://github.com/MineraleYT/sugo/actions/workflows/ci.yml)
  [![Release](https://github.com/MineraleYT/sugo/actions/workflows/release.yml/badge.svg)](https://github.com/MineraleYT/sugo/actions/workflows/release.yml)
  [![Latest Release](https://img.shields.io/github/v/release/MineraleYT/sugo?color=e05d44&logo=github)](https://github.com/MineraleYT/sugo/releases/latest)
  [![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
  [![KDE Plasma 6](https://img.shields.io/badge/KDE-Plasma%206-1d99f3?logo=kde)](https://kde.org)
  [![Qt 6](https://img.shields.io/badge/Qt-6.8+-41cd52?logo=qt)](https://www.qt.io)

</div>

---

## About Sugo

**Sugo** is an independent, native Pomodoro and interval timer for Linux desktops, at home on **KDE Plasma** and just as much elsewhere. It has a calm interface of its own, draws its window buttons the way your desktop does, and keeps your data local and under your control.

---

## Features

- **Flexible focus and breaks**: work, short and long breaks with configurable durations, cycles and auto-start modes, starting from a classic *Pomodoro* (25/5) that silences notifications while you work, plus timers of your own.
- **Distraction-free**: silences notifications and keeps the screen awake while you work, can pause when the screen locks, and plays optional ambient sounds.
- **Statistics**: day, week, month and year views, streaks, breakdown by timer, category and task.
- **Task notes**: jot down what you did when a work session ends, from the notification or an in-app prompt.
- **Calendar events (optional)**: log in with Nextcloud, or use any CalDAV calendar, and every finished work session becomes an event.
- **Desktop integration**: a tray icon with a live countdown and a menu to control the timer, native notifications with actions, KRunner commands, start at login, and a D-Bus interface for scripts and shortcuts.
- **Private**: everything lives in a local SQLite database, with one-click backup and restore. Sugo sends no data about you: the AppImage and the macOS app look for updates on GitHub (you can turn that off) and download them when you ask, and the other packages only look for one when you ask; the calendar sync is off until you set it up.

See the [user guide](docs/USAGE.md) for the details of each feature.

---

## Installation

### AppImage (recommended)

One file that runs on any recent x86_64 Linux and keeps itself up to date:

1. Download [Sugo-x86_64.AppImage](https://github.com/MineraleYT/sugo/releases/latest/download/Sugo-x86_64.AppImage).
2. Make it executable and start it:
   ```sh
   chmod +x Sugo-x86_64.AppImage
   ./Sugo-x86_64.AppImage
   ```

Sugo then tells you when a new release is out: **Install update** downloads it, **Restart to update** starts it. In Settings → General → Updates you can turn the automatic check off, or choose the **Nightly** channel: test builds of what is coming, published as [pre-releases](https://github.com/MineraleYT/sugo/releases), the newest one named in the [nightly feed release](https://github.com/MineraleYT/sugo/releases/tag/nightly).

### Flatpak

Releases also provide an x86_64 Flatpak bundle (it does not update itself):

1. Download `sugo-v*-x86_64.flatpak` from the [latest release](https://github.com/MineraleYT/sugo/releases/latest).
2. Install and run it:
   ```sh
   flatpak install --user ./sugo-v*-x86_64.flatpak
   flatpak run io.github.mineraleyt.sugo
   ```

*Flatpak downloads the KDE 6.11 runtime from Flathub the first time, if it is not installed yet.*

### macOS

Releases also provide a disk image for **macOS 13 Ventura or later**, one app for both Apple Silicon and Intel Macs:

1. Download [Sugo-macOS-universal.dmg](https://github.com/MineraleYT/sugo/releases/latest/download/Sugo-macOS-universal.dmg), open it and drag **Sugo** to **Applications**.
2. Start it. Sugo is not notarized by Apple yet, so macOS blocks the first launch:
   - on macOS 15 Sequoia or later, open **System Settings → Privacy & Security**, scroll to the message about Sugo and click **Open Anyway**;
   - on macOS 13 and 14, Control-click Sugo in Applications, choose **Open**, then **Open** again;
   - or, in Terminal, remove the quarantine mark: `xattr -dr com.apple.quarantine /Applications/Sugo.app`.

   macOS remembers the choice: later launches open normally.

Sugo for macOS updates itself like the AppImage when it is in a folder you can write to, such as **Applications** for an administrator account: **Install update** downloads the new disk image, checks it and prepares the new app, **Restart to update** (or simply quitting) puts it in place. In Settings you can turn the automatic check off or choose the **Nightly** channel. Installed elsewhere, it only tells you that a new release is out: download the new disk image and replace the app. Some features are Linux-only for now: control from scripts and launchers (the D-Bus interface and KRunner commands) and silencing other notifications while you work.

### From source

Sugo needs Qt 6.8+ (and extra-cmake-modules 6.8+ to build). Dependencies for Fedora, Ubuntu, Arch and openSUSE, build and install steps, and a local Flatpak build are in the [development guide](docs/DEVELOPMENT.md#building-from-source).

---

## Quick start

- **First start:** a short animated tour shows how timers work and sets up the tray and starting at login.
- **From the tray:** click the Sugo icon for a menu with the status, **Start/Pause**, **Stop**, **Skip**, a **Timer** submenu to switch timers, and shortcuts to Statistics and Settings.
- **From KRunner** (`Alt+Space`): type `pomodoro 25` to start a 25-minute session, or `pomodoro` to see the status and controls.
- **From a script:** `qdbus io.github.mineraleyt.sugo /Timer toggle`, handy for a global shortcut; `startPreset "Deep work"`, `nextPreset` and `previousPreset` choose the timer.

---

## Documentation

| Document | What it covers |
|:---|:---|
| [User guide](docs/USAGE.md) | Timers, the tray menu, shortcuts, statistics, task notes, calendar sync, KRunner, D-Bus, data and privacy |
| [Development guide](docs/DEVELOPMENT.md) | Building, testing, project layout and the release checklist |

---

## Contributing

Bug reports and ideas are very welcome. Settings → Advanced → Troubleshooting & Diagnostics can copy a diagnostics report to attach to an issue (your home directory and user name are redacted). How to build and run the tests is in the [development guide](docs/DEVELOPMENT.md).

---

## License

- Application code is licensed under the [GNU General Public License v3.0 or later (GPL-3.0-or-later)](LICENSE).
- Documentation and media are licensed under [Creative Commons Attribution-ShareAlike 4.0 International (CC-BY-SA-4.0)](https://creativecommons.org/licenses/by-sa/4.0/).
- AppStream metadata is licensed under [CC0-1.0](https://creativecommons.org/publicdomain/zero/1.0/).

*Sugo is an independent community project and is not affiliated with the KDE e.V. or official KDE releases.*
