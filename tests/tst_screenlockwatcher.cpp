// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDBusConnection>
#include <QDBusMessage>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include <memory>

#include "AppSettings.h"
#include "Autostart.h"
#include "FakeClock.h"
#include "PrivateDBus.h"
#include "ScreenLockWatcher.h"
#include "TimerEngine.h"

class ScreenLockWatcherTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<FakeClock> clock;
    std::unique_ptr<TimerEngine> engine;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<ScreenLockWatcher> watcher;

private Q_SLOTS:
    void initTestCase()
    {
        // Keep the settings file away from the user's real configuration.
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        clock = std::make_unique<FakeClock>();
        engine = std::make_unique<TimerEngine>(clock.get(), nullptr);
        settings = std::make_unique<AppSettings>(Settings::openDefault(), nullptr, false);
        settings->setAutoPauseOnScreenLock(true);

        // Disconnected bus so tests don't touch system D-Bus
        QDBusConnection bus(QStringLiteral("tst_disconnected_bus"));
        watcher = std::make_unique<ScreenLockWatcher>(bus);
        watcher->attach(engine.get(), settings.get());
    }

    void lockPausesActiveWorkTimer()
    {
        engine->start();
        QCOMPARE(engine->state(), TimerEngine::State::Working);

        QSignalSpy pauseSpy(watcher.get(), &ScreenLockWatcher::pausedByScreenLockChanged);
        QSignalSpy unlockSpy(watcher.get(), &ScreenLockWatcher::screenUnlockedAfterPause);

        // Screen is locked
        watcher->onActiveChanged(true);

        QCOMPARE(engine->state(), TimerEngine::State::Paused);
        QVERIFY(watcher->isPausedByScreenLock());
        QCOMPARE(pauseSpy.count(), 1);

        // Screen is unlocked
        watcher->onActiveChanged(false);
        QCOMPARE(unlockSpy.count(), 1);
        QVERIFY(watcher->isPausedByScreenLock());

        // Resuming clears the flag
        engine->resume();
        QCOMPARE(engine->state(), TimerEngine::State::Working);
        QVERIFY(!watcher->isPausedByScreenLock());
    }

    void disabledSettingDoesNotPause()
    {
        settings->setAutoPauseOnScreenLock(false);
        engine->start();
        QCOMPARE(engine->state(), TimerEngine::State::Working);

        watcher->onActiveChanged(true);

        QCOMPARE(engine->state(), TimerEngine::State::Working);
        QVERIFY(!watcher->isPausedByScreenLock());
    }

    void lockWhileAlreadyPausedDoesNotMarkScreenLock()
    {
        engine->start();
        engine->pause();
        QCOMPARE(engine->state(), TimerEngine::State::Paused);

        watcher->onActiveChanged(true);
        QVERIFY(!watcher->isPausedByScreenLock());
    }

    void theSameChangeReportedTwiceCountsOnce()
    {
        engine->start();
        QSignalSpy pauseSpy(watcher.get(), &ScreenLockWatcher::pausedByScreenLockChanged);
        QSignalSpy unlockSpy(watcher.get(), &ScreenLockWatcher::screenUnlockedAfterPause);

        // Plasma sends ActiveChanged on /ScreenSaver and /org/freedesktop/ScreenSaver.
        watcher->onActiveChanged(true);
        watcher->onActiveChanged(true);
        QCOMPARE(pauseSpy.count(), 1);

        watcher->onActiveChanged(false);
        watcher->onActiveChanged(false);
        QCOMPARE(unlockSpy.count(), 1);
    }

    void clearNoticeClearsFlag()
    {
        engine->start();
        watcher->onActiveChanged(true);
        QVERIFY(watcher->isPausedByScreenLock());

        watcher->clearLockPauseNotice();
        QVERIFY(!watcher->isPausedByScreenLock());
    }

    void sleepPausesTheRunningPhaseUntilTheUserResumes()
    {
        engine->start();
        QSignalSpy sleepSpy(watcher.get(), &ScreenLockWatcher::pausedBySleepChanged);
        QSignalSpy wokeSpy(watcher.get(), &ScreenLockWatcher::wokeUpAfterPause);

        watcher->onPrepareForSleep(true);
        QCOMPARE(engine->state(), TimerEngine::State::Paused);
        QVERIFY(watcher->isPausedBySleep());
        QCOMPARE(sleepSpy.count(), 1);

        // An hour asleep: on waking up the countdown has not moved on, and stays paused.
        const int remaining = engine->remainingSeconds();
        clock->advanceSeconds(60 * 60);
        engine->refresh();
        watcher->onPrepareForSleep(false);
        QCOMPARE(wokeSpy.count(), 1);
        QCOMPARE(engine->state(), TimerEngine::State::Paused);
        QCOMPARE(engine->remainingSeconds(), remaining);

        engine->resume();
        QVERIFY(!watcher->isPausedBySleep());
    }

    void sleepPausesBreaksTooAndIgnoresIdle()
    {
        watcher->onPrepareForSleep(true);
        QCOMPARE(engine->state(), TimerEngine::State::Idle);
        QVERIFY(!watcher->isPausedBySleep());
        watcher->onPrepareForSleep(false);

        engine->start();
        engine->skip(); // into the short break
        QCOMPARE(engine->state(), TimerEngine::State::ShortBreak);
        watcher->onPrepareForSleep(true);
        QCOMPARE(engine->state(), TimerEngine::State::Paused);
        QVERIFY(watcher->isPausedBySleep());
    }

    void sleepWhenThePhaseIsAlreadyDuePausesTheNextOne()
    {
        // The deadline passed but no tick has noticed yet; the break that follows starts by itself.
        engine->setAutoStartMode(TimerEngine::AutoStartMode::All);
        engine->start();
        clock->advanceSeconds(engine->remainingSeconds() + 1);
        QSignalSpy wokeSpy(watcher.get(), &ScreenLockWatcher::wokeUpAfterPause);

        watcher->onPrepareForSleep(true);
        QCOMPARE(engine->state(), TimerEngine::State::Paused);
        QVERIFY(watcher->isPausedBySleep());
        watcher->onPrepareForSleep(false);
        QCOMPARE(wokeSpy.count(), 1);
    }

    void lockWhenWorkIsAlreadyDueLeavesTheBreakRunning()
    {
        engine->setAutoStartMode(TimerEngine::AutoStartMode::All);
        engine->start();
        clock->advanceSeconds(engine->remainingSeconds() + 1);

        watcher->onActiveChanged(true);
        QCOMPARE(engine->state(), TimerEngine::State::ShortBreak);
        QVERIFY(!watcher->isPausedByScreenLock());
    }

    void wakingUpAfterTheUserResumedSaysNothing()
    {
        engine->start();
        watcher->onPrepareForSleep(true);
        watcher->clearLockPauseNotice();
        QVERIFY(!watcher->isPausedBySleep());
        QSignalSpy wokeSpy(watcher.get(), &ScreenLockWatcher::wokeUpAfterPause);
        watcher->onPrepareForSleep(false);
        QCOMPARE(wokeSpy.count(), 0);
    }

    void onlyTheScreenSaverItselfCanReportALock()
    {
#ifndef Q_OS_LINUX
        QSKIP("The screen saver is watched over D-Bus on Linux only");
#endif
        PrivateDBus bus;
        if (!bus.start()) {
            QSKIP("dbus-daemon is not available");
        }
        QDBusConnection session = QDBusConnection::connectToBus(bus.address(), QStringLiteral("watcher-session"));
        QDBusConnection impostor = QDBusConnection::connectToBus(bus.address(), QStringLiteral("impostor"));
        QDBusConnection screenSaver = QDBusConnection::connectToBus(bus.address(), QStringLiteral("screen-saver"));
        QVERIFY(screenSaver.registerService(QStringLiteral("org.freedesktop.ScreenSaver")));

        ScreenLockWatcher lockWatcher(session);
        lockWatcher.attach(engine.get(), settings.get());
        engine->start();
        const auto lockSignal = [] {
            QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/org/freedesktop/ScreenSaver"),
                                                             QStringLiteral("org.freedesktop.ScreenSaver"),
                                                             QStringLiteral("ActiveChanged"));
            signal << true;
            return signal;
        };

        QVERIFY(impostor.send(lockSignal()));
        QTest::qWait(200);
        QCOMPARE(engine->state(), TimerEngine::State::Working);

        QVERIFY(screenSaver.send(lockSignal()));
        QTRY_COMPARE(engine->state(), TimerEngine::State::Paused);

        for (const auto &name : {QStringLiteral("watcher-session"), QStringLiteral("impostor"), QStringLiteral("screen-saver")}) {
            QDBusConnection::disconnectFromBus(name);
        }
    }

    void logindSignalOnTheSystemBusPausesTheTimer()
    {
#ifndef Q_OS_LINUX
        QSKIP("logind exists on Linux only");
#endif
        PrivateDBus bus;
        if (!bus.start()) {
            QSKIP("dbus-daemon is not available");
        }
        QDBusConnection systemBus = QDBusConnection::connectToBus(bus.address(), QStringLiteral("fake-system"));
        QDBusConnection logind = QDBusConnection::connectToBus(bus.address(), QStringLiteral("fake-logind"));
        QVERIFY(logind.registerService(QStringLiteral("org.freedesktop.login1")));

        ScreenLockWatcher sleepWatcher(QDBusConnection(QStringLiteral("tst_disconnected_bus")), systemBus);
        sleepWatcher.attach(engine.get(), settings.get());
        engine->start();

        QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/org/freedesktop/login1"),
                                                         QStringLiteral("org.freedesktop.login1.Manager"),
                                                         QStringLiteral("PrepareForSleep"));
        signal << true;
        QVERIFY(logind.send(signal));
        QTRY_COMPARE(engine->state(), TimerEngine::State::Paused);
        QVERIFY(sleepWatcher.isPausedBySleep());

        QDBusConnection::disconnectFromBus(QStringLiteral("fake-logind"));
        QDBusConnection::disconnectFromBus(QStringLiteral("fake-system"));
    }
};

QTEST_GUILESS_MAIN(ScreenLockWatcherTest)
#include "tst_screenlockwatcher.moc"
