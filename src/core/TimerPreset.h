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

    QVariant option(const QString &key) const
    {
        if (options.contains(key)) {
            return options.value(key);
        }
        if (key == PresetOption::AutoStartMode && options.contains(PresetOption::AutoStart)) {
            return options.value(PresetOption::AutoStart).toBool() ? 2 : 0;
        }
        return PresetSettings::defaultValue(key);
    }

    bool boolOption(const QString &key) const
    {
        if (key == PresetOption::AutoStart && options.contains(PresetOption::AutoStartMode)) {
            return option(PresetOption::AutoStartMode).toInt() == 2;
        }
        return option(key).toBool();
    }
};
