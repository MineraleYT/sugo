// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

/// Keys of the per-timer options. Stored as strings so that adding an option
/// never needs a database schema change.
namespace PresetOption
{
inline const QString SilenceWhileWorking = QStringLiteral("silenceWhileWorking");
inline const QString AutoStartMode = QStringLiteral("autoStartMode");
inline const QString SoundOnEnd = QStringLiteral("soundOnEnd");
inline const QString NotifyOnEnd = QStringLiteral("notifyOnEnd");
} // namespace PresetOption

struct OptionDescriptor {
    QString key;
    QString label;
    QString description;
    QVariant defaultValue;
    QString type; ///< Selects the editor in QML: "bool" or "int".
    int minimum = 0; ///< Valid range of an "int" option.
    int maximum = 0;
};

/**
 * Registry of the options every timer has.
 *
 * To add an option: add a key to PresetOption, add one descriptor in
 * descriptors(), and read it where it matters. The settings page is generated
 * from the descriptors and storage is a key/value map, so nothing else changes.
 */
class PresetSettings
{
public:
    static QList<OptionDescriptor> descriptors();
    static QVariant defaultValue(const QString &key);
    static QVariantMap defaults();
};
