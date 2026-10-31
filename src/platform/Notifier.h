// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

/**
 * Shows desktop notifications with actions.
 *
 * Implementations: FreedesktopNotifier (Linux, org.freedesktop.Notifications over D-Bus) and
 * TrayNotifier (a message from the tray icon, elsewhere). NotifierFactory picks one. What a
 * desktop cannot show is left out: a tray message has only the click on it.
 *
 * Nothing here plays a sound: Sugo plays its own, at the volume the user chose.
 */
class Notifier : public QObject
{
public:
    /// Runs when the user picks an action. `activationToken` (empty if the desktop gave none)
    /// lets a window take the focus on Wayland.
    using Handler = std::function<void(const QString &activationToken)>;

    struct Action {
        QString label;
        Handler handler;
    };

    /// A text field in the notification ("inline reply"). On a desktop without one, it is an
    /// ordinary action labelled `label` that runs `fallback`.
    struct Reply {
        QString label;
        QString placeholder;
        QString submitLabel;
        QString submitIconName;
        std::function<void(const QString &text)> replied;
        Handler fallback;
    };

    struct Notification {
        QString title;
        QString text;
        QString iconName;
        QList<Action> actions;
        QString defaultActionLabel;
        Handler defaultAction; ///< A click on the notification itself; none if empty.
        std::optional<Reply> reply;
    };

    using QObject::QObject;

    /// Shows `notification`. Its handlers run only while `context` exists, and are forgotten
    /// once the notification is closed.
    virtual void notify(QObject *context, const Notification &notification) = 0;
};
