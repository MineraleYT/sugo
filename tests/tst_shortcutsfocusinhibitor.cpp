// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QTest>

#include <memory>

#include "FakeShortcuts.h"
#include "ShortcutsFocusInhibitor.h"

namespace
{
const QString kOn = QStringLiteral("Sugo Focus On");
const QString kOff = QStringLiteral("Sugo Focus Off");
const QString kRunOn = QStringLiteral("run|Sugo Focus On|");
const QString kRunOff = QStringLiteral("run|Sugo Focus Off|");
} // namespace

/// The macOS Focus through two shortcuts, driven against a fake `shortcuts` program.
class ShortcutsFocusInhibitorTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<FakeShortcuts> fake;
    std::unique_ptr<ShortcutsFocusInhibitor> inhibitor;

    /// Names set, list read: ready to silence.
    bool makeReady()
    {
        fake->setList({QStringLiteral("Other"), kOn, kOff});
        QSignalSpy listed(inhibitor.get(), &ShortcutsFocusInhibitor::shortcutListChanged);
        inhibitor->setShortcutNames(kOn, kOff);
        return listed.wait(5000) && inhibitor->isAvailable();
    }

    bool waitForInhibited(bool inhibited)
    {
        return QTest::qWaitFor([&]() { return inhibitor->isInhibited() == inhibited; }, 5000);
    }

private Q_SLOTS:
    void init()
    {
        fake = std::make_unique<FakeShortcuts>();
        QVERIFY(fake->isValid());
        inhibitor = std::make_unique<ShortcutsFocusInhibitor>(fake->program());
        inhibitor->setTimeoutsForTesting(2000, 2000);
    }

    void cleanup()
    {
        inhibitor.reset();
        fake.reset();
    }

    void parsesTheListOutput()
    {
        QCOMPARE(ShortcutsFocusInhibitor::parseShortcutList("b\n  A \n\nb\r\nc"),
                 (QStringList{QStringLiteral("A"), QStringLiteral("b"), QStringLiteral("c")}));
        QVERIFY(ShortcutsFocusInhibitor::parseShortcutList("").isEmpty());
    }

    void unavailableWithoutTheTool()
    {
        ShortcutsFocusInhibitor missing(QStringLiteral("/nonexistent/shortcuts"));
        QSignalSpy listed(&missing, &ShortcutsFocusInhibitor::shortcutListChanged);
        missing.setShortcutNames(kOn, kOff);
        QVERIFY(!missing.toolFound());
        QVERIFY(!missing.isAvailable());
        missing.inhibit(QStringLiteral("x"));
        QVERIFY(!missing.isInhibited());
    }

    void availableOnlyWhenBothShortcutsExist()
    {
        QVERIFY(!inhibitor->isAvailable()); // nothing read yet
        fake->setList({kOn});
        QSignalSpy available(inhibitor.get(), &NotificationInhibitor::availableChanged);
        QSignalSpy listed(inhibitor.get(), &ShortcutsFocusInhibitor::shortcutListChanged);
        inhibitor->setShortcutNames(kOn, kOff);
        QVERIFY(listed.wait(5000));
        QCOMPARE(fake->calls(), QStringList{QStringLiteral("list|")});
        QVERIFY(!inhibitor->isAvailable());
        QCOMPARE(inhibitor->shortcutList(), QStringList{kOn});

        // The user creates the second one; the list is read again.
        fake->setList({kOn, kOff});
        inhibitor->refreshShortcutList();
        QVERIFY(listed.wait(5000));
        QVERIFY(inhibitor->isAvailable());
        QCOMPARE(available.count(), 1);

        // A name that is not there.
        inhibitor->setShortcutNames(kOn, QStringLiteral("Nope"));
        QVERIFY(!inhibitor->isAvailable());
        QCOMPARE(available.count(), 2);
    }

    void inhibitRunsOnAndReleaseRunsOff()
    {
        QVERIFY(makeReady());
        QSignalSpy changed(inhibitor.get(), &NotificationInhibitor::inhibitedChanged);
        inhibitor->inhibit(QStringLiteral("work"));
        QVERIFY(inhibitor->isInhibited()); // in flight already counts
        QVERIFY(!inhibitor->isReleasing());
        QTest::qWait(300);
        QVERIFY(inhibitor->isInhibited());
        QCOMPARE(fake->runs(), QStringList{kRunOn});

        inhibitor->inhibit(QStringLiteral("again")); // idempotent
        inhibitor->release();
        QVERIFY(inhibitor->isReleasing());
        QVERIFY(waitForInhibited(false));
        QCOMPARE(fake->runs(), (QStringList{kRunOn, kRunOff}));
        QCOMPARE(changed.count(), 2); // on, then off

        inhibitor->release(); // nothing held: nothing run
        QTest::qWait(200);
        QCOMPARE(fake->runs().size(), 2);
    }

    void releaseWhileTurningOnStillTurnsOff()
    {
        QVERIFY(makeReady());
        fake->setDelay(QStringLiteral("0.3"));
        inhibitor->inhibit(QStringLiteral("work"));
        inhibitor->release();
        QVERIFY(inhibitor->isInhibited());
        QVERIFY(inhibitor->waitUntilReleased(5000));
        QCOMPARE(fake->runs(), (QStringList{kRunOn, kRunOff}));
    }

    void aFailingOnShortcutHoldsNothing()
    {
        QVERIFY(makeReady());
        fake->setFails(kOn, true);
        inhibitor->inhibit(QStringLiteral("work"));
        QVERIFY(waitForInhibited(false));
        inhibitor->release();
        QTest::qWait(200);
        QCOMPARE(fake->runs(), QStringList{kRunOn}); // nothing to turn off
    }

    void aFailingOffShortcutDoesNotBlockTheRelease()
    {
        QVERIFY(makeReady());
        fake->setFails(kOff, true);
        inhibitor->inhibit(QStringLiteral("work"));
        QTest::qWait(300);
        inhibitor->release();
        QVERIFY(inhibitor->waitUntilReleased(5000));
        QCOMPARE(fake->runs(), (QStringList{kRunOn, kRunOff}));
    }

    void aHungShortcutTimesOutAndIsTurnedOffAnyway()
    {
        QVERIFY(makeReady());
        inhibitor->setTimeoutsForTesting(300, 2000);
        fake->setHang(true);
        inhibitor->inhibit(QStringLiteral("work"));
        QTest::qWait(600);
        // Unknown outcome: counts as on, so the release runs "off".
        QVERIFY(inhibitor->isInhibited());
        fake->setHang(false);
        inhibitor->release();
        QVERIFY(inhibitor->waitUntilReleased(inhibitor->releaseTimeoutMs()));
        QCOMPARE(fake->runs(), (QStringList{kRunOn, kRunOff}));
    }

    void aHungOffShortcutIsRunOnceMore()
    {
        QVERIFY(makeReady());
        inhibitor->setTimeoutsForTesting(300, 2000);
        inhibitor->inhibit(QStringLiteral("work"));
        QTest::qWait(200);
        QCOMPARE(fake->runs(), QStringList{kRunOn});

        fake->setHang(true);
        inhibitor->release();
        QTest::qWait(500);
        // "Off" timed out: the Focus may still be on, so it is not reported as released.
        QCOMPARE(fake->runs(), (QStringList{kRunOn, kRunOff}));
        QVERIFY(inhibitor->isInhibited());
        QVERIFY(inhibitor->isReleasing());

        fake->setHang(false);
        QVERIFY(inhibitor->waitUntilReleased(10000));
        QCOMPARE(fake->runs(), (QStringList{kRunOn, kRunOff, kRunOff}));
    }

    void aHungOffShortcutIsRetriedOnlyOnce()
    {
        QVERIFY(makeReady());
        inhibitor->setTimeoutsForTesting(300, 2000);
        inhibitor->inhibit(QStringLiteral("work"));
        QTest::qWait(200);

        fake->setHang(true);
        inhibitor->release();
        QVERIFY(inhibitor->waitUntilReleased(10000)); // gives up after the second try
        QCOMPARE(fake->runs(), (QStringList{kRunOn, kRunOff, kRunOff}));
        QTest::qWait(1500);
        QCOMPARE(fake->runs().size(), 3); // no retry loop
    }

    void offWaitsAMomentAfterAHungOn()
    {
        QVERIFY(makeReady());
        inhibitor->setTimeoutsForTesting(300, 2000);
        fake->setHang(true);
        inhibitor->inhibit(QStringLiteral("work"));
        inhibitor->release();
        QTest::qWait(500);
        fake->setHang(false);
        // The killed client may not have stopped the shortcut inside Shortcuts: an "off" run at
        // once could land before a late "on".
        QCOMPARE(fake->runs(), QStringList{kRunOn});
        QVERIFY(inhibitor->isInhibited());

        QVERIFY(inhibitor->waitUntilReleased(10000));
        QCOMPARE(fake->runs(), (QStringList{kRunOn, kRunOff}));
    }

    void wantedBeforeTheShortcutsExistTurnsOnOnceTheyDo()
    {
        fake->setList({});
        QSignalSpy listed(inhibitor.get(), &ShortcutsFocusInhibitor::shortcutListChanged);
        inhibitor->setShortcutNames(kOn, kOff);
        QVERIFY(listed.wait(5000));
        inhibitor->inhibit(QStringLiteral("work"));
        QVERIFY(!inhibitor->isInhibited());
        QVERIFY(fake->runs().isEmpty());

        fake->setList({kOn, kOff});
        inhibitor->refreshShortcutList();
        QVERIFY(waitForInhibited(true));
        QTRY_COMPARE_WITH_TIMEOUT(fake->runs(), QStringList{kRunOn}, 5000);
    }

    void aFailingListMakesItUnavailable()
    {
        QVERIFY(makeReady());
        fake->setFails(QStringLiteral("list"), true);
        QSignalSpy listed(inhibitor.get(), &ShortcutsFocusInhibitor::shortcutListChanged);
        inhibitor->refreshShortcutList();
        QVERIFY(listed.wait(5000));
        QVERIFY(!inhibitor->isAvailable());
        QVERIFY(inhibitor->shortcutList().isEmpty());
    }
};

QTEST_GUILESS_MAIN(ShortcutsFocusInhibitorTest)
#include "tst_shortcutsfocusinhibitor.moc"
