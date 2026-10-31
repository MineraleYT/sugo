// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QSignalSpy>
#include <QTest>

#include "DBusTimerService.h"
#include "FakeClock.h"
#include "PrivateDBus.h"
#include "TimerEngine.h"

/// The /Timer interface as scripts and global shortcuts reach it, over a real (private) bus.
class DBusTimerServiceTest : public QObject
{
    Q_OBJECT

private:
    PrivateDBus bus;
    const QString service = QStringLiteral("org.example.sugo.timertest");

    /// Calls a method of /Timer from another connection. Asynchronous: the object answering
    /// lives in this same thread.
    QDBusMessage call(const QString &method)
    {
        QDBusConnection caller(QStringLiteral("caller"));
        const QDBusPendingCall pending = caller.asyncCall(QDBusMessage::createMethodCall(
            service, QStringLiteral("/Timer"), QStringLiteral("io.github.mineraleyt.sugo.Timer"), method));
        QDBusPendingCallWatcher watcher(pending);
        QSignalSpy done(&watcher, &QDBusPendingCallWatcher::finished);
        done.wait(5000);
        return pending.reply();
    }

private Q_SLOTS:
    void initTestCase()
    {
        if (!bus.start()) {
            QSKIP("dbus-daemon is not available");
        }
        QDBusConnection::connectToBus(bus.address(), QStringLiteral("app"));
        QDBusConnection::connectToBus(bus.address(), QStringLiteral("caller"));
        QVERIFY(QDBusConnection(QStringLiteral("app")).registerService(service));
    }

    void cleanupTestCase()
    {
        QDBusConnection::disconnectFromBus(QStringLiteral("app"));
        QDBusConnection::disconnectFromBus(QStringLiteral("caller"));
    }

    void commandsFollowTheSetting()
    {
        FakeClock clock;
        TimerEngine engine(&clock, nullptr);
        auto *host = new TimerDBusObject(&engine);
        auto *adaptor = new DBusTimerService(&engine, host);
        bool allowed = true;
        adaptor->setControlAllowed([&allowed]() { return allowed; });
        QDBusConnection app(QStringLiteral("app"));
        QVERIFY(app.registerObject(QStringLiteral("/Timer"), host, QDBusConnection::ExportAdaptors));

        QCOMPARE(call(QStringLiteral("start")).type(), QDBusMessage::ReplyMessage);
        QVERIFY(engine.isRunning());

        // Switched off: an explicit error, and the timer is left alone.
        allowed = false;
        for (const QString &method : {QStringLiteral("pause"), QStringLiteral("stop"), QStringLiteral("skip"), QStringLiteral("toggle")}) {
            const QDBusMessage reply = call(method);
            QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
            QCOMPARE(reply.errorName(), DBusTimerService::ControlDisabledError);
            QVERIFY(engine.isRunning());
        }
        // Reading the state stays possible.
        const QDBusMessage status = call(QStringLiteral("status"));
        QCOMPARE(status.type(), QDBusMessage::ReplyMessage);
        QVERIFY(status.arguments().value(0).toString().startsWith(QStringLiteral("running:work:")));

        allowed = true;
        QCOMPARE(call(QStringLiteral("stop")).type(), QDBusMessage::ReplyMessage);
        QVERIFY(!engine.isActive());
        app.unregisterObject(QStringLiteral("/Timer"));
    }
};

QTEST_GUILESS_MAIN(DBusTimerServiceTest)
#include "tst_dbustimerservice.moc"
