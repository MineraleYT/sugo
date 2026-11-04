// SPDX-License-Identifier: GPL-3.0-or-later
#include "SystemTray.h"

#include <QtGlobal>

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusConnectionInterface>

bool systemTrayAvailable()
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.isConnected() && bus.interface()
        && bus.interface()->isServiceRegistered(QStringLiteral("org.kde.StatusNotifierWatcher"));
}
#elif defined(Q_OS_MACOS)
#include <QSystemTrayIcon>

bool systemTrayAvailable()
{
    // The menu bar is always there.
    return QSystemTrayIcon::isSystemTrayAvailable();
}
#else
bool systemTrayAvailable()
{
    return false;
}
#endif
