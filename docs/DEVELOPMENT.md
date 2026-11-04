<!--
  SPDX-FileCopyrightText: 2026 Sugo contributors
  SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Sugo development guide

How to build Sugo, run its tests and publish a release. For using the application see the [user guide](USAGE.md).

**Contents**

1. [Building from source](#building-from-source)
2. [Building the Flatpak](#building-the-flatpak)
3. [Building the AppImage](#building-the-appimage)
4. [Building the macOS app](#building-the-macos-app)
5. [Tests](#tests)
6. [Project layout](#project-layout)
7. [Translating](#translating)
8. [Releasing](#releasing)

---

## Building from source

### Requirements

- **C++17** compiler (GCC 11+ or Clang 14+)
- **CMake** 3.20+ and **Ninja**
- **Qt 6.8+** (Core, Gui, Widgets, Qml, Quick, QuickControls2, Sql, DBus, Network)
- **extra-cmake-modules 6.8+** (build tool only: no KDE Frameworks library is used)
- **Gettext** (`xgettext`), only to extract the strings

### Build dependencies

#### Fedora
```sh
sudo dnf install cmake ninja-build gcc-c++ \
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg qt6-qtmultimedia-devel kf6-extra-cmake-modules
```

#### Ubuntu 26.04 or newer
```sh
sudo apt install cmake ninja-build g++ \
    qt6-base-dev qt6-declarative-dev qt6-svg-plugins qt6-multimedia-dev libqt6sql6-sqlite extra-cmake-modules
```

#### Arch Linux
```sh
sudo pacman -S cmake ninja gcc qt6-base qt6-declarative qt6-svg qt6-multimedia qt6-multimedia-ffmpeg extra-cmake-modules
```

#### openSUSE Tumbleweed
```sh
sudo zypper install cmake ninja gcc-c++ \
    qt6-base-devel qt6-declarative-devel qt6-svg-devel qt6-multimedia-devel extra-cmake-modules
```

#### macOS 13 or newer

With the Xcode command line tools (`xcode-select --install`) and [Homebrew](https://brew.sh):

```sh
brew install qt cmake ninja extra-cmake-modules
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON \
    -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
open build/bin/Sugo.app
```

The program is the app bundle `build/bin/Sugo.app` (its binary is `Sugo.app/Contents/MacOS/Sugo`); there is nothing to install. The Linux files (desktop entry, KRunner plugin, D-Bus service, metainfo) are not installed on macOS. Homebrew's Qt is enough to develop; the app published in the releases is built with Qt from qt.io, see [Building the macOS app](#building-the-macos-app).

For a system-wide install use `-DCMAKE_INSTALL_PREFIX=/usr` and run the install step with `sudo`.

The install step also puts the KRunner plugin, and the D-Bus activation file in place. A running Sugo keeps the old program in memory: quit it and start it again after installing, and restart KRunner (`kquitapp6 krunner`) when the runner definition changed.

---

## Building the Flatpak

```sh
flatpak-builder --user --install --install-deps-from=flathub --force-clean \
    "$HOME/.cache/sugo-flatpak-build" io.github.mineraleyt.sugo.yml
```

The sandbox permissions are in the `finish-args` of `io.github.mineraleyt.sugo.yml`.

---

## Building the AppImage

The AppImage is built in an Ubuntu 22.04 container (the oldest glibc it runs on) with Qt 6.10 from qt.io, extra-cmake-modules built from source and linuxdeploy, all pinned in `packaging/appimage/build.sh`:

```sh
podman run --rm -v "$PWD:/src:ro,z" -v "$PWD/out:/out:z" docker.io/library/ubuntu:22.04 \
    bash /src/packaging/appimage/build.sh 1.0.1-dev /out
```

The first argument is the version the program shows and compares when it looks for updates. To try the result without installing it, unpack it (`./out/Sugo-x86_64.AppImage --appimage-extract`) and run `squashfs-root/AppRun`, or pass that to `tests/smoke_app.sh`. If AppImageLauncher is installed, starting the AppImage itself makes it offer to move the file into `~/Applications`.

---

## Building the macOS app

The release disk image is built on a Mac by `packaging/macos/build.sh` with Qt 6.10 from qt.io (installed with aqtinstall) and extra-cmake-modules from source, at the same versions as the AppImage. It needs the Xcode command line tools, CMake, Python 3 and git (Ninja is installed with Homebrew if missing):

```sh
bash packaging/macos/build.sh 1.0.1-dev out
```

The arguments are the version, as for the AppImage, and the folder that receives `Sugo-macOS-universal.dmg`. The script builds one universal app (`arm64` and `x86_64`, macOS 13 or later), copies Qt into it with `macdeployqt` (SQLite is the only SQL driver kept; the `offscreen` platform plugin is added for the smoke tests), and fails if the bundle is incomplete: a missing platform, SQL, SVG, multimedia (FFmpeg) or TLS plugin or QML module, a library loaded from outside the bundle (other than macOS's own), or a file without both architectures. It then signs the app ad hoc (`codesign --sign -`, required on Apple Silicon), checks the signature and `--version`, and makes the disk image with a link to `/Applications`. The downloads and the build are kept in `WORK` (`/tmp/sugo-macos` by default) and reused by the next run.

The app is not signed with a Developer ID nor notarized, so Gatekeeper asks users to confirm the first launch (see the README). `SIGN_IDENTITY="Developer ID Application: …"` signs with that identity and the hardened runtime instead; notarization is not done by the script.

To try the result, open the disk image, or run `tests/smoke_app.sh` on `Sugo.app/Contents/MacOS/Sugo` (it needs GNU `timeout`: `brew install coreutils` and put `$(brew --prefix coreutils)/libexec/gnubin` first in `PATH`). In CI, `macos.yml` builds it and starts it without a display; `nightly.yml` and `release.yml` publish it when that succeeds, and publish the Linux packages without it when it fails.

---

## Tests

```sh
ctest --test-dir build --output-on-failure
```

The suite covers the timer engine, the database and statistics, the platform services (D-Bus, inhibitors, autostart, KRunner), the calendar client against an in-process fake server, and smoke runs of every QML page.

The continuous-integration server has no display and no session bus. To get the same conditions locally, run the tests in a clean environment:

```sh
env -i PATH=/usr/bin:/bin HOME=/tmp/sugo-test-home \
    ctest --test-dir build --output-on-failure
```

A test that builds a GUI application sets `QT_QPA_PLATFORM=offscreen` for itself (see `tests/CMakeLists.txt`); tests that need D-Bus start a private daemon and are written to work without a session bus. The build is expected to be free of compiler warnings.

CI builds on Linux with GCC (and runs the suite), with Clang and warnings as errors (its warnings are close to those of Apple's compiler), and on macOS (arm64, Qt from qt.io, warnings as errors, the whole suite; not blocking yet).

**On macOS** the same `ctest` command works (the smoke runs need GNU `timeout`, see [Building the macOS app](#building-the-macos-app)). What is tested only on Linux: the tests that start a private D-Bus daemon (`tst_dbustimerservice`, `tst_singleinstance`, `tst_freedesktopnotifier`, `tst_screenlockwatcher`, `tst_secretstore`, `tst_autostart`'s portal cases) skip when there is no `dbus-daemon`, which macOS does not have; the freedesktop services they cover (notifications, notification and power inhibitors, KRunner, the AppImage self-update) are not used on macOS anyway.

### Environment variables for development

| Variable | Effect |
|---|---|
| `SUGO_START_PAGE` | Opens on another page: `presets` (the timer in use), `stats`, `settings`, `about`, `welcome` |
| `SUGO_SMOKE_PAGE`, `SUGO_STATS_SMOKE_TEST=1` | Smoke runs (`tests/smoke_app.sh`): check a page, then quit |
| `SUGO_SCREENSHOT=file.png` | Saves a picture of the window after `SUGO_SCREENSHOT_DELAY` ms (1500 by default), then quits |
| `SUGO_WINDOW_STYLE` | Forces the title bar style (`kde`, `gnome`, `macos`, `windows`, `generic`, ...); `macos` also shows the wording and shortcuts of the Mac version, to preview it on Linux |
| `SUGO_DATA_DIR` | The folder that holds `sugo.db` and `sugo.conf`, instead of the usual ones: keeps test runs (and the macOS smoke runs, where `XDG_*` has no effect) away from your data |
| `SUGO_OVERRIDE_VERSION` | The version the update check compares, to try it against the published feeds |

---

## Project layout

| Path | Contents |
|:---|:---|
| `src/core/` | Timer engine, presets, settings, notifications, tray, calendar sync, update checker, diagnostics |
| `src/db/` | SQLite access: schema and migrations, presets, sessions, the recorder that stores finished phases |
| `src/stats/` | Statistics aggregation, the model behind the Statistics page |
| `src/platform/` | Desktop services: D-Bus API, KRunner, inhibitors, autostart, screen lock, single instance, signals |
| `src/qml/` | The interface (Qt Quick, our own components and `theme/Theme.qml`): `Main.qml`, `pages/`, `components/` |
| `data/` | Desktop entries, KRunner and D-Bus activation files, AppStream metadata, icon |
| `po/` | String template (`sugo.pot`) and the script that extracts it |
| `tests/` | The test suite |
| `.github/workflows/` | CI and release workflows |

All the C++ code but `src/main.cpp` is built once, as the static library `sugo_core` (also the QML module), which the program and every test link.

Conventions worth knowing: user-visible strings go through `i18n()` (never `tr()`), with `i18np()` for anything that can be plural; QML uses only the colours and sizes of `Theme` (`src/qml/theme/Theme.qml`).

---

## Translating

For now the program is in English only. The texts still go through the KI18n-style `i18n()`, `i18nc()`, `i18np()` and `i18ncp()` functions of `src/core/Localization.h` (in C++ and QML), so translations can come back without touching the sources: `bash po/Messages.sh` still extracts every string to `po/sugo.pot`.

---

## Branches

| Branch | For | A push runs |
|:---|:---|:---|
| `dev` | Daily work: every commit goes here | CI (build and tests) |
| `testing` | What is ready to try: `dev` brought over when you decide | CI, then the nightly build (`nightly.yml`) |
| `main` | What has been tried: `testing` brought over once it works | CI; releases are tagged here |

Each step is a fast-forward push:

```sh
git push origin dev:testing     # publish a nightly build of dev
git push origin testing:main    # mark the tested build as the base of the next release
```

---

## Releasing

A release is a version bump, a commit, an annotated tag on `main` and a push. The release workflow (`release.yml`) builds the AppImage and the Flatpak bundle and publishes the GitHub release when it sees a `v*` tag, with `stable.json`: the feed the AppImages on the stable channel read to update themselves (`releases/latest/download/stable.json`, so the release must be the latest one).

**Nightly builds.** Every push to `testing` that passes CI is built by `nightly.yml` and published as a new pre-release tagged `vX.Y.Z-nightly.YYYYMMDD.N` (`release.yml` ignores these tags): the AppImage and `nightly.json`. The pre-release `nightly` stays as the fixed address of the feed of the nightly channel (it holds only the latest `nightly.json`; its tag follows `testing`, the branch can't be called `nightly` like the tag) and as the starting point of the next release notes. Old nightlies are not deleted. Its version is a pre-release of the version in `CMakeLists.txt`, `X.Y.Z-nightly.YYYYMMDD.N` (of the next patch if `vX.Y.Z` is already released), so a nightly is newer than the last release and older than the next one. Bump `CMakeLists.txt` after a release to name the next one. The build commands of both are in `appimage.yml`, which also starts the AppImage without a display before anything is published.

**macOS.** Both workflows also build `Sugo-macOS-universal.dmg` with `macos.yml` (on a macOS runner, see [Building the macOS app](#building-the-macos-app)). For now a failure there does not stop the release: the Linux packages and the feed are published without the disk image. When it is there it is uploaded with the other files, the notes explain how to open it the first time (Gatekeeper), and the feed gets a `macos` entry (`url`, `sha256`, `size` of the disk image) next to the AppImage's keys, which the AppImages ignore. The macOS app only reports new versions, from the release page.

**Checklist** for version `X.Y.Z`:

1. `CMakeLists.txt`: `project(Sugo VERSION X.Y.Z …)`.
2. `data/io.github.mineraleyt.sugo.metainfo.xml`: add a new `<release version="X.Y.Z" date="YYYY-MM-DD">` **above** the previous ones, with a short list of what changed.
3. Build and run the whole test suite in a clean environment.
4. Validate the metadata:
   ```sh
   appstreamcli validate --no-net data/io.github.mineraleyt.sugo.metainfo.xml
   desktop-file-validate data/io.github.mineraleyt.sugo.desktop \
       build/io.github.mineraleyt.sugo.runner.desktop
   ```
5. Commit (`Release vX.Y.Z: …`), then tag and push:
   ```sh
   git tag -a vX.Y.Z -m "Sugo X.Y.Z: …"
   git push origin main vX.Y.Z
   ```
6. Check the macOS part of the run: the job of `macos.yml` passed, the release has `Sugo-macOS-universal.dmg` and its notes start with the Gatekeeper instructions, and `stable.json` has a `macos` entry. If the macOS job failed, the release went out without it: fix the build and attach the disk image by hand (`bash packaging/macos/build.sh X.Y.Z out` on a Mac, then `gh release upload vX.Y.Z out/Sugo-macOS-universal.dmg`), or leave it for the next release. On a Mac, open the disk image, drag Sugo to Applications, and check the first launch and `Sugo.app/Contents/MacOS/Sugo --version`.

The workflow refuses to publish unless the tag, the version in `CMakeLists.txt` and the newest `<release>` in the metainfo all agree. Every `v*` tag is built, tested and published, whatever the tagged commit changed. Pushes to `main` that only change Markdown files or `docs/` skip CI. Third-party actions and the build image are pinned to a commit or digest; Dependabot proposes updates.
