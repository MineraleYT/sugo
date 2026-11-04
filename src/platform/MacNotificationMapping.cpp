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
            hash.addData(QByteArray::number(bytes.size()) + ':');
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
    if (notification.defaultAction) {
        entry.actions.insert(MacNotificationMapping::defaultActionId(), notification.defaultAction);
    }
    for (qsizetype i = 0; i < notification.actions.size(); ++i) {
        entry.actions.insert(QStringLiteral("action-%1").arg(i), notification.actions.at(i).handler);
    }
    if (notification.reply) {
        entry.replied = notification.reply->replied;
    }
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
    if (actionId == MacNotificationMapping::replyActionId()) {
        if (entry.replied) {
            entry.replied(text);
            return true;
        }
        return false;
    }
    const Notifier::Handler handler = entry.actions.value(actionId);
    if (handler) {
        handler(QString());
        return true;
    }
    return false;
}
