// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QVariantMap>

#include "PresetSettings.h"

/// The longest phases and sets accepted anywhere: the timer editor, stored timers, the engine,
/// KRunner and D-Bus all stay within them.
namespace PresetLimits
{
constexpr int MaxWorkSeconds = 6 * 3600;
constexpr int MaxBreakSeconds = 2 * 3600;
constexpr int MaxCycles = 99;
constexpr int MaxNameLength = 100;
}

/// A timer definition: the fixed fields plus a free-form option map (see PresetSettings).
struct TimerPreset {
    QString uuid;
    QString name;
    QString category;
    int workSeconds = 25 * 60;
    int shortBreakSeconds = 5 * 60;
    int longBreakSeconds = 15 * 60;
    int cyclesBeforeLong = 4; ///< 0 = never take a long break
    bool builtin = false;
    QVariantMap options;      ///< Only explicitly set options; the rest fall back to defaults.
    /// Wall-clock times in milliseconds since the epoch (UTC), which Sugo Sync compares across
    /// computers. 0: not known (the built-in preset as shipped, timers older than schema 2).
    qint64 createdAt = 0;
    qint64 updatedAt = 0;
    /// The Sugo Sync device that made the last change; empty: this computer.
    QString updatedBy;

    /// Same timer, whatever its times: the fields and options a user can change.
    bool sameContent(const TimerPreset &other) const
    {
        return uuid == other.uuid && name == other.name && category == other.category
            && workSeconds == other.workSeconds && shortBreakSeconds == other.shortBreakSeconds
            && longBreakSeconds == other.longBreakSeconds && cyclesBeforeLong == other.cyclesBeforeLong
            && builtin == other.builtin && options == other.options;
    }

    QVariant option(const QString &key) const
    {
        if (options.contains(key)) {
            return options.value(key);
        }
        return PresetSettings::defaultValue(key);
    }

    bool boolOption(const QString &key) const
    {
        return option(key).toBool();
    }
};
