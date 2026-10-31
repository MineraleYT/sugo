// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusConnection>
#include <QDBusServiceWatcher>

#include "NotificationInhibitor.h"

class QDBusPendingCallWatcher;

/**
 * Do Not Disturb through org.freedesktop.Notifications.Inhibit / UnInhibit.
 *
 * Why this and not Plasma's own "Do Not Disturb" setting: the inhibition belongs to our
 * D-Bus connection, so Plasma removes it when the application exits or crashes, and it
 * is independent of the user's manual Do Not Disturb. A setting written to
 * plasmanotifyrc would outlive a crash and would overwrite the user's own choice.
 *
 * The object is a small asynchronous state machine: it remembers what is *wanted*
 * and, one D-Bus call at a time, converges the server to it. That makes
 * release-while-acquiring, re-acquiring, errors and a restarting notification
 * service all fall out of the same two rules in sync().
 */
class FreedesktopInhibitor final : public NotificationInhibitor
{
    Q_OBJECT

public:
    /// `appId` is the desktop file name the server shows as the inhibiting application.
    FreedesktopInhibitor(const QDBusConnection &bus, const QString &appId, QObject *parent = nullptr);

    bool isAvailable() const override { return true; }
    void inhibit(const QString &reason) override;
    void release() override;
    bool isInhibited() const override { return m_hasCookie || m_busy; }
    bool isReleasing() const override { return isInhibited() && !m_wanted; }

private:
    void sync();
    void startCall(const QDBusMessage &message, void (FreedesktopInhibitor::*handler)(QDBusPendingCallWatcher *));
    void onInhibitFinished(QDBusPendingCallWatcher *watcher);
    void onUnInhibitFinished(QDBusPendingCallWatcher *watcher);
    void onOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void report();

    QDBusConnection m_bus;
    QString m_appId;
    QDBusServiceWatcher m_watcher;

    bool m_wanted = false;   ///< What the caller asked for.
    QString m_reason;
    bool m_hasCookie = false; ///< An inhibition is held on the server.
    quint32 m_cookie = 0;
    bool m_busy = false;      ///< A D-Bus call is in flight; only one at a time.
    int m_unInhibitTimeouts = 0; ///< UnInhibit calls in a row left unanswered.
    bool m_reported = false;  ///< Last value announced through inhibitedChanged().
    quint64 m_generation = 0; ///< Bumped on every owner change; replies from older ones are ignored.
};
