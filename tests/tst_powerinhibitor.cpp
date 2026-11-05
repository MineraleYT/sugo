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
#include "PrivateDBus.h"
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

/// Counts the refusals of Inhibit the client has handled: it warns about each one in its reply
/// handler, which also starts the retry. (The fake screen saver counts its calls before the
/// client has the reply.)
class RefusalCounter
{
public:
    RefusalCounter()
    {
        s_count = 0;
        s_previous = qInstallMessageHandler(&RefusalCounter::handle);
    }
    ~RefusalCounter() { qInstallMessageHandler(s_previous); }
    RefusalCounter(const RefusalCounter &) = delete;
    RefusalCounter &operator=(const RefusalCounter &) = delete;

    int count() const { return s_count; }

private:
    static void handle(QtMsgType type, const QMessageLogContext &context, const QString &message)
    {
        if (type == QtWarningMsg && qstrcmp(context.category, "sugo.platform.power") == 0
            && message.startsWith(QLatin1String("Failed to inhibit"))) {
            ++s_count;
        }
        if (s_previous) {
            s_previous(type, context, message);
        }
    }
    static inline int s_count = 0;
    static inline QtMessageHandler s_previous = nullptr;
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
        // The release is asynchronous: let it land before the next test starts. The screen saver
        // may have handed out its cookie with the reply still on its way: quit as the program does
        // (shutdown() waits for that reply and hands the cookie back), since a plain destruction
        // would drop the reply and with it the UnInhibit.
        const bool wasInhibited = inhibitor && screenInhibited();
        const int before = server.calls.size();
        if (inhibitor) {
            inhibitor->shutdown();
        }
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

    void theCookieOfAScreenSaverStartedByTheInhibitCallIsKept()
    {
        // The first Inhibit starts the screen saver (D-Bus activation). The bus announces the new
        // owner before its reply.
        PrivateDBus bus;
        if (!bus.start({kService})) {
            QSKIP("dbus-daemon is not available");
        }
        QDBusConnection client = QDBusConnection::connectToBus(bus.address(), QStringLiteral("activation-client"));
        QDBusConnection serverConnection = QDBusConnection::connectToBus(bus.address(), QStringLiteral("activation-server"));
        QVERIFY(client.isConnected());
        QVERIFY(serverConnection.isConnected());
        FakeScreenSaver started;
        {
            PowerInhibitor lateInhibitor(client);
            lateInhibitor.inhibit();
            QTest::qWait(200); // the call waits on the bus for the screen saver it asked to start
            QVERIFY(started.calls.isEmpty());

            QVERIFY(serverConnection.registerObject(kPath, &started, QDBusConnection::ExportAllSlots));
            QVERIFY(serverConnection.registerService(kService));
            QTRY_COMPARE_WITH_TIMEOUT(started.held.size(), 1, 3000);
            QTest::qWait(300);
            // Its reply was kept: no second Inhibit, so no cookie left behind.
            QCOMPARE(started.calls, (QStringList{QStringLiteral("Inhibit:Sugo")}));

            lateInhibitor.release();
            QTRY_VERIFY(started.held.isEmpty());
            QCOMPARE(started.calls.last(), QStringLiteral("UnInhibit:0"));
        }
        serverConnection.unregisterService(kService);
        serverConnection.unregisterObject(kPath);
        QDBusConnection::disconnectFromBus(QStringLiteral("activation-client"));
        QDBusConnection::disconnectFromBus(QStringLiteral("activation-server"));
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
        QTRY_COMPARE_WITH_TIMEOUT(server.calls.size(), 4, 6000); // the request and three retries
        QTest::qWait(1200);                                       // longer than a retry's delay: no fourth one
        QCOMPARE(server.calls.size(), 4);
        QVERIFY(!screenInhibited());
        server.failures = 0;
    }

    void aNewPhaseDoesNotInheritTheOldRetry()
    {
        server.failures = 100;
        RefusalCounter refusals;
        engine->start();
        // The first Inhibit failed and a retry is due. (Waiting for the client's reply, not for
        // the server: with the reply still in flight, the new phase would take over the request.)
        QTRY_COMPARE_WITH_TIMEOUT(refusals.count(), 1, 3000);
        QCOMPARE(server.calls.size(), 1);
        engine->stop();
        engine->start();
        // One refused call, then the new phase's request and three retries.
        QTRY_COMPARE_WITH_TIMEOUT(server.calls.size(), 5, 6000);
        QTest::qWait(1200); // longer than a retry's delay: no retry left over from the old phase
        QCOMPARE(server.calls.size(), 5);
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
