// SPDX-License-Identifier: GPL-3.0-or-later
#include "MacNotificationMapping.h"

#include <QCryptographicHash>
#include <QUuid>

namespace MacNotificationMapping
{
QString defaultActionId()
{
    return QStringLiteral("default");
}

QString dismissActionId()
{
    return QStringLiteral("dismiss");
}

QString replyActionId()
{
    return QStringLiteral("reply");
}

QList<ActionSpec> actionsFor(const Notifier::Notification &notification)
{
    QList<ActionSpec> result;
    for (qsizetype i = 0; i < notification.actions.size(); ++i) {
        ActionSpec spec;
        spec.identifier = QStringLiteral("action-%1").arg(i);
        spec.title = notification.actions.at(i).label;
        result.append(spec);
    }
    if (notification.reply) {
        ActionSpec spec;
        spec.identifier = replyActionId();
        spec.title = notification.reply->label;
        spec.textInput = true;
        spec.submitTitle = notification.reply->submitLabel;
        spec.placeholder = notification.reply->placeholder;
        result.append(spec);
    }
    return result;
}

QString categoryIdentifier(const QList<ActionSpec> &actions)
{
    if (actions.isEmpty()) {
        return QString();
    }
    QCryptographicHash hash(QCryptographicHash::Sha1);
    for (const ActionSpec &spec : actions) {
        // Every field prefixed with its length, so no two different sets give the same bytes.
        for (const QString &field : {spec.identifier, spec.title, spec.textInput ? QStringLiteral("1") : QStringLiteral("0"),
                                     spec.submitTitle, spec.placeholder}) {
            const QByteArray bytes = field.toUtf8();
            QByteArray length = QByteArray::number(bytes.size());
            length.append(':');
            hash.addData(length);
            hash.addData(bytes);
        }
    }
    return QStringLiteral("io.github.mineraleyt.sugo.actions.") + QString::fromLatin1(hash.result().toHex().left(16));
}
} // namespace MacNotificationMapping

MacNotificationRegistry::MacNotificationRegistry(int capacity)
    : m_capacity(qMax(1, capacity))
{
}

QString MacNotificationRegistry::add(QObject *context, const Notifier::Notification &notification)
{
    const QString id = QStringLiteral("io.github.mineraleyt.sugo.") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    Entry entry;
    entry.context = context;
    entry.notification = notification;
    m_entries.insert(id, entry);
    m_order.append(id);
    while (m_order.size() > m_capacity) {
        m_entries.remove(m_order.takeFirst());
    }
    return id;
}

void MacNotificationRegistry::remove(const QString &requestId)
{
    m_entries.remove(requestId);
    m_order.removeOne(requestId);
}

QStringList MacNotificationRegistry::takeEarlier(QObject *context)
{
    QStringList taken;
    for (const QString &id : std::as_const(m_order)) {
        const QObject *owner = m_entries.value(id).context.data();
        if (!owner || owner == context) {
            taken.append(id);
        }
    }
    for (const QString &id : std::as_const(taken)) {
        remove(id);
    }
    return taken;
}

std::optional<MacNotificationRegistry::Unshown> MacNotificationRegistry::takeUnshown(const QString &requestId)
{
    const auto it = m_entries.constFind(requestId);
    if (it == m_entries.cend()) {
        return std::nullopt;
    }
    const Entry entry = *it;
    remove(requestId);
    if (!entry.context) {
        return std::nullopt;
    }
    return Unshown{entry.context.data(), entry.notification};
}

bool MacNotificationRegistry::respond(const QString &requestId, const QString &actionId, const QString &text)
{
    const auto it = m_entries.constFind(requestId);
    if (it == m_entries.cend()) {
        return false;
    }
    // Copied before the entry goes: the handler may show another notification.
    const Entry entry = *it;
    remove(requestId);
    if (!entry.context) {
        return false;
    }
    const Notifier::Notification &notification = entry.notification;
    if (actionId == MacNotificationMapping::replyActionId()) {
        if (notification.reply && notification.reply->replied) {
            notification.reply->replied(text);
            return true;
        }
        return false;
    }
    Notifier::Handler handler;
    if (actionId == MacNotificationMapping::defaultActionId()) {
        handler = notification.defaultAction;
    } else {
        // The identifiers actionsFor() gave the buttons.
        for (qsizetype i = 0; i < notification.actions.size(); ++i) {
            if (actionId == QStringLiteral("action-%1").arg(i)) {
                handler = notification.actions.at(i).handler;
                break;
            }
        }
    }
    if (handler) {
        handler(QString());
        return true;
    }
    return false;
}

quint64 MacPendingNotifications::add(QObject *context, const Notifier::Notification &notification)
{
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        const QObject *owner = it->context.data();
        if (!owner || owner == context) {
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
    const quint64 ticket = m_nextTicket++;
    m_entries.insert(ticket, Entry{context, notification});
    return ticket;
}

std::optional<MacPendingNotifications::Item> MacPendingNotifications::take(quint64 ticket)
{
    m_waiting.removeOne(ticket);
    const auto it = m_entries.constFind(ticket);
    if (it == m_entries.cend()) {
        return std::nullopt;
    }
    const Entry entry = *it;
    m_entries.erase(it);
    if (!entry.context) {
        return std::nullopt;
    }
    return Item{entry.context.data(), entry.notification};
}

bool MacPendingNotifications::beginQuestion()
{
    if (m_asking) {
        return false;
    }
    m_asking = true;
    return true;
}

void MacPendingNotifications::waitForAnswer(quint64 ticket)
{
    if (!m_waiting.contains(ticket)) {
        m_waiting.append(ticket);
    }
}

QList<quint64> MacPendingNotifications::endQuestion()
{
    m_asking = false;
    QList<quint64> answered;
    for (const quint64 ticket : std::as_const(m_waiting)) {
        if (m_entries.contains(ticket)) {
            answered.append(ticket);
        }
    }
    m_waiting.clear();
    return answered;
}
