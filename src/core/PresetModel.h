// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QtQml/qqmlregistration.h>

#include <functional>

#include "TimerPreset.h"

/**
 * List of timer presets plus the currently selected one.
 *
 * The built-in "default" preset (25/5) is always present and cannot be removed;
 * it can be duplicated, edited and reset to its factory values.
 */
class PresetModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    /// The limits the editor offers; values outside them are clamped (see PresetLimits).
    Q_PROPERTY(int maxWorkMinutes READ maxWorkMinutes CONSTANT FINAL)
    Q_PROPERTY(int maxBreakMinutes READ maxBreakMinutes CONSTANT FINAL)
    Q_PROPERTY(int maxCycles READ maxCycles CONSTANT FINAL)
    Q_PROPERTY(int maxNameLength READ maxNameLength CONSTANT FINAL)
    Q_PROPERTY(QString currentUuid READ currentUuid WRITE setCurrentUuid NOTIFY currentChanged FINAL)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged FINAL)

    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged FINAL)
    Q_PROPERTY(QString currentCategory READ currentCategory NOTIFY currentChanged FINAL)

public:
    static int maxWorkMinutes() { return PresetLimits::MaxWorkSeconds / 60; }
    static int maxBreakMinutes() { return PresetLimits::MaxBreakSeconds / 60; }
    static int maxCycles() { return PresetLimits::MaxCycles; }
    static int maxNameLength() { return PresetLimits::MaxNameLength; }

    enum Role {
        UuidRole = Qt::UserRole + 1,
        NameRole,
        CategoryRole,
        WorkSecondsRole,
        ShortBreakSecondsRole,
        LongBreakSecondsRole,
        CyclesBeforeLongRole,
        BuiltinRole,
        IconNameRole,
        WorkMinutesRole,
    };
    Q_ENUM(Role)

    static const QString DefaultUuid;
    /// The built-in timer as it comes, before any change.
    static TimerPreset makeDefaultPreset();

    explicit PresetModel(QObject *parent = nullptr);

    /// The wall clock that stamps createdAt and updatedAt (tests set their own). Not the
    /// timer's monotonic Clock: these times are compared with other computers'.
    void setNowProvider(std::function<QDateTime()> provider);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_presets.size()); }
    QString currentUuid() const { return m_currentUuid; }
    void setCurrentUuid(const QString &uuid);
    int currentIndex() const { return indexOf(m_currentUuid); }

    QString currentName() const;
    QString currentCategory() const;

    /// C++ access for the engine binding and the notifier.
    TimerPreset currentPreset() const;
    const QList<TimerPreset> &presets() const { return m_presets; }

    /// Curated list of selectable symbolic icons for timers.
    Q_INVOKABLE QVariantList availableIcons() const;
    /// What an Icon's `source` needs for an icon name: Sugo's own timer icons are files
    /// inside the program, anything else is a theme icon name.
    Q_INVOKABLE static QString iconSource(const QString &iconName);

    /// Replaces the whole list (used when loading from storage). Guarantees that the
    /// built-in preset exists, that no other preset claims to be built-in and that every
    /// value is within PresetLimits. Does not emit modified(), so loading never writes back;
    /// emits presetChanged() for each timer that was there before and whose content changed.
    void setPresets(const QList<TimerPreset> &presets, const QString &currentUuid);

    Q_INVOKABLE int indexOf(const QString &uuid) const;
    /// The uuid of the preset in `row`; empty when there is none.
    Q_INVOKABLE QString uuidAt(int row) const;
    /// All fields of a preset, plus `options` with defaults merged in. Empty map if unknown.
    Q_INVOKABLE QVariantMap get(const QString &uuid) const;

    /// `fields` may hold: name, category, workSeconds, shortBreakSeconds,
    /// longBreakSeconds, cyclesBeforeLong. Values are validated and clamped.
    /// Returns the new preset's uuid.
    Q_INVOKABLE QString create(const QVariantMap &fields);
    Q_INVOKABLE bool update(const QString &uuid, const QVariantMap &fields);
    /// Copies fields and options; the copy is never built-in. Returns its uuid, or "".
    Q_INVOKABLE QString duplicate(const QString &uuid);
    /// Refuses (returns false) for the built-in preset.
    Q_INVOKABLE bool remove(const QString &uuid);
    /// Restores the built-in preset to its factory values (changed now, for Sugo Sync). No-op for
    /// other presets.
    Q_INVOKABLE bool reset(const QString &uuid);

    /// Stamps every preset as changed now by this computer and emits modified(): after a backup
    /// restore, so Sugo Sync publishes the restored timers over what other computers hold.
    void markAllModified();

    Q_INVOKABLE QVariant option(const QString &uuid, const QString &key) const;
    /// Unknown keys are rejected. Values are coerced to the option's type.
    Q_INVOKABLE bool setOption(const QString &uuid, const QString &key, const QVariant &value);

Q_SIGNALS:
    void countChanged();
    /// The selection changed, or the current row moved because of an insert/remove.
    void currentChanged();
    /// Any content change of one preset (fields or options).
    void presetChanged(const QString &uuid);
    /// Content or structure changed through the editing API (create, update, duplicate,
    /// remove, reset, setOption). Listeners persist on this signal.
    void modified();
    /// create() or update() had to bring `fields` (keys such as "workSeconds") into range.
    void valuesAdjusted(const QString &uuid, const QStringList &fields);

private:
    /// Returns the keys whose value was out of range or unreadable and had to be adjusted.
    static QStringList applyFields(TimerPreset &preset, const QVariantMap &fields);
    static QString iconNameFor(const TimerPreset &preset);
    qint64 nowMs() const;
    /// The time of a change to a version of `previous`: now, but always after it, so that the
    /// change wins over the version it was made from even when this computer's clock is behind.
    qint64 changeTime(qint64 previous) const;

    QList<TimerPreset> m_presets;
    std::function<QDateTime()> m_now;
    QString m_currentUuid;
};
