// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

#include <functional>

#include "TimerPreset.h"

/// Stores timer presets, their options and the selected preset.
class PresetRepository
{
public:
    explicit PresetRepository(const QString &connectionName);

    /// All presets in display order. `ok` (optional) reports query failures, a row that cannot be
    /// read included; the list is then empty, never a part of the presets.
    QList<TimerPreset> loadAll(bool *ok = nullptr) const;

    /// How long a deleted timer is remembered for Sugo Sync: 180 days.
    static constexpr qint64 TombstoneLifetimeMs = 180LL * 24 * 3600 * 1000;

    /// Makes the database match `presets` exactly (insert, update, reorder, delete)
    /// in one transaction. Existing sessions of a deleted preset are kept.
    ///
    /// Every preset it deletes leaves a tombstone: the one in `received` for that uuid (deleted on
    /// another computer, stored with its own time and device), else one stamped now by this
    /// computer. Tombstones in `received` for presets this computer no longer has are stored as
    /// they are, never one for the built-in preset or for a preset in `presets`; a preset in
    /// `presets` loses its tombstone. Tombstones older than TombstoneLifetimeMs are dropped.
    bool saveAll(const QList<TimerPreset> &presets, const QList<PresetTombstone> &received = {});

    /// The remembered deletions, oldest first. `ok` (optional) reports query failures, a row that
    /// cannot be read included; the list is then empty.
    QList<PresetTombstone> tombstones(bool *ok = nullptr) const;

    /// The wall clock for tombstones and their expiry (tests set their own).
    void setNowProvider(std::function<QDateTime()> provider);

    QString currentUuid() const;
    bool setCurrentUuid(const QString &uuid);

private:
    qint64 nowMs() const;

    QString m_connection;
    std::function<QDateTime()> m_now;
};
