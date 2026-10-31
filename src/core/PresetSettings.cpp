// SPDX-License-Identifier: GPL-3.0-or-later
#include "PresetSettings.h"

#include "Localization.h"

QList<OptionDescriptor> PresetSettings::descriptors()
{
    return {
        {PresetOption::SilenceWhileWorking,
         i18n("Silence notifications while working"),
         i18n("Turns off all system notifications for the whole work phase and turns them back on during the break."),
         false,
         QStringLiteral("bool")},
        {PresetOption::AutoStartMode,
         i18n("Auto-start mode"),
         i18n("Choose whether breaks, work phases, or neither start automatically."),
         0,
         QStringLiteral("int"),
         0,
         2},
        {PresetOption::SoundOnEnd,
         i18n("Play a sound when a phase ends"),
         QString(),
         true,
         QStringLiteral("bool")},
        {PresetOption::NotifyOnEnd,
         i18n("Show a notification when a phase ends"),
         QString(),
         true,
         QStringLiteral("bool")},
    };
}

QVariant PresetSettings::defaultValue(const QString &key)
{
    const auto all = descriptors();
    for (const OptionDescriptor &d : all) {
        if (d.key == key) {
            return d.defaultValue;
        }
    }
    return {};
}

QVariantMap PresetSettings::defaults()
{
    QVariantMap map;
    const auto all = descriptors();
    for (const OptionDescriptor &d : all) {
        map.insert(d.key, d.defaultValue);
    }
    return map;
}

