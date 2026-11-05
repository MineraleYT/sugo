// SPDX-License-Identifier: GPL-3.0-or-later
#include "NotifierFactory.h"

#include "TrayNotifier.h"

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QGuiApplication>

#include "FreedesktopNotifier.h"
#endif

#ifdef Q_OS_MACOS
#include "MacNotifier.h"
#endif

std::unique_ptr<Notifier> createNotifier(std::function<QSystemTrayIcon *()> trayIcon, QObject *parent)
{
#ifdef Q_OS_LINUX
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        auto notifier = std::make_unique<FreedesktopNotifier>(bus, QGuiApplication::applicationDisplayName(),
                                                              QGuiApplication::desktopFileName(), parent);
        // No notification server on the bus: a message from the tray icon, if there is one.
        notifier->setFallback(std::make_unique<TrayNotifier>(std::move(trayIcon)));
        return notifier;
    }
#endif
#ifdef Q_OS_MACOS
    // The Notification Center needs an application bundle; while notifications are not
    // allowed, MacNotifier hands them to the menu bar icon.
    if (MacNotifier::isSupported()) {
        return std::make_unique<MacNotifier>(std::make_unique<TrayNotifier>(std::move(trayIcon)), parent);
    }
#endif
    return std::make_unique<TrayNotifier>(std::move(trayIcon), parent);
}
