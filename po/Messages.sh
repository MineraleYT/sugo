#! /usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Translation string extraction script for Sugo
set -e

podir=${podir:-po}
mkdir -p "$podir"

XGETTEXT=${XGETTEXT:-xgettext}
MSGCAT=${MSGCAT:-msgcat}

CPP_FILES=$(find src -name "*.cpp" -o -name "*.h")
QML_FILES=$(find src -name "*.qml")

$XGETTEXT --from-code=UTF-8 -C \
    --keyword=i18n:1 --keyword=i18nc:1c,2 --keyword=i18np:1,2 --keyword=i18ncp:1c,2,3 \
    -o "$podir/sugo_cpp.pot" $CPP_FILES

$XGETTEXT --from-code=UTF-8 --language=JavaScript \
    --keyword=i18n:1 --keyword=i18nc:1c,2 --keyword=i18np:1,2 --keyword=i18ncp:1c,2,3 \
    -o "$podir/sugo_qml.pot" $QML_FILES

$MSGCAT --use-first -o "$podir/sugo.pot" "$podir/sugo_cpp.pot" "$podir/sugo_qml.pot"
rm -f "$podir/sugo_cpp.pot" "$podir/sugo_qml.pot"
