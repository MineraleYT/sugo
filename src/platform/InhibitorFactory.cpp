// SPDX-License-Identifier: GPL-3.0-or-later
#include "InhibitorFactory.h"

#include "NoopInhibitor.h"

#ifdef Q_OS_MACOS
#include "ShortcutsFocusInhibitor.h"
#endif

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QGuiApplication>
#include <QLoggingCategory>

#include "FreedesktopInhibitor.h"

Q_LOGGING_CATEGORY(lcInhibitorFactory, "sugo.platform.inhibitor.factory")

namespace
{
const QString kService = QStringLiteral("org.freedesktop.Notifications");
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");

/// Asks a running notification server whether it offers Inhibit (introspection).
bool serverSupportsInhibit(const QDBusConnection &bus)
{
    QDBusInterface server(kService, kPath, kService, bus);
    return server.isValid() && server.metaObject()->indexOfMethod("Inhibit(QString,QString,QVariantMap)") >= 0;
}
} // namespace
#endif

std::unique_ptr<NotificationInhibitor> createNotificationInhibitor(QObject *parent)
{
#ifdef Q_OS_LINUX
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (bus.isConnected()) {
        const bool serverRunning = bus.interface() && bus.interface()->isServiceRegistered(kService);
        if (!serverRunning || serverSupportsInhibit(bus)) {
            return std::make_unique<FreedesktopInhibitor>(bus, QGuiApplication::desktopFileName(), parent);
        }
        qCInfo(lcInhibitorFactory) << "The notification server has no Inhibit method; silencing is unavailable";
    }
#endif
#ifdef Q_OS_MACOS
    // A Focus through the user's two shortcuts; unavailable until they exist and are chosen.
    return std::make_unique<ShortcutsFocusInhibitor>(ShortcutsFocusInhibitor::DefaultProgram, parent);
#else
    return std::make_unique<NoopInhibitor>(parent);
#endif
}
