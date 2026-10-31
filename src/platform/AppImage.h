// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace AppImage
{
/**
 * The AppImage file this program runs from, or an empty string.
 *
 * The AppImage runtime sets $APPIMAGE (the file) and $APPDIR (where it is mounted) for the
 * program it starts, and every process started from there inherits them: a Sugo built from
 * source and launched from inside another AppImage (a terminal in an AppImage editor) would
 * take that other file for its own, and replace it with an update. So $APPIMAGE only counts
 * when this executable lies inside $APPDIR.
 */
QString path();

/// path() for the given environment and executable (tests).
QString path(const QString &appImage, const QString &appDir, const QString &executable);
} // namespace AppImage
