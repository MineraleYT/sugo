// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "Localization.h"

#include <memory>

#include "PresetModel.h"
#include "TimerBinding.h"
#include "TimerEngine.h"

class TimerBindingTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<TimerEngine> engine;
    std::unique_ptr<PresetModel> presets;
    std::unique_ptr<TimerBinding> binding;

private Q_SLOTS:
    void initTestCase()
    {
    }

    void init()
    {
        engine = std::make_unique<TimerEngine>();
        presets = std::make_unique<PresetModel>();
        binding = std::make_unique<TimerBinding>(engine.get(), presets.get());
    }

    void appliesDefaultPresetAtStartup()
    {
        QCOMPARE(engine->workSeconds(), 25 * 60);
        QCOMPARE(engine->shortBreakSeconds(), 5 * 60);
        QCOMPARE(engine->longBreakSeconds(), 15 * 60);
        QCOMPARE(engine->cyclesBeforeLong(), 4);
        QCOMPARE(engine->autoStartMode(), TimerEngine::AutoStartMode::Manual);
    }

    void selectingPresetConfiguresEngine()
    {
        const QString uuid = presets->create({
            {QStringLiteral("name"), QStringLiteral("Study")},
            {QStringLiteral("workSeconds"), 50 * 60},
            {QStringLiteral("shortBreakSeconds"), 10 * 60},
            {QStringLiteral("longBreakSeconds"), 30 * 60},
            {QStringLiteral("cyclesBeforeLong"), 2},
        });
        presets->setOption(uuid, PresetOption::AutoStart, true);

        presets->setCurrentUuid(uuid);

        QCOMPARE(engine->workSeconds(), 50 * 60);
        QCOMPARE(engine->shortBreakSeconds(), 10 * 60);
        QCOMPARE(engine->longBreakSeconds(), 30 * 60);
        QCOMPARE(engine->cyclesBeforeLong(), 2);
        QCOMPARE(engine->autoStartMode(), TimerEngine::AutoStartMode::All);
        QCOMPARE(engine->remainingSeconds(), 50 * 60); // idle display follows the preset
    }

    void editingCurrentPresetUpdatesEngine()
    {
        presets->update(PresetModel::DefaultUuid, {{QStringLiteral("workSeconds"), 30 * 60}});
        QCOMPARE(engine->workSeconds(), 30 * 60);

        presets->setOption(PresetModel::DefaultUuid, PresetOption::AutoStart, true);
        QCOMPARE(engine->autoStartMode(), TimerEngine::AutoStartMode::All);
    }

    void editingOtherPresetLeavesEngineAlone()
    {
        const QString other = presets->create({{QStringLiteral("workSeconds"), 10 * 60}});
        presets->update(other, {{QStringLiteral("workSeconds"), 11 * 60}});
        QCOMPARE(engine->workSeconds(), 25 * 60);
    }

    void switchingPresetResetsEngine()
    {
        const QString uuid = presets->create({});
        engine->start();
        QCOMPARE(engine->state(), TimerEngine::State::Working);

        presets->setCurrentUuid(uuid);
        QCOMPARE(engine->state(), TimerEngine::State::Idle);
        QCOMPARE(engine->phase(), TimerEngine::Phase::Work);
    }

    void unrelatedModelChangesDoNotResetRunningTimer()
    {
        engine->start();
        presets->duplicate(PresetModel::DefaultUuid); // shifts rows, emits currentChanged
        presets->create({});
        QCOMPARE(engine->state(), TimerEngine::State::Working);
    }

    void removingCurrentPresetFallsBackToDefaultConfig()
    {
        const QString uuid = presets->create({{QStringLiteral("workSeconds"), 10 * 60}});
        presets->setCurrentUuid(uuid);
        QCOMPARE(engine->workSeconds(), 10 * 60);

        presets->remove(uuid);
        QCOMPARE(engine->workSeconds(), 25 * 60);
    }

    void autoStartModeAppliesToEngine()
    {
        presets->setOption(PresetModel::DefaultUuid, PresetOption::AutoStartMode, 1);
        QCOMPARE(engine->autoStartMode(), TimerEngine::AutoStartMode::BreaksOnly);

        presets->setOption(PresetModel::DefaultUuid, PresetOption::AutoStartMode, 2);
        QCOMPARE(engine->autoStartMode(), TimerEngine::AutoStartMode::All);

        presets->setOption(PresetModel::DefaultUuid, PresetOption::AutoStartMode, 0);
        QCOMPARE(engine->autoStartMode(), TimerEngine::AutoStartMode::Manual);
    }
};

QTEST_GUILESS_MAIN(TimerBindingTest)
#include "tst_timerbinding.moc"
