// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusError>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include "Localization.h"

#include <memory>

#include "AppSettings.h"
#include "Autostart.h"
#include "FakeClock.h"
#include "PowerInhibitor.h"
#include "TimerEngine.h"

namespace
{
const QString kService = QStringLiteral("org.freedesktop.ScreenSaver");
const QString kPath = QStringLiteral("/org/freedesktop/ScreenSaver");

class FakeScreenSaver : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.ScreenSaver")

public:
    QStringList calls;
    uint nextCookie = 0; ///< kscreenlocker numbers its cookies from 0, and 0 is a valid one.
    int failures = 0;    ///< Inhibit calls to refuse.
    QList<uint> held;    ///< Cookies handed out and not given back yet.

public Q_SLOTS:
    uint Inhibit(const QString &application, const QString &reason)
    {
        Q_UNUSED(reason)
        calls << QStringLiteral("Inhibit:%1").arg(application);
        if (failures > 0) {
            --failures;
            sendErrorReply(QDBusError::Failed, QStringLiteral("busy"));
            return 0;
        }
        const uint cookie = nextCookie++;
        held.append(cookie);
        return cookie;
    }
    void UnInhibit(uint cookie)
    {
        calls << QStringLiteral("UnInhibit:%1").arg(cookie);
        held.removeAll(cookie);
    }
};

class FakeAutostart final : public Autostart
{
public:
    bool isSupported() const override { return false; }
    bool isEnabled() const override { return false; }
    bool setEnabled(bool) override { return false; }
    void repair() override {}
};
}

class PowerInhibitorTest : public QObject
{
    Q_OBJECT

private:
    QProcess daemon;
    QString address;
    bool busReady = false;
    FakeScreenSaver server;
    QTemporaryDir configDir;
    std::unique_ptr<AppSettings> settings;
    FakeClock clock;
    std::unique_ptr<TimerEngine> engine;
    std::unique_ptr<PowerInhibitor> inhibitor;

    QDBusConnection serverBus() const { return QDBusConnection(QStringLiteral("power-test-server")); }
    QDBusConnection clientBus() const { return QDBusConnection(QStringLiteral("power-test-client")); }
    /// The screen saver holds an inhibition of ours.
    bool screenInhibited() const { return !server.held.isEmpty(); }

private Q_SLOTS:
    void initTestCase()
    {
#ifdef Q_OS_MACOS
        QSKIP("macOS keeps the screen awake with IOKit power assertions (tst_macpowerassertion)");
#endif
        daemon.start(QStringLiteral("dbus-daemon"),
                     {QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
        if (!daemon.waitForStarted(3000) || !daemon.waitForReadyRead(5000)) QSKIP("dbus-daemon is not available");
        address = QString::fromLocal8Bit(daemon.readLine().trimmed());
        if (address.isEmpty()) QSKIP("Could not obtain a private D-Bus address");
        QDBusConnection::connectToBus(address, QStringLiteral("power-test-server"));
        QDBusConnection::connectToBus(address, QStringLiteral("power-test-client"));
        QVERIFY(serverBus().isConnected());
        QVERIFY(clientBus().isConnected());
        QVERIFY(serverBus().registerObject(kPath, &server, QDBusConnection::ExportAllSlots));
        QVERIFY(serverBus().registerService(kService));
        busReady = true;
    }

    void cleanupTestCase()
    {
        inhibitor.reset();
        if (busReady) {
            serverBus().unregisterService(kService);
            serverBus().unregisterObject(kPath);
            QDBusConnection::disconnectFromBus(QStringLiteral("power-test-client"));
            QDBusConnection::disconnectFromBus(QStringLiteral("power-test-server"));
        }
        daemon.terminate();
        daemon.waitForFinished(3000);
    }

    void init()
    {
        settings = std::make_unique<AppSettings>(Settings::open(configDir.filePath(QStringLiteral("settings.conf"))),
                                                 std::make_unique<FakeAutostart>(), false);
        settings->setKeepScreenAwake(true);
        clock = FakeClock();
        engine = std::make_unique<TimerEngine>(&clock, nullptr);
        inhibitor = std::make_unique<PowerInhibitor>(clientBus());
        inhibitor->attach(engine.get(), settings.get());
        server.calls.clear();
        server.held.clear();
        server.nextCookie = 0;
    }

    void cleanup()
    {
        // The release on destruction is asynchronous: let it land before the next test starts.
        const bool wasInhibited = inhibitor && screenInhibited();
        const int before = server.calls.size();
        inhibitor.reset();
        if (wasInhibited) {
            QTRY_COMPARE_WITH_TIMEOUT(server.calls.size(), before + 1, 3000);
        }
        engine.reset();
        settings.reset();
    }

    void inhibitsOnlyWhileWorkIsRunningAndReleasesCookie()
    {
        engine->start();
        QTRY_VERIFY(screenInhibited());
        QCOMPARE(server.calls, (QStringList{QStringLiteral("Inhibit:Sugo")}));
        QCOMPARE(server.held, (QList<uint>{0}));

        // Cookie 0 counts as held: another update while working does not ask again.
        inhibitor->update();
        QTest::qWait(100);
        QCOMPARE(server.calls.size(), 1);

        engine->pause();
        QTRY_VERIFY(!screenInhibited());
        QTRY_COMPARE_WITH_TIMEOUT(server.calls.size(), 2, 3000);
        QCOMPARE(server.calls.last(), QStringLiteral("UnInhibit:0"));

        engine->resume();
        QTRY_VERIFY(screenInhibited());
        settings->setKeepScreenAwake(false);
        QTRY_VERIFY(!screenInhibited());
        QTRY_COMPARE_WITH_TIMEOUT(server.calls.size(), 4, 3000);
        QCOMPARE(server.calls.last(), QStringLiteral("UnInhibit:1"));
    }

    void aCookieThatArrivesAfterPauseIsHandedBack()
    {
        engine->start();
        engine->pause(); // before Inhibit() has answered
        QTRY_COMPARE_WITH_TIMEOUT(server.calls.size(), 2, 3000);
        QCOMPARE(server.calls, (QStringList{QStringLiteral("Inhibit:Sugo"), QStringLiteral("UnInhibit:0")}));
        QVERIFY(!screenInhibited());
    }

    void aCookieStillOnItsWayAtQuitIsHandedBack()
    {
        engine->start();
        inhibitor->shutdown(); // quitting before Inhibit() has answered
        inhibitor.reset();
        QTRY_COMPARE_WITH_TIMEOUT(server.calls.size(), 2, 3000);
        QCOMPARE(server.calls, (QStringList{QStringLiteral("Inhibit:Sugo"), QStringLiteral("UnInhibit:0")}));
    }

    void quickPauseAndResumeSendsOnlyOneInhibit()
    {
        engine->start();
        engine->pause();
        engine->resume();
        settings->setKeepScreenAwake(false);
        settings->setKeepScreenAwake(true);
        QTRY_VERIFY(screenInhibited());
        QTest::qWait(100);
        QCOMPARE(server.calls, (QStringList{QStringLiteral("Inhibit:Sugo")}));

        engine->stop();
        QTRY_COMPARE_WITH_TIMEOUT(server.calls.size(), 2, 3000);
        QCOMPARE(server.calls.last(), QStringLiteral("UnInhibit:0"));
    }

    void aRestartedScreenSaverIsAskedAgain()
    {
        engine->start();
        QTRY_VERIFY(screenInhibited());

        QVERIFY(serverBus().unregisterService(kService));
        server.held.clear(); // a new screen saver knows no cookies of the old one
        QTest::qWait(200); // the owner change reaches the client
        QVERIFY(serverBus().registerService(kService));
        QTRY_VERIFY(screenInhibited());
        QCOMPARE(server.calls, (QStringList{QStringLiteral("Inhibit:Sugo"), QStringLiteral("Inhibit:Sugo")}));

        // The old cookie was dropped, not kept: stopping hands back the new one, once.
        engine->stop();
        QTRY_COMPARE_WITH_TIMEOUT(server.calls.size(), 3, 3000);
        QCOMPARE(server.calls.last(), QStringLiteral("UnInhibit:1"));
        QTest::qWait(100);
        QCOMPARE(server.calls.size(), 3);
        QVERIFY(!screenInhibited());
    }

    void aFailedInhibitIsRetriedWhileStillWanted()
    {
        server.failures = 1;
        engine->start();
        QTRY_VERIFY_WITH_TIMEOUT(screenInhibited(), 5000);
        QCOMPARE(server.calls, (QStringList{QStringLiteral("Inhibit:Sugo"), QStringLiteral("Inhibit:Sugo")}));
    }

    void retriesAreBounded()
    {
        server.failures = 100;
        engine->start();
        QTest::qWait(4500);
        QCOMPARE(server.calls.size(), 4); // the request and three retries
        QVERIFY(!screenInhibited());
        server.failures = 0;
    }

    void aNewPhaseDoesNotInheritTheOldRetry()
    {
        server.failures = 100;
        engine->start();
        QTest::qWait(300); // the first Inhibit failed and a retry is due
        engine->stop();
        engine->start();
        QTest::qWait(4500);
        QCOMPARE(server.calls.size(), 5); // one refused call, then the new phase's request and three retries
        server.failures = 0;
    }

    void disabledSettingDoesNotRequestInhibition()
    {
        settings->setKeepScreenAwake(false);
        engine->start();
        QTest::qWait(100);
        QVERIFY(server.calls.isEmpty());
        QVERIFY(!screenInhibited());
    }
};

QTEST_GUILESS_MAIN(PowerInhibitorTest)
#include "tst_powerinhibitor.moc"
