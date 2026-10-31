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
#else
bool systemTrayAvailable()
{
    return false;
}
#endif
