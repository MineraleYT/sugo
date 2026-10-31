// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>

#include "TimerPreset.h"

/// Stores timer presets, their options and the selected preset.
class PresetRepository
{
public:
    explicit PresetRepository(const QString &connectionName);

    /// All presets in display order. `ok` (optional) reports query failures.
    QList<TimerPreset> loadAll(bool *ok = nullptr) const;

    /// Makes the database match `presets` exactly (insert, update, reorder, delete)
    /// in one transaction. Existing sessions of a deleted preset are kept.
    bool saveAll(const QList<TimerPreset> &presets);

    QString currentUuid() const;
    bool setCurrentUuid(const QString &uuid);

    bool presetsInitialized() const;
    bool setPresetsInitialized(bool initialized);

private:
    QString m_connection;
};
