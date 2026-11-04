#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Starts Sugo on a private session bus, checks that its D-Bus objects answer, then ends it
# with SIGTERM: the program must shut down cleanly (exit 0, no QML errors). glibc's malloc
# checks are on and freed memory is overwritten, so an object used after it was deleted during
# the teardown is likely to crash the run instead of passing unnoticed.
set -euo pipefail

app=$1
runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/sugo-shutdown.XXXXXX")
daemon_pid=
app_pid=
cleanup() {
    [[ -n $app_pid ]] && kill -9 "$app_pid" 2>/dev/null || true
    [[ -n $daemon_pid ]] && kill "$daemon_pid" 2>/dev/null || true
    rm -rf "$runtime_dir"
}
trap cleanup EXIT

if ! command -v dbus-daemon >/dev/null || ! command -v dbus-send >/dev/null; then
    echo "dbus-daemon or dbus-send is missing; skipping"
    exit 77
fi

# A bus without service activation, like tests/PrivateDBus.h: nothing installed gets started.
cat >"$runtime_dir/bus.conf" <<'EOF'
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
EOF
exec 3< <(dbus-daemon --config-file="$runtime_dir/bus.conf" --nofork --print-address=1 --print-pid=1)
read -r address <&3
read -r daemon_pid <&3
export DBUS_SESSION_BUS_ADDRESS=$address

# SUGO_DATA_DIR, read before the XDG variables, must not reach the user's real data either.
mkdir -p "$runtime_dir/data/sugo"
env QT_QPA_PLATFORM=offscreen \
    SUGO_DATA_DIR="$runtime_dir/data/sugo" \
    XDG_DATA_HOME="$runtime_dir/data" \
    XDG_CONFIG_HOME="$runtime_dir/config" \
    MALLOC_CHECK_=3 MALLOC_PERTURB_=165 \
    "$app" >"$runtime_dir/out.log" 2>&1 &
app_pid=$!

status_call() {
    dbus-send --session --print-reply --dest=io.github.mineraleyt.sugo /Timer \
        io.github.mineraleyt.sugo.Timer.status >/dev/null 2>&1
}
for _ in $(seq 1 100); do
    status_call && break
    sleep 0.1
done
if ! status_call; then
    echo "The /Timer object never answered on the session bus" >&2
    cat "$runtime_dir/out.log" >&2
    exit 1
fi
# Something running while it quits: the session is saved, the silencing handed back.
dbus-send --session --print-reply --dest=io.github.mineraleyt.sugo /Timer io.github.mineraleyt.sugo.Timer.start >/dev/null

kill -TERM "$app_pid"
set +e
# Keep calling while it shuts down: a call that reaches an object being destroyed must not crash.
while kill -0 "$app_pid" 2>/dev/null; do
    status_call
done &
caller_pid=$!
timeout 10 tail --pid="$app_pid" -f /dev/null
wait "$caller_pid" 2>/dev/null
wait "$app_pid"
status=$?
set -e
app_pid=

if [[ $status -ne 0 ]]; then
    echo "Sugo exited with status $status after SIGTERM (expected 0)" >&2
    cat "$runtime_dir/out.log" >&2
    exit 1
fi
if grep -E "TypeError|ReferenceError|Cannot read property|QML .* (warning|error)|\.qml:[0-9]+" "$runtime_dir/out.log" >&2; then
    echo "QML errors during the shutdown" >&2
    exit 1
fi
echo "Clean shutdown"
