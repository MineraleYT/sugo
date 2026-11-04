// SPDX-License-Identifier: GPL-3.0-or-later
#include "Settings.h"

#include <QDir>
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
    const QString path = settings.fileName();
    const QFile::Permissions ownerOnly = QFile::ReadOwner | QFile::WriteOwner;
    // QSettings writes through QSaveFile, which keeps the mode of an existing file but gives a new
    // one the default mode (0644 under the usual umask) until we could change it. Create the file
    // private and empty first, so its contents are never readable by others, not even briefly.
    if (!QFileInfo::exists(path)) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::NewOnly, ownerOnly)) {
            file.close();
        }
    } else {
        QFile::setPermissions(path, ownerOnly);
    }
    settings.sync();
    if (QFileInfo::exists(path)) {
        QFile::setPermissions(path, ownerOnly);
    }
}

} // namespace Settings
