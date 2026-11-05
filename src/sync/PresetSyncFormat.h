// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "TimerPreset.h"

/**
 * The timers as Sugo Sync stores them (the plain text inside the encrypted blob, see
 * docs/sync-plan.md, "Formato e unione dei preset"), and the merge of two such lists.
 *
 * Pure functions: no database, no network, no clock but the `nowMs` the caller passes.
 *
 * Authors: a preset's updatedBy and a tombstone's deletedBy name the Sugo Sync device that made
 * the change. Locally, empty means "this computer"; toJson() writes the device id in its place,
 * and merge() turns this device's own id back into empty.
 */
namespace PresetSyncFormat
{
/// The format this version reads and writes. A blob with a higher one is read but never written
/// ("Update Sugo to keep syncing").
constexpr int Format = 1;

/// Tombstones older than this are dropped (as PresetRepository::TombstoneLifetimeMs).
constexpr qint64 TombstoneLifetimeMs = 180LL * 24 * 3600 * 1000;
/// A change or deletion time further than this ahead of the merge's `nowMs` counts as `nowMs`.
constexpr qint64 MaxTimeAheadMs = 24LL * 3600 * 1000;

/// One side of a sync: its timers in display order and its remembered deletions.
struct Data {
    QList<TimerPreset> presets;
    QList<PresetTombstone> tombstones;
};

struct ParseResult {
    bool ok = false;   ///< A JSON object with a valid "format"; the rest is read as far as it can be.
    int format = 0;    ///< The blob's format; above Format, the caller must not upload.
    Data data;

    bool newerFormat() const { return format > Format; }
};

struct MergeResult {
    Data result;              ///< What both sides should hold now, in the local order.
    /// `result` differs from the local side: store it. Expired tombstones and authors written
    /// as this device's own id do not count.
    bool changedLocal = false;
    /// `result` differs from the remote side, order aside: upload it.
    bool changedRemote = false;
};

/// The JSON text of `data`, compact, in format Format. Empty authors become `deviceId`.
QByteArray toJson(const Data &data, const QString &deviceId);

/// Reads a JSON text. Entries that cannot be used (no uuid, not an object) are skipped; values
/// outside PresetLimits or an option's range are brought into it; options this version does not
/// know are kept as they are, so they go back out unchanged. A tombstone for the built-in preset
/// is ignored: it is never deleted.
ParseResult fromJson(const QByteArray &json);

/**
 * Merges the local and the remote side.
 *
 * For each uuid the newest version wins: a preset by updatedAt, a deletion by deletedAt; equal
 * times go to the higher author id (in byte order), and a deletion beats an edit made at the same
 * time by the same author. A time more than MaxTimeAheadMs ahead of `nowMs` (a clock far off,
 * bad data) counts as `nowMs` and is written back as `nowMs`, so it does not win every later
 * merge; two such times keep their order. The built-in preset is never deleted. Tombstones older than
 * TombstoneLifetimeMs before `nowMs` are dropped. Order: the local one; presets new to this
 * computer follow at the end by createdAt.
 *
 * `deviceId` is this computer's Sugo Sync device id (the author of local changes).
 * `remoteFormat` is the format the remote side was read in: above Format, changedRemote stays
 * false (a newer Sugo wrote it; this one only reads).
 */
MergeResult merge(const Data &local, const Data &remote, const QString &deviceId, qint64 nowMs,
                  int remoteFormat = Format);
} // namespace PresetSyncFormat
