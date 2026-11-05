// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDateTime>
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

#include "Localization.h"

#include <algorithm>
#include <memory>

#include "PresetModel.h"

class PresetModelTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<PresetModel> model;

    QString nameAt(int row) const
    {
        return model->data(model->index(row), PresetModel::NameRole).toString();
    }

private Q_SLOTS:
    void initTestCase()
    {
    }

    void init()
    {
        model = std::make_unique<PresetModel>();
    }

    void editsAreStampedWithTheWallClock()
    {
        qint64 now = 1762300000000;
        model->setNowProvider([&now] { return QDateTime::fromMSecsSinceEpoch(now, QTimeZone::UTC); });

        // The built-in timer as shipped: the same, unstamped, on every computer.
        QCOMPARE(model->presets().at(0).createdAt, 0);
        QCOMPARE(model->presets().at(0).updatedAt, 0);

        const QString uuid = model->create({{QStringLiteral("name"), QStringLiteral("A")}});
        const auto stored = [this](const QString &id) { return model->presets().at(model->indexOf(id)); };
        QCOMPARE(stored(uuid).createdAt, now);
        QCOMPARE(stored(uuid).updatedAt, now);

        now += 1000;
        QVERIFY(model->update(uuid, {{QStringLiteral("name"), QStringLiteral("A")}})); // nothing changes
        QCOMPARE(stored(uuid).updatedAt, now - 1000);
        QVERIFY(model->update(uuid, {{QStringLiteral("workSeconds"), 600}}));
        QCOMPARE(stored(uuid).updatedAt, now);
        QCOMPARE(stored(uuid).createdAt, now - 1000);

        now += 1000;
        QVERIFY(model->setOption(uuid, PresetOption::NotifyOnEnd, false));
        QCOMPARE(stored(uuid).updatedAt, now);
        now += 1000;
        QVERIFY(model->setOption(uuid, PresetOption::NotifyOnEnd, false)); // the same value again
        QCOMPARE(stored(uuid).updatedAt, now - 1000);

        now += 1000;
        const QString copy = model->duplicate(uuid);
        QCOMPARE(stored(copy).createdAt, now);
        QCOMPARE(stored(copy).updatedAt, now);

        now += 1000;
        QVERIFY(model->reset(PresetModel::DefaultUuid));
        QCOMPARE(stored(PresetModel::DefaultUuid).updatedAt, now); // a reset reaches other computers
        QCOMPARE(stored(PresetModel::DefaultUuid).createdAt, 0);
    }

    void anEditHereClearsTheAuthor()
    {
        TimerPreset received;
        received.uuid = QStringLiteral("r");
        received.name = QStringLiteral("From elsewhere");
        received.updatedAt = 5;
        received.updatedBy = QStringLiteral("dev_other");
        model->setPresets({received}, received.uuid);
        QCOMPARE(model->presets().at(model->indexOf(received.uuid)).updatedBy, QStringLiteral("dev_other"));

        QVERIFY(model->update(received.uuid, {{QStringLiteral("name"), QStringLiteral("Mine now")}}));
        QCOMPARE(model->presets().at(model->indexOf(received.uuid)).updatedBy, QString());
    }

    void defaultPresetIsPresentAndCurrent()
    {
        QCOMPARE(model->count(), 1);
        QCOMPARE(model->currentUuid(), PresetModel::DefaultUuid);
        QCOMPARE(model->currentIndex(), 0);

        const QVariantMap d = model->get(PresetModel::DefaultUuid);
        QCOMPARE(d.value(QStringLiteral("workSeconds")).toInt(), 25 * 60);
        QCOMPARE(d.value(QStringLiteral("shortBreakSeconds")).toInt(), 5 * 60);
        QCOMPARE(d.value(QStringLiteral("longBreakSeconds")).toInt(), 15 * 60);
        QCOMPARE(d.value(QStringLiteral("cyclesBeforeLong")).toInt(), 4);
        QVERIFY(d.value(QStringLiteral("builtin")).toBool());
    }

    void createAddsRowAndClampsValues()
    {
        QSignalSpy inserted(model.get(), &QAbstractItemModel::rowsInserted);
        const QString uuid = model->create({
            {QStringLiteral("name"), QStringLiteral("  Deep work ")},
            {QStringLiteral("category"), QStringLiteral(" Work ")},
            {QStringLiteral("workSeconds"), 50 * 60},
            {QStringLiteral("shortBreakSeconds"), -10},
            {QStringLiteral("cyclesBeforeLong"), 1000},
        });

        QVERIFY(!uuid.isEmpty());
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(model->count(), 2);

        const QVariantMap p = model->get(uuid);
        QCOMPARE(p.value(QStringLiteral("name")).toString(), QStringLiteral("Deep work"));
        QCOMPARE(p.value(QStringLiteral("category")).toString(), QStringLiteral("Work"));
        QCOMPARE(p.value(QStringLiteral("workSeconds")).toInt(), 50 * 60);
        QCOMPARE(p.value(QStringLiteral("shortBreakSeconds")).toInt(), 0);
        QCOMPARE(p.value(QStringLiteral("cyclesBeforeLong")).toInt(), 99);
        QVERIFY(!p.value(QStringLiteral("builtin")).toBool());
    }

    void adjustedValuesAreReportedNotJustClamped()
    {
        QSignalSpy adjusted(model.get(), &PresetModel::valuesAdjusted);

        // Valid input: nothing to report.
        const QString uuid = model->create({{QStringLiteral("name"), QStringLiteral("Fine")},
                                            {QStringLiteral("workSeconds"), 50 * 60},
                                            {QStringLiteral("cyclesBeforeLong"), 4}});
        QCOMPARE(adjusted.count(), 0);

        // Out of range, unreadable and too long: each field is named.
        QVERIFY(model->update(uuid, {{QStringLiteral("workSeconds"), 100 * 3600},
                                     {QStringLiteral("shortBreakSeconds"), -10},
                                     {QStringLiteral("cyclesBeforeLong"), QStringLiteral("lots")},
                                     {QStringLiteral("name"), QString(300, QLatin1Char('n'))},
                                     {QStringLiteral("longBreakSeconds"), 10 * 60}}));
        QCOMPARE(adjusted.count(), 1);
        QCOMPARE(adjusted.first().at(0).toString(), uuid);
        QStringList fields = adjusted.first().at(1).toStringList();
        fields.sort();
        QCOMPARE(fields, (QStringList{QStringLiteral("cyclesBeforeLong"), QStringLiteral("name"),
                                      QStringLiteral("shortBreakSeconds"), QStringLiteral("workSeconds")}));
        // The values that were kept are the clamped ones (and the unreadable one is unchanged).
        const QVariantMap p = model->get(uuid);
        QCOMPARE(p.value(QStringLiteral("workSeconds")).toInt(), PresetLimits::MaxWorkSeconds);
        QCOMPARE(p.value(QStringLiteral("shortBreakSeconds")).toInt(), 0);
        QCOMPARE(p.value(QStringLiteral("cyclesBeforeLong")).toInt(), 4);
        QCOMPARE(p.value(QStringLiteral("longBreakSeconds")).toInt(), 10 * 60);

        // An unreadable option is reported too, and keeps its value (even one that reads as 0).
        QVERIFY(model->setOption(uuid, PresetOption::AutoStartMode, 1));
        adjusted.clear();
        QVERIFY(model->update(uuid, {{PresetOption::AutoStartMode, QStringLiteral("abc")}}));
        QCOMPARE(adjusted.count(), 1);
        QCOMPARE(adjusted.first().at(1).toStringList(), QStringList{PresetOption::AutoStartMode});
        QCOMPARE(model->option(uuid, PresetOption::AutoStartMode).toInt(), 1);
    }

        void namesAreLimitedLikeTheEditorFields()
    {
        // The editor stops typing at maxNameLength; other callers (QML, stored data) are cut there.
        const QString longName(500, QLatin1Char('n'));
        const QString uuid = model->create({{QStringLiteral("name"), longName}, {QStringLiteral("category"), longName}});
        const QVariantMap p = model->get(uuid);
        QCOMPARE(p.value(QStringLiteral("name")).toString().size(), PresetModel::maxNameLength());
        QCOMPARE(p.value(QStringLiteral("category")).toString().size(), PresetModel::maxNameLength());
        // The editor's limits are the model's.
        QCOMPARE(PresetModel::maxWorkMinutes() * 60, PresetLimits::MaxWorkSeconds);
        QCOMPARE(PresetModel::maxBreakMinutes() * 60, PresetLimits::MaxBreakSeconds);
        QCOMPARE(PresetModel::maxCycles(), PresetLimits::MaxCycles);
    }

    void namesAreNeverCutInsideACharacter()
    {
        // An emoji takes two UTF-16 units: cut after the first, it would be half a character.
        const QString name = QString(PresetModel::maxNameLength() - 1, QLatin1Char('n')) + QString::fromUtf8("\xF0\x9F\x8D\x85");
        QSignalSpy adjusted(model.get(), &PresetModel::valuesAdjusted);
        const QString uuid = model->create({{QStringLiteral("name"), name}});
        QCOMPARE(model->get(uuid).value(QStringLiteral("name")).toString(), name.left(PresetModel::maxNameLength() - 1));
        QCOMPARE(adjusted.count(), 1);
    }

    void loadedPresetsAreBroughtWithinTheLimits()
    {
        TimerPreset stored;
        stored.uuid = QStringLiteral("stored");
        stored.name = QString(500, QLatin1Char('n'));
        stored.category = QString(500, QLatin1Char('c'));
        stored.workSeconds = 0;
        stored.shortBreakSeconds = -5;
        stored.longBreakSeconds = PresetLimits::MaxBreakSeconds + 1;
        stored.cyclesBeforeLong = 1000;
        stored.options.insert(PresetOption::AutoStartMode, 9);

        QSignalSpy modified(model.get(), &PresetModel::modified);
        model->setPresets({stored}, stored.uuid);
        QCOMPARE(modified.count(), 0); // loading never writes back

        const QVariantMap p = model->get(stored.uuid);
        QCOMPARE(p.value(QStringLiteral("workSeconds")).toInt(), 1);
        QCOMPARE(p.value(QStringLiteral("shortBreakSeconds")).toInt(), 0);
        QCOMPARE(p.value(QStringLiteral("longBreakSeconds")).toInt(), PresetLimits::MaxBreakSeconds);
        QCOMPARE(p.value(QStringLiteral("cyclesBeforeLong")).toInt(), PresetLimits::MaxCycles);
        QCOMPARE(p.value(QStringLiteral("name")).toString().size(), PresetModel::maxNameLength());
        QCOMPARE(p.value(QStringLiteral("category")).toString().size(), PresetModel::maxNameLength());
        QCOMPARE(model->option(stored.uuid, PresetOption::AutoStartMode).toInt(), 2);
    }

    void duplicateKeepsTheNameWithinTheLimit()
    {
        const QString uuid = model->create({{QStringLiteral("name"), QString(500, QLatin1Char('n'))}});
        const QString copyUuid = model->duplicate(uuid);
        const QString copyName = model->get(copyUuid).value(QStringLiteral("name")).toString();
        QVERIFY(!copyName.isEmpty());
        QVERIFY(copyName.size() <= PresetModel::maxNameLength());
        QVERIFY(copyName != model->get(uuid).value(QStringLiteral("name")).toString());
    }

        void createWithBlankNameGetsFallbackName()
    {
        const QString uuid = model->create({{QStringLiteral("name"), QStringLiteral("   ")}});
        QVERIFY(!model->get(uuid).value(QStringLiteral("name")).toString().isEmpty());
    }

    void updateChangesFieldsAndNotifies()
    {
        const QString uuid = model->create({{QStringLiteral("name"), QStringLiteral("A")}});
        QSignalSpy changed(model.get(), &QAbstractItemModel::dataChanged);
        QSignalSpy presetChanged(model.get(), &PresetModel::presetChanged);

        QVERIFY(model->update(uuid, {{QStringLiteral("name"), QStringLiteral("B")},
                                     {QStringLiteral("workSeconds"), 10 * 60}}));
        QCOMPARE(nameAt(1), QStringLiteral("B"));
        QCOMPARE(model->get(uuid).value(QStringLiteral("workSeconds")).toInt(), 10 * 60);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(presetChanged.count(), 1);
        QCOMPARE(presetChanged.at(0).at(0).toString(), uuid);

        QVERIFY(!model->update(QStringLiteral("missing"), {}));
    }

    void updateKeepsTheFieldsItIsNotGiven()
    {
        // The editor passes only what the user changed: the rest stays as stored, which may be a
        // change that arrived from another computer while the editor was open.
        const QString uuid = model->create({{QStringLiteral("name"), QStringLiteral("A")},
                                            {QStringLiteral("category"), QStringLiteral("Work")},
                                            {QStringLiteral("workSeconds"), 25 * 60}});
        QVERIFY(model->update(uuid, {{QStringLiteral("workSeconds"), 30 * 60}}));
        const QVariantMap p = model->get(uuid);
        QCOMPARE(p.value(QStringLiteral("name")).toString(), QStringLiteral("A"));
        QCOMPARE(p.value(QStringLiteral("category")).toString(), QStringLiteral("Work"));
        QCOMPARE(p.value(QStringLiteral("workSeconds")).toInt(), 30 * 60);
    }

    void replacingTheListAnnouncesTheTimersThatChanged()
    {
        const QString renamed = model->create({{QStringLiteral("name"), QStringLiteral("A")}});
        const QString kept = model->create({{QStringLiteral("name"), QStringLiteral("B")}});
        const QString gone = model->create({{QStringLiteral("name"), QStringLiteral("C")}});

        // What a download from Sugo Sync brings: one timer renamed, one only restamped, one
        // deleted, one new.
        QList<TimerPreset> next = model->presets();
        for (TimerPreset &preset : next) {
            if (preset.uuid == renamed) {
                preset.name = QStringLiteral("A2");
                preset.updatedBy = QStringLiteral("desktop");
            } else if (preset.uuid == kept) {
                preset.updatedAt += 1000;
            }
        }
        next.erase(std::remove_if(next.begin(), next.end(),
                                  [&](const TimerPreset &preset) { return preset.uuid == gone; }),
                   next.end());
        TimerPreset added;
        added.uuid = QStringLiteral("added");
        added.name = QStringLiteral("D");
        next.append(added);

        QSignalSpy presetChanged(model.get(), &PresetModel::presetChanged);
        QSignalSpy modified(model.get(), &PresetModel::modified);
        model->setPresets(next, model->currentUuid());
        QCOMPARE(modified.count(), 0);
        QCOMPARE(presetChanged.count(), 1);
        QCOMPARE(presetChanged.at(0).at(0).toString(), renamed);
        // Listeners already read the new version.
        QCOMPARE(model->get(renamed).value(QStringLiteral("name")).toString(), QStringLiteral("A2"));
    }

    void createAndApplyFieldsWithOptions()
    {
        const QString uuid = model->create({
            {QStringLiteral("name"), QStringLiteral("Custom")},
            {PresetOption::AutoStartMode, 1},
            {PresetOption::SilenceWhileWorking, true},
            {PresetOption::SoundOnEnd, false},
            {PresetOption::NotifyOnEnd, true}
        });

        QCOMPARE(model->option(uuid, PresetOption::AutoStartMode).toInt(), 1);
        QCOMPARE(model->option(uuid, PresetOption::SilenceWhileWorking).toBool(), true);
        QCOMPARE(model->option(uuid, PresetOption::SoundOnEnd).toBool(), false);
        QCOMPARE(model->option(uuid, PresetOption::NotifyOnEnd).toBool(), true);

        // Update with new options
        QVERIFY(model->update(uuid, {
            {PresetOption::AutoStartMode, 2},
            {PresetOption::SilenceWhileWorking, false}
        }));
        QCOMPARE(model->option(uuid, PresetOption::AutoStartMode).toInt(), 2);
        QCOMPARE(model->option(uuid, PresetOption::SilenceWhileWorking).toBool(), false);
    }

    void duplicateCopiesFieldsAndOptionsAndIsNeverBuiltin()
    {
        QVERIFY(model->setOption(PresetModel::DefaultUuid, PresetOption::SilenceWhileWorking, true));
        const QString copyUuid = model->duplicate(PresetModel::DefaultUuid);

        QVERIFY(!copyUuid.isEmpty());
        QVERIFY(copyUuid != PresetModel::DefaultUuid);
        QCOMPARE(model->count(), 2);
        QCOMPARE(model->indexOf(copyUuid), 1); // right after its source

        const QVariantMap copy = model->get(copyUuid);
        QVERIFY(!copy.value(QStringLiteral("builtin")).toBool());
        QVERIFY(copy.value(QStringLiteral("name")).toString().contains(nameAt(0)));
        QCOMPARE(copy.value(QStringLiteral("workSeconds")).toInt(), 25 * 60);
        QVERIFY(model->option(copyUuid, PresetOption::SilenceWhileWorking).toBool());
    }

    void duplicatingUnknownUuidFails()
    {
        QVERIFY(model->duplicate(QStringLiteral("missing")).isEmpty());
        QCOMPARE(model->count(), 1);
    }

    void builtinCannotBeRemoved()
    {
        QVERIFY(!model->remove(PresetModel::DefaultUuid));
        QCOMPARE(model->count(), 1);
    }

    void removingCurrentFallsBackToDefault()
    {
        const QString uuid = model->create({{QStringLiteral("name"), QStringLiteral("Temp")}});
        model->setCurrentUuid(uuid);
        QCOMPARE(model->currentUuid(), uuid);

        QSignalSpy current(model.get(), &PresetModel::currentChanged);
        QVERIFY(model->remove(uuid));
        QCOMPARE(model->count(), 1);
        QCOMPARE(model->currentUuid(), PresetModel::DefaultUuid);
        QVERIFY(current.count() >= 1);
    }

    void removalListenersAlreadySeeTheNewSelection()
    {
        // The built-in timer, edited: its values are no longer the factory ones.
        QVERIFY(model->update(PresetModel::DefaultUuid, {{QStringLiteral("workSeconds"), 40 * 60}}));
        const QString uuid = model->create({{QStringLiteral("name"), QStringLiteral("Temp")}});
        model->setCurrentUuid(uuid);

        QList<int> indexes;
        QList<int> workSeconds;
        const auto record = [&] {
            indexes.append(model->currentIndex());
            workSeconds.append(model->currentPreset().workSeconds);
        };
        const auto removed = connect(model.get(), &QAbstractItemModel::rowsRemoved, this, record);
        const auto counted = connect(model.get(), &PresetModel::countChanged, this, record);

        const bool ok = model->remove(uuid);
        disconnect(removed); // the slot reads locals of this test
        disconnect(counted);
        QVERIFY(ok);
        QCOMPARE(indexes, (QList<int>{0, 0}));
        QCOMPARE(workSeconds, (QList<int>{40 * 60, 40 * 60}));
    }

    void removingOtherPresetKeepsSelection()
    {
        const QString a = model->create({{QStringLiteral("name"), QStringLiteral("A")}});
        const QString b = model->create({{QStringLiteral("name"), QStringLiteral("B")}});
        model->setCurrentUuid(b);
        QVERIFY(model->remove(a));
        QCOMPARE(model->currentUuid(), b);
        QCOMPARE(model->currentIndex(), 1);
    }

    void unknownCurrentUuidIsIgnored()
    {
        model->setCurrentUuid(QStringLiteral("missing"));
        QCOMPARE(model->currentUuid(), PresetModel::DefaultUuid);
    }

    void optionsFallBackToDefaultsAndRoundTrip()
    {
        // A timer of one's own: the default timer has silencing switched on.
        const QString uuid = model->create({{QStringLiteral("name"), QStringLiteral("Mine")}});
        QVERIFY(model->option(uuid, PresetOption::SoundOnEnd).toBool());          // default on
        QCOMPARE(model->option(uuid, PresetOption::AutoStartMode).toInt(), 0); // default off
        QVERIFY(!model->option(uuid, PresetOption::SilenceWhileWorking).toBool());

        QSignalSpy presetChanged(model.get(), &PresetModel::presetChanged);
        QVERIFY(model->setOption(uuid, PresetOption::AutoStartMode, 2));
        QCOMPARE(model->option(uuid, PresetOption::AutoStartMode).toInt(), 2);
        QCOMPARE(presetChanged.count(), 1);

        const QVariantMap options = model->get(uuid).value(QStringLiteral("options")).toMap();
        QCOMPARE(options.value(PresetOption::AutoStartMode).toInt(), 2);
        QVERIFY(options.contains(PresetOption::NotifyOnEnd)); // defaults merged in
    }

    void unknownOptionIsRejected()
    {
        QVERIFY(!model->setOption(PresetModel::DefaultUuid, QStringLiteral("bogus"), true));
        QVERIFY(!model->setOption(QStringLiteral("missing"), PresetOption::AutoStartMode, 2));
    }

    void everyDescriptorHasLabelAndDefault()
    {
        const QList<OptionDescriptor> list = PresetSettings::descriptors();
        QVERIFY(list.size() >= 4);
        bool foundAutoStartMode = false;
        for (const OptionDescriptor &d : list) {
            QVERIFY(!d.key.isEmpty());
            QVERIFY(!d.label.isEmpty());
            QVERIFY(d.defaultValue.isValid());
            if (d.key == PresetOption::AutoStartMode) {
                QCOMPARE(d.type, QStringLiteral("int"));
                QCOMPARE(d.defaultValue.toInt(), 0);
                QCOMPARE(d.minimum, 0);
                QCOMPARE(d.maximum, 2);
                foundAutoStartMode = true;
            } else {
                QCOMPARE(d.type, QStringLiteral("bool"));
            }
        }
        QVERIFY(foundAutoStartMode);
    }

    void resetRestoresBuiltinOnly()
    {
        const QString uuid = PresetModel::DefaultUuid;
        model->update(uuid, {{QStringLiteral("workSeconds"), 99 * 60}, {QStringLiteral("name"), QStringLiteral("X")}});
        model->setOption(uuid, PresetOption::AutoStartMode, 2);

        QVERIFY(model->reset(uuid));
        const QVariantMap d = model->get(uuid);
        QCOMPARE(d.value(QStringLiteral("workSeconds")).toInt(), 25 * 60);
        QCOMPARE(model->option(uuid, PresetOption::AutoStartMode).toInt(), 0);

        const QString custom = model->create({});
        QVERIFY(!model->reset(custom));
    }

    void rolesExposeBuiltinFlag()
    {
        const QModelIndex idx = model->index(0);
        QVERIFY(model->data(idx, PresetModel::BuiltinRole).toBool());
        QVERIFY(model->roleNames().values().contains("uuid"));
        QVERIFY(model->roleNames().values().contains("iconName"));
        QVERIFY(model->roleNames().values().contains("workMinutes"));
    }

    void newUsersStartWithOnePomodoroThatSilences()
    {
        QCOMPARE(model->count(), 1);
        const TimerPreset preset = model->currentPreset();
        QCOMPARE(preset.uuid, PresetModel::DefaultUuid);
        QCOMPARE(preset.workSeconds, 25 * 60);
        QCOMPARE(preset.shortBreakSeconds, 5 * 60);
        QCOMPARE(preset.longBreakSeconds, 15 * 60);
        QCOMPARE(preset.cyclesBeforeLong, 4);
        QCOMPARE(model->option(PresetModel::DefaultUuid, PresetOption::SilenceWhileWorking).toBool(), true);
        QCOMPARE(model->data(model->index(0), PresetModel::IconNameRole).toString(), QStringLiteral("sugo-timer"));

        // Rows by position (the search lists them so).
        QCOMPARE(model->uuidAt(0), PresetModel::DefaultUuid);
        QVERIFY(model->uuidAt(1).isEmpty());
        QVERIFY(model->uuidAt(-1).isEmpty());

        // Reset to defaults brings the silencing back.
        model->setOption(PresetModel::DefaultUuid, PresetOption::SilenceWhileWorking, false);
        QVERIFY(model->reset(PresetModel::DefaultUuid));
        QCOMPARE(model->option(PresetModel::DefaultUuid, PresetOption::SilenceWhileWorking).toBool(), true);
    }

    void currentPropertiesReflectSelectedPreset()
    {
        QCOMPARE(model->currentUuid(), PresetModel::DefaultUuid);
        QCOMPARE(model->currentPreset().workSeconds, 25 * 60);

        const QString deepWork = model->create({{QStringLiteral("name"), QStringLiteral("Deep Work")},
                                                {QStringLiteral("workSeconds"), 50 * 60},
                                                {QStringLiteral("iconName"), QStringLiteral("sugo-focus")}});
        model->setCurrentUuid(deepWork);
        QCOMPARE(model->currentPreset().workSeconds, 50 * 60);
        QCOMPARE(model->currentName(), QStringLiteral("Deep Work"));
        QCOMPARE(model->data(model->index(model->currentIndex()), PresetModel::IconNameRole).toString(), QStringLiteral("sugo-focus"));
    }

    void customIconCanBeSelectedAndOverridesDefault()
    {
        const QVariantList icons = model->availableIcons();
        QVERIFY(!icons.isEmpty());
        QVERIFY(icons.size() >= 10);

        const QString customUuid = model->create({
            {QStringLiteral("name"), QStringLiteral("Reading Session")},
            {QStringLiteral("iconName"), QStringLiteral("sugo-research")}
        });

        const int row = model->indexOf(customUuid);
        QVERIFY(row >= 0);
        QCOMPARE(model->data(model->index(row), PresetModel::IconNameRole).toString(), QStringLiteral("sugo-research"));

        model->update(customUuid, {{QStringLiteral("iconName"), QStringLiteral("sugo-goal")}});
        QCOMPARE(model->data(model->index(row), PresetModel::IconNameRole).toString(), QStringLiteral("sugo-goal"));

        // Theme icons chosen by earlier versions show as the matching Sugo icon.
        model->update(customUuid, {{QStringLiteral("iconName"), QStringLiteral("flash-symbolic")}});
        QCOMPARE(model->data(model->index(row), PresetModel::IconNameRole).toString(), QStringLiteral("sugo-focus"));
        model->update(customUuid, {{QStringLiteral("iconName"), QStringLiteral("view-readermode-symbolic")}});
        QCOMPARE(model->data(model->index(row), PresetModel::IconNameRole).toString(), QStringLiteral("sugo-study"));
        // Any other theme icon is kept as it is.
        model->update(customUuid, {{QStringLiteral("iconName"), QStringLiteral("games-highscores")}});
        QCOMPARE(model->data(model->index(row), PresetModel::IconNameRole).toString(), QStringLiteral("games-highscores"));

        QCOMPARE(PresetModel::iconSource(QStringLiteral("sugo-study")), QStringLiteral("qrc:/icons/sugo-study.svg"));
        QCOMPARE(PresetModel::iconSource(QStringLiteral("games-highscores")), QStringLiteral("games-highscores"));
    }

    void setOptionNotifiesTheRow()
    {
        QSignalSpy changed(model.get(), &QAbstractItemModel::dataChanged);
        QVERIFY(model->setOption(PresetModel::DefaultUuid, PresetOption::SoundOnEnd, false));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().at(0).toModelIndex().row(), 0);
    }

    void intOptionsAreClampedToTheirOwnRange()
    {
        const QString uuid = PresetModel::DefaultUuid;
        QVERIFY(model->setOption(uuid, PresetOption::AutoStartMode, 7));
        QCOMPARE(model->option(uuid, PresetOption::AutoStartMode).toInt(), 2);
        QVERIFY(model->setOption(uuid, PresetOption::AutoStartMode, -3));
        QCOMPARE(model->option(uuid, PresetOption::AutoStartMode).toInt(), 0);

        model->update(uuid, {{PresetOption::AutoStartMode, 42}});
        QCOMPARE(model->option(uuid, PresetOption::AutoStartMode).toInt(), 2);
    }
};

QTEST_GUILESS_MAIN(PresetModelTest)
#include "tst_presetmodel.moc"
