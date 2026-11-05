// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QList>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <optional>

#include "Notifier.h"

/**
 * What MacNotifier sends to UNUserNotificationCenter and how it finds the handlers again, in
 * plain C++ so it is tested on every platform.
 *
 * - Every action becomes a UNNotificationAction "action-<n>"; the reply field a
 *   UNTextInputNotificationAction "reply". A click on the notification itself arrives as the
 *   system's default action, which MacNotifier passes here as defaultActionId().
 * - Actions belong to a category, registered once per distinct set of actions: its identifier
 *   is derived from everything the actions are made of, so equal sets share it.
 */
namespace MacNotificationMapping
{
/// A click on the notification (UNNotificationDefaultActionIdentifier).
QString defaultActionId();
/// The notification was dismissed (UNNotificationDismissActionIdentifier): nothing to run.
QString dismissActionId();
/// The text field.
QString replyActionId();

struct ActionSpec {
    QString identifier;
    QString title;
    bool textInput = false;
    QString submitTitle; ///< Text input only: the button that sends the text.
    QString placeholder; ///< Text input only.
};

/// The buttons of `notification`, in order: its actions, then the reply field.
QList<ActionSpec> actionsFor(const Notifier::Notification &notification);

/// The category for `actions`: "io.github.mineraleyt.sugo.actions.<hash>", empty if there are none.
QString categoryIdentifier(const QList<ActionSpec> &actions);
} // namespace MacNotificationMapping

/**
 * The handlers of the notifications on display, by request identifier.
 *
 * A notification's handlers run only while its `context` exists, and are forgotten once the
 * user has answered it. Notifications nobody answers are dropped oldest first beyond `capacity`.
 */
class MacNotificationRegistry
{
public:
    explicit MacNotificationRegistry(int capacity = 32);

    /// Keeps the handlers of `notification` and returns the identifier of a new request.
    QString add(QObject *context, const Notifier::Notification &notification);
    /// Forgets a request.
    void remove(const QString &requestId);

    /// A request that could not be shown, handed back so another notifier can show it.
    struct Unshown {
        QObject *context = nullptr;
        Notifier::Notification notification;
    };
    /// Request `requestId` could not be shown: forgets it and returns its notification, unless
    /// it is unknown (already answered or dropped) or its context no longer exists.
    std::optional<Unshown> takeUnshown(const QString &requestId);
    /// The user answered request `requestId` with action `actionId` (`text`: what they typed
    /// in the reply field). Runs the handler, if there is one and its context still exists, and
    /// forgets the request. True if a handler ran.
    bool respond(const QString &requestId, const QString &actionId, const QString &text = QString());

    bool contains(const QString &requestId) const { return m_entries.contains(requestId); }
    int size() const { return int(m_entries.size()); }

private:
    struct Entry {
        QPointer<QObject> context;
        Notifier::Notification notification; ///< As given: its handlers, and all of it for takeUnshown().
    };

    int m_capacity;
    QHash<QString, Entry> m_entries;
    QStringList m_order; ///< Request identifiers, oldest first.
};
