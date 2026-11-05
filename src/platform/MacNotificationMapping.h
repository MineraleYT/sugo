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

/// The category for `actions`: "stream.sugo.app.actions.<hash>", empty if there are none.
QString categoryIdentifier(const QList<ActionSpec> &actions);
} // namespace MacNotificationMapping

/**
 * The handlers of the notifications on display, by request identifier.
 *
 * A notification's handlers run only while its `context` exists, and are forgotten once the
 * user has answered or dismissed it, or a newer notification of the same context replaces it
 * (takeEarlier()). Notifications nobody answers are dropped oldest first beyond `capacity`.
 */
class MacNotificationRegistry
{
public:
    explicit MacNotificationRegistry(int capacity = 32);

    /// Keeps the handlers of `notification` and returns the identifier of a new request.
    QString add(QObject *context, const Notifier::Notification &notification);
    /// Forgets a request.
    void remove(const QString &requestId);
    /// A new notification of `context` replaces its earlier ones: forgets them (and the requests
    /// whose context no longer exists) and returns their identifiers, oldest first, so their
    /// banners can be taken out of the Notification Center.
    QStringList takeEarlier(QObject *context);

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

/**
 * The notifications waiting for MacNotifier to learn whether it may show them (the setting is
 * read before each one, and asked for while the user has not answered the question yet), by
 * ticket.
 *
 * A notification replaces the earlier waiting ones of its context, as it would replace them on
 * screen, so a question left unanswered for a while does not let them pile up and arrive all at
 * once when it is answered. While a question is outstanding, notifications that find the
 * permission undecided wait for its answer instead of asking again.
 */
class MacPendingNotifications
{
public:
    struct Item {
        QObject *context = nullptr;
        Notifier::Notification notification;
    };

    /// Keeps `notification` until take(), dropping the earlier ones of `context` (and those whose
    /// context no longer exists). Returns its ticket.
    quint64 add(QObject *context, const Notifier::Notification &notification);
    /// Forgets the ticket and returns its notification, unless it was replaced, already taken,
    /// or its context no longer exists.
    std::optional<Item> take(quint64 ticket);

    /// The question for the permission is to be asked: true if none is outstanding (and it
    /// now is), false if the one already asked has not been answered yet.
    bool beginQuestion();
    /// `ticket` waits for the answer to the outstanding question.
    void waitForAnswer(quint64 ticket);
    /// The question was answered: no longer outstanding. Returns the tickets that waited for it
    /// and are still there, oldest first.
    QList<quint64> endQuestion();

    int size() const { return int(m_entries.size()); }

private:
    struct Entry {
        QPointer<QObject> context;
        Notifier::Notification notification;
    };

    QHash<quint64, Entry> m_entries;
    QList<quint64> m_waiting; ///< For the answer, oldest first.
    quint64 m_nextTicket = 1;
    bool m_asking = false;
};
