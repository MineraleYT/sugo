// SPDX-License-Identifier: GPL-3.0-or-later
#include "FreedesktopInhibitor.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QVariantMap>

Q_LOGGING_CATEGORY(lcInhibitor, "sugo.platform.inhibitor")

namespace
{
const QString kService = QStringLiteral("org.freedesktop.Notifications");
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");
const QString kInterface = QStringLiteral("org.freedesktop.Notifications");
constexpr int kCallTimeoutMs = 3000;
// libdbus's DBUS_TIMEOUT_INFINITE. The Inhibit reply is the only place the cookie appears: one
// that came after a timeout would be dropped, leaving an inhibition nothing could end. So Inhibit
// waits as long as the server takes; a server that goes away ends the wait (onOwnerChanged()).
constexpr int kNoTimeout = 0x7fffffff;
constexpr int kMaxUnInhibitAttempts = 3;
} // namespace

FreedesktopInhibitor::FreedesktopInhibitor(const QDBusConnection &bus, const QString &appId, QObject *parent)
    : NotificationInhibitor(parent)
    , m_bus(bus)
    , m_appId(appId)
    , m_watcher(kService, m_bus, QDBusServiceWatcher::WatchForOwnerChange)
{
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &FreedesktopInhibitor::onOwnerChanged);

    // Who serves notifications now; from then on the watcher reports every change. Answers and
    // signals arrive in the order the bus sent them, so whichever comes last is the newest.
    QDBusMessage question = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
                                                           QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
    question.setArguments({kService});
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(question, kCallTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusPendingReply<QString> reply = *w;
        m_owner = reply.isError() ? QString() : reply.value(); // an error: no owner
    });
}

void FreedesktopInhibitor::inhibit(const QString &reason)
{
    m_wanted = true;
    m_reason = reason;
    sync();
}

void FreedesktopInhibitor::release()
{
    m_wanted = false;
    sync();
}

// The whole behaviour: with no call in flight, make the server match what is wanted.
// Every completed call ends by running this again, so a change of mind during a call
// is picked up as soon as the call returns.
void FreedesktopInhibitor::sync()
{
    if (m_busy) {
        return;
    }
    if (m_wanted && !m_hasCookie) {
        // To the current owner's unique name, so only that server can answer, and the cookie is
        // known to belong to it. With no owner known, to the service name, which may start one.
        m_cookieOwner = m_owner.isEmpty() ? kService : m_owner;
        QDBusMessage message = QDBusMessage::createMethodCall(m_cookieOwner, kPath, kInterface, QStringLiteral("Inhibit"));
        message.setArguments({m_appId, m_reason, QVariant::fromValue(QVariantMap())});
        startCall(message, kNoTimeout, &FreedesktopInhibitor::onInhibitFinished);
    } else if (!m_wanted && m_hasCookie) {
        QDBusMessage message = QDBusMessage::createMethodCall(m_cookieOwner, kPath, kInterface, QStringLiteral("UnInhibit"));
        message.setArguments({m_cookie});
        startCall(message, kCallTimeoutMs, &FreedesktopInhibitor::onUnInhibitFinished);
    }
}

void FreedesktopInhibitor::startCall(const QDBusMessage &message, int timeoutMs,
                                     void (FreedesktopInhibitor::*handler)(QDBusPendingCallWatcher *))
{
    m_busy = true;
    report();
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, timeoutMs), this);
    const quint64 generation = m_generation;
    const QString destination = message.service();
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, handler, generation, destination](QDBusPendingCallWatcher *w) {
        if (generation != m_generation) {
            // Sent before an owner change: onOwnerChanged() already reset the state and asks the
            // current server, so the answer is dropped. But a granted Inhibit is an inhibition
            // nothing else knows of: hand it back. Sent to a unique name, it was granted by that
            // very connection; if the server is gone, the call just fails. (Sent to the service
            // name, the owner was unknown: only an activation, which bumps no generation, or a
            // change right at startup; there the server that granted it cannot be told.)
            w->deleteLater();
            if (handler == &FreedesktopInhibitor::onInhibitFinished && destination != kService) {
                const QDBusPendingReply<quint32> reply = *w;
                if (!reply.isError()) {
                    QDBusMessage giveBack = QDBusMessage::createMethodCall(destination, kPath, kInterface, QStringLiteral("UnInhibit"));
                    giveBack.setArguments({reply.value()});
                    m_bus.send(giveBack); // nothing to do with the answer
                }
            }
            return;
        }
        (this->*handler)(w);
    });
}

void FreedesktopInhibitor::onInhibitFinished(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();
    QDBusPendingReply<quint32> reply = *watcher;
    m_busy = false;

    if (reply.isError()) {
        qCWarning(lcInhibitor) << "Cannot silence notifications:" << reply.error().message();
        // No retry loop: still wanted, so the next inhibit() or a (re)started notification
        // service tries again. isInhibited() turning false makes the failure visible.
        report();
        return;
    }

    m_cookie = reply.value();
    m_hasCookie = true;
    if (m_cookieOwner == kService && !m_owner.isEmpty()) {
        m_cookieOwner = m_owner; // the server the call started, which no owner change has replaced
    }
    report();
    sync(); // release() may have been called while the call was in flight
}

void FreedesktopInhibitor::onUnInhibitFinished(QDBusPendingCallWatcher *watcher)
{
    watcher->deleteLater();
    QDBusPendingReply<> reply = *watcher;
    m_busy = false;

    if (reply.isError()) {
        qCWarning(lcInhibitor) << "UnInhibit failed:" << reply.error().message();
    }
    // A timeout says nothing: the server may still hold the inhibition, so sync() asks again,
    // a few times only so a hung server cannot keep us calling (or block shutdown) forever.
    // Any other error means the server no longer knows the cookie (it restarted) or is gone;
    // either way nothing of ours is held, as the server drops inhibitions with the connection.
    const bool timedOut = reply.isError() && reply.error().type() == QDBusError::NoReply;
    if (!timedOut || ++m_unInhibitTimeouts >= kMaxUnInhibitAttempts) {
        m_hasCookie = false;
        m_unInhibitTimeouts = 0;
    }
    report();
    sync(); // inhibit() may have been called again while the call was in flight
}

// The notification service restarted (for example plasmashell): inhibitions died with it.
// A service that has only just appeared took nothing with it, and a call in flight may be the very
// one that started it (D-Bus activation): its reply, a cookie of the new server, is still to come.
void FreedesktopInhibitor::onOwnerChanged(const QString &, const QString &oldOwner, const QString &newOwner)
{
    m_owner = newOwner;
    if (!oldOwner.isEmpty()) {
        ++m_generation; // abandon any call still in flight to the old owner
        m_busy = false;
        m_hasCookie = false;
        report();
    }
    if (!newOwner.isEmpty()) {
        sync(); // still wanted: take the inhibition (again) on the new server, after a call in flight
    }
}

void FreedesktopInhibitor::report()
{
    const bool now = isInhibited();
    if (now != m_reported) {
        m_reported = now;
        Q_EMIT inhibitedChanged();
    }
}
