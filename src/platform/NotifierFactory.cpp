// SPDX-License-Identifier: GPL-3.0-or-later
#include "NotifierFactory.h"

#include "TrayNotifier.h"

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QGuiApplication>

#include "FreedesktopNotifier.h"
#endif

std::unique_ptr<Notifier> createNotifier(std::function<QSystemTrayIcon *()> trayIcon, QObject *parent)
{
#ifdef Q_OS_LINUX
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        return std::make_unique<FreedesktopNotifier>(bus, QGuiApplication::applicationDisplayName(), QGuiApplication::desktopFileName(), parent);
    }
#endif
    return std::make_unique<TrayNotifier>(std::move(trayIcon), parent);
}
