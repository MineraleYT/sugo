// SPDX-License-Identifier: GPL-3.0-or-later
#include "PresetRepository.h"

#include "PresetModel.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

#include <algorithm>

Q_LOGGING_CATEGORY(lcPresetRepo, "sugo.db.presets")

namespace
{
const QString kCurrentKey = QStringLiteral("currentPresetUuid");

// QSqlQuery binds a null QString (e.g. a default-constructed one) as SQL NULL, which the
// NOT NULL text columns reject. Bind empty text instead.
QString text(const QString &value)
{
    return value.isNull() ? QStringLiteral("") : value;
}

// Option values are stored as JSON, wrapped in an array so scalars are valid documents.
QString encodeValue(const QVariant &value)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{QJsonValue::fromVariant(value)}).toJson(QJsonDocument::Compact));
}

QVariant decodeValue(const QString &text)
{
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    return doc.isArray() && !doc.array().isEmpty() ? doc.array().first().toVariant() : QVariant();
}
} // namespace

PresetRepository::PresetRepository(const QString &connectionName)
    : m_connection(connectionName)
{
}

void PresetRepository::setNowProvider(std::function<QDateTime()> provider)
{
    m_now = std::move(provider);
}

qint64 PresetRepository::nowMs() const
{
    return (m_now ? m_now() : QDateTime::currentDateTimeUtc()).toMSecsSinceEpoch();
}

QList<TimerPreset> PresetRepository::loadAll(bool *ok) const
{
    if (ok) {
        *ok = false;
    }
    QSqlDatabase db = QSqlDatabase::database(m_connection, false);

    QHash<qint64, QVariantMap> optionsById;
    QSqlQuery optionQuery(db);
    if (!optionQuery.exec(QStringLiteral("SELECT preset_id, key, value FROM preset_option"))) {
        qCWarning(lcPresetRepo) << optionQuery.lastError().text();
        return {};
    }
    while (optionQuery.next()) {
        const QVariant value = decodeValue(optionQuery.value(2).toString());
        if (value.isValid()) {
            optionsById[optionQuery.value(0).toLongLong()].insert(optionQuery.value(1).toString(), value);
        } else {
            // Left in the database untouched (saveAll() keeps it), in case a later version reads it.
            qCWarning(lcPresetRepo) << "Ignoring an unreadable value of timer option" << optionQuery.value(1).toString();
        }
    }

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT id, uuid, name, category, work_sec, short_break_sec, long_break_sec, "
                                   "cycles_before_long, is_builtin, created_at, updated_at, updated_by "
                                   "FROM preset ORDER BY sort_order, id"))) {
        qCWarning(lcPresetRepo) << query.lastError().text();
        return {};
    }

    QList<TimerPreset> result;
    while (query.next()) {
        TimerPreset p;
        p.uuid = query.value(1).toString();
        p.name = query.value(2).toString();
        p.category = query.value(3).toString();
        p.workSeconds = query.value(4).toInt();
        p.shortBreakSeconds = query.value(5).toInt();
        p.longBreakSeconds = query.value(6).toInt();
        p.cyclesBeforeLong = query.value(7).toInt();
        p.builtin = query.value(8).toBool();
        p.createdAt = query.value(9).toLongLong();
        p.updatedAt = query.value(10).toLongLong();
        p.updatedBy = query.value(11).toString();
        p.options = optionsById.value(query.value(0).toLongLong());
        result.append(p);
    }
    if (ok) {
        *ok = true;
    }
    return result;
}

bool PresetRepository::saveAll(const QList<TimerPreset> &presets, const QList<PresetTombstone> &received)
{
    QSqlDatabase db = QSqlDatabase::database(m_connection, false);
    if (!db.transaction()) {
        qCWarning(lcPresetRepo) << db.lastError().text();
        return false;
    }

    auto fail = [&](const QSqlQuery &q) {
        qCWarning(lcPresetRepo) << q.lastError().text();
        db.rollback();
        return false;
    };

    QSqlQuery upsert(db);
    upsert.prepare(QStringLiteral(
        "INSERT INTO preset (uuid, name, category, work_sec, short_break_sec, long_break_sec, "
        "cycles_before_long, is_builtin, sort_order, created_at, updated_at, updated_by) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(uuid) DO UPDATE SET name = excluded.name, category = excluded.category, "
        "work_sec = excluded.work_sec, short_break_sec = excluded.short_break_sec, "
        "long_break_sec = excluded.long_break_sec, cycles_before_long = excluded.cycles_before_long, "
        "is_builtin = excluded.is_builtin, sort_order = excluded.sort_order, "
        "created_at = excluded.created_at, updated_at = excluded.updated_at, updated_by = excluded.updated_by"));

    QSqlQuery findId(db);
    findId.prepare(QStringLiteral("SELECT id FROM preset WHERE uuid = ?"));
    QSqlQuery clearOptions(db);
    clearOptions.prepare(QStringLiteral("DELETE FROM preset_option WHERE preset_id = ? AND key = ?"));
    QSqlQuery storedOptions(db);
    storedOptions.prepare(QStringLiteral("SELECT key, value FROM preset_option WHERE preset_id = ?"));
    QSqlQuery insertOption(db);
    insertOption.prepare(QStringLiteral("INSERT INTO preset_option (preset_id, key, value) VALUES (?, ?, ?)"));

    QSet<QString> keep;
    const qint64 now = nowMs();
    for (int i = 0; i < presets.size(); ++i) {
        const TimerPreset &p = presets.at(i);
        keep.insert(p.uuid);

        upsert.addBindValue(p.uuid);
        upsert.addBindValue(text(p.name));
        upsert.addBindValue(text(p.category));
        upsert.addBindValue(p.workSeconds);
        upsert.addBindValue(p.shortBreakSeconds);
        upsert.addBindValue(p.longBreakSeconds);
        upsert.addBindValue(p.cyclesBeforeLong);
        upsert.addBindValue(p.builtin ? 1 : 0);
        upsert.addBindValue(i);
        upsert.addBindValue(p.createdAt);
        upsert.addBindValue(p.updatedAt);
        upsert.addBindValue(text(p.updatedBy));
        if (!upsert.exec()) {
            return fail(upsert);
        }

        findId.addBindValue(p.uuid);
        if (!findId.exec() || !findId.next()) {
            return fail(findId);
        }
        const qint64 id = findId.value(0).toLongLong();
        findId.finish();

        // Rewrite the options, except stored values that could not be read (loadAll() skipped them,
        // so the model cannot have meant to remove them): those stay as they are.
        storedOptions.addBindValue(id);
        if (!storedOptions.exec()) {
            return fail(storedOptions);
        }
        QStringList replaced;
        while (storedOptions.next()) {
            const QString key = storedOptions.value(0).toString();
            if (decodeValue(storedOptions.value(1).toString()).isValid() || p.options.contains(key)) {
                replaced.append(key);
            }
        }
        storedOptions.finish();
        for (const QString &key : std::as_const(replaced)) {
            clearOptions.addBindValue(id);
            clearOptions.addBindValue(key);
            if (!clearOptions.exec()) {
                return fail(clearOptions);
            }
        }
        for (auto it = p.options.cbegin(); it != p.options.cend(); ++it) {
            insertOption.addBindValue(id);
            insertOption.addBindValue(it.key());
            insertOption.addBindValue(encodeValue(it.value()));
            if (!insertOption.exec()) {
                return fail(insertOption);
            }
        }
    }

    // Drop presets that no longer exist (the built-in one is never removed).
    QSqlQuery existing(db);
    if (!existing.exec(QStringLiteral("SELECT uuid, updated_at FROM preset WHERE is_builtin = 0"))) {
        return fail(existing);
    }
    QStringList stale;
    QHash<QString, qint64> staleUpdatedAt;
    while (existing.next()) {
        const QString uuid = existing.value(0).toString();
        if (!keep.contains(uuid)) {
            stale.append(uuid);
            staleUpdatedAt.insert(uuid, existing.value(1).toLongLong());
        }
    }
    existing.finish();
    for (const QString &uuid : std::as_const(stale)) {
        QSqlQuery remove(db);
        remove.prepare(QStringLiteral("DELETE FROM preset WHERE uuid = ?"));
        remove.addBindValue(uuid);
        if (!remove.exec()) {
            return fail(remove);
        }
    }

    // Tombstones: the received ones as they are, then one stamped now for each preset deleted
    // here without one; none for a preset that exists, nor for the built-in one.
    QHash<QString, PresetTombstone> tombstones;
    for (const PresetTombstone &t : received) {
        if (!t.uuid.isEmpty() && !keep.contains(t.uuid) && t.uuid != PresetModel::DefaultUuid) {
            tombstones.insert(t.uuid, t);
        }
    }
    for (const QString &uuid : std::as_const(stale)) {
        if (!tombstones.contains(uuid)) {
            // After the version deleted, even when this computer's clock is behind its author's.
            tombstones.insert(uuid, PresetTombstone{uuid, std::max(now, staleUpdatedAt.value(uuid) + 1), QString()});
        }
    }
    QSqlQuery writeTombstone(db);
    writeTombstone.prepare(QStringLiteral(
        "INSERT INTO preset_tombstone (uuid, deleted_at, deleted_by) VALUES (?, ?, ?) "
        "ON CONFLICT(uuid) DO UPDATE SET deleted_at = excluded.deleted_at, deleted_by = excluded.deleted_by"));
    for (const PresetTombstone &t : std::as_const(tombstones)) {
        writeTombstone.addBindValue(t.uuid);
        writeTombstone.addBindValue(t.deletedAt);
        writeTombstone.addBindValue(text(t.deletedBy));
        if (!writeTombstone.exec()) {
            return fail(writeTombstone);
        }
    }
    QSqlQuery dropTombstone(db);
    dropTombstone.prepare(QStringLiteral("DELETE FROM preset_tombstone WHERE uuid = ?"));
    for (const QString &uuid : std::as_const(keep)) {
        dropTombstone.addBindValue(uuid);
        if (!dropTombstone.exec()) {
            return fail(dropTombstone);
        }
    }
    QSqlQuery expire(db);
    expire.prepare(QStringLiteral("DELETE FROM preset_tombstone WHERE deleted_at < ?"));
    expire.addBindValue(now - TombstoneLifetimeMs);
    if (!expire.exec()) {
        return fail(expire);
    }

    if (!db.commit()) {
        qCWarning(lcPresetRepo) << db.lastError().text();
        db.rollback();
        return false;
    }
    return true;
}

QList<PresetTombstone> PresetRepository::tombstones(bool *ok) const
{
    if (ok) {
        *ok = false;
    }
    QSqlQuery query(QSqlDatabase::database(m_connection, false));
    if (!query.exec(QStringLiteral("SELECT uuid, deleted_at, deleted_by FROM preset_tombstone ORDER BY deleted_at, uuid"))) {
        qCWarning(lcPresetRepo) << query.lastError().text();
        return {};
    }
    QList<PresetTombstone> result;
    while (query.next()) {
        result.append(PresetTombstone{query.value(0).toString(), query.value(1).toLongLong(), query.value(2).toString()});
    }
    if (ok) {
        *ok = true;
    }
    return result;
}

QString PresetRepository::currentUuid() const
{
    QSqlDatabase db = QSqlDatabase::database(m_connection, false);
    QSqlQuery query(db);
    query.prepare(QStringLiteral("SELECT value FROM meta WHERE key = ?"));
    query.addBindValue(kCurrentKey);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return {};
}

bool PresetRepository::setCurrentUuid(const QString &uuid)
{
    QSqlDatabase db = QSqlDatabase::database(m_connection, false);
    QSqlQuery query(db);
    query.prepare(QStringLiteral("INSERT INTO meta (key, value) VALUES (?, ?) "
                                 "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
    query.addBindValue(kCurrentKey);
    query.addBindValue(uuid);
    if (!query.exec()) {
        qCWarning(lcPresetRepo) << query.lastError().text();
        return false;
    }
    return true;
}
