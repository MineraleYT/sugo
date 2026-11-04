// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSettings>
#include <QString>

#include <memory>

/**
 * The settings file of the program, through QSettings (INI format, the same on every system).
 *
 * The keys of the former [General] group are at the top level (QSettings writes them under
 * [General]); the other groups are paths such as "Calendar/nextcloud/Username". The file is
 * private to the user: it can hold the calendar password when no keyring answers.
 */
namespace Settings
{
using Ptr = std::shared_ptr<QSettings>;

/// <config dir>/sugo/sugo.conf (on macOS <data dir>/sugo/sugo.conf); $SUGO_DATA_DIR/sugo.conf when set.
QString defaultPath();
/// The settings of the program, shared by everyone who asks.
Ptr openDefault();
/// The settings in `path` (tests, or a file of one's own).
Ptr open(const QString &path);

/// Writes pending changes to disk and keeps the file readable by the user only.
void sync(QSettings &settings);
} // namespace Settings
