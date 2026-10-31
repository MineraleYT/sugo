#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The interface takes its colours from theme/Theme.qml (and the tray icon's from TrayIconStyle):
# fails on any colour written into the other QML files as a name, a hex code or
# Qt.rgb/rgba/hsl/hsla. "transparent" (no fill, for outlines) is allowed.
set -euo pipefail

qml_dir=${1:?usage: check_qml_colours.sh <qml directory>}
pattern='"(white|black|red|green|blue|yellow|orange|gray|grey|silver|purple|pink|brown|cyan|magenta)"|"#[0-9a-fA-F]{3,8}"|Qt\.(rgba?|hsla?|hsva?)\('

if matches=$(grep -rnEi --include='*.qml' --exclude='Theme.qml' "$pattern" "$qml_dir"); then
    echo "Hard-coded colours in the QML (use Theme or TrayIconStyle):" >&2
    echo "$matches" >&2
    exit 1
fi
echo "No hard-coded colours"
