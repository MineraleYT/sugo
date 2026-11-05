#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Development: puts Sugo on Linux and on macOS side by side. Takes the same screenshots of every
# screen on both (offscreen, isolated data), runs the inventory of what each screen offers
# (tests/check_platform_parity.py) on both, and writes an HTML report: each screen side by side,
# with what differs and why (tests/platform-parity.json).
#
# usage: tests/compare_platforms.sh <Linux sugo binary> <output directory>
#   SUGO_MAC_SSH  the command that reaches the Mac, e.g. "ssh -i ~/.ssh/key user@host"
#   SUGO_MAC_APP  the Sugo binary there, e.g. ~/sugo/build/bin/Sugo.app/Contents/MacOS/Sugo
# Build both from the same commit first. Without SUGO_MAC_SSH only the Linux side is made.
set -euo pipefail

app=$1
out=$2
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out/linux" "$out/macos"

screens="timer presets stats settings/general settings/timers settings/sounds settings/focus settings/calendar settings/sync settings/advanced settings/about welcome/0 welcome/1 welcome/2 welcome/3"

# The environment of one isolated, offscreen run; the welcome tour shows only on request.
# $1: data directory, $2: whether the tour was seen.
run_env() {
    printf 'HOME=%s QT_QPA_PLATFORM=offscreen SUGO_DATA_DIR=%s/data XDG_DATA_HOME=%s/xdg-data XDG_CONFIG_HOME=%s/xdg-config DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null' "$1" "$1" "$1" "$1"
}

shoot_linux() {
    local screen=$1 file=$2 tmp
    tmp=$(mktemp -d "${TMPDIR:-/tmp}/sugo-compare.XXXXXX")
    mkdir -p "$tmp/data"
    printf '[General]\nFirstRunCompleted=true\n' >"$tmp/data/sugo.conf"
    # shellcheck disable=SC2046
    env $(run_env "$tmp") SUGO_START_PAGE="$screen" SUGO_SCREENSHOT="$file" SUGO_SCREENSHOT_DELAY=3000 \
        timeout 30 "$app" >/dev/null 2>&1 || true
    rm -rf "$tmp"
}

echo "Linux: screenshots and inventory"
for screen in $screens; do
    shoot_linux "$screen" "$out/linux/${screen//\//-}.png"
done
python3 -I "$here/check_platform_parity.py" --dump "$app" "$out/linux/inventory.json"

if [[ -n ${SUGO_MAC_SSH:-} ]]; then
    echo "macOS: screenshots and inventory"
    mac_app=${SUGO_MAC_APP:?SUGO_MAC_APP: the Sugo binary on the Mac}
    remote=$($SUGO_MAC_SSH 'mktemp -d /tmp/sugo-compare.XXXXXX')
    for screen in $screens; do
        name=${screen//\//-}
        $SUGO_MAC_SSH "t=\$(mktemp -d /tmp/sugo-compare-run.XXXXXX); mkdir -p \$t/data; printf '[General]\nFirstRunCompleted=true\n' > \$t/data/sugo.conf; \
            env $(run_env '$t') SUGO_START_PAGE=$screen SUGO_SCREENSHOT=$remote/$name.png SUGO_SCREENSHOT_DELAY=3000 \
            $mac_app >/dev/null 2>&1; rm -rf \$t" || true
        $SUGO_MAC_SSH "cat $remote/$name.png" >"$out/macos/$name.png" 2>/dev/null || rm -f "$out/macos/$name.png"
    done
    $SUGO_MAC_SSH "cat >$remote/check.py" <"$here/check_platform_parity.py"
    $SUGO_MAC_SSH "/usr/bin/python3 -I $remote/check.py --dump $mac_app $remote/inventory.json && cat $remote/inventory.json" \
        >"$out/macos/inventory.json"
    $SUGO_MAC_SSH "rm -rf $remote"
fi

python3 -I "$here/check_platform_parity.py" --report "$here/platform-parity.json" "$out" "$screens"
echo "Report: $out/index.html"
