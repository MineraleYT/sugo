#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds Sugo for macOS 13 or later as a universal (arm64 + x86_64) app in a disk image, with Qt
# from qt.io (the same version as the AppImage). Runs on a Mac with the Xcode command line tools,
# CMake, Python 3 and git (a GitHub macos runner has them all):
#
#   bash packaging/macos/build.sh 1.0.0 out
#
# Arguments: the version the program shows and compares when it looks for updates (see
# UpdateChecker), and the directory that receives Sugo-macOS-universal.dmg.
#
# The app is signed ad hoc (Apple Silicon runs no unsigned code, and macdeployqt's changes break
# the linker's signature), so Gatekeeper asks the user to confirm the first launch. SIGN_IDENTITY
# set to a "Developer ID Application" identity signs it for distribution instead, with the hardened
# runtime and the entitlements in entitlements.plist (notarization is a separate step, not done here).
set -euo pipefail

VERSION=${1:?version}
OUT=${2:?output directory}
SRC=$(cd "$(dirname "$0")/../.." && pwd)
WORK=${WORK:-/tmp/sugo-macos}
SIGN_IDENTITY=${SIGN_IDENTITY:--}

# Pinned like packaging/appimage/build.sh: keep the two in step.
QT_VERSION=6.10.3
AQT_VERSION=3.3.0
ECM_TAG=v6.8.0
DEPLOYMENT_TARGET=13.0
ARCHS="arm64;x86_64"
DMG_NAME=Sugo-macOS-universal.dmg

fail() { echo "error: $*" >&2; exit 1; }

mkdir -p "$WORK" "$OUT"
# Absolute: the script works in WORK, and APP and the exit trap name paths under it.
WORK=$(cd "$WORK" && pwd)
OUT=$(cd "$OUT" && pwd)
cd "$WORK"

if ! command -v ninja >/dev/null 2>&1; then
    command -v brew >/dev/null 2>&1 || fail "ninja is missing (brew install ninja)"
    brew install ninja >/dev/null
fi

# --- Qt and extra-cmake-modules ------------------------------------------------------------------
# aqt names the folder "macos" from Qt 6.7 on, "clang_64" before: accept either.
find_qt() {
    for dir in "$WORK/qt/$QT_VERSION/macos" "$WORK/qt/$QT_VERSION/clang_64"; do
        if [ -x "$dir/bin/macdeployqt" ]; then
            echo "$dir"
            return 0
        fi
    done
    return 1
}
if ! QT=$(find_qt); then
    # A virtual environment: Homebrew's Python refuses packages installed next to its own.
    python3 -m venv "$WORK/aqt-venv"
    "$WORK/aqt-venv/bin/pip" install -q "aqtinstall==$AQT_VERSION"
    "$WORK/aqt-venv/bin/python" -m aqt install-qt mac desktop "$QT_VERSION" clang_64 -m qtmultimedia -O "$WORK/qt" >/dev/null
    QT=$(find_qt) || fail "aqt installed no Qt in $WORK/qt/$QT_VERSION"
fi
if [ ! -d "$WORK/ecm" ]; then
    rm -rf ecm-src ecm-build
    git clone -q --depth 1 --branch "$ECM_TAG" https://invent.kde.org/frameworks/extra-cmake-modules.git ecm-src
    cmake -S ecm-src -B ecm-build -G Ninja -DBUILD_TESTING=OFF -DBUILD_HTML_DOCS=OFF \
        -DBUILD_MAN_DOCS=OFF -DBUILD_QTHELP_DOCS=OFF -DCMAKE_INSTALL_PREFIX="$WORK/ecm" >/dev/null
    cmake --install ecm-build >/dev/null
fi

# --- Sugo ----------------------------------------------------------------------------------------
cmake -S "$SRC" -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCMAKE_PREFIX_PATH="$QT;$WORK/ecm" \
    -DCMAKE_OSX_ARCHITECTURES="$ARCHS" -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOYMENT_TARGET" \
    -DSUGO_VERSION="$VERSION" >/dev/null
cmake --build build
[ -x build/bin/Sugo.app/Contents/MacOS/Sugo ] || fail "build/bin/Sugo.app was not built"

# A copy, so that a rebuild never sees a bundle macdeployqt has already changed.
rm -rf app
mkdir app
ditto build/bin/Sugo.app app/Sugo.app
APP="$WORK/app/Sugo.app"
CONTENTS="$APP/Contents"

# The libraries a Mach-O file loads, once each (a universal file lists them for each architecture,
# under a header line per architecture; the dependencies are the indented lines).
deps() {
    otool -L "$1" | awk '/^[[:space:]]/ { print $1 }' | sort -u
}

# --- Qt in the bundle ----------------------------------------------------------------------------
# Only SQLite is used: the other SQL drivers need client libraries that are not there. Set aside
# while macdeployqt runs (Qt's CMake files still refer to them), put back on exit.
mkdir -p sqldrivers-unused
find "$QT/plugins/sqldrivers" -name '*.dylib' ! -name libqsqlite.dylib -exec mv {} sqldrivers-unused/ \;
trap 'mv -f "$WORK"/sqldrivers-unused/*.dylib "$QT/plugins/sqldrivers/" 2>/dev/null || true' EXIT

"$QT/bin/macdeployqt" "$APP" -qmldir="$SRC/src/qml" -verbose=1 > macdeployqt.log 2>&1 \
    || { tail -40 macdeployqt.log; exit 1; }
# Not worth reporting: our own QML module (compiled into the program) and the styles of other systems.
grep -iE 'error|warning' macdeployqt.log \
    | grep -viE 'io\.github\.mineraleyt\.sugo|QtQuick/Controls/(Windows|Fusion|Imagine|Material|Universal|FluentWinUI3|iOS)' || true
find "$CONTENTS/PlugIns/sqldrivers" -name '*.dylib' ! -name libqsqlite.dylib -delete 2>/dev/null || true

# offscreen: the smoke tests start the app without a display before it is published.
# macdeployqt brings only the Cocoa platform plugin; this one points at the bundled frameworks.
offscreen="$CONTENTS/PlugIns/platforms/libqoffscreen.dylib"
cp "$QT/plugins/platforms/libqoffscreen.dylib" "$offscreen"
chmod u+w "$offscreen"
deps "$offscreen" | while read -r dep; do
    case "$dep" in
        @rpath/Qt*.framework/*) install_name_tool -change "$dep" "@executable_path/../Frameworks/${dep#@rpath/}" "$offscreen" ;;
        "$QT"/lib/Qt*.framework/*) install_name_tool -change "$dep" "@executable_path/../Frameworks/${dep#"$QT"/lib/}" "$offscreen" ;;
    esac
done

# The build's own rpaths to Qt would make the app look for Qt where it was built; the bundled
# frameworks are found through @executable_path/../Frameworks.
rpaths() {
    otool -l "$CONTENTS/MacOS/Sugo" | awk '/cmd LC_RPATH/ { getline; getline; print $2 }' | sort -u
}
rpaths | while read -r rpath; do
    case "$rpath" in
        @*) ;;
        *) install_name_tool -delete_rpath "$rpath" "$CONTENTS/MacOS/Sugo" ;;
    esac
done
if ! rpaths | grep -qx '@executable_path/../Frameworks'; then
    install_name_tool -add_rpath @executable_path/../Frameworks "$CONTENTS/MacOS/Sugo"
fi

# --- Checks: fail on anything missing ------------------------------------------------------------
require() { # file under Contents, what it is for
    [ -e "$CONTENTS/$1" ] || fail "missing $1 ($2)"
}
require PlugIns/platforms/libqcocoa.dylib "the window system"
require PlugIns/platforms/libqoffscreen.dylib "the smoke tests"
require PlugIns/sqldrivers/libqsqlite.dylib "presets and sessions"
require PlugIns/imageformats/libqsvg.dylib "SVG pictures"
require PlugIns/iconengines/libqsvgicon.dylib "SVG icons"
require PlugIns/multimedia/libffmpegmediaplugin.dylib "the focus sounds"
for module in QtQuick QtQuick/Controls QtQuick/Controls/Basic QtQuick/Layouts QtQuick/Shapes QtQuick/Dialogs; do
    require "Resources/qml/$module/qmldir" "QML module $module"
done
[ "$(find "$CONTENTS/PlugIns/sqldrivers" -name '*.dylib' | wc -l | tr -d ' ')" = 1 ] \
    || fail "SQL drivers other than SQLite were deployed"

# D-Bus is Linux-only: Sugo itself must not link QtDBus on macOS. Qt from qt.io is built with
# its dbus feature, though, and QtGui links QtDBus.framework, so macdeployqt has to ship it: name
# what pulls it in instead of deleting it (QtGui would not load without it).
if deps "$CONTENTS/MacOS/Sugo" | grep -q 'QtDBus\.framework/'; then
    fail "Sugo links QtDBus.framework: D-Bus code is built on macOS (see SUGO_DBUS in CMakeLists.txt)"
fi
if [ -e "$CONTENTS/Frameworks/QtDBus.framework" ]; then
    users=$(find "$APP" -type f \( -perm -u+x -o -name '*.dylib' \) -not -path '*/QtDBus.framework/*' -print0 \
        | while IFS= read -r -d '' f; do
              if otool -L "$f" 2>/dev/null | grep -q 'QtDBus\.framework/'; then echo "${f#"$CONTENTS"/}"; fi
          done)
    [ -n "$users" ] || fail "QtDBus.framework was deployed but nothing in the bundle links it"
    echo "note: QtDBus.framework is in the bundle only for Qt itself: $(echo $users)" >&2
fi

# HTTPS (update checks, CalDAV): a TLS backend that needs nothing outside the bundle.
if [ ! -e "$CONTENTS/PlugIns/tls/libqsecuretransportbackend.dylib" ]; then
    [ -e "$CONTENTS/PlugIns/tls/libqopensslbackend.dylib" ] || fail "no TLS backend in PlugIns/tls"
    find "$CONTENTS/Frameworks" -name 'libssl*.dylib' | grep -q . \
        || fail "only the OpenSSL TLS backend was deployed, without OpenSSL"
fi

# FFmpeg: the media plugin's libraries, when it links them dynamically, must be in the bundle.
for lib in $(deps "$CONTENTS/PlugIns/multimedia/libffmpegmediaplugin.dylib" | grep -E 'lib(av|sw)[a-z]*\.' || true); do
    [ -e "$CONTENTS/Frameworks/$(basename "$lib")" ] || fail "FFmpeg library $(basename "$lib") is not in the bundle"
done

# Every program and library: both architectures, and nothing loaded from outside the bundle but
# the system's own libraries.
macho_files() {
    find "$APP" -type f \( -perm -u+x -o -name '*.dylib' \) -print0 | while IFS= read -r -d '' f; do
        if file -b "$f" | grep -q 'Mach-O'; then
            printf '%s\0' "$f"
        fi
    done
}
problems=0
while IFS= read -r -d '' f; do
    archs=$(lipo -archs "$f" 2>/dev/null || true)
    for arch in x86_64 arm64; do
        case " $archs " in
            *" $arch "*) ;;
            *) echo "no $arch ($archs): ${f#"$APP"/}" >&2; problems=1 ;;
        esac
    done
    dir=$(dirname "$f")
    # A library lists its own install name among its dependencies: skip it.
    own=$(otool -D "$f" 2>/dev/null | awk '!/:$/ { print; exit }')
    while read -r dep; do
        [ -n "$dep" ] && [ "$dep" != "$own" ] || continue
        case "$dep" in
            /System/* | /usr/lib/*) continue ;;
            @executable_path/*) target="$CONTENTS/MacOS/${dep#@executable_path/}" ;;
            @loader_path/*) target="$dir/${dep#@loader_path/}" ;;
            @rpath/*) target="$CONTENTS/Frameworks/${dep#@rpath/}" ;;
            *) echo "outside the bundle: ${f#"$APP"/} -> $dep" >&2; problems=1; continue ;;
        esac
        [ -e "$target" ] || { echo "not in the bundle: ${f#"$APP"/} -> $dep" >&2; problems=1; }
    done < <(deps "$f")
done < <(macho_files)
[ "$problems" = 0 ] || fail "the bundle is not self-contained"

# --- Signature -----------------------------------------------------------------------------------
# Extended attributes (Finder information, quarantine) are not allowed in a signed bundle.
xattr -cr "$APP"
if [ "$SIGN_IDENTITY" = - ]; then
    codesign --force --deep --sign - "$APP"
else
    # The hardened runtime: entitlements.plist lets Qt's QML engine use its JIT.
    codesign --force --deep --options runtime --timestamp --entitlements "$SRC/packaging/macos/entitlements.plist" \
        --sign "$SIGN_IDENTITY" "$APP"
fi
codesign --verify --deep --strict --verbose=2 "$APP"

actual=$(QT_QPA_PLATFORM=offscreen "$CONTENTS/MacOS/Sugo" --version 2>/dev/null || true)
[ "$actual" = "sugo $VERSION" ] || fail "--version printed \"$actual\", expected \"sugo $VERSION\""

# --- The disk image ------------------------------------------------------------------------------
rm -rf dmg
mkdir dmg
ditto "$APP" dmg/Sugo.app
ln -s /Applications dmg/Applications
OUTPUT="$OUT/$DMG_NAME"
rm -f "$OUTPUT"
# hdiutil sometimes fails with "Resource busy" on the CI runners: try again a few times.
for attempt in 1 2 3 4 5; do
    if hdiutil create -volname Sugo -srcfolder dmg -fs APFS -format ULFO -ov "$OUTPUT" >/dev/null; then
        break
    fi
    [ "$attempt" = 5 ] && fail "hdiutil could not create the disk image"
    echo "hdiutil failed (attempt $attempt), trying again" >&2
    sleep $((attempt * 5))
done
hdiutil verify "$OUTPUT" >/dev/null
echo "Built $OUTPUT ($VERSION, $(shasum -a 256 "$OUTPUT" | cut -d' ' -f1))"
