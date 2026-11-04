// SPDX-License-Identifier: GPL-3.0-or-later
#include "PresetModel.h"

#include "Localization.h"

#include <QSet>
#include <QUuid>

#include <algorithm>
#include <optional>

const QString PresetModel::DefaultUuid = QStringLiteral("default");

namespace
{
constexpr int kMaxWorkSeconds = PresetLimits::MaxWorkSeconds;
constexpr int kMaxBreakSeconds = PresetLimits::MaxBreakSeconds;
constexpr int kMaxCycles = PresetLimits::MaxCycles;

int clampedInt(const QVariant &value, int low, int high, int fallback)
{
    bool ok = false;
    const int n = value.toInt(&ok);
    return ok ? std::clamp(n, low, high) : fallback;
}

/// The registry entry of an option, or nothing for unknown keys.
std::optional<OptionDescriptor> descriptorFor(const QString &key)
{
    const auto all = PresetSettings::descriptors();
    for (const OptionDescriptor &d : all) {
        if (d.key == key) {
            return d;
        }
    }
    return std::nullopt;
}

/// The value as stored for this option: ints clamped to the option's own range, others as bool.
QVariant coercedOption(const OptionDescriptor &desc, const QVariant &value)
{
    if (desc.type == QLatin1String("int")) {
        return clampedInt(value, desc.minimum, desc.maximum, desc.defaultValue.toInt());
    }
    return value.toBool();
}

/// A name or category as stored: trimmed, at most `maxLength` (MaxNameLength) UTF-16 units, and
/// never cut between the two halves of a character outside the Basic Multilingual Plane (an emoji).
QString limitedText(const QString &text, int maxLength = PresetLimits::MaxNameLength)
{
    QString kept = text.trimmed().left(std::max(0, maxLength));
    if (!kept.isEmpty() && kept.back().isHighSurrogate()) {
        kept.chop(1);
    }
    return kept.trimmed();
}

/// A stored timer (an older version's, a restored backup's, a hand-edited file's) brought within
/// the limits the editor keeps, so the engine and the editor never see values they would refuse.
void clampToLimits(TimerPreset &preset)
{
    preset.workSeconds = std::clamp(preset.workSeconds, 1, kMaxWorkSeconds);
    preset.shortBreakSeconds = std::clamp(preset.shortBreakSeconds, 0, kMaxBreakSeconds);
    preset.longBreakSeconds = std::clamp(preset.longBreakSeconds, 0, kMaxBreakSeconds);
    preset.cyclesBeforeLong = std::clamp(preset.cyclesBeforeLong, 0, kMaxCycles);
    const QString name = limitedText(preset.name);
    if (!name.isEmpty()) {
        preset.name = name;
    }
    preset.category = limitedText(preset.category);
    for (const auto &desc : PresetSettings::descriptors()) {
        if (preset.options.contains(desc.key)) {
            preset.options.insert(desc.key, coercedOption(desc, preset.options.value(desc.key)));
        }
    }
}

} // namespace

PresetModel::PresetModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_currentUuid(DefaultUuid)
{
    m_presets.append(makeDefaultPreset());
}

TimerPreset PresetModel::makeDefaultPreset()
{
    // The one timer a new user starts with: the classic 25/5, with a long break of 15 after four,
    // and the desktop's notifications silenced while working.
    TimerPreset preset;
    preset.uuid = DefaultUuid;
    preset.name = i18n("Pomodoro");
    preset.category = i18n("Work");
    preset.builtin = true;
    preset.options.insert(QStringLiteral("iconName"), QStringLiteral("sugo-timer"));
    preset.options.insert(PresetOption::SilenceWhileWorking, true);
    return preset;
}

QVariantList PresetModel::availableIcons() const
{
    const auto entry = [](const char *name, const QString &label) {
        return QVariantMap{{QStringLiteral("iconName"), QString::fromLatin1(name)}, {QStringLiteral("label"), label}};
    };
    return {
        entry("sugo-timer", i18n("Timer / Pomodoro")),
        entry("sugo-tomato", i18n("Tomato")),
        entry("sugo-focus", i18n("Focus / Target")),
        entry("sugo-study", i18n("Study / Reading")),
        entry("sugo-sprint", i18n("Sprint / Speed")),
        entry("sugo-writing", i18n("Writing / Notes")),
        entry("sugo-coding", i18n("Coding / Execution")),
        entry("sugo-tasks", i18n("Tasks / Checklist")),
        entry("sugo-research", i18n("Research")),
        entry("sugo-goal", i18n("Goal / Milestone")),
        entry("sugo-favorite", i18n("Favorite")),
        entry("sugo-break", i18n("Break / Rest")),
    };
}

QString PresetModel::iconSource(const QString &iconName)
{
    // Sugo's own timer icons ship inside the program; any other name is a theme icon.
    if (iconName.startsWith(QStringLiteral("sugo-"))) {
        return QStringLiteral("qrc:/icons/") + iconName + QStringLiteral(".svg");
    }
    return iconName;
}

QStringList PresetModel::applyFields(TimerPreset &preset, const QVariantMap &fields)
{
    // Values the editor cannot produce (other callers, hand-made data) are brought into range
    // rather than refused; every field that had to change is reported.
    QStringList adjusted;
    const auto text = [&](const QString &key) {
        const QString given = fields.value(key).toString().trimmed();
        const QString kept = limitedText(given);
        if (kept != given) {
            adjusted.append(key);
        }
        return kept;
    };
    const auto number = [&](const QString &key, int low, int high, int current) {
        if (!fields.contains(key)) {
            return current;
        }
        bool ok = false;
        const int given = fields.value(key).toInt(&ok);
        const int kept = ok ? std::clamp(given, low, high) : current;
        if (!ok || kept != given) {
            adjusted.append(key);
        }
        return kept;
    };

    if (fields.contains(QStringLiteral("name"))) {
        const QString name = text(QStringLiteral("name"));
        if (!name.isEmpty()) {
            preset.name = name;
        }
    }
    if (fields.contains(QStringLiteral("category"))) {
        preset.category = text(QStringLiteral("category"));
    }
    if (fields.contains(QStringLiteral("iconName"))) {
        const QString icon = fields.value(QStringLiteral("iconName")).toString().trimmed();
        if (!icon.isEmpty()) {
            preset.options.insert(QStringLiteral("iconName"), icon);
        } else {
            preset.options.remove(QStringLiteral("iconName"));
        }
    }
    preset.workSeconds = number(QStringLiteral("workSeconds"), 1, kMaxWorkSeconds, preset.workSeconds);
    preset.shortBreakSeconds = number(QStringLiteral("shortBreakSeconds"), 0, kMaxBreakSeconds, preset.shortBreakSeconds);
    preset.longBreakSeconds = number(QStringLiteral("longBreakSeconds"), 0, kMaxBreakSeconds, preset.longBreakSeconds);
    preset.cyclesBeforeLong = number(QStringLiteral("cyclesBeforeLong"), 0, kMaxCycles, preset.cyclesBeforeLong);

    for (const auto &desc : PresetSettings::descriptors()) {
        if (fields.contains(desc.key)) {
            const QVariant value = coercedOption(desc, fields.value(desc.key));
            if (desc.type == QLatin1String("int") && value.toInt() != fields.value(desc.key).toInt()) {
                adjusted.append(desc.key);
            }
            preset.options.insert(desc.key, value);
        }
    }
    return adjusted;
}

QString PresetModel::iconNameFor(const TimerPreset &preset)
{
    // Theme icons chosen by earlier versions, shown as the matching Sugo icon.
    static const QHash<QString, QString> earlier = {
        {QStringLiteral("chronometer"), QStringLiteral("sugo-timer")},
        {QStringLiteral("io.github.mineraleyt.sugo"), QStringLiteral("sugo-tomato")},
        {QStringLiteral("flash-symbolic"), QStringLiteral("sugo-focus")},
        {QStringLiteral("crosshairs-symbolic"), QStringLiteral("sugo-focus")},
        {QStringLiteral("view-readermode-symbolic"), QStringLiteral("sugo-study")},
        {QStringLiteral("speedometer-symbolic"), QStringLiteral("sugo-sprint")},
        {QStringLiteral("document-edit-symbolic"), QStringLiteral("sugo-writing")},
        {QStringLiteral("system-run-symbolic"), QStringLiteral("sugo-coding")},
        {QStringLiteral("format-text-code-symbolic"), QStringLiteral("sugo-coding")},
        {QStringLiteral("view-pim-tasks-symbolic"), QStringLiteral("sugo-tasks")},
        {QStringLiteral("bookmarks-symbolic"), QStringLiteral("sugo-research")},
        {QStringLiteral("flag-symbolic"), QStringLiteral("sugo-goal")},
        {QStringLiteral("favorites-symbolic"), QStringLiteral("sugo-favorite")},
    };
    const QString custom = preset.options.value(QStringLiteral("iconName")).toString().trimmed();
    if (!custom.isEmpty()) {
        return earlier.value(custom, custom);
    }

    const QString id = preset.uuid;
    const QString cat = preset.category.toLower();
    const QString name = preset.name.toLower();

    if (id == QStringLiteral("deep-work") || cat.contains(QStringLiteral("focus")) || name.contains(QStringLiteral("deep"))) {
        return QStringLiteral("sugo-focus");
    }
    if (id == QStringLiteral("study") || cat.contains(QStringLiteral("study")) || cat.contains(QStringLiteral("edu")) || cat.contains(QStringLiteral("learn")) || name.contains(QStringLiteral("study"))) {
        return QStringLiteral("sugo-study");
    }
    if (id == QStringLiteral("quick-sprint") || cat.contains(QStringLiteral("sprint")) || cat.contains(QStringLiteral("task")) || cat.contains(QStringLiteral("quick")) || name.contains(QStringLiteral("sprint"))) {
        return QStringLiteral("sugo-sprint");
    }
    return QStringLiteral("sugo-timer");
}

// --- Model interface -------------------------------------------------------

int PresetModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant PresetModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const TimerPreset &p = m_presets.at(index.row());
    switch (role) {
    case UuidRole:
        return p.uuid;
    case Qt::DisplayRole:
    case NameRole:
        return p.name;
    case CategoryRole:
        return p.category;
    case WorkSecondsRole:
        return p.workSeconds;
    case ShortBreakSecondsRole:
        return p.shortBreakSeconds;
    case LongBreakSecondsRole:
        return p.longBreakSeconds;
    case CyclesBeforeLongRole:
        return p.cyclesBeforeLong;
    case BuiltinRole:
        return p.builtin;
    case IconNameRole:
        return iconNameFor(p);
    case WorkMinutesRole:
        return std::max(1, p.workSeconds / 60);
    }
    return {};
}

QHash<int, QByteArray> PresetModel::roleNames() const
{
    return {
        {UuidRole, "uuid"},
        {NameRole, "name"},
        {CategoryRole, "category"},
        {WorkSecondsRole, "workSeconds"},
        {ShortBreakSecondsRole, "shortBreakSeconds"},
        {LongBreakSecondsRole, "longBreakSeconds"},
        {CyclesBeforeLongRole, "cyclesBeforeLong"},
        {BuiltinRole, "builtin"},
        {IconNameRole, "iconName"},
        {WorkMinutesRole, "workMinutes"},
    };
}

void PresetModel::setPresets(const QList<TimerPreset> &presets, const QString &currentUuid)
{
    QList<TimerPreset> clean;
    QSet<QString> seen;
    for (TimerPreset preset : presets) {
        if (preset.uuid.isEmpty() || seen.contains(preset.uuid)) {
            continue;
        }
        seen.insert(preset.uuid);
        preset.builtin = (preset.uuid == DefaultUuid);
        clampToLimits(preset);
        clean.append(preset);
    }
    if (!seen.contains(DefaultUuid)) {
        clean.prepend(makeDefaultPreset());
    }

    beginResetModel();
    m_presets = clean;
    m_currentUuid = seen.contains(currentUuid) ? currentUuid : DefaultUuid;
    endResetModel();
    Q_EMIT countChanged();
    Q_EMIT currentChanged();
}

// --- Selection -------------------------------------------------------------

void PresetModel::setCurrentUuid(const QString &uuid)
{
    if (uuid == m_currentUuid || indexOf(uuid) < 0) {
        return;
    }
    m_currentUuid = uuid;
    Q_EMIT currentChanged();
}

TimerPreset PresetModel::currentPreset() const
{
    const int row = currentIndex();
    return row >= 0 ? m_presets.at(row) : makeDefaultPreset();
}

QString PresetModel::currentName() const
{
    return currentPreset().name;
}

QString PresetModel::currentCategory() const
{
    return currentPreset().category;
}

int PresetModel::indexOf(const QString &uuid) const
{
    for (int i = 0; i < count(); ++i) {
        if (m_presets.at(i).uuid == uuid) {
            return i;
        }
    }
    return -1;
}

QString PresetModel::uuidAt(int row) const
{
    return row >= 0 && row < count() ? m_presets.at(row).uuid : QString();
}

QVariantMap PresetModel::get(const QString &uuid) const
{
    const int row = indexOf(uuid);
    if (row < 0) {
        return {};
    }
    const TimerPreset &p = m_presets.at(row);

    QVariantMap options = PresetSettings::defaults();
    options.insert(p.options);

    return {
        {QStringLiteral("uuid"), p.uuid},
        {QStringLiteral("name"), p.name},
        {QStringLiteral("category"), p.category},
        {QStringLiteral("workSeconds"), p.workSeconds},
        {QStringLiteral("shortBreakSeconds"), p.shortBreakSeconds},
        {QStringLiteral("longBreakSeconds"), p.longBreakSeconds},
        {QStringLiteral("cyclesBeforeLong"), p.cyclesBeforeLong},
        {QStringLiteral("builtin"), p.builtin},
        {QStringLiteral("iconName"), iconNameFor(p)},
        {QStringLiteral("workMinutes"), std::max(1, p.workSeconds / 60)},
        {QStringLiteral("options"), options},
    };
}

// --- Editing ---------------------------------------------------------------

QString PresetModel::create(const QVariantMap &fields)
{
    TimerPreset preset;
    preset.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    preset.name = i18n("New Timer");
    const QStringList adjusted = applyFields(preset, fields);

    const int row = count();
    beginInsertRows({}, row, row);
    m_presets.append(preset);
    endInsertRows();
    Q_EMIT countChanged();
    Q_EMIT modified();
    if (!adjusted.isEmpty()) {
        Q_EMIT valuesAdjusted(preset.uuid, adjusted);
    }
    return preset.uuid;
}

bool PresetModel::update(const QString &uuid, const QVariantMap &fields)
{
    const int row = indexOf(uuid);
    if (row < 0) {
        return false;
    }
    const QStringList adjusted = applyFields(m_presets[row], fields);
    Q_EMIT dataChanged(index(row), index(row));
    Q_EMIT presetChanged(uuid);
    if (uuid == m_currentUuid) {
        Q_EMIT currentChanged();
    }
    Q_EMIT modified();
    if (!adjusted.isEmpty()) {
        Q_EMIT valuesAdjusted(uuid, adjusted);
    }
    return true;
}

QString PresetModel::duplicate(const QString &uuid)
{
    const int source = indexOf(uuid);
    if (source < 0) {
        return {};
    }
    TimerPreset copy = m_presets.at(source);
    copy.uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.builtin = false;
    // Shorten the original name first so the suffix always fits: cut from the end instead, the
    // copy of a name at the limit would lose " (copy)" and carry the very same name.
    const auto suffixLength = static_cast<int>(i18n("%1 (copy)", QString()).size());
    const QString base = limitedText(copy.name, PresetLimits::MaxNameLength - suffixLength);
    copy.name = limitedText(i18n("%1 (copy)", base));

    const int row = source + 1;
    beginInsertRows({}, row, row);
    m_presets.insert(row, copy);
    endInsertRows();
    Q_EMIT countChanged();
    Q_EMIT currentChanged(); // the current row index may have shifted
    Q_EMIT modified();
    return copy.uuid;
}

bool PresetModel::remove(const QString &uuid)
{
    const int row = indexOf(uuid);
    if (row < 0 || m_presets.at(row).builtin) {
        return false;
    }
    beginRemoveRows({}, row, row);
    m_presets.removeAt(row);
    endRemoveRows();
    Q_EMIT countChanged();

    if (uuid == m_currentUuid) {
        m_currentUuid = DefaultUuid;
    }
    Q_EMIT currentChanged();
    Q_EMIT modified();
    return true;
}

bool PresetModel::reset(const QString &uuid)
{
    const int row = indexOf(uuid);
    if (row < 0 || !m_presets.at(row).builtin) {
        return false;
    }
    m_presets[row] = makeDefaultPreset();
    Q_EMIT dataChanged(index(row), index(row));
    Q_EMIT presetChanged(uuid);
    if (uuid == m_currentUuid) {
        Q_EMIT currentChanged();
    }
    Q_EMIT modified();
    return true;
}

// --- Options ---------------------------------------------------------------

QVariant PresetModel::option(const QString &uuid, const QString &key) const
{
    const int row = indexOf(uuid);
    return row >= 0 ? m_presets.at(row).option(key) : QVariant();
}

bool PresetModel::setOption(const QString &uuid, const QString &key, const QVariant &value)
{
    const int row = indexOf(uuid);
    const std::optional<OptionDescriptor> descriptor = descriptorFor(key);
    if (row < 0 || !descriptor) {
        return false;
    }
    m_presets[row].options.insert(key, coercedOption(*descriptor, value));
    Q_EMIT dataChanged(index(row), index(row));
    Q_EMIT presetChanged(uuid);
    if (uuid == m_currentUuid) {
        Q_EMIT currentChanged();
    }
    Q_EMIT modified();
    return true;
}
