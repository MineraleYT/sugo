// SPDX-License-Identifier: GPL-3.0-or-later
#include "Settings.h"

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtGlobal>

namespace Settings
{
QString defaultPath()
{
    // Tests and smoke runs: one directory for the database and the settings (QStandardPaths
    // ignores the XDG_* variables on macOS).
    const QString dataDir = qEnvironmentVariable("SUGO_DATA_DIR");
    if (!dataDir.isEmpty()) {
        return dataDir + QStringLiteral("/sugo.conf");
    }
#ifdef Q_OS_MACOS
    // Next to the database in ~/Library/Application Support: ~/Library/Preferences is for plists.
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/sugo/sugo.conf");
#else
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/sugo/sugo.conf");
#endif
}

Ptr open(const QString &path)
{
    return std::make_shared<QSettings>(path, QSettings::IniFormat);
}

Ptr openDefault()
{
    static std::weak_ptr<QSettings> shared;
    if (Ptr existing = shared.lock()) {
        return existing;
    }
    Ptr settings = open(defaultPath());
    shared = settings;
    return settings;
}

void sync(QSettings &settings)
{
    settings.sync();
    if (QFileInfo::exists(settings.fileName())) {
        QFile::setPermissions(settings.fileName(), QFile::ReadOwner | QFile::WriteOwner);
    }
}

} // namespace Settings
