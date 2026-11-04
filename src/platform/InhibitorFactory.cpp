// SPDX-License-Identifier: GPL-3.0-or-later
#include "InhibitorFactory.h"

#include "NoopInhibitor.h"

#ifdef Q_OS_MACOS
#include "ShortcutsFocusInhibitor.h"
#endif

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QStringList>
#include <QXmlStreamReader>

#include "FreedesktopInhibitor.h"

Q_LOGGING_CATEGORY(lcInhibitorFactory, "sugo.platform.inhibitor.factory")

namespace
{
const QString kService = QStringLiteral("org.freedesktop.Notifications");
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");
// Asked on the GUI thread before the window shows: a server still busy starting (plasmashell at
// login) must not hold the first window back for D-Bus's default 25 s.
constexpr int kIntrospectTimeoutMs = 2000;

/// Whether introspection data lists Inhibit(s, s, a{sv}) in the notification interface.
bool listsInhibit(const QString &introspection)
{
    QXmlStreamReader xml(introspection);
    bool inInterface = false;
    bool inInhibit = false;
    QStringList arguments;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto attributes = xml.attributes();
            if (xml.name() == QLatin1String("interface")) {
                inInterface = attributes.value(QLatin1String("name")) == kService;
            } else if (inInterface && xml.name() == QLatin1String("method")) {
                inInhibit = attributes.value(QLatin1String("name")) == QLatin1String("Inhibit");
                arguments.clear();
            } else if (inInhibit && xml.name() == QLatin1String("arg")
                       && attributes.value(QLatin1String("direction")) != QLatin1String("out")) {
                arguments << attributes.value(QLatin1String("type")).toString();
            }
        } else if (xml.isEndElement()) {
            if (xml.name() == QLatin1String("interface")) {
                inInterface = false;
            } else if (inInhibit && xml.name() == QLatin1String("method")) {
                if (arguments == QStringList{QStringLiteral("s"), QStringLiteral("s"), QStringLiteral("a{sv}")}) {
                    return true;
                }
                inInhibit = false;
            }
        }
    }
    return false;
}

/// Asks a running notification server whether it offers Inhibit (introspection, with a short timeout).
bool serverSupportsInhibit(const QDBusConnection &bus)
{
    const QDBusMessage introspect = QDBusMessage::createMethodCall(
        kService, kPath, QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
    const QDBusMessage reply = bus.call(introspect, QDBus::Block, kIntrospectTimeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        const QDBusError::ErrorType error = QDBusError(reply).type();
        if (error == QDBusError::NoReply || error == QDBusError::Timeout) {
            // Too busy to answer yet: like a server not started yet, assume it can. The inhibitor
            // copes with a refused Inhibit, and a silencing that fails is shown as not working.
            qCInfo(lcInhibitorFactory) << "The notification server did not answer in time; trying Inhibit anyway";
            return true;
        }
        return false;
    }
    return !reply.arguments().isEmpty() && listsInhibit(reply.arguments().constFirst().toString());
}
} // namespace

std::unique_ptr<NotificationInhibitor> createNotificationInhibitor(const QDBusConnection &bus, QObject *parent)
{
    if (bus.isConnected()) {
        const bool serverRunning = bus.interface() && bus.interface()->isServiceRegistered(kService);
        if (!serverRunning || serverSupportsInhibit(bus)) {
            return std::make_unique<FreedesktopInhibitor>(bus, QGuiApplication::desktopFileName(), parent);
        }
        qCInfo(lcInhibitorFactory) << "The notification server has no Inhibit method; silencing is unavailable";
    }
    return std::make_unique<NoopInhibitor>(parent);
}
#endif

std::unique_ptr<NotificationInhibitor> createNotificationInhibitor(QObject *parent)
{
#ifdef Q_OS_LINUX
    return createNotificationInhibitor(QDBusConnection::sessionBus(), parent);
#elif defined(Q_OS_MACOS)
    // A Focus through the user's two shortcuts; unavailable until they exist and are chosen.
    return std::make_unique<ShortcutsFocusInhibitor>(ShortcutsFocusInhibitor::DefaultProgram, parent);
#else
    return std::make_unique<NoopInhibitor>(parent);
#endif
}
