#! /usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Translation string extraction script for Sugo
set -euo pipefail

# Paths below are relative to the source tree, wherever the script is started from.
cd "$(dirname "$0")/.."

podir=${podir:-po}
mkdir -p "$podir"

XGETTEXT=${XGETTEXT:-xgettext}
MSGCAT=${MSGCAT:-msgcat}

# Null-separated, so file names with spaces stay whole.
find src \( -name "*.cpp" -o -name "*.h" \) -print0 | LC_ALL=C sort -z |
    xargs -0 "$XGETTEXT" --from-code=UTF-8 -C \
    --package-name=sugo --msgid-bugs-address=https://github.com/sugoHQ/sugo/issues \
    --keyword=i18n:1 --keyword=i18nc:1c,2 --keyword=i18np:1,2 --keyword=i18ncp:1c,2,3 \
    -o "$podir/sugo_cpp.pot"

find src -name "*.qml" -print0 | LC_ALL=C sort -z |
    xargs -0 "$XGETTEXT" --from-code=UTF-8 --language=JavaScript \
    --package-name=sugo --msgid-bugs-address=https://github.com/sugoHQ/sugo/issues \
    --keyword=i18n:1 --keyword=i18nc:1c,2 --keyword=i18np:1,2 --keyword=i18ncp:1c,2,3 \
    -o "$podir/sugo_qml.pot"

$MSGCAT --use-first -o "$podir/sugo.pot" "$podir/sugo_cpp.pot" "$podir/sugo_qml.pot"
rm -f "$podir/sugo_cpp.pot" "$podir/sugo_qml.pot"
