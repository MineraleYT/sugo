#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

app=$1
mode=${2:-startup}
runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/sugo-smoke.XXXXXX")
trap 'rm -rf "$runtime_dir"' EXIT

set +e
if [[ $mode != startup ]]; then
    if [[ $mode == stats ]]; then
        env QT_QPA_PLATFORM=offscreen \
        SUGO_STATS_SMOKE_TEST=1 \
        DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null \
        XDG_DATA_HOME="$runtime_dir/data" \
        XDG_CONFIG_HOME="$runtime_dir/config" \
        timeout --signal=TERM --kill-after=2s 8s "$app"
    else
        env QT_QPA_PLATFORM=offscreen \
        SUGO_SMOKE_PAGE="$mode" \
        DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null \
        XDG_DATA_HOME="$runtime_dir/data" \
        XDG_CONFIG_HOME="$runtime_dir/config" \
        timeout --signal=TERM --kill-after=2s 8s "$app"
    fi
    status=$?
    set -e
    if [[ $status -ne 0 ]]; then
        echo "Sugo $mode UI smoke test exited with status $status (expected 0)" >&2
        exit 1
    fi
    exit 0
fi

env QT_QPA_PLATFORM=offscreen \
    DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null \
    XDG_DATA_HOME="$runtime_dir/data" \
    XDG_CONFIG_HOME="$runtime_dir/config" \
    timeout --signal=TERM --kill-after=2s 8s "$app"
status=$?
set -e

# The timeout is expected: the GUI must initialize and remain alive until stopped.
if [[ $status -ne 124 ]]; then
    echo "Sugo startup smoke test exited with status $status (expected timeout status 124)" >&2
    exit 1
fi
