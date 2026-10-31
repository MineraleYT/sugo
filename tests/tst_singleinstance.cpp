// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusContext>
#include <QDBusError>
#include <QLoggingCategory>
#include <QSignalSpy>
#include <QPointer>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QTest>

#include <memory>

#include "PrivateDBus.h"
#include "SingleInstance.h"

namespace
{
const QString kService = QStringLiteral("org.example.sugo.test");

/// A primary instance that quits while a second launch asks it to show itself.
class QuittingInstance : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Application")

public Q_SLOTS:
    void Activate(const QVariantMap &)
    {
        connection().unregisterService(kService);
        sendErrorReply(QDBusError::NoReply, QStringLiteral("quitting"));
    }
};
}

class SingleInstanceTest : public QObject
{
    Q_OBJECT

private:
    PrivateDBus bus;

    QDBusConnection first() const { return QDBusConnection(QStringLiteral("first")); }
    QDBusConnection second() const { return QDBusConnection(QStringLiteral("second")); }

private Q_SLOTS:
    void initTestCase()
    {
        if (!bus.start()) {
            QSKIP("dbus-daemon is not available");
        }
        QDBusConnection::connectToBus(bus.address(), QStringLiteral("first"));
        QDBusConnection::connectToBus(bus.address(), QStringLiteral("second"));
        QVERIFY(first().isConnected());
        QVERIFY(second().isConnected());
        QLoggingCategory::setFilterRules(QStringLiteral("sugo.platform.singleinstance.warning=false"));
    }

    void cleanupTestCase()
    {
        QDBusConnection::disconnectFromBus(QStringLiteral("first"));
        QDBusConnection::disconnectFromBus(QStringLiteral("second"));
    }

    void theFirstLaunchBecomesThePrimaryInstance()
    {
        SingleInstance primary(first(), kService);
        QVERIFY(primary.acquire());
        QVERIFY(first().interface()->isServiceRegistered(kService));
    }

    void aSecondLaunchAsksTheFirstToShowItselfAndShouldExit()
    {
        SingleInstance primary(first(), kService);
        QVERIFY(primary.acquire());
        QSignalSpy activated(&primary, &SingleInstance::activateRequested);

        SingleInstance later(second(), kService);
        QVERIFY(!later.acquire()); // "exit, the other one has it"

        QTRY_COMPARE(activated.count(), 1);
    }

    void theActivationTokenReachesTheFirstInstance()
    {
        SingleInstance primary(first(), kService);
        QVERIFY(primary.acquire());
        QSignalSpy activated(&primary, &SingleInstance::activateRequested);

        qputenv("XDG_ACTIVATION_TOKEN", "token-123");
        SingleInstance later(second(), kService);
        QVERIFY(!later.acquire());
        qunsetenv("XDG_ACTIVATION_TOKEN");

        QTRY_COMPARE(activated.count(), 1);
        QCOMPARE(activated.first().first().toString(), QStringLiteral("token-123"));
    }

    void everyExtraLaunchActivatesAgain()
    {
        SingleInstance primary(first(), kService);
        QVERIFY(primary.acquire());
        QSignalSpy activated(&primary, &SingleInstance::activateRequested);

        for (int i = 0; i < 3; ++i) {
            SingleInstance later(second(), kService);
            QVERIFY(!later.acquire());
        }
        QTRY_COMPARE(activated.count(), 3);
    }

    void aBackgroundLaunchDoesNotShowTheWindow()
    {
        SingleInstance primary(first(), kService);
        QVERIFY(primary.acquire());
        QSignalSpy activated(&primary, &SingleInstance::activateRequested);

        SingleInstance later(second(), kService);
        QVERIFY(!later.acquire(false)); // still "exit", but quietly
        QTest::qWait(200);
        QCOMPARE(activated.count(), 0);
    }

    void aRequestBeforeAnyoneListensIsDeliveredOnConnect()
    {
        SingleInstance primary(first(), kService);
        QVERIFY(primary.acquire());

        qputenv("XDG_ACTIVATION_TOKEN", "early");
        SingleInstance later(second(), kService);
        QVERIFY(!later.acquire());
        qunsetenv("XDG_ACTIVATION_TOKEN");

        QSignalSpy activated(&primary, &SingleInstance::activateRequested);
        QTRY_COMPARE(activated.count(), 1);
        QCOMPARE(activated.first().first().toString(), QStringLiteral("early"));
    }

    void aLaunchTakesOverWhenThePrimaryQuitsMeanwhile()
    {
        QuittingInstance quitting;
        const QString path = QStringLiteral("/org/example/sugo/test");
        QVERIFY(first().registerObject(path, &quitting, QDBusConnection::ExportAllSlots));
        QVERIFY(first().registerService(kService));

        {
            SingleInstance later(second(), kService);
            QVERIFY(later.acquire()); // the name was free again: this launch is the instance now
            QVERIFY(second().interface()->isServiceRegistered(kService));
        }
        first().unregisterObject(path);
    }

    void theNameIsFreeAgainWhenThePrimaryInstanceGoesAway()
    {
        {
            SingleInstance primary(first(), kService);
            QVERIFY(primary.acquire());
        }
        SingleInstance next(second(), kService);
        QVERIFY(next.acquire());
    }

    void onlyActivateIsReachableOverDBus()
    {
        // ExportAllSlots exports the class' own slots only: QObject's deleteLater() (which would
        // end the instance and free the name for a second copy) is not one of them.
        SingleInstance primary(first(), kService);
        QVERIFY(primary.acquire());
        QPointer<SingleInstance> alive(&primary);
        const QString path = QStringLiteral("/org/example/sugo/test");
        // Asynchronous calls: the object answering lives in this same thread.
        const auto call = [&](const QString &interface, const QString &method) {
            QDBusPendingCall pending = second().asyncCall(QDBusMessage::createMethodCall(kService, path, interface, method));
            QDBusPendingCallWatcher watcher(pending);
            QSignalSpy done(&watcher, &QDBusPendingCallWatcher::finished);
            done.wait(5000);
            return pending.reply();
        };
        for (const QString &method : {QStringLiteral("deleteLater"), QStringLiteral("destroyed")}) {
            QCOMPARE(call(QString(), method).type(), QDBusMessage::ErrorMessage);
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(alive);
        QVERIFY(first().interface()->isServiceRegistered(kService));

        // The introspection lists Activate and nothing of QObject.
        const QString xml = call(QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect")).arguments().value(0).toString();
        QVERIFY2(xml.contains(QStringLiteral("Activate")), qPrintable(xml));
        QVERIFY(!xml.contains(QStringLiteral("deleteLater")));
    }

        void withoutASessionBusThereIsNothingToCoordinateWith()
    {
        SingleInstance alone(QDBusConnection(QStringLiteral("no-such-connection")), kService);
        QVERIFY(alone.acquire());
    }
};

QTEST_GUILESS_MAIN(SingleInstanceTest)
#include "tst_singleinstance.moc"
