#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sizes in the interface come from theme/Theme.qml: fails on a literal width, height, spacing,
# margin, radius, pixel size or line width of 2 px or more, and on "Units.x + 4" style offsets.
# Allowed: 0 and 1 (no spacing, layout weights, 1 px borders), and artwork drawn in its own
# coordinates and scaled as a whole, between "// artwork-begin" and "// artwork-end".
set -euo pipefail

qml_dir=${1:?usage: check_qml_sizes.sh <qml directory>}
pattern='\b(width|height|spacing|[a-zA-Z]*[mM]argin|radius|[a-zA-Z]*Radius|pixelSize|lineWidth|border\.width)\s*:\s*([2-9]|[1-9][0-9]+)(\.[0-9]+)?\s*(;|$)|(Units\.[a-zA-Z.]+|gridUnit)\s*[-+]\s*[2-9]'

found=0
while IFS= read -r -d '' file; do
    # Lines outside the artwork, as "file:line: text"; then the pattern.
    hits=$(awk '
        /\/\/ artwork-begin/ { art = 1 }
        /\/\/ artwork-end/ { art = 0 }
        !art { printf "%s:%d: %s\n", FILENAME, FNR, $0 }
    ' "$file" | grep -E "$pattern" || true)
    if [[ -n $hits ]]; then
        echo "$hits" >&2
        found=1
    fi
done < <(find "$qml_dir" -name '*.qml' ! -name 'Theme.qml' -print0)

if [[ $found -ne 0 ]]; then
    echo "Hard-coded sizes in the QML (use Theme)" >&2
    exit 1
fi
echo "No hard-coded sizes"
