// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QSignalSpy>
#include <QTest>

#include "DBusTimerService.h"
#include "FakeClock.h"
#include "PresetModel.h"
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
    QDBusMessage call(const QString &method, const QVariantList &arguments = {})
    {
        QDBusConnection caller(QStringLiteral("caller"));
        QDBusMessage message = QDBusMessage::createMethodCall(
            service, QStringLiteral("/Timer"), QStringLiteral("io.github.mineraleyt.sugo.Timer"), method);
        message.setArguments(arguments);
        const QDBusPendingCall pending = caller.asyncCall(message);
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
        PresetModel presets;
        auto *host = new TimerDBusObject(&engine);
        auto *adaptor = new DBusTimerService(&engine, &presets, host);
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

private:
    /// The timers "Pomodoro" (built-in), "Deep Work" and "Reading" and a service around them.
    struct Fixture
    {
        FakeClock clock;
        TimerEngine engine{&clock, nullptr};
        PresetModel presets;
        TimerDBusObject *host = new TimerDBusObject(&engine);
        DBusTimerService *adaptor = new DBusTimerService(&engine, &presets, host);
        bool allowed = true;
        QString deep, reading;

        Fixture()
        {
            deep = presets.create({{QStringLiteral("name"), QStringLiteral("Deep Work")}});
            reading = presets.create({{QStringLiteral("name"), QStringLiteral("Reading")}});
            adaptor->setControlAllowed([this]() { return allowed; });
            QDBusConnection(QStringLiteral("app")).registerObject(QStringLiteral("/Timer"), host, QDBusConnection::ExportAdaptors);
            // The real app resets the engine when the timer changes (TimerBinding).
            QObject::connect(&presets, &PresetModel::currentChanged, &engine, [this]() { engine.reset(); });
        }
        ~Fixture() { QDBusConnection(QStringLiteral("app")).unregisterObject(QStringLiteral("/Timer")); }
    };

private Q_SLOTS:
    void presetsListIsReadOnly()
    {
        Fixture f;
        const QStringList expected{f.presets.index(0, 0).data(PresetModel::NameRole).toString(), QStringLiteral("Deep Work"),
                                   QStringLiteral("Reading")};
        QDBusMessage reply = call(QStringLiteral("presets"));
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.arguments().value(0).toStringList(), expected);

        f.allowed = false;
        reply = call(QStringLiteral("presets"));
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.arguments().value(0).toStringList(), expected);
    }

    void selectByNameAndUuid()
    {
        Fixture f;
        QDBusMessage reply = call(QStringLiteral("selectPreset"), {QStringLiteral("deep WORK")});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.arguments().value(0).toBool(), true);
        QCOMPARE(f.presets.currentUuid(), f.deep);

        reply = call(QStringLiteral("selectPreset"), {f.reading});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(f.presets.currentUuid(), f.reading);
    }

    void unknownPresetIsAnError()
    {
        Fixture f;
        const QString before = f.presets.currentUuid();
        for (const QString &method : {QStringLiteral("selectPreset"), QStringLiteral("startPreset")}) {
            const QDBusMessage reply = call(method, {QStringLiteral("nope")});
            QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
            QCOMPARE(reply.errorName(), DBusTimerService::UnknownPresetError);
            QVERIFY(!reply.errorMessage().isEmpty());
        }
        QCOMPARE(f.presets.currentUuid(), before);
        QVERIFY(!f.engine.isActive());
    }

    void nextAndPreviousWrapAround()
    {
        Fixture f;
        const QString first = f.presets.uuidAt(0);
        QCOMPARE(f.presets.currentUuid(), first);

        QCOMPARE(call(QStringLiteral("previousPreset")).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(f.presets.currentUuid(), f.reading);
        QCOMPARE(call(QStringLiteral("nextPreset")).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(f.presets.currentUuid(), first);
        QCOMPARE(call(QStringLiteral("nextPreset")).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(f.presets.currentUuid(), f.deep);
        QCOMPARE(call(QStringLiteral("nextPreset")).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(f.presets.currentUuid(), f.reading);
        QCOMPARE(call(QStringLiteral("nextPreset")).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(f.presets.currentUuid(), first);
    }

    void refusedWhileActive()
    {
        Fixture f;
        const QString first = f.presets.currentUuid();
        f.engine.start();
        QVERIFY(f.engine.isRunning());

        const QList<QPair<QString, QVariantList>> commands{{QStringLiteral("selectPreset"), {QStringLiteral("Deep Work")}},
                                                           {QStringLiteral("startPreset"), {f.deep}},
                                                           {QStringLiteral("nextPreset"), {}},
                                                           {QStringLiteral("previousPreset"), {}}};
        for (int paused = 0; paused < 2; ++paused) {
            for (const auto &command : commands) {
                const QDBusMessage reply = call(command.first, command.second);
                QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
                QCOMPARE(reply.errorName(), DBusTimerService::TimerActiveError);
                QVERIFY(!reply.errorMessage().isEmpty());
                QCOMPARE(f.presets.currentUuid(), first);
                QVERIFY(f.engine.isActive());
            }
            f.engine.pause();
        }

        // The timer that is already current is not a change: selecting it is fine, and
        // startPreset resumes the paused phase.
        QDBusMessage reply = call(QStringLiteral("selectPreset"), {first});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.arguments().value(0).toBool(), true);
        QVERIFY(f.engine.isPaused());
        reply = call(QStringLiteral("startPreset"), {first});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QVERIFY(f.engine.isRunning());
    }

    void startPresetSelectsAndStarts()
    {
        Fixture f;
        const QDBusMessage reply = call(QStringLiteral("startPreset"), {QStringLiteral("reading")});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.arguments().value(0).toBool(), true);
        QCOMPARE(f.presets.currentUuid(), f.reading);
        QVERIFY(f.engine.isRunning());
    }

    void presetCommandsFollowTheSetting()
    {
        Fixture f;
        f.allowed = false;
        const QString first = f.presets.currentUuid();
        const QList<QPair<QString, QVariantList>> commands{{QStringLiteral("selectPreset"), {QStringLiteral("Deep Work")}},
                                                           {QStringLiteral("startPreset"), {QStringLiteral("Deep Work")}},
                                                           {QStringLiteral("selectPreset"), {QStringLiteral("nope")}},
                                                           {QStringLiteral("nextPreset"), {}},
                                                           {QStringLiteral("previousPreset"), {}}};
        for (const auto &command : commands) {
            const QDBusMessage reply = call(command.first, command.second);
            QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
            QCOMPARE(reply.errorName(), DBusTimerService::ControlDisabledError);
            QCOMPARE(f.presets.currentUuid(), first);
            QVERIFY(!f.engine.isActive());
        }
    }
};

QTEST_GUILESS_MAIN(DBusTimerServiceTest)
#include "tst_dbustimerservice.moc"
