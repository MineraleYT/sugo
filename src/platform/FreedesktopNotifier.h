// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QHash>
#include <QPointer>
#include <QStringList>

#include <memory>

#include "Notifier.h"

class QDBusError;

/**
 * Notifications through org.freedesktop.Notifications (Desktop Notifications Specification),
 * straight over D-Bus.
 *
 * Every notification first asks the server for its capabilities (it may have been restarted, or
 * replaced, since the last one): actions are sent only to a server that shows them, and the
 * reply field only to one with KDE's "inline-reply" (answered by `NotificationReplied`);
 * elsewhere it becomes an ordinary action.
 *
 * The handlers of a notification are kept until the server reports it closed, or goes away.
 *
 * Where the bus has no notification server (a window manager without a notification daemon), a
 * notification goes to the fallback notifier, if one was set, instead of being lost.
 */
class FreedesktopNotifier final : public Notifier
{
    Q_OBJECT

public:
    /// `appName` is shown by the server; `desktopEntry` (the desktop file name, without
    /// ".desktop") ties the notifications to the application in the desktop's settings.
    FreedesktopNotifier(const QDBusConnection &bus, const QString &appName, const QString &desktopEntry, QObject *parent = nullptr);

    void notify(QObject *context, const Notification &notification) override;

    /// Shows the notifications no server is there for (NotifierFactory gives a TrayNotifier).
    void setFallback(std::unique_ptr<Notifier> fallback);

private Q_SLOTS:
    void onActionInvoked(uint id, const QString &actionKey);
    void onActivationToken(uint id, const QString &token);
    void onNotificationReplied(uint id, const QString &text);
    void onNotificationClosed(uint id, uint reason);

private:
    /// What a notification on screen still needs.
    struct Shown {
        QPointer<QObject> context;
        QHash<QString, Handler> actions; ///< By action key.
        std::function<void(const QString &)> replied;
        QString activationToken; ///< Sent by the server just before the action it belongs to.
    };

    void send(QObject *context, const Notification &notification, const QStringList &capabilities);
    /// Hands `notification` to the fallback if `error` says there is no server; false if not.
    bool showElsewhere(const QDBusError &error, QObject *context, const Notification &notification);

    QDBusConnection m_bus;
    QString m_appName;
    QString m_desktopEntry;
    QDBusServiceWatcher m_watcher;
    QHash<uint, Shown> m_shown;
    std::unique_ptr<Notifier> m_fallback;
    quint64 m_generation = 0; ///< Bumped when the server changes; answers from an older one are dropped.
};
