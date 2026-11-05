// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtGlobal>
#ifndef Q_OS_MACOS
#include <QDBusConnection>
#include <QDBusMessage>
#endif
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
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
    QTemporaryDir dataDir;
    std::unique_ptr<FakeClock> clock;
    std::unique_ptr<TimerEngine> engine;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<ScreenLockWatcher> watcher;

private Q_SLOTS:
    void initTestCase()
    {
        // Keep the settings file away from the user's real configuration: SUGO_DATA_DIR, which
        // Settings::defaultPath() reads first, in a directory of its own (test mode alone would
        // leave a file in ~/.qttest, and an exported SUGO_DATA_DIR would be written to).
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(dataDir.isValid());
        qputenv("SUGO_DATA_DIR", QFile::encodeName(dataDir.path()));
    }

    void init()
    {
        clock = std::make_unique<FakeClock>();
        engine = std::make_unique<TimerEngine>(clock.get(), nullptr);
        settings = std::make_unique<AppSettings>(Settings::openDefault(), nullptr, false);
        settings->setAutoPauseOnScreenLock(true);

        // Listening to nothing (on Linux: disconnected buses), so tests don't touch the system's events
        watcher = std::make_unique<ScreenLockWatcher>(ScreenLockWatcher::Source::Nothing);
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
#else
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
#endif
    }

    void gnomeScreenSaverLockPausesTheTimer()
    {
#ifndef Q_OS_LINUX
        QSKIP("The screen saver is watched over D-Bus on Linux only");
#else
        PrivateDBus bus;
        if (!bus.start()) {
            QSKIP("dbus-daemon is not available");
        }
        QDBusConnection session = QDBusConnection::connectToBus(bus.address(), QStringLiteral("gnome-session"));
        QDBusConnection gnomeShell = QDBusConnection::connectToBus(bus.address(), QStringLiteral("gnome-shell"));
        // GNOME announces locks on its own name only, never on org.freedesktop.ScreenSaver.
        QVERIFY(gnomeShell.registerService(QStringLiteral("org.gnome.ScreenSaver")));

        ScreenLockWatcher lockWatcher(session);
        lockWatcher.attach(engine.get(), settings.get());
        QSignalSpy unlockSpy(&lockWatcher, &ScreenLockWatcher::screenUnlockedAfterPause);
        engine->start();
        const auto activeChanged = [](bool active) {
            QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/org/gnome/ScreenSaver"),
                                                             QStringLiteral("org.gnome.ScreenSaver"),
                                                             QStringLiteral("ActiveChanged"));
            signal << active;
            return signal;
        };

        QVERIFY(gnomeShell.send(activeChanged(true)));
        QTRY_COMPARE(engine->state(), TimerEngine::State::Paused);
        QVERIFY(lockWatcher.isPausedByScreenLock());

        QVERIFY(gnomeShell.send(activeChanged(false)));
        QTRY_COMPARE(unlockSpy.count(), 1);

        QDBusConnection::disconnectFromBus(QStringLiteral("gnome-shell"));
        QDBusConnection::disconnectFromBus(QStringLiteral("gnome-session"));
#endif
    }

    void aLockerThatRestartedWhileLockedCanLockAgain()
    {
#ifndef Q_OS_LINUX
        QSKIP("The screen saver is watched over D-Bus on Linux only");
#else
        PrivateDBus bus;
        if (!bus.start()) {
            QSKIP("dbus-daemon is not available");
        }
        QDBusConnection session = QDBusConnection::connectToBus(bus.address(), QStringLiteral("restart-session"));
        const auto locked = [] {
            QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/org/freedesktop/ScreenSaver"),
                                                             QStringLiteral("org.freedesktop.ScreenSaver"),
                                                             QStringLiteral("ActiveChanged"));
            signal << true;
            return signal;
        };

        ScreenLockWatcher lockWatcher(session);
        lockWatcher.attach(engine.get(), settings.get());
        engine->start();
        {
            QDBusConnection locker = QDBusConnection::connectToBus(bus.address(), QStringLiteral("locker"));
            QVERIFY(locker.registerService(QStringLiteral("org.freedesktop.ScreenSaver")));
            QVERIFY(locker.send(locked()));
            QTRY_COMPARE(engine->state(), TimerEngine::State::Paused);
        }
        engine->resume();
        QCOMPARE(engine->state(), TimerEngine::State::Working);

        // The locker crashes while the screen is locked: its connection is gone (no copy of it
        // is left here) and ActiveChanged(false) never comes. It starts again, and the next lock
        // must pause the work phase as the first one did.
        QDBusConnection::disconnectFromBus(QStringLiteral("locker"));
        QDBusConnection restarted = QDBusConnection::connectToBus(bus.address(), QStringLiteral("locker-restarted"));
        QVERIFY(restarted.registerService(QStringLiteral("org.freedesktop.ScreenSaver")));
        QVERIFY(restarted.send(locked()));
        QTRY_COMPARE(engine->state(), TimerEngine::State::Paused);
        QVERIFY(lockWatcher.isPausedByScreenLock());

        QDBusConnection::disconnectFromBus(QStringLiteral("locker-restarted"));
        QDBusConnection::disconnectFromBus(QStringLiteral("restart-session"));
#endif
    }

    void aLockerGoneWhileLockedCountsAsAnUnlock()
    {
#ifndef Q_OS_LINUX
        QSKIP("The screen saver is watched over D-Bus on Linux only");
#else
        PrivateDBus bus;
        if (!bus.start()) {
            QSKIP("dbus-daemon is not available");
        }
        QDBusConnection session = QDBusConnection::connectToBus(bus.address(), QStringLiteral("gone-session"));
        const auto activeChanged = [](bool active) {
            QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/org/freedesktop/ScreenSaver"),
                                                             QStringLiteral("org.freedesktop.ScreenSaver"),
                                                             QStringLiteral("ActiveChanged"));
            signal << active;
            return signal;
        };

        ScreenLockWatcher lockWatcher(session);
        lockWatcher.attach(engine.get(), settings.get());
        QSignalSpy pauseSpy(&lockWatcher, &ScreenLockWatcher::pausedByScreenLockChanged);
        QSignalSpy unlockSpy(&lockWatcher, &ScreenLockWatcher::screenUnlockedAfterPause);
        engine->start();
        {
            QDBusConnection locker = QDBusConnection::connectToBus(bus.address(), QStringLiteral("gone-locker"));
            QVERIFY(locker.registerService(QStringLiteral("org.freedesktop.ScreenSaver")));
            QVERIFY(locker.send(activeChanged(true)));
            QTRY_COMPARE(engine->state(), TimerEngine::State::Paused);
        }
        QCOMPARE(pauseSpy.count(), 1);

        // The locker crashes: the user is asked whether to resume, as after an unlock; the timer
        // stays paused until they answer.
        QDBusConnection::disconnectFromBus(QStringLiteral("gone-locker"));
        QTRY_COMPARE(unlockSpy.count(), 1);
        QCOMPARE(engine->state(), TimerEngine::State::Paused);
        QVERIFY(lockWatcher.isPausedByScreenLock());
        QCOMPARE(pauseSpy.count(), 1);

        // The locker that replaces it reports an unlock of its own: already told.
        QDBusConnection restarted = QDBusConnection::connectToBus(bus.address(), QStringLiteral("gone-restarted"));
        QVERIFY(restarted.registerService(QStringLiteral("org.freedesktop.ScreenSaver")));
        QVERIFY(restarted.send(activeChanged(false)));
        QTest::qWait(200);
        QCOMPARE(unlockSpy.count(), 1);

        QDBusConnection::disconnectFromBus(QStringLiteral("gone-restarted"));
        QDBusConnection::disconnectFromBus(QStringLiteral("gone-session"));
#endif
    }

    void anotherScreenSaverNameLeavingKeepsTheLock()
    {
#ifndef Q_OS_LINUX
        QSKIP("The screen saver is watched over D-Bus on Linux only");
#else
        PrivateDBus bus;
        if (!bus.start()) {
            QSKIP("dbus-daemon is not available");
        }
        QDBusConnection session = QDBusConnection::connectToBus(bus.address(), QStringLiteral("proxy-session"));
        QDBusConnection gnomeShell = QDBusConnection::connectToBus(bus.address(), QStringLiteral("proxy-gnome-shell"));
        QVERIFY(gnomeShell.registerService(QStringLiteral("org.gnome.ScreenSaver")));
        const auto activeChanged = [](bool active) {
            QDBusMessage signal = QDBusMessage::createSignal(QStringLiteral("/org/gnome/ScreenSaver"),
                                                             QStringLiteral("org.gnome.ScreenSaver"),
                                                             QStringLiteral("ActiveChanged"));
            signal << active;
            return signal;
        };

        ScreenLockWatcher lockWatcher(session);
        lockWatcher.attach(engine.get(), settings.get());
        QSignalSpy unlockSpy(&lockWatcher, &ScreenLockWatcher::screenUnlockedAfterPause);
        engine->start();
        {
            // On GNOME, gsd-screensaver-proxy owns org.freedesktop.ScreenSaver for Inhibit only.
            QDBusConnection proxy = QDBusConnection::connectToBus(bus.address(), QStringLiteral("proxy"));
            QVERIFY(proxy.registerService(QStringLiteral("org.freedesktop.ScreenSaver")));
            QVERIFY(gnomeShell.send(activeChanged(true)));
            QTRY_COMPARE(engine->state(), TimerEngine::State::Paused);
        }

        // The proxy restarting says nothing about gnome-shell's lock.
        QDBusConnection::disconnectFromBus(QStringLiteral("proxy"));
        QTest::qWait(200);
        QCOMPARE(unlockSpy.count(), 0);

        QVERIFY(gnomeShell.send(activeChanged(false)));
        QTRY_COMPARE(unlockSpy.count(), 1);

        QDBusConnection::disconnectFromBus(QStringLiteral("proxy-gnome-shell"));
        QDBusConnection::disconnectFromBus(QStringLiteral("proxy-session"));
#endif
    }

    void availableFollowsTheBackend()
    {
        const ScreenLockWatcher offline(ScreenLockWatcher::Source::Nothing);
        QVERIFY(!offline.available());

#ifdef Q_OS_MACOS
        // The default constructor listens through MacSessionEvents, with or without a bus.
        const ScreenLockWatcher mac;
        QVERIFY(mac.available());
#else
        PrivateDBus bus;
        if (!bus.start()) {
            QSKIP("dbus-daemon is not available");
        }
        QDBusConnection session = QDBusConnection::connectToBus(bus.address(), QStringLiteral("available-session"));
        {
            const ScreenLockWatcher online(session);
            QVERIFY(online.available());
        }
        QDBusConnection::disconnectFromBus(QStringLiteral("available-session"));
#endif
    }

    void logindSignalOnTheSystemBusPausesTheTimer()
    {
#ifndef Q_OS_LINUX
        QSKIP("logind exists on Linux only");
#else
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
#endif
    }
};

QTEST_GUILESS_MAIN(ScreenLockWatcherTest)
#include "tst_screenlockwatcher.moc"
