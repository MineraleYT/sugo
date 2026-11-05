#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Renders the macOS app icon (data/icons/macos/<size>-apps-stream.sugo.app.png, turned into
# Sugo.icns by ecm_add_app_icon) from the scalable icon. Run it again when the SVG changes and commit
# the PNGs. Needs rsvg-convert (librsvg).
#
# macOS icons since Big Sur keep a transparent margin: on a 1024 px canvas the tile is 824 px wide.
# The SVG's tile spans 8..120 of its 128-unit viewBox, so the viewBox is widened until those 112
# units are 824/1024 of it: 112 * 1024 / 824 = 139.184 units, starting at 8 - (139.184 - 112) / 2.
set -euo pipefail

root=$(cd "$(dirname "$0")/../.." && pwd)
svg=$root/data/icons/hicolor/scalable/apps/stream.sugo.app.svg
out=$root/data/icons/macos

command -v rsvg-convert >/dev/null || { echo "rsvg-convert not found (install librsvg)" >&2; exit 1; }
grep -q 'viewBox="0 0 128 128"' "$svg" || { echo "unexpected viewBox in $svg: update this script" >&2; exit 1; }

mkdir -p "$out"
for size in 16 32 64 128 256 512 1024; do
    png=$out/$size-apps-stream.sugo.app.png
    sed 's/viewBox="0 0 128 128"/viewBox="-5.592 -5.592 139.184 139.184"/' "$svg" \
        | rsvg-convert --width "$size" --height "$size" --format png --output "$png"
    printf 'SPDX-License-Identifier: GPL-3.0-or-later\n' >"$png.license"
    echo "$png"
done
