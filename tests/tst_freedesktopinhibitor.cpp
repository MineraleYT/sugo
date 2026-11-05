// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusError>
#include <QDBusMessage>
#include <QLoggingCategory>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <memory>

#include "FreedesktopInhibitor.h"
#include "InhibitorFactory.h"
#include "PrivateDBus.h"

namespace
{
const QString kService = QStringLiteral("org.freedesktop.Notifications");
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");

/// Stand-in for plasmashell's notification server: same method names and signatures.
class FakeNotificationServer : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    QList<quint32> active;   ///< Inhibitions that are in effect on the "server".
    QStringList calls;       ///< "Inhibit" / "UnInhibit", in the order received.
    QStringList appIds;
    quint32 nextCookie = 1;
    int replyDelayMs = 0;    ///< Delay the Inhibit reply, to overlap calls.
    bool failInhibit = false;
    int unansweredUnInhibits = 0; ///< UnInhibit calls to leave unanswered (and not act on).

public Q_SLOTS:
    uint Inhibit(const QString &appId, const QString &reason, const QVariantMap &hints)
    {
        Q_UNUSED(reason)
        Q_UNUSED(hints)
        calls << QStringLiteral("Inhibit");
        appIds << appId;
        if (failInhibit) {
            sendErrorReply(QDBusError::Failed, QStringLiteral("refused"));
            return 0;
        }
        const quint32 cookie = nextCookie++;
        if (replyDelayMs > 0) {
            setDelayedReply(true);
            const QDBusMessage request = message();
            QDBusConnection bus = connection();
            QTimer::singleShot(replyDelayMs, this, [this, cookie, request, bus]() mutable {
                active.append(cookie);
                bus.send(request.createReply(cookie));
            });
            return 0;
        }
        active.append(cookie);
        return cookie;
    }

    void UnInhibit(uint cookie)
    {
        calls << QStringLiteral("UnInhibit");
        if (unansweredUnInhibits > 0) {
            --unansweredUnInhibits;
            setDelayedReply(true); // never sent: the caller times out
            return;
        }
        active.removeAll(cookie);
    }
};

/// A notification server without Inhibit, as on desktops whose server does not offer it.
class ServerWithoutInhibit : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public Q_SLOTS:
    QStringList GetCapabilities() { return {QStringLiteral("body")}; }
};

/// Publishes a notification server from a thread of its own: the factory asks it a blocking
/// question, which a server on the test's thread could not answer.
class ThreadedServer
{
public:
    ThreadedServer(const QString &address, std::unique_ptr<QObject> object)
        : m_object(std::move(object))
    {
        m_thread.start();
        m_object->moveToThread(&m_thread);
        QMetaObject::invokeMethod(
            m_object.get(),
            [this, address] {
                QDBusConnection bus = QDBusConnection::connectToBus(address, QStringLiteral("threaded-server"));
                m_registered = bus.registerObject(kPath, m_object.get(), QDBusConnection::ExportAllSlots)
                    && bus.registerService(kService);
            },
            Qt::BlockingQueuedConnection);
    }

    ~ThreadedServer()
    {
        QMetaObject::invokeMethod(
            m_object.get(),
            [this] {
                QDBusConnection bus(QStringLiteral("threaded-server"));
                bus.unregisterService(kService);
                bus.unregisterObject(kPath);
                QDBusConnection::disconnectFromBus(QStringLiteral("threaded-server"));
                m_object.reset();
            },
            Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait();
    }

    bool registered() const { return m_registered; }

private:
    QThread m_thread;
    std::unique_ptr<QObject> m_object;
    bool m_registered = false;
};
} // namespace

class FreedesktopInhibitorTest : public QObject
{
    Q_OBJECT

private:
    QProcess daemon;
    QString address;
    std::unique_ptr<FakeNotificationServer> server;
    std::unique_ptr<FreedesktopInhibitor> inhibitor;

    QDBusConnection serverBus() const { return QDBusConnection(QStringLiteral("server")); }
    QDBusConnection clientBus() const { return QDBusConnection(QStringLiteral("client")); }

    void publishServer()
    {
        QVERIFY(serverBus().registerObject(kPath, server.get(), QDBusConnection::ExportAllSlots));
        QVERIFY(serverBus().registerService(kService));
    }

    void unpublishServer()
    {
        serverBus().unregisterService(kService);
        serverBus().unregisterObject(kPath);
    }

private Q_SLOTS:
    void initTestCase()
    {
        // A private message bus: the tests never touch the user's real notification settings.
        daemon.start(QStringLiteral("dbus-daemon"),
                     {QStringLiteral("--session"), QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
        if (!daemon.waitForStarted(3000) || !daemon.waitForReadyRead(5000)) {
            QSKIP("dbus-daemon is not available");
        }
        address = QString::fromLocal8Bit(daemon.readLine().trimmed());

        QDBusConnection::connectToBus(address, QStringLiteral("server"));
        QDBusConnection::connectToBus(address, QStringLiteral("client"));
        QVERIFY(serverBus().isConnected());
        QVERIFY(clientBus().isConnected());

        // Errors are provoked on purpose below; keep the log readable.
        QLoggingCategory::setFilterRules(QStringLiteral("sugo.platform.inhibitor.warning=false"));
    }

    void cleanupTestCase()
    {
        inhibitor.reset();
        QDBusConnection::disconnectFromBus(QStringLiteral("client"));
        QDBusConnection::disconnectFromBus(QStringLiteral("server"));
        daemon.terminate();
        daemon.waitForFinished(3000);
    }

    void init()
    {
        server = std::make_unique<FakeNotificationServer>();
        publishServer();
        inhibitor = std::make_unique<FreedesktopInhibitor>(clientBus(), QStringLiteral("io.github.mineraleyt.sugo"));
    }

    void cleanup()
    {
        inhibitor.reset();
        unpublishServer();
        server.reset();
    }

    void inhibitIsIdempotentAndReleaseEndsTheServerSideInhibition()
    {
        inhibitor->inhibit(QStringLiteral("focus"));
        inhibitor->inhibit(QStringLiteral("focus")); // a second call must not stack another inhibition
        QVERIFY(inhibitor->isInhibited());           // true already while the call is in flight
        QTRY_COMPARE(server->active.size(), 1);
        QCOMPARE(server->calls, (QStringList{QStringLiteral("Inhibit")}));
        QCOMPARE(server->appIds.first(), QStringLiteral("io.github.mineraleyt.sugo"));

        inhibitor->release();
        inhibitor->release();
        QTRY_VERIFY(!inhibitor->isInhibited());
        QVERIFY(server->active.isEmpty()); // the cookie we were given is gone
        QCOMPARE(server->calls, (QStringList{QStringLiteral("Inhibit"), QStringLiteral("UnInhibit")}));
    }

    void releaseWithNothingHeldDoesNothing()
    {
        inhibitor->release();
        QTest::qWait(100);
        QVERIFY(server->calls.isEmpty());
        QVERIFY(!inhibitor->isInhibited());
    }

    void releaseRequestedWhileInhibitIsInFlightStillEndsReleased()
    {
        server->replyDelayMs = 200;
        inhibitor->inhibit(QStringLiteral("focus"));
        inhibitor->release(); // the phase ended before the server even answered

        // Until the release is confirmed, "inhibited" must stay true: callers wait on it.
        QVERIFY(inhibitor->isInhibited());
        QTRY_VERIFY_WITH_TIMEOUT(!inhibitor->isInhibited(), 3000);
        QVERIFY(server->active.isEmpty());
        QCOMPARE(server->calls, (QStringList{QStringLiteral("Inhibit"), QStringLiteral("UnInhibit")}));
    }

    void inhibitAgainWhileReleaseIsInFlightEndsInhibited()
    {
        inhibitor->inhibit(QStringLiteral("focus"));
        QTRY_COMPARE_WITH_TIMEOUT(server->active.size(), 1, 3000);

        inhibitor->release();
        inhibitor->inhibit(QStringLiteral("focus")); // changed our mind immediately
        QTRY_COMPARE_WITH_TIMEOUT(server->calls.size(), 3, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(server->active.size(), 1, 3000);
        QVERIFY(inhibitor->isInhibited());
        QCOMPARE(server->calls,
                 (QStringList{QStringLiteral("Inhibit"), QStringLiteral("UnInhibit"), QStringLiteral("Inhibit")}));

        inhibitor->release();
        QTRY_VERIFY(!inhibitor->isInhibited());
        QVERIFY(server->active.isEmpty());
    }

    void inhibitedChangedIsEmittedOncePerTransition()
    {
        QSignalSpy spy(inhibitor.get(), &NotificationInhibitor::inhibitedChanged);
        inhibitor->inhibit(QStringLiteral("focus"));
        QTRY_COMPARE(server->active.size(), 1);
        QCOMPARE(spy.count(), 1); // false -> true, and no flapping while the call completes

        inhibitor->release();
        QTRY_VERIFY(!inhibitor->isInhibited());
        QCOMPARE(spy.count(), 2); // true -> false
    }

    void aRefusedInhibitLeavesNotificationsOnAndDoesNotRetryInALoop()
    {
        server->failInhibit = true;
        inhibitor->inhibit(QStringLiteral("focus"));
        QTRY_VERIFY(!inhibitor->isInhibited());
        QTest::qWait(300);
        QCOMPARE(server->calls.size(), 1); // gave up, no retry storm

        server->failInhibit = false; // a later request works again
        inhibitor->inhibit(QStringLiteral("focus"));
        QTRY_COMPARE(server->active.size(), 1);
        QVERIFY(inhibitor->isInhibited());
    }

    void aRefusedInhibitIsTriedAgainWhenTheServiceRestarts()
    {
        server->failInhibit = true;
        inhibitor->inhibit(QStringLiteral("focus"));
        QTRY_COMPARE(server->calls.size(), 1);
        QTRY_VERIFY(!inhibitor->isInhibited());

        server->failInhibit = false;
        unpublishServer();
        publishServer();
        QTRY_COMPARE_WITH_TIMEOUT(server->active.size(), 1, 3000);
        QVERIFY(inhibitor->isInhibited());
    }

    void aSlowInhibitIsWaitedForAndItsCookieReleased()
    {
        // plasmashell is busy as the focus session starts and answers only after several seconds,
        // yet still grants the inhibition: its cookie must be kept, or nothing would ever end it.
        server->replyDelayMs = 4000;
        inhibitor->inhibit(QStringLiteral("focus"));
        QTest::qWait(3500);
        QVERIFY(inhibitor->isInhibited()); // still waiting for the answer, not given up
        inhibitor->release();              // the session ends before the server has answered

        QTRY_COMPARE_WITH_TIMEOUT(server->calls.size(), 2, 5000);
        QTRY_VERIFY(!inhibitor->isInhibited());
        QVERIFY(server->active.isEmpty()); // the late cookie was handed back
        QCOMPARE(server->calls, (QStringList{QStringLiteral("Inhibit"), QStringLiteral("UnInhibit")}));
    }

    void anUnansweredUnInhibitIsSentAgain()
    {
        inhibitor->inhibit(QStringLiteral("focus"));
        QTRY_COMPARE(server->active.size(), 1);
        server->unansweredUnInhibits = 1;
        inhibitor->release();
        // The first UnInhibit times out (3 s): the inhibition may still be held, so it is asked again.
        QTRY_VERIFY_WITH_TIMEOUT(server->active.isEmpty(), 8000);
        QCOMPARE(server->calls.count(QStringLiteral("UnInhibit")), 2);
        QTRY_VERIFY(!inhibitor->isInhibited());
    }

    void aHungServerIsNotAskedForeverToUnInhibit()
    {
        inhibitor->inhibit(QStringLiteral("focus"));
        QTRY_COMPARE(server->active.size(), 1);
        server->unansweredUnInhibits = 100;
        inhibitor->release();
        // Three unanswered calls (3 s each), then the cookie is given up.
        QTRY_VERIFY_WITH_TIMEOUT(!inhibitor->isInhibited(), 12000);
        QCOMPARE(server->calls.count(QStringLiteral("UnInhibit")), 3);
    }

    void aMissingNotificationServerIsHandledGracefully()
    {
        unpublishServer();
        inhibitor->inhibit(QStringLiteral("focus"));
        QTRY_VERIFY_WITH_TIMEOUT(!inhibitor->isInhibited(), 5000);
        QVERIFY(server->calls.isEmpty());

        inhibitor->release(); // harmless on a failed inhibitor
        QVERIFY(!inhibitor->isInhibited());
        publishServer(); // so cleanup() finds what it expects
    }

    void inhibitionIsTakenAgainAfterTheNotificationServiceRestarts()
    {
        inhibitor->inhibit(QStringLiteral("focus"));
        QTRY_COMPARE(server->active.size(), 1);

        // plasmashell restarts: its inhibitions vanish with it.
        unpublishServer();
        QTRY_VERIFY(!inhibitor->isInhibited());
        server->active.clear();
        publishServer();

        // Still wanted, so the new server gets a fresh inhibition without any new request.
        QTRY_COMPARE_WITH_TIMEOUT(server->active.size(), 1, 3000);
        QTRY_VERIFY(inhibitor->isInhibited());

        inhibitor->release();
        QTRY_VERIFY(!inhibitor->isInhibited());
        QVERIFY(server->active.isEmpty());
    }

    void aLateReplyFromTheOldServerIsIgnored()
    {
        server->replyDelayMs = 400;
        inhibitor->inhibit(QStringLiteral("focus"));
        QTRY_COMPARE(server->calls.size(), 1);

        // The service restarts while Inhibit (cookie 1) is still being answered.
        unpublishServer();
        publishServer();
        server->replyDelayMs = 0;

        // Still wanted: the new owner is asked again (cookie 2) and the stale cookie 1,
        // arriving later, neither replaces it nor cancels the silencing.
        QTRY_COMPARE_WITH_TIMEOUT(server->calls.size(), 2, 3000);
        QTest::qWait(600);
        QVERIFY(inhibitor->isInhibited());
        QVERIFY(server->active.contains(2));

        inhibitor->release();
        QTRY_VERIFY(!inhibitor->isInhibited());
        QVERIFY(!server->active.contains(2));
    }

    void aCookieFromTheNewServerInAStaleReplyIsReleased()
    {
        // Let the watcher's match rule settle first.
        inhibitor->inhibit(QStringLiteral("focus"));
        QTRY_COMPARE(server->active.size(), 1);
        inhibitor->release();
        QTRY_VERIFY(!inhibitor->isInhibited());
        server->calls.clear();

        // The service is taken again (here by the same connection, so the same unique name) and
        // Sugo has not yet seen NameOwnerChanged when a work phase starts: its Inhibit (cookie 2) is
        // answered after the owner change, and a second Inhibit follows.
        unpublishServer();
        publishServer();
        inhibitor->inhibit(QStringLiteral("focus")); // before the event loop delivers the change
        QTRY_COMPARE_WITH_TIMEOUT(server->calls.count(QStringLiteral("Inhibit")), 2, 3000);
        QTRY_VERIFY(inhibitor->isInhibited());

        // The cookie of the stale reply is handed back, so ending the phase leaves nothing held.
        inhibitor->release();
        QTRY_VERIFY(!inhibitor->isInhibited());
        QTRY_VERIFY_WITH_TIMEOUT(server->active.isEmpty(), 3000);
    }

    void aRaceWithANewServerProcessLeavesNoInhibitionBehind()
    {
        // As above, but the new server is another process (another connection), as when
        // plasmashell restarts. Let the inhibitor learn who serves notifications first.
        inhibitor->inhibit(QStringLiteral("focus"));
        QTRY_COMPARE(server->active.size(), 1);
        inhibitor->release();
        QTRY_VERIFY(!inhibitor->isInhibited());

        QDBusConnection newBus = QDBusConnection::connectToBus(address, QStringLiteral("new-server"));
        QVERIFY(newBus.isConnected());
        FakeNotificationServer newServer;
        unpublishServer();
        QVERIFY(newBus.registerObject(kPath, &newServer, QDBusConnection::ExportAllSlots));
        QVERIFY(newBus.registerService(kService));
        inhibitor->inhibit(QStringLiteral("focus")); // before the event loop delivers the change

        QTRY_COMPARE_WITH_TIMEOUT(newServer.active.size(), 1, 3000);
        QTRY_VERIFY(inhibitor->isInhibited());
        inhibitor->release();
        QTRY_VERIFY(!inhibitor->isInhibited());
        QTRY_VERIFY_WITH_TIMEOUT(newServer.active.isEmpty(), 3000);
        QVERIFY(server->active.isEmpty());

        newBus.unregisterService(kService);
        newBus.unregisterObject(kPath);
        QDBusConnection::disconnectFromBus(QStringLiteral("new-server"));
        QTRY_VERIFY(!inhibitor->isInhibited());
        publishServer(); // for cleanup()
    }

    void theCookieOfAServerStartedByTheInhibitCallIsKept()
    {
        // Sugo started before the notification server; the first Inhibit starts it (D-Bus
        // activation). The bus announces the new owner before the server's reply.
        PrivateDBus bus;
        if (!bus.start({kService})) {
            QSKIP("dbus-daemon is not available");
        }
        QDBusConnection client = QDBusConnection::connectToBus(bus.address(), QStringLiteral("activation-client"));
        QDBusConnection serverConnection = QDBusConnection::connectToBus(bus.address(), QStringLiteral("activation-server"));
        QVERIFY(client.isConnected());
        QVERIFY(serverConnection.isConnected());
        FakeNotificationServer started;
        {
            FreedesktopInhibitor lateInhibitor(client, QStringLiteral("io.github.mineraleyt.sugo"));
            lateInhibitor.inhibit(QStringLiteral("focus"));
            QTest::qWait(200); // the call waits on the bus for the server it asked to start
            QVERIFY(started.calls.isEmpty());

            QVERIFY(serverConnection.registerObject(kPath, &started, QDBusConnection::ExportAllSlots));
            QVERIFY(serverConnection.registerService(kService));
            QTRY_COMPARE_WITH_TIMEOUT(started.active.size(), 1, 3000);
            QTest::qWait(300);
            // Its reply was kept: no second Inhibit, so no cookie left behind.
            QCOMPARE(started.calls, (QStringList{QStringLiteral("Inhibit")}));
            QVERIFY(lateInhibitor.isInhibited());

            lateInhibitor.release();
            QTRY_VERIFY(!lateInhibitor.isInhibited());
            QVERIFY(started.active.isEmpty());
        }
        serverConnection.unregisterService(kService);
        serverConnection.unregisterObject(kPath);
        QDBusConnection::disconnectFromBus(QStringLiteral("activation-client"));
        QDBusConnection::disconnectFromBus(QStringLiteral("activation-server"));
    }

    void theFactoryChoosesByWhatTheServerOffers()
    {
#ifndef Q_OS_LINUX
        QSKIP("The factory looks for a notification server on Linux only");
#else
        PrivateDBus bus;
        if (!bus.start()) {
            QSKIP("dbus-daemon is not available");
        }
        QDBusConnection client = QDBusConnection::connectToBus(bus.address(), QStringLiteral("factory-client"));
        QVERIFY(client.isConnected());

        // No server yet: it may still be starting, so silencing is tried.
        QVERIFY(createNotificationInhibitor(client)->isAvailable());
        {
            const ThreadedServer plasma(bus.address(), std::make_unique<FakeNotificationServer>());
            QVERIFY(plasma.registered());
            QVERIFY(createNotificationInhibitor(client)->isAvailable());
        }
        {
            const ThreadedServer other(bus.address(), std::make_unique<ServerWithoutInhibit>());
            QVERIFY(other.registered());
            QVERIFY(!createNotificationInhibitor(client)->isAvailable());
        }
        QDBusConnection::disconnectFromBus(QStringLiteral("factory-client"));
#endif
    }

    void aServiceRestartAfterReleaseDoesNotInhibitAgain()
    {
        inhibitor->inhibit(QStringLiteral("focus"));
        QTRY_COMPARE(server->active.size(), 1);
        inhibitor->release();
        QTRY_VERIFY(!inhibitor->isInhibited());
        server->calls.clear();

        unpublishServer();
        publishServer();
        QTest::qWait(300);
        QVERIFY(server->calls.isEmpty());
        QVERIFY(!inhibitor->isInhibited());
    }

    void whenReleasedWaitsForTheServerToConfirm()
    {
        inhibitor->inhibit(QStringLiteral("focus"));
        QTRY_COMPARE(server->active.size(), 1);

        inhibitor->release();
        bool ran = false;
        bool serverHadReleased = false;
        inhibitor->whenReleased(this, [&]() {
            ran = true;
            serverHadReleased = server->active.isEmpty(); // a notification posted now would be shown
        });
        QVERIFY(!ran); // the UnInhibit call has not been answered yet
        QTRY_VERIFY(ran);
        QVERIFY(serverHadReleased);
    }

    void whenReleasedDoesNotWaitWhileSilencingIsWanted()
    {
        // A break ends and a silenced work phase starts: the inhibition is being taken, so there
        // is no release to wait for, and waiting would only delay the end-of-break notification.
        inhibitor->inhibit(QStringLiteral("focus"));
        QVERIFY(inhibitor->isInhibited());
        QVERIFY(!inhibitor->isReleasing());
        bool ran = false;
        inhibitor->whenReleased(this, [&]() { ran = true; });
        QVERIFY(ran);
        QTRY_COMPARE(server->active.size(), 1); // the inhibition itself still goes through
    }
};

QTEST_GUILESS_MAIN(FreedesktopInhibitorTest)
#include "tst_freedesktopinhibitor.moc"
