// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>

namespace MacBundle
{
/**
 * The .app bundle this program runs from (".../Sugo.app"), or an empty string: always empty
 * outside macOS, and for a binary not inside a bundle (a build run from the build tree's bin
 * folder still is in one, build/bin/Sugo.app).
 */
QString path();

/// path() for the folder holding the executable, `<bundle>/Contents/MacOS` (tests).
QString path(const QString &executableDir);
} // namespace MacBundle
