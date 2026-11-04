// SPDX-License-Identifier: GPL-3.0-or-later
#include "MacBundle.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace MacBundle
{
QString path()
{
#ifdef Q_OS_MACOS
    return path(QCoreApplication::applicationDirPath());
#else
    return QString();
#endif
}

QString path(const QString &executableDir)
{
    if (executableDir.isEmpty()) {
        return QString();
    }
    const QDir macos(QDir::cleanPath(executableDir));
    if (macos.dirName() != QLatin1String("MacOS")) {
        return QString();
    }
    const QString bundle = QDir::cleanPath(macos.absolutePath() + QStringLiteral("/../.."));
    if (!bundle.endsWith(QLatin1String(".app")) || !QFileInfo(bundle + QStringLiteral("/Contents/Info.plist")).isFile()) {
        return QString();
    }
    return bundle;
}
} // namespace MacBundle
