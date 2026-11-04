#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The interface takes its colours from theme/Theme.qml (and the tray icon's from TrayIconStyle):
# fails on any colour written into the other QML files as a name or a hex code (in double or
# single quotes) or Qt.rgb/rgba/hsl/hsla. "transparent" (no fill, for outlines) is allowed.
set -euo pipefail

qml_dir=${1:?usage: check_qml_colours.sh <qml directory>}
# Colour names: the SVG/CSS names Qt accepts (QColor::colorNames()), the common ones at least.
names='white|black|red|green|blue|yellow|orange|gr[ae]y|darkgr[ae]y|dimgr[ae]y|lightgr[ae]y|slategr[ae]y|lightslategr[ae]y|darkslategr[ae]y|gainsboro|whitesmoke|silver'
names+='|navy|darkblue|mediumblue|royalblue|steelblue|lightsteelblue|lightblue|skyblue|lightskyblue|deepskyblue|dodgerblue|cornflowerblue|cadetblue|powderblue|aliceblue|midnightblue|slateblue|darkslateblue|mediumslateblue'
names+='|teal|darkcyan|cyan|aqua|aquamarine|turquoise|darkturquoise|mediumturquoise|lightcyan|azure'
names+='|olive|olivedrab|darkolivegreen|lime|limegreen|lightgreen|darkgreen|forestgreen|seagreen|mediumseagreen|darkseagreen|lightseagreen|springgreen|mediumspringgreen|palegreen|lawngreen|chartreuse|greenyellow|yellowgreen|honeydew|mintcream'
names+='|maroon|darkred|firebrick|crimson|indianred|lightcoral|salmon|darksalmon|lightsalmon|tomato|orangered|coral|darkorange'
names+='|purple|indigo|violet|darkviolet|blueviolet|mediumpurple|rebeccapurple|darkmagenta|magenta|fuchsia|orchid|mediumorchid|darkorchid|plum|thistle|lavender|lavenderblush'
names+='|pink|lightpink|hotpink|deeppink|mediumvioletred|palevioletred|mistyrose'
names+='|brown|saddlebrown|sienna|chocolate|peru|tan|burlywood|wheat|sandybrown|rosybrown|goldenrod|darkgoldenrod|palegoldenrod'
names+='|gold|khaki|darkkhaki|lightyellow|lemonchiffon|lightgoldenrodyellow|beige|ivory|cornsilk|bisque|blanchedalmond|moccasin|navajowhite|papayawhip|peachpuff|antiquewhite|oldlace|linen|floralwhite|seashell|snow|ghostwhite'
# Both quote kinds: "red" and 'red' are the same colour in QML.
pattern="\"($names)\"|'($names)'|\"#[0-9a-fA-F]{3,8}\"|'#[0-9a-fA-F]{3,8}'|Qt\\.(rgba?|hsla?|hsva?)\\("

if matches=$(grep -rnEi --include='*.qml' --exclude='Theme.qml' "$pattern" "$qml_dir"); then
    echo "Hard-coded colours in the QML (use Theme or TrayIconStyle):" >&2
    echo "$matches" >&2
    exit 1
fi
echo "No hard-coded colours"
