// SPDX-License-Identifier: GPL-3.0-or-later
#include "Localization.h"
#include <QSignalSpy>
#include <QTest>
#include <QTimeZone>

#include "Database.h"
#include "PresetModel.h"
#include "PresetPersistence.h"
#include "PresetRepository.h"
#include "SessionRecorder.h"
#include "SessionRepository.h"
#include "StatsModel.h"
#include "TimerEngine.h"

class TestStatsModel : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
    }

    void init();
    void cleanup();

    void testNavigation();
    void testReactiveUpdateOnRecord();
    void testFormatters();
    void testBreakdownModes();
    void testDateRolloverMovesTodayAlong();
    void testDateRolloverKeepsAPastPeriod();
    void testRolloverIsNoticedAfterAnEarlierRefresh();
    void testDateCheckFollowsTheDefaultLocale();
    void testDateCheckFollowsTheFirstDayOfWeek();
    void testRepeatedHourTooltipsDiffer();
    void testWeekTitles();
    void testStreakUpdatesOnRecord();
    void testDays();
    void testPreviousPeriodComparesTheSameStretch();
    void testDateCheckKeepsAPeriodUnderWayCurrent();
    void testPreviousYearComparesTheSameDate();
    void testPreviousMonthCountsAClampedDayWhole();

private:
    void insertWork(const QDateTime &start, bool completed = true);
    /// The days of the shown period, as the page lists them.
    QList<QDate> shownDays() const;

    std::unique_ptr<Database> m_db;
    std::unique_ptr<SessionRepository> m_repo;
    std::unique_ptr<StatsModel> m_model;
    StatsModel::Environment m_env;
};

void TestStatsModel::init()
{
    m_db = std::make_unique<Database>(QStringLiteral(":memory:"));
    QVERIFY(m_db->open());
    m_repo = std::make_unique<SessionRepository>(m_db->connectionName());

    m_env.timeZone = QTimeZone::utc();
    m_env.firstDayOfWeek = Qt::Monday;
    m_env.now = []() {
        return QDateTime(QDate(2026, 10, 3), QTime(12, 0), QTimeZone::utc());
    };

    m_model = std::make_unique<StatsModel>();
    m_model->setEnvironment(m_env);
    m_model->attach(m_repo.get());
}

void TestStatsModel::cleanup()
{
    m_model.reset();
    m_repo.reset();
    m_db.reset();
}

void TestStatsModel::testNavigation()
{
    m_model->setPeriod(StatsModel::Period::Day);
    QCOMPARE(shownDays(), QList<QDate>{QDate(2026, 10, 3)});
    QVERIFY(m_model->isCurrent());
    QVERIFY(!m_model->canGoNext()); // Cannot go into future

    m_model->previousPeriod();
    QCOMPARE(shownDays(), QList<QDate>{QDate(2026, 10, 2)});
    QVERIFY(!m_model->isCurrent());
    QVERIFY(m_model->canGoNext());

    m_model->nextPeriod();
    QCOMPARE(shownDays(), QList<QDate>{QDate(2026, 10, 3)});
    QVERIFY(m_model->isCurrent());

    m_model->previousPeriod();
    m_model->previousPeriod();
    QCOMPARE(shownDays(), QList<QDate>{QDate(2026, 10, 1)});
    m_model->today();
    QCOMPARE(shownDays(), QList<QDate>{QDate(2026, 10, 3)});
}

void TestStatsModel::testReactiveUpdateOnRecord()
{
    PresetRepository presetRepo(m_db->connectionName());
    PresetModel presets;
    PresetPersistence persistence(&presets, &presetRepo);
    TimerEngine engine;
    SessionRecorder recorder(&engine, &presets, m_repo.get());

    m_model->attach(m_repo.get(), &recorder);
    QCOMPARE(m_model->workSeconds(), 0);

    QSignalSpy spy(m_model.get(), &StatsModel::statsChanged);

    // Insert a session into repository and fire signal
    SessionRecord r;
    r.kind = SessionKind::Work;
    r.startedAtMs = m_env.now().toMSecsSinceEpoch() - 1500000;
    r.endedAtMs = m_env.now().toMSecsSinceEpoch();
    r.durationSec = 1500;
    r.plannedSec = 1500;
    r.completed = true;
    r.presetName = QStringLiteral("Default");
    m_repo->insert(r);

    Q_EMIT recorder.sessionRecorded();

    QVERIFY(spy.count() > 0);
    QCOMPARE(m_model->workSeconds(), 1500);
    QCOMPARE(m_model->completedWorkSessions(), 1);
    QVERIFY(m_model->hasData());
}

void TestStatsModel::testFormatters()
{
    QCOMPARE(m_model->formatDuration(0), QStringLiteral("0m"));
    QCOMPARE(m_model->formatDuration(45), QStringLiteral("45s"));
    QCOMPARE(m_model->formatDuration(300), QStringLiteral("5m"));
    QCOMPARE(m_model->formatDuration(3600), QStringLiteral("1h"));
    QCOMPARE(m_model->formatDuration(5400), QStringLiteral("1h 30m"));

}

void TestStatsModel::testBreakdownModes()
{
    SessionRecord r1;
    r1.kind = SessionKind::Work;
    r1.startedAtMs = m_env.now().toMSecsSinceEpoch() - 3600000;
    r1.endedAtMs = r1.startedAtMs + 2000000;
    r1.durationSec = 2000;
    r1.plannedSec = 2000;
    r1.completed = true;
    r1.presetName = QStringLiteral("Timer A");
    r1.category = QStringLiteral("Coding");
    r1.note = QStringLiteral("Refactor auth");
    m_repo->insert(r1);

    SessionRecord r2;
    r2.kind = SessionKind::Work;
    r2.startedAtMs = m_env.now().toMSecsSinceEpoch() - 1000000;
    r2.endedAtMs = m_env.now().toMSecsSinceEpoch();
    r2.durationSec = 1000;
    r2.plannedSec = 1000;
    r2.completed = true;
    r2.presetName = QStringLiteral("Timer B");
    r2.category = QStringLiteral("");
    r2.note = QStringLiteral("");
    m_repo->insert(r2);

    m_model->refresh();

    // ByTimer
    m_model->setBreakdownMode(StatsModel::BreakdownMode::ByTimer);
    QVariantList list = m_model->breakdown();
    QCOMPARE(list.size(), 2);

    // ByCategory
    m_model->setBreakdownMode(StatsModel::BreakdownMode::ByCategory);
    list = m_model->breakdown();
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Coding"));
    QCOMPARE(list.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Uncategorized"));

    // ByTask
    m_model->setBreakdownMode(StatsModel::BreakdownMode::ByTask);
    list = m_model->breakdown();
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Refactor auth"));
    QCOMPARE(list.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Without note"));
}

void TestStatsModel::testPreviousPeriodComparesTheSameStretch()
{
    // Now is Saturday 3 October 2026, 12:00: the week under way began on Monday 28 September.
    const auto at = [](int month, int day, int hour) {
        return QDateTime(QDate(2026, month, day), QTime(hour, 0), QTimeZone::utc());
    };
    insertWork(at(9, 22, 10));        // last week, before the same point: counted
    insertWork(at(9, 26, 11));        // last Saturday 11:00: counted
    insertWork(at(9, 26, 13));        // last Saturday 13:00: after the same point
    insertWork(at(9, 27, 10));        // last Sunday: after the same point
    insertWork(at(9, 23, 9), false);  // interrupted: its time counts too
    insertWork(at(10, 1, 10));        // this week
    m_model->setPeriod(StatsModel::Period::Week);
    m_model->refresh();
    QCOMPARE(m_model->workSeconds(), 1500);
    QCOMPARE(m_model->previousWorkSeconds(), 3600);

    // A past week compares with the whole week before it.
    m_model->previousPeriod();
    QCOMPARE(m_model->workSeconds(), 6600);
    QCOMPARE(m_model->previousWorkSeconds(), 0);
}

void TestStatsModel::testDateCheckKeepsAPeriodUnderWayCurrent()
{
    // Nothing recomputes the model during the day but the periodic check: it must still move
    // the comparison and the bar of "now" along with the clock.
    auto now = std::make_shared<QDateTime>(QDate(2026, 10, 3), QTime(9, 0), QTimeZone::utc());
    StatsModel::Environment env = m_env;
    env.now = [now]() { return *now; };
    m_model->setEnvironment(env);
    m_model->setPeriod(StatsModel::Period::Day);
    insertWork(QDateTime(QDate(2026, 10, 2), QTime(14, 0), QTimeZone::utc()));
    m_model->refresh();
    QCOMPARE(m_model->previousWorkSeconds(), 0); // yesterday at 14:00 is after 9:00
    QVERIFY(m_model->buckets().at(9).toMap().value(QStringLiteral("isCurrent")).toBool());

    QSignalSpy periodSpy(m_model.get(), &StatsModel::periodChanged);
    *now = QDateTime(QDate(2026, 10, 3), QTime(16, 0), QTimeZone::utc());
    m_model->checkDateChange();
    QCOMPARE(m_model->previousWorkSeconds(), 1500);
    const QVariantList buckets = m_model->buckets();
    QCOMPARE(buckets.size(), 24);
    QVERIFY(!buckets.at(9).toMap().value(QStringLiteral("isCurrent")).toBool());
    QVERIFY(buckets.at(16).toMap().value(QStringLiteral("isCurrent")).toBool());
    QCOMPARE(periodSpy.count(), 0); // still the same day

    // A past period has nothing to catch up with.
    m_model->previousPeriod();
    QSignalSpy statsSpy(m_model.get(), &StatsModel::statsChanged);
    m_model->checkDateChange();
    QCOMPARE(statsSpy.count(), 0);
}

void TestStatsModel::testPreviousYearComparesTheSameDate()
{
    // 1 March 2028, after a 29 February: the same point of 2027 is 1 March, not 2 March.
    StatsModel::Environment env = m_env;
    env.now = []() { return QDateTime(QDate(2028, 3, 1), QTime(12, 0), QTimeZone::utc()); };
    m_model->setEnvironment(env);
    m_model->setPeriod(StatsModel::Period::Year);
    insertWork(QDateTime(QDate(2027, 3, 1), QTime(9, 0), QTimeZone::utc()));  // before the same point
    insertWork(QDateTime(QDate(2027, 3, 1), QTime(18, 0), QTimeZone::utc())); // after it
    m_model->refresh();
    QCOMPARE(m_model->previousWorkSeconds(), 1500);
}

void TestStatsModel::testPreviousMonthCountsAClampedDayWhole()
{
    // 31 March, early: February has no 31st, so the whole of 28 February counts.
    StatsModel::Environment env = m_env;
    env.now = []() { return QDateTime(QDate(2026, 3, 31), QTime(0, 30), QTimeZone::utc()); };
    m_model->setEnvironment(env);
    m_model->setPeriod(StatsModel::Period::Month);
    insertWork(QDateTime(QDate(2026, 2, 28), QTime(9, 0), QTimeZone::utc()));
    insertWork(QDateTime(QDate(2026, 2, 28), QTime(22, 0), QTimeZone::utc()));
    m_model->refresh();
    QCOMPARE(m_model->previousWorkSeconds(), 3000);
}

QList<QDate> TestStatsModel::shownDays() const
{
    QList<QDate> result;
    for (const QVariant &day : m_model->days()) {
        result.append(day.toMap().value(QStringLiteral("date")).toDate());
    }
    return result;
}

void TestStatsModel::insertWork(const QDateTime &start, bool completed)
{
    SessionRecord r;
    r.kind = SessionKind::Work;
    r.startedAtMs = start.toMSecsSinceEpoch();
    r.endedAtMs = r.startedAtMs + 1'500'000;
    r.durationSec = completed ? 1500 : 600;
    r.plannedSec = 1500;
    r.completed = completed;
    r.presetName = QStringLiteral("Default");
    QVERIFY(m_repo->insert(r));
}

void TestStatsModel::testDateRolloverMovesTodayAlong()
{
    auto now = std::make_shared<QDateTime>(QDate(2026, 10, 3), QTime(23, 59), QTimeZone::utc());
    StatsModel::Environment env = m_env;
    env.now = [now]() { return *now; };
    m_model->setEnvironment(env);
    m_model->setPeriod(StatsModel::Period::Day);

    insertWork(QDateTime(QDate(2026, 10, 3), QTime(10, 0), QTimeZone::utc()));
    m_model->refresh();
    QCOMPARE(m_model->currentStreak(), 1);
    QCOMPARE(m_model->completedWorkSessions(), 1);

    QSignalSpy periodSpy(m_model.get(), &StatsModel::periodChanged);
    m_model->checkDateChange(); // same day: nothing happens
    QCOMPARE(periodSpy.count(), 0);

    *now = QDateTime(QDate(2026, 10, 4), QTime(0, 0, 30), QTimeZone::utc());
    m_model->checkDateChange();
    QCOMPARE(periodSpy.count(), 1);
    QCOMPARE(shownDays(), QList<QDate>{QDate(2026, 10, 4)});
    QVERIFY(m_model->isCurrent());
    QCOMPARE(m_model->currentStreak(), 1); // the new day has not broken the streak yet
    QCOMPARE(m_model->completedWorkSessions(), 0);
}

void TestStatsModel::testDateRolloverKeepsAPastPeriod()
{
    auto now = std::make_shared<QDateTime>(QDate(2026, 10, 3), QTime(23, 59), QTimeZone::utc());
    StatsModel::Environment env = m_env;
    env.now = [now]() { return *now; };
    m_model->setEnvironment(env);
    m_model->setPeriod(StatsModel::Period::Day);
    m_model->previousPeriod();
    QCOMPARE(shownDays(), QList<QDate>{QDate(2026, 10, 2)});

    *now = QDateTime(QDate(2026, 10, 4), QTime(0, 1), QTimeZone::utc());
    m_model->checkDateChange();
    QCOMPARE(shownDays(), QList<QDate>{QDate(2026, 10, 2)}); // the user looked back on purpose

    // The week view of the old today still contains the new today: it follows along.
    m_model->setPeriod(StatsModel::Period::Week);
    m_model->today();
    *now = QDateTime(QDate(2026, 10, 5), QTime(0, 1), QTimeZone::utc());
    m_model->checkDateChange();
    QCOMPARE(shownDays().first(), QDate(2026, 10, 5)); // the week of the new today, Monday first
    QCOMPARE(shownDays().last(), QDate(2026, 10, 11));
    QVERIFY(m_model->isCurrent());
}

void TestStatsModel::testRolloverIsNoticedAfterAnEarlierRefresh()
{
    // After midnight, something (a recorded session, a note) recomputes the streaks before the
    // midnight timer fires. That must not make the model believe it is still the old day.
    auto now = std::make_shared<QDateTime>(QDate(2026, 10, 3), QTime(23, 59), QTimeZone::utc());
    StatsModel::Environment env = m_env;
    env.now = [now]() { return *now; };
    m_model->setEnvironment(env);
    m_model->setPeriod(StatsModel::Period::Day);
    QCOMPARE(shownDays(), QList<QDate>{QDate(2026, 10, 3)});

    *now = QDateTime(QDate(2026, 10, 4), QTime(0, 0, 30), QTimeZone::utc());
    m_model->refresh();
    QCOMPARE(shownDays(), QList<QDate>{QDate(2026, 10, 3)});

    m_model->checkDateChange();
    QCOMPARE(shownDays(), QList<QDate>{QDate(2026, 10, 4)});
    QVERIFY(m_model->isCurrent());
}

void TestStatsModel::testDateCheckFollowsTheDefaultLocale()
{
    const QLocale original;
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));

    StatsModel::Environment env = m_env;
    env.followSystem = true;
    m_model->setEnvironment(env);
    m_model->setPeriod(StatsModel::Period::Month);
    QCOMPARE(m_model->title(), QStringLiteral("Oktober 2026"));

    QSignalSpy periodSpy(m_model.get(), &StatsModel::periodChanged);
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    m_model->checkDateChange(); // the periodic check notices the new locale too
    QCOMPARE(m_model->title(), QStringLiteral("October 2026"));
    QVERIFY(periodSpy.count() > 0);

    m_model->setPeriod(StatsModel::Period::Year);
    m_model->setPeriod(StatsModel::Period::Week);
    // en_US weeks start on Sunday.
    const QVariantList buckets = m_model->buckets();
    QCOMPARE(buckets.size(), 7);
    QCOMPARE(buckets.first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Sun"));

    QLocale::setDefault(original);
}

void TestStatsModel::testDateCheckFollowsTheFirstDayOfWeek()
{
    const QLocale original;
    QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
    StatsModel::Environment env = m_env;
    env.followSystem = true;
    m_model->setEnvironment(env);
    m_model->setPeriod(StatsModel::Period::Week);
    QCOMPARE(m_model->buckets().first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Mo."));

    // The periodic check (same day) notices that weeks now start on Sunday.
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    m_model->checkDateChange();
    QCOMPARE(m_model->buckets().first().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Sun"));

    QLocale::setDefault(original);
}

void TestStatsModel::testRepeatedHourTooltipsDiffer()
{
    const QTimeZone berlin("Europe/Berlin");
    if (!berlin.isValid()) {
        QSKIP("No time zone data");
    }
    StatsModel::Environment env = m_env;
    env.timeZone = berlin;
    env.locale = QLocale(QLocale::English, QLocale::UnitedStates);
    env.now = [berlin]() { return QDateTime(QDate(2026, 10, 25), QTime(12, 0), berlin); }; // clocks go back at 3:00
    m_model->setEnvironment(env);
    m_model->setPeriod(StatsModel::Period::Day);

    const QVariantList buckets = m_model->buckets();
    QCOMPARE(buckets.size(), 25);
    const QString repeated = buckets.at(2).toMap().value(QStringLiteral("tooltip")).toString();
    QVERIFY2(repeated.startsWith(QStringLiteral("2:00 UTC+02:00 – 2:00 UTC+01:00")), qPrintable(repeated));
    QVERIFY(buckets.at(3).toMap().value(QStringLiteral("tooltip")).toString().startsWith(QStringLiteral("2:00 – 3:00")));
}

void TestStatsModel::testWeekTitles()
{
    auto now = std::make_shared<QDateTime>(QDate(2026, 10, 3), QTime(12, 0), QTimeZone::utc());
    StatsModel::Environment env = m_env;
    env.locale = QLocale(QLocale::English, QLocale::UnitedStates); // first day of week stays Monday (m_env)
    env.now = [now]() { return *now; };
    m_model->setEnvironment(env);
    m_model->setPeriod(StatsModel::Period::Week);
    QCOMPARE(m_model->title(), QStringLiteral("28 Sep – 4 Oct 2026"));
    m_model->nextPeriod(); // not allowed into the future
    m_model->previousPeriod();
    QCOMPARE(m_model->title(), QStringLiteral("21 – 27 September 2026"));

    *now = QDateTime(QDate(2027, 1, 1), QTime(12, 0), QTimeZone::utc());
    m_model->setEnvironment(env);
    QCOMPARE(m_model->title(), QStringLiteral("28 Dec 2026 – 3 Jan 2027"));
}

void TestStatsModel::testStreakUpdatesOnRecord()
{
    PresetRepository presetRepo(m_db->connectionName());
    PresetModel presets;
    PresetPersistence persistence(&presets, &presetRepo);
    TimerEngine engine;
    SessionRecorder recorder(&engine, &presets, m_repo.get());
    m_model->attach(m_repo.get(), &recorder);

    insertWork(QDateTime(QDate(2026, 10, 2), QTime(9, 0), QTimeZone::utc()));
    m_model->refresh();
    QCOMPARE(m_model->currentStreak(), 1);

    // Recorded sessions extend the cache incrementally.
    insertWork(QDateTime(QDate(2026, 10, 3), QTime(9, 0), QTimeZone::utc()));
    insertWork(QDateTime(QDate(2026, 10, 3), QTime(10, 0), QTimeZone::utc()), false);
    Q_EMIT recorder.sessionRecorded();
    QCOMPARE(m_model->currentStreak(), 2);
    QCOMPARE(m_model->bestStreak(), 2);

    // A full refresh (after clear/restore) starts over.
    QVERIFY(m_repo->deleteAll());
    m_model->refresh();
    QCOMPARE(m_model->currentStreak(), 0);
}

void TestStatsModel::testDays()
{
    // "Now" is Saturday 2026-10-03; the week runs from Monday 09-28.
    insertWork(QDateTime(QDate(2026, 9, 29), QTime(9, 0), QTimeZone::utc()));
    insertWork(QDateTime(QDate(2026, 9, 29), QTime(10, 0), QTimeZone::utc()));
    insertWork(QDateTime(QDate(2026, 10, 1), QTime(9, 0), QTimeZone::utc()), false);
    insertWork(QDateTime(QDate(2026, 10, 3), QTime(9, 0), QTimeZone::utc()));
    m_model->setPeriod(StatsModel::Period::Week);
    m_model->refresh();

    const QVariantList days = m_model->days();
    QCOMPARE(days.size(), 7);
    QCOMPARE(days.at(0).toMap().value(QStringLiteral("date")).toDate(), QDate(2026, 9, 28));
    QCOMPARE(days.at(1).toMap().value(QStringLiteral("workSeconds")).toLongLong(), 3000);
    QCOMPARE(days.at(3).toMap().value(QStringLiteral("workSeconds")).toLongLong(), 600);
    QVERIFY(days.at(5).toMap().value(QStringLiteral("isToday")).toBool());

    m_model->setPeriod(StatsModel::Period::Year);
    QCOMPARE(m_model->days().size(), 365);
}

QTEST_MAIN(TestStatsModel)
#include "tst_statsmodel.moc"
