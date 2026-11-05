// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "Localization.h"

#include <memory>

#include "FakeClock.h"
#include "PresetModel.h"
#include "TimerBinding.h"
#include "TimerEngine.h"

class TimerBindingTest : public QObject
{
    Q_OBJECT

private:
    FakeClock clock;
    std::unique_ptr<TimerEngine> engine;
    std::unique_ptr<PresetModel> presets;
    std::unique_ptr<TimerBinding> binding;

    /// The engine's auto-start mode, as it behaves: whether a break, then work, start by
    /// themselves when the phase before ends. Leaves the engine reset.
    TimerEngine::AutoStartMode autoStartBehaviour()
    {
        engine->reset();
        engine->start();
        clock.advanceSeconds(engine->workSeconds());
        engine->refresh();
        const bool breakStarted = engine->isRunning();
        if (!breakStarted) {
            engine->start();
        }
        clock.advanceSeconds(engine->shortBreakSeconds());
        engine->refresh();
        const bool workStarted = engine->isRunning();
        engine->reset();
        if (workStarted) {
            // Work alone starting by itself is no mode at all: matches none of them.
            return breakStarted ? TimerEngine::AutoStartMode::All : static_cast<TimerEngine::AutoStartMode>(-1);
        }
        return breakStarted ? TimerEngine::AutoStartMode::BreaksOnly : TimerEngine::AutoStartMode::Manual;
    }

private Q_SLOTS:
    void initTestCase()
    {
    }

    void init()
    {
        clock = FakeClock();
        engine = std::make_unique<TimerEngine>(&clock, nullptr);
        presets = std::make_unique<PresetModel>();
        binding = std::make_unique<TimerBinding>(engine.get(), presets.get());
    }

    void appliesDefaultPresetAtStartup()
    {
        QCOMPARE(engine->workSeconds(), 25 * 60);
        QCOMPARE(engine->shortBreakSeconds(), 5 * 60);
        QCOMPARE(engine->longBreakSeconds(), 15 * 60);
        QCOMPARE(engine->cyclesBeforeLong(), 4);
        QCOMPARE(autoStartBehaviour(), TimerEngine::AutoStartMode::Manual);
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
        presets->setOption(uuid, PresetOption::AutoStartMode, 2);

        presets->setCurrentUuid(uuid);

        QCOMPARE(engine->workSeconds(), 50 * 60);
        QCOMPARE(engine->shortBreakSeconds(), 10 * 60);
        QCOMPARE(engine->longBreakSeconds(), 30 * 60);
        QCOMPARE(engine->cyclesBeforeLong(), 2);
        QCOMPARE(autoStartBehaviour(), TimerEngine::AutoStartMode::All);
        QCOMPARE(engine->remainingSeconds(), 50 * 60); // idle display follows the preset
    }

    void editingCurrentPresetUpdatesEngine()
    {
        presets->update(PresetModel::DefaultUuid, {{QStringLiteral("workSeconds"), 30 * 60}});
        QCOMPARE(engine->workSeconds(), 30 * 60);

        presets->setOption(PresetModel::DefaultUuid, PresetOption::AutoStartMode, 2);
        QCOMPARE(autoStartBehaviour(), TimerEngine::AutoStartMode::All);
    }

    void editingThePendingBreakToZeroLeavesWorkWaiting()
    {
        engine->start();
        clock.advanceSeconds(engine->workSeconds());
        engine->refresh();
        QCOMPARE(engine->state(), TimerEngine::State::Idle);
        QCOMPARE(engine->phase(), TimerEngine::Phase::ShortBreak);

        // Editing the current timer while Idle is allowed: no 0:00 break may stay pending.
        presets->update(PresetModel::DefaultUuid, {{QStringLiteral("shortBreakSeconds"), 0}});
        QCOMPARE(engine->state(), TimerEngine::State::Idle);
        QCOMPARE(engine->phase(), TimerEngine::Phase::Work);
        QCOMPARE(engine->remainingSeconds(), engine->workSeconds());
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
        QCOMPARE(autoStartBehaviour(), TimerEngine::AutoStartMode::BreaksOnly);

        presets->setOption(PresetModel::DefaultUuid, PresetOption::AutoStartMode, 2);
        QCOMPARE(autoStartBehaviour(), TimerEngine::AutoStartMode::All);

        presets->setOption(PresetModel::DefaultUuid, PresetOption::AutoStartMode, 0);
        QCOMPARE(autoStartBehaviour(), TimerEngine::AutoStartMode::Manual);
    }
};

QTEST_GUILESS_MAIN(TimerBindingTest)
#include "tst_timerbinding.moc"
