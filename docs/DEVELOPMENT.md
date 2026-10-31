<!--
  SPDX-FileCopyrightText: 2026 Sugo contributors
  SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Sugo development guide

How to build Sugo, run its tests and publish a release. For using the application see the [user guide](USAGE.md).

**Contents**

1. [Building from source](#building-from-source)
2. [Building the Flatpak](#building-the-flatpak)
3. [Tests](#tests)
4. [Project layout](#project-layout)
5. [Translating](#translating)
6. [Releasing](#releasing)

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
    qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg kf6-extra-cmake-modules
```

#### Ubuntu 26.04 or newer
```sh
sudo apt install cmake ninja-build g++ \
    qt6-base-dev qt6-declarative-dev qt6-svg-plugins libqt6sql6-sqlite extra-cmake-modules
```

#### Arch Linux
```sh
sudo pacman -S cmake ninja gcc qt6-base qt6-declarative qt6-svg extra-cmake-modules
```

#### openSUSE Tumbleweed
```sh
sudo zypper install cmake ninja gcc-c++ \
    qt6-base-devel qt6-declarative-devel qt6-svg-devel extra-cmake-modules
```

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

The AppImage is built in an Ubuntu 22.04 container (the oldest glibc it runs on) with Qt 6.8 from qt.io, extra-cmake-modules built from source and linuxdeploy, all pinned in `packaging/appimage/build.sh`:

```sh
podman run --rm -v "$PWD:/src:ro,z" -v "$PWD/out:/out:z" docker.io/library/ubuntu:22.04 \
    bash /src/packaging/appimage/build.sh 1.2.0-dev /out
```

The first argument is the version the program shows and compares when it looks for updates. To try the result without installing it, unpack it (`./out/Sugo-x86_64.AppImage --appimage-extract`) and run `squashfs-root/AppRun`, or pass that to `tests/smoke_app.sh`. If AppImageLauncher is installed, starting the AppImage itself makes it offer to move the file into `~/Applications`.

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

**Nightly builds.** Every push to `testing` that passes CI is built by `nightly.yml` and replaces the assets of the pre-release `nightly` (its tag follows `testing`; the branch can't be called `nightly` like the tag): the AppImage and `nightly.json`, the feed of the nightly channel. Its version is a pre-release of the version in `CMakeLists.txt`, `X.Y.Z-nightly.YYYYMMDD.N` (of the next patch if `vX.Y.Z` is already released), so a nightly is newer than the last release and older than the next one. Bump `CMakeLists.txt` after a release to name the next one. The build commands of both are in `appimage.yml`, which also starts the AppImage without a display before anything is published.

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

The workflow refuses to publish unless the tag, the version in `CMakeLists.txt` and the newest `<release>` in the metainfo all agree. Every `v*` tag is built, tested and published, whatever the tagged commit changed. Pushes to `main` that only change Markdown files or `docs/` skip CI. Third-party actions and the build image are pinned to a commit or digest; Dependabot proposes updates.
