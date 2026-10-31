// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppImage.h"

#include <QCoreApplication>
#include <QDir>

namespace AppImage
{
QString path()
{
    return path(qEnvironmentVariable("APPIMAGE"), qEnvironmentVariable("APPDIR"), QCoreApplication::applicationFilePath());
}

QString path(const QString &appImage, const QString &appDir, const QString &executable)
{
    if (appImage.isEmpty() || appDir.isEmpty() || executable.isEmpty()) {
        return QString();
    }
    const QString mount = QDir::cleanPath(appDir) + QLatin1Char('/');
    return QDir::cleanPath(executable).startsWith(mount) ? appImage : QString();
}
} // namespace AppImage
