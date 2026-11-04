#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail

app=$1
mode=${2:-startup}
runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/sugo-smoke.XXXXXX")
trap 'rm -rf "$runtime_dir"' EXIT

# Like GNU `timeout --signal=TERM --kill-after=2s <seconds>`, also with bash 3.2 and BSD tools
# (macOS): runs the command, sends SIGTERM after <seconds>, SIGKILL 2 s later. Exit status: the
# command's, 124 if it was stopped with SIGTERM, 137 if it needed SIGKILL.
run_with_timeout() {
    local seconds=$1
    shift
    rm -f "$runtime_dir/timed-out" "$runtime_dir/killed"
    "$@" &
    local pid=$!
    (
        ticks=$((seconds * 10))
        while ((ticks > 0)) && kill -0 "$pid" 2>/dev/null; do
            sleep 0.1
            ticks=$((ticks - 1))
        done
        kill -0 "$pid" 2>/dev/null || exit 0
        : >"$runtime_dir/timed-out"
        kill -TERM "$pid" 2>/dev/null
        ticks=20
        while ((ticks > 0)) && kill -0 "$pid" 2>/dev/null; do
            sleep 0.1
            ticks=$((ticks - 1))
        done
        if kill -0 "$pid" 2>/dev/null; then
            : >"$runtime_dir/killed"
            kill -KILL "$pid" 2>/dev/null
        fi
    ) </dev/null >/dev/null 2>&1 &
    local watcher=$!
    local status=0
    wait "$pid" || status=$?
    wait "$watcher" 2>/dev/null || true
    if [[ -e $runtime_dir/killed ]]; then
        return 137
    elif [[ -e $runtime_dir/timed-out ]]; then
        return 124
    fi
    return "$status"
}

# Isolated data: SUGO_DATA_DIR holds the database and the settings on every system (on macOS
# QStandardPaths ignores the XDG variables); the XDG variables isolate everything else on Linux.
mkdir -p "$runtime_dir/data/sugo"
isolation="SUGO_DATA_DIR=$runtime_dir/data/sugo"

# The output of the quit and startup runs, where main.cpp does not count QML warnings itself:
# the script looks for them there, with the pattern of shutdown_app.sh.
log=$runtime_dir/out.log
show_log() {
    cat "$log" >&2
}
check_qml_output() {
    if grep -E "TypeError|ReferenceError|Cannot read property|QML .* (warning|error)|\.qml:[0-9]+" "$log" >/dev/null; then
        echo "QML errors or warnings in the $mode smoke test:" >&2
        show_log
        exit 1
    fi
}

# damaged-db: the timer page's checks, started on a sugo.db that is not a database. Sugo must
# move it aside, start a new one and say so (Main.qml fails with 55 otherwise).
smoke_page=$mode
if [[ $mode == damaged-db ]]; then
    printf 'This is not an SQLite database.\n' >"$runtime_dir/data/sugo/sugo.db"
    smoke_page=timer
fi

set +e
# Quit: started normally, then ended with SIGTERM (as the system does at logout): it must quit
# by itself, cleanly (exit status 0, no QML warnings), within 10 seconds. Works without D-Bus,
# so on every system (Linux also runs tst_app_shutdown, over a private bus).
if [[ $mode == quit ]]; then
    env QT_QPA_PLATFORM=offscreen \
        DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null \
        "$isolation" \
        XDG_DATA_HOME="$runtime_dir/data" \
        XDG_CONFIG_HOME="$runtime_dir/config" \
        "$app" >"$log" 2>&1 &
    pid=$!
    sleep 4
    if ! kill -0 "$pid" 2>/dev/null; then
        wait "$pid"
        echo "Sugo ended by itself with status $? before it was asked to quit" >&2
        show_log
        exit 1
    fi
    kill -TERM "$pid"
    ticks=100
    while ((ticks > 0)) && kill -0 "$pid" 2>/dev/null; do
        sleep 0.1
        ticks=$((ticks - 1))
    done
    if kill -0 "$pid" 2>/dev/null; then
        kill -KILL "$pid" 2>/dev/null
        echo "Sugo did not quit within 10 seconds of SIGTERM" >&2
        show_log
        exit 1
    fi
    wait "$pid"
    status=$?
    if [[ $status -ne 0 ]]; then
        echo "Sugo quit with status $status after SIGTERM (expected 0)" >&2
        show_log
        exit 1
    fi
    check_qml_output
    exit 0
fi

if [[ $mode != startup ]]; then
    if [[ $mode == stats ]]; then
        run_with_timeout 8 env QT_QPA_PLATFORM=offscreen \
            SUGO_STATS_SMOKE_TEST=1 \
            DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null \
            "$isolation" \
            XDG_DATA_HOME="$runtime_dir/data" \
            XDG_CONFIG_HOME="$runtime_dir/config" \
            "$app"
    else
        run_with_timeout 8 env QT_QPA_PLATFORM=offscreen \
            SUGO_SMOKE_PAGE="$smoke_page" \
            DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null \
            "$isolation" \
            XDG_DATA_HOME="$runtime_dir/data" \
            XDG_CONFIG_HOME="$runtime_dir/config" \
            "$app"
    fi
    status=$?
    set -e
    if [[ $status -ne 0 ]]; then
        echo "Sugo $mode UI smoke test exited with status $status (expected 0)" >&2
        exit 1
    fi
    if [[ $mode == damaged-db ]] && ! compgen -G "$runtime_dir/data/sugo/sugo.db.corrupt-*" >/dev/null; then
        echo "Sugo did not keep the damaged database aside as sugo.db.corrupt-*" >&2
        exit 1
    fi
    exit 0
fi

run_with_timeout 8 env QT_QPA_PLATFORM=offscreen \
    DBUS_SESSION_BUS_ADDRESS=unix:path=/dev/null \
    "$isolation" \
    XDG_DATA_HOME="$runtime_dir/data" \
    XDG_CONFIG_HOME="$runtime_dir/config" \
    "$app" >"$log" 2>&1
status=$?
set -e

# The timeout is expected: the GUI must initialize and remain alive until stopped.
if [[ $status -ne 124 ]]; then
    echo "Sugo startup smoke test exited with status $status (expected timeout status 124)" >&2
    show_log
    exit 1
fi
check_qml_output
