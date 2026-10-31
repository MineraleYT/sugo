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
    uint lastCookie = 0;
    int failures = 0; ///< Inhibit calls to refuse.

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
        lastCookie = 42;
        return lastCookie;
    }
    void UnInhibit(uint cookie)
    {
        calls << QStringLiteral("UnInhibit:%1").arg(cookie);
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

private Q_SLOTS:
    void initTestCase()
    {
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
    }

    void cleanup()
    {
        // The release on destruction is asynchronous: let it land before the next test starts.
        const bool wasInhibited = inhibitor && inhibitor->isInhibited();
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
        QTRY_VERIFY(inhibitor->isInhibited());
        QCOMPARE(server.calls, (QStringList{QStringLiteral("Inhibit:Sugo")}));

        engine->pause();
        QTRY_VERIFY(!inhibitor->isInhibited());
        QTRY_COMPARE_WITH_TIMEOUT(server.calls.size(), 2, 3000);
        QCOMPARE(server.calls.last(), QStringLiteral("UnInhibit:42"));

        engine->resume();
        QTRY_VERIFY(inhibitor->isInhibited());
        settings->setKeepScreenAwake(false);
        QTRY_VERIFY(!inhibitor->isInhibited());
        QTRY_COMPARE_WITH_TIMEOUT(server.calls.size(), 4, 3000);
        QCOMPARE(server.calls.last(), QStringLiteral("UnInhibit:42"));
    }

    void aCookieThatArrivesAfterPauseIsHandedBack()
    {
        engine->start();
        engine->pause(); // before Inhibit() has answered
        QTRY_COMPARE_WITH_TIMEOUT(server.calls.size(), 2, 3000);
        QCOMPARE(server.calls, (QStringList{QStringLiteral("Inhibit:Sugo"), QStringLiteral("UnInhibit:42")}));
        QVERIFY(!inhibitor->isInhibited());
    }

    void aCookieStillOnItsWayAtQuitIsHandedBack()
    {
        engine->start();
        inhibitor->shutdown(); // quitting before Inhibit() has answered
        inhibitor.reset();
        QTRY_COMPARE_WITH_TIMEOUT(server.calls.size(), 2, 3000);
        QCOMPARE(server.calls, (QStringList{QStringLiteral("Inhibit:Sugo"), QStringLiteral("UnInhibit:42")}));
    }

    void quickPauseAndResumeSendsOnlyOneInhibit()
    {
        engine->start();
        engine->pause();
        engine->resume();
        settings->setKeepScreenAwake(false);
        settings->setKeepScreenAwake(true);
        QTRY_VERIFY(inhibitor->isInhibited());
        QTest::qWait(100);
        QCOMPARE(server.calls, (QStringList{QStringLiteral("Inhibit:Sugo")}));

        engine->stop();
        QTRY_COMPARE_WITH_TIMEOUT(server.calls.size(), 2, 3000);
        QCOMPARE(server.calls.last(), QStringLiteral("UnInhibit:42"));
    }

    void aRestartedScreenSaverIsAskedAgain()
    {
        engine->start();
        QTRY_VERIFY(inhibitor->isInhibited());

        QVERIFY(serverBus().unregisterService(kService));
        QTRY_VERIFY(!inhibitor->isInhibited());
        QVERIFY(serverBus().registerService(kService));
        QTRY_VERIFY(inhibitor->isInhibited());
        QCOMPARE(server.calls, (QStringList{QStringLiteral("Inhibit:Sugo"), QStringLiteral("Inhibit:Sugo")}));
    }

    void aFailedInhibitIsRetriedWhileStillWanted()
    {
        server.failures = 1;
        engine->start();
        QTRY_VERIFY_WITH_TIMEOUT(inhibitor->isInhibited(), 5000);
        QCOMPARE(server.calls, (QStringList{QStringLiteral("Inhibit:Sugo"), QStringLiteral("Inhibit:Sugo")}));
    }

    void retriesAreBounded()
    {
        server.failures = 100;
        engine->start();
        QTest::qWait(4500);
        QCOMPARE(server.calls.size(), 4); // the request and three retries
        QVERIFY(!inhibitor->isInhibited());
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
        QVERIFY(!inhibitor->isInhibited());
    }
};

QTEST_GUILESS_MAIN(PowerInhibitorTest)
#include "tst_powerinhibitor.moc"
