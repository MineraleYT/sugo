// SPDX-License-Identifier: GPL-3.0-or-later
#include "PresetSyncFormat.h"

#include "Localization.h"
#include "PresetModel.h"
#include "PresetSettings.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>

#include <algorithm>
#include <optional>

namespace PresetSyncFormat
{
namespace
{
const QString kIconName = QStringLiteral("iconName");

// --- Reading ---------------------------------------------------------------

qint64 timeValue(const QJsonValue &value)
{
    // JSON numbers are doubles: milliseconds since the epoch fit exactly (below 2^53). Brought
    // into that range before the conversion, which is undefined (and differs by CPU) outside it.
    return value.isDouble() ? qint64(std::clamp(value.toDouble(), 0.0, 9007199254740992.0)) : 0;
}

int intValue(const QJsonValue &value, int low, int high, int fallback)
{
    if (!value.isDouble()) {
        return fallback;
    }
    const double d = std::clamp(value.toDouble(), double(low), double(high));
    return int(d);
}

QString textValue(const QJsonValue &value)
{
    return value.toString().trimmed().left(PresetLimits::MaxNameLength).trimmed();
}

/// Known options in their own type and range, the icon as a name; any other key as it came.
QVariantMap optionsValue(const QJsonObject &object)
{
    QVariantMap options;
    const QList<OptionDescriptor> descriptors = PresetSettings::descriptors();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString key = it.key();
        if (key == kIconName) {
            const QString icon = it.value().toString().trimmed();
            if (!icon.isEmpty()) {
                options.insert(key, icon);
            }
            continue;
        }
        const auto known = std::find_if(descriptors.cbegin(), descriptors.cend(),
                                        [&key](const OptionDescriptor &d) { return d.key == key; });
        if (known == descriptors.cend()) {
            options.insert(key, it.value().toVariant()); // written by a newer Sugo: kept, sent back
        } else if (known->type == QLatin1String("int")) {
            options.insert(key, intValue(it.value(), known->minimum, known->maximum, known->defaultValue.toInt()));
        } else {
            options.insert(key, it.value().isBool() ? it.value().toBool() : known->defaultValue.toBool());
        }
    }
    return options;
}

std::optional<TimerPreset> presetValue(const QJsonValue &value)
{
    if (!value.isObject()) {
        return std::nullopt;
    }
    const QJsonObject o = value.toObject();
    TimerPreset p;
    p.uuid = o.value(QLatin1String("uuid")).toString().trimmed();
    if (p.uuid.isEmpty()) {
        return std::nullopt;
    }
    p.name = textValue(o.value(QLatin1String("name")));
    if (p.name.isEmpty()) {
        p.name = i18n("New Timer");
    }
    p.category = textValue(o.value(QLatin1String("category")));
    p.workSeconds = intValue(o.value(QLatin1String("workSeconds")), 1, PresetLimits::MaxWorkSeconds, p.workSeconds);
    p.shortBreakSeconds = intValue(o.value(QLatin1String("shortBreakSeconds")), 0, PresetLimits::MaxBreakSeconds, p.shortBreakSeconds);
    p.longBreakSeconds = intValue(o.value(QLatin1String("longBreakSeconds")), 0, PresetLimits::MaxBreakSeconds, p.longBreakSeconds);
    p.cyclesBeforeLong = intValue(o.value(QLatin1String("cyclesBeforeLong")), 0, PresetLimits::MaxCycles, p.cyclesBeforeLong);
    p.builtin = p.uuid == PresetModel::DefaultUuid;
    p.options = optionsValue(o.value(QLatin1String("options")).toObject());
    p.createdAt = timeValue(o.value(QLatin1String("createdAt")));
    p.updatedAt = timeValue(o.value(QLatin1String("updatedAt")));
    p.updatedBy = o.value(QLatin1String("updatedBy")).toString();
    return p;
}

std::optional<PresetTombstone> tombstoneValue(const QJsonValue &value)
{
    const QJsonObject o = value.toObject();
    PresetTombstone t;
    t.uuid = o.value(QLatin1String("uuid")).toString().trimmed();
    if (t.uuid.isEmpty() || t.uuid == PresetModel::DefaultUuid) {
        return std::nullopt;
    }
    t.deletedAt = timeValue(o.value(QLatin1String("deletedAt")));
    t.deletedBy = o.value(QLatin1String("deletedBy")).toString();
    return t;
}

// --- Merging ---------------------------------------------------------------

/// The newest state of one uuid on one side: a preset or a deletion.
struct Version {
    bool deleted = false;
    TimerPreset preset;
    PresetTombstone tombstone;

    qint64 time() const { return deleted ? tombstone.deletedAt : preset.updatedAt; }
    const QString &author() const { return deleted ? tombstone.deletedBy : preset.updatedBy; }
};

/// True when `a` is newer than `b`. Authors compare as device ids, empty standing for `self`.
bool newer(const Version &a, const Version &b, const QString &self)
{
    if (a.time() != b.time()) {
        return a.time() > b.time();
    }
    // The built-in timer at the same time, as it comes on one side and changed on the other: both
    // carry time 0 when it was changed before Sugo recorded change times. The change wins.
    if (!a.deleted && !b.deleted && a.preset.uuid == PresetModel::DefaultUuid) {
        const TimerPreset factory = PresetModel::makeDefaultPreset();
        const bool aFactory = a.preset.sameContent(factory);
        const bool bFactory = b.preset.sameContent(factory);
        if (aFactory != bFactory) {
            return bFactory;
        }
    }
    const QString authorA = a.author().isEmpty() ? self : a.author();
    const QString authorB = b.author().isEmpty() ? self : b.author();
    if (authorA != authorB) {
        return authorA > authorB;
    }
    return a.deleted && !b.deleted;
}

bool samePreset(const TimerPreset &a, const TimerPreset &b)
{
    return a.sameContent(b) && a.createdAt == b.createdAt && a.updatedAt == b.updatedAt && a.updatedBy == b.updatedBy;
}

/// The side with this device's own id as the empty author, the built-in flag set from the uuid,
/// expired tombstones and any for the built-in preset left out.
Data normalized(const Data &side, const QString &self, qint64 nowMs)
{
    Data out;
    QSet<QString> seen;
    for (TimerPreset p : side.presets) {
        if (p.uuid.isEmpty() || seen.contains(p.uuid)) {
            continue;
        }
        seen.insert(p.uuid);
        if (p.updatedBy == self) {
            p.updatedBy.clear();
        }
        p.builtin = p.uuid == PresetModel::DefaultUuid;
        out.presets.append(p);
    }
    for (PresetTombstone t : side.tombstones) {
        if (t.uuid.isEmpty() || t.uuid == PresetModel::DefaultUuid || t.deletedAt < nowMs - TombstoneLifetimeMs) {
            continue;
        }
        if (t.deletedBy == self) {
            t.deletedBy.clear();
        }
        out.tombstones.append(t);
    }
    return out;
}

QHash<QString, Version> versions(const Data &side, const QString &self)
{
    QHash<QString, Version> map;
    for (const TimerPreset &p : side.presets) {
        Version v;
        v.preset = p;
        map.insert(p.uuid, v);
    }
    for (const PresetTombstone &t : side.tombstones) {
        Version v;
        v.deleted = true;
        v.tombstone = t;
        const auto it = map.constFind(t.uuid);
        if (it == map.constEnd() || newer(v, *it, self)) {
            map.insert(t.uuid, v);
        }
    }
    return map;
}

bool sameTombstones(QList<PresetTombstone> a, QList<PresetTombstone> b)
{
    const auto byUuid = [](const PresetTombstone &x, const PresetTombstone &y) { return x.uuid < y.uuid; };
    std::sort(a.begin(), a.end(), byUuid);
    std::sort(b.begin(), b.end(), byUuid);
    return a == b;
}

/// Same presets, in any order.
bool samePresetSet(const QList<TimerPreset> &a, const QList<TimerPreset> &b)
{
    if (a.size() != b.size()) {
        return false;
    }
    QHash<QString, const TimerPreset *> byUuid;
    for (const TimerPreset &p : b) {
        byUuid.insert(p.uuid, &p);
    }
    return std::all_of(a.cbegin(), a.cend(), [&byUuid](const TimerPreset &p) {
        const TimerPreset *other = byUuid.value(p.uuid);
        return other && samePreset(p, *other);
    });
}

bool samePresetList(const QList<TimerPreset> &a, const QList<TimerPreset> &b)
{
    return std::equal(a.cbegin(), a.cend(), b.cbegin(), b.cend(), samePreset);
}
} // namespace

QByteArray toJson(const Data &data, const QString &deviceId)
{
    const auto author = [&deviceId](const QString &by) { return by.isEmpty() ? deviceId : by; };

    QJsonArray presets;
    for (const TimerPreset &p : data.presets) {
        presets.append(QJsonObject{
            {QStringLiteral("uuid"), p.uuid},
            {QStringLiteral("name"), p.name},
            {QStringLiteral("category"), p.category},
            {QStringLiteral("workSeconds"), p.workSeconds},
            {QStringLiteral("shortBreakSeconds"), p.shortBreakSeconds},
            {QStringLiteral("longBreakSeconds"), p.longBreakSeconds},
            {QStringLiteral("cyclesBeforeLong"), p.cyclesBeforeLong},
            {QStringLiteral("options"), QJsonObject::fromVariantMap(p.options)},
            {QStringLiteral("createdAt"), p.createdAt},
            {QStringLiteral("updatedAt"), p.updatedAt},
            {QStringLiteral("updatedBy"), author(p.updatedBy)},
        });
    }
    QJsonArray tombstones;
    for (const PresetTombstone &t : data.tombstones) {
        tombstones.append(QJsonObject{
            {QStringLiteral("uuid"), t.uuid},
            {QStringLiteral("deletedAt"), t.deletedAt},
            {QStringLiteral("deletedBy"), author(t.deletedBy)},
        });
    }
    const QJsonObject root{
        {QStringLiteral("format"), Format},
        {QStringLiteral("presets"), presets},
        {QStringLiteral("tombstones"), tombstones},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

ParseResult fromJson(const QByteArray &json)
{
    ParseResult result;
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return result;
    }
    const QJsonObject root = doc.object();
    const QJsonValue format = root.value(QLatin1String("format"));
    if (!format.isDouble() || format.toDouble() < 1 || format.toDouble() != double(format.toInt())) {
        return result;
    }
    result.format = format.toInt();
    result.ok = true;

    QSet<QString> seen;
    const QJsonArray presets = root.value(QLatin1String("presets")).toArray();
    for (const QJsonValue &value : presets) {
        const std::optional<TimerPreset> preset = presetValue(value);
        if (preset && !seen.contains(preset->uuid)) {
            seen.insert(preset->uuid);
            result.data.presets.append(*preset);
        }
    }
    QSet<QString> deleted;
    const QJsonArray tombstones = root.value(QLatin1String("tombstones")).toArray();
    for (const QJsonValue &value : tombstones) {
        const std::optional<PresetTombstone> tombstone = tombstoneValue(value);
        if (tombstone && !deleted.contains(tombstone->uuid)) {
            deleted.insert(tombstone->uuid);
            result.data.tombstones.append(*tombstone);
        }
    }
    return result;
}

MergeResult merge(const Data &localSide, const Data &remoteSide, const QString &deviceId, qint64 nowMs, int remoteFormat)
{
    const Data local = normalized(localSide, deviceId, nowMs);
    const Data remote = normalized(remoteSide, deviceId, nowMs);
    const QHash<QString, Version> mine = versions(local, deviceId);
    const QHash<QString, Version> theirs = versions(remote, deviceId);

    QHash<QString, Version> winners = mine;
    for (auto it = theirs.cbegin(); it != theirs.cend(); ++it) {
        const auto own = winners.constFind(it.key());
        if (own == winners.constEnd() || newer(*it, *own, deviceId)) {
            winners.insert(it.key(), *it);
        }
    }

    MergeResult merged;
    QSet<QString> placed;
    for (const TimerPreset &p : local.presets) {
        const Version &winner = winners[p.uuid];
        if (!winner.deleted) {
            merged.result.presets.append(winner.preset);
            placed.insert(p.uuid);
        }
    }
    QList<TimerPreset> arrived;
    for (const Version &winner : std::as_const(winners)) {
        if (winner.deleted) {
            merged.result.tombstones.append(winner.tombstone);
        } else if (!placed.contains(winner.preset.uuid)) {
            arrived.append(winner.preset);
        }
    }
    std::sort(arrived.begin(), arrived.end(), [](const TimerPreset &a, const TimerPreset &b) {
        return a.createdAt != b.createdAt ? a.createdAt < b.createdAt : a.uuid < b.uuid;
    });
    merged.result.presets.append(arrived);
    std::sort(merged.result.tombstones.begin(), merged.result.tombstones.end(),
              [](const PresetTombstone &a, const PresetTombstone &b) {
                  return a.deletedAt != b.deletedAt ? a.deletedAt < b.deletedAt : a.uuid < b.uuid;
              });

    // Against the normalised sides: this device's own id for "" or a tombstone past its time is
    // no reason to write (saveAll() drops old tombstones anyway).
    merged.changedLocal = !samePresetList(merged.result.presets, local.presets)
        || !sameTombstones(merged.result.tombstones, local.tombstones);
    merged.changedRemote = remoteFormat <= Format
        && (!samePresetSet(merged.result.presets, remote.presets) || !sameTombstones(merged.result.tombstones, remote.tombstones));
    return merged;
}
} // namespace PresetSyncFormat
