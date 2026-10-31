// SPDX-License-Identifier: GPL-3.0-or-later
#include "Settings.h"

#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace Settings
{
QString defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/sugo/sugo.conf");
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
