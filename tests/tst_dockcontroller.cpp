// SPDX-License-Identifier: GPL-3.0-or-later
#include <QAction>
#include <QApplication>
#include <QMenu>
#include <QTemporaryDir>
#include <QTest>
#include <QWindow>

#include <memory>

#include "AppSettings.h"
#include "Autostart.h"
#include "DockController.h"
#include "FakeClock.h"
#include "PresetModel.h"
#include "Settings.h"
#include "TimerEngine.h"

namespace
{
class NoAutostart final : public Autostart
{
public:
    bool isSupported() const override { return false; }
    bool isEnabled() const override { return false; }
    bool setEnabled(bool) override { return false; }
    void repair() override {}
};

QAction *actionNamed(const QMenu *menu, const QString &text)
{
    const QList<QAction *> actions = menu->actions();
    for (QAction *action : actions) {
        if (action->text() == text) {
            return action;
        }
    }
    return nullptr;
}
} // namespace

/// The Dock icon's badge, menu and hiding, through a fake Dock.
class DockControllerTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir dir;
    FakeClock clock;
    std::unique_ptr<TimerEngine> engine;
    std::unique_ptr<PresetModel> presets;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<DockController> dock;
    QStringList badges;      ///< Every badge sent to the Dock.
    QList<bool> hiddenCalls; ///< Every hide/show of the Dock icon.
    QMenu *installedMenu = nullptr;

    DockController::Native fakeDock()
    {
        return {[this](const QString &label) { badges.append(label); },
                [this](bool hidden) { hiddenCalls.append(hidden); },
                [this](QMenu *menu) { installedMenu = menu; }};
    }

private Q_SLOTS:
    void init()
    {
        clock = FakeClock();
        badges.clear();
        hiddenCalls.clear();
        installedMenu = nullptr;
        engine = std::make_unique<TimerEngine>(&clock, nullptr);
        presets = std::make_unique<PresetModel>();
        settings = std::make_unique<AppSettings>(Settings::open(dir.filePath(QStringLiteral("settings.conf"))),
                                                 std::make_unique<NoAutostart>(), true);
        dock = std::make_unique<DockController>(engine.get(), presets.get(), settings.get(), fakeDock());
    }

    void cleanup()
    {
        dock.reset();
        settings.reset();
        presets.reset();
        engine.reset();
        QFile::remove(dir.filePath(QStringLiteral("settings.conf")));
    }

    void badgeText()
    {
        using State = TimerEngine::State;
        QCOMPARE(DockController::badgeText(State::Idle, 1500), QString());
        QCOMPARE(DockController::badgeText(State::Working, 1500), QStringLiteral("25"));
        QCOMPARE(DockController::badgeText(State::Working, 1441), QStringLiteral("25"));
        QCOMPARE(DockController::badgeText(State::Working, 1440), QStringLiteral("24"));
        QCOMPARE(DockController::badgeText(State::Working, 1), QStringLiteral("1"));
        QCOMPARE(DockController::badgeText(State::ShortBreak, 0), QStringLiteral("0"));
        QCOMPARE(DockController::badgeText(State::Paused, 600), QStringLiteral("10"));
        QCOMPARE(DockController::badgeText(State::Working, 90 * 60), QStringLiteral("90"));
    }

    void badgeFollowsTheTimerOncePerMinute()
    {
        QVERIFY(badges.isEmpty()); // idle: nothing, and nothing sent
        QCOMPARE(dock->badge(), QString());

        engine->start();
        QCOMPARE(dock->badge(), QStringLiteral("25"));
        QCOMPARE(badges, QStringList{QStringLiteral("25")});

        // Ticks within the minute send nothing.
        for (int i = 0; i < 59; ++i) {
            clock.advanceSeconds(1);
            engine->refresh();
        }
        QCOMPARE(badges.size(), 1);
        clock.advanceSeconds(1);
        engine->refresh();
        QCOMPARE(dock->badge(), QStringLiteral("24"));
        QCOMPARE(badges.size(), 2);

        // Paused: the value stays.
        engine->pause();
        QCOMPARE(dock->badge(), QStringLiteral("24"));
        QCOMPARE(badges.size(), 2);

        engine->stop();
        QCOMPARE(dock->badge(), QString());
        QCOMPARE(badges.last(), QString());
    }

    void badgeSettingTurnsItOff()
    {
        engine->start();
        settings->setShowDockBadge(false);
        QCOMPARE(dock->badge(), QString());
        QCOMPARE(badges.last(), QString());
        clock.advanceSeconds(120);
        engine->refresh();
        QCOMPARE(dock->badge(), QString());
        settings->setShowDockBadge(true);
        QCOMPARE(dock->badge(), QStringLiteral("23"));
    }

    void hiddenIconDecision()
    {
        QVERIFY(DockController::wantsHiddenIcon(true, true, false));
        QVERIFY(!DockController::wantsHiddenIcon(true, true, true));
        QVERIFY(!DockController::wantsHiddenIcon(true, false, false)); // no menu bar icon to come back from
        QVERIFY(!DockController::wantsHiddenIcon(false, true, false));
    }

    void iconHidesWithTheWindow()
    {
        QWindow window;
        dock->setWindow(&window);
        QVERIFY(!dock->iconHidden()); // option off by default
        QVERIFY(hiddenCalls.isEmpty());

        settings->setHideDockIcon(true);
        QVERIFY(dock->iconHidden()); // window hidden
        QCOMPARE(hiddenCalls, QList<bool>{true});

        // Showing the window gives the icon back first, then nothing more is sent.
        dock->prepareToShowWindow();
        QVERIFY(!dock->iconHidden());
        window.show();
        QVERIFY(!dock->iconHidden());
        QCOMPARE(hiddenCalls, (QList<bool>{true, false}));

        window.hide();
        QVERIFY(dock->iconHidden());

        // Without the menu bar icon there would be no way back: the icon stays.
        settings->setShowTrayIcon(false);
        QVERIFY(!dock->iconHidden());
        settings->setShowTrayIcon(true);
        QVERIFY(dock->iconHidden());
        settings->setTrayAvailable(false);
        QVERIFY(!dock->iconHidden());
        settings->setTrayAvailable(true);
        settings->setHideDockIcon(false);
        QVERIFY(!dock->iconHidden());
        QCOMPARE(hiddenCalls, (QList<bool>{true, false, true, false, true, false, true, false}));
    }

    void menuControlsTheTimer()
    {
        QCOMPARE(installedMenu, dock->menu());
        QMenu *menu = dock->menu();
        QAction *start = actionNamed(menu, QStringLiteral("Start"));
        QAction *stop = actionNamed(menu, QStringLiteral("Stop"));
        QAction *skip = actionNamed(menu, QStringLiteral("Skip"));
        QAction *timers = actionNamed(menu, QStringLiteral("Timer"));
        QVERIFY(start && stop && skip && timers && timers->menu());
        QVERIFY(!stop->isVisible());
        QVERIFY(!skip->isVisible());
        QVERIFY(timers->isEnabled());

        start->trigger();
        QCOMPARE(engine->state(), TimerEngine::State::Working);
        QCOMPARE(start->text(), QStringLiteral("Pause"));
        QVERIFY(stop->isVisible());
        QVERIFY(skip->isVisible());
        QVERIFY(!timers->isEnabled());
        start->trigger();
        QCOMPARE(start->text(), QStringLiteral("Resume"));
        stop->trigger();
        QCOMPARE(engine->state(), TimerEngine::State::Idle);
        QCOMPARE(start->text(), QStringLiteral("Start"));
    }

    void menuPicksTheTimer()
    {
        const QString other = presets->create({{QStringLiteral("name"), QStringLiteral("Reading")}});
        QAction *timers = actionNamed(dock->menu(), QStringLiteral("Timer"));
        QAction *reading = actionNamed(timers->menu(), QStringLiteral("Reading"));
        QVERIFY(reading);
        reading->trigger();
        QCOMPARE(presets->currentUuid(), other);
        QVERIFY(reading->isChecked());
    }

    void menuKeepsTheTimerWhileAPhaseRuns()
    {
        const QString current = presets->currentUuid();
        presets->create({{QStringLiteral("name"), QStringLiteral("Reading")}});
        QCOMPARE(presets->currentUuid(), current);
        QAction *timers = actionNamed(dock->menu(), QStringLiteral("Timer"));
        QAction *reading = actionNamed(timers->menu(), QStringLiteral("Reading"));
        QVERIFY(reading);

        engine->start();
        QVERIFY(!timers->isEnabled());
        // A copy of the menu shown before the start can still trigger the entry itself.
        reading->trigger();
        QCOMPARE(presets->currentUuid(), current);
        QCOMPARE(engine->state(), TimerEngine::State::Working);
        QVERIFY(!reading->isChecked());
    }
};

QTEST_MAIN(DockControllerTest)
#include "tst_dockcontroller.moc"
