// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>
#include <QTimeZone>

#include "StatsAggregator.h"

class TestStatsAggregator : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testRangeForDay();
    void testRangeForWeek();
    void testRangeForMonth();
    void testRangeForYear();
    void testShifted();
    void testSamePointInPreviousPeriod();
    void testSamePointInPreviousPeriodAcrossClockChanges();
    void testSummarizeEmpty();
    void testSummarizeSessions();
    void testDayViewFollowsTheRealHoursOnClockChangeDays();
    void testCategoriesIgnoreSurroundingWhitespace();
    void testBreakdownOrder();
    void testTaskBreakdown();
    void testTimerBreakdownGroupsByPreset();
    void testStreaks();
    void testWeekendStreakProtection();
};

void TestStatsAggregator::testRangeForDay()
{
    const QTimeZone tz = QTimeZone::utc();
    const QDate day(2026, 10, 3);
    const PeriodRange range = Stats::rangeFor(StatsPeriod::Day, day, Qt::Monday, tz);

    QCOMPARE(range.firstDay, day);
    QCOMPARE(range.lastDay, day);
    QCOMPARE(range.startMs, QDateTime(day, QTime(0, 0), tz).toMSecsSinceEpoch());
    QCOMPARE(range.endMs, QDateTime(day.addDays(1), QTime(0, 0), tz).toMSecsSinceEpoch());
}

void TestStatsAggregator::testRangeForWeek()
{
    const QTimeZone tz = QTimeZone::utc();
    // 2026-10-03 is a Saturday
    const QDate sat(2026, 10, 3);

    // Monday-first week: 2026-09-28 (Mon) to 2026-10-04 (Sun)
    const PeriodRange monWeek = Stats::rangeFor(StatsPeriod::Week, sat, Qt::Monday, tz);
    QCOMPARE(monWeek.firstDay, QDate(2026, 9, 28));
    QCOMPARE(monWeek.lastDay, QDate(2026, 10, 4));

    // Sunday-first week: 2026-09-27 (Sun) to 2026-10-03 (Sat)
    const PeriodRange sunWeek = Stats::rangeFor(StatsPeriod::Week, sat, Qt::Sunday, tz);
    QCOMPARE(sunWeek.firstDay, QDate(2026, 9, 27));
    QCOMPARE(sunWeek.lastDay, QDate(2026, 10, 3));
}

void TestStatsAggregator::testRangeForMonth()
{
    const QTimeZone tz = QTimeZone::utc();
    const QDate midMonth(2026, 2, 14);
    const PeriodRange range = Stats::rangeFor(StatsPeriod::Month, midMonth, Qt::Monday, tz);

    QCOMPARE(range.firstDay, QDate(2026, 2, 1));
    QCOMPARE(range.lastDay, QDate(2026, 2, 28)); // 2026 is not a leap year
}

void TestStatsAggregator::testRangeForYear()
{
    const QTimeZone tz = QTimeZone::utc();
    const QDate anyDate(2026, 6, 15);
    const PeriodRange range = Stats::rangeFor(StatsPeriod::Year, anyDate, Qt::Monday, tz);

    QCOMPARE(range.firstDay, QDate(2026, 1, 1));
    QCOMPARE(range.lastDay, QDate(2026, 12, 31));
}

void TestStatsAggregator::testShifted()
{
    const QDate base(2026, 1, 31);
    QCOMPARE(Stats::shifted(StatsPeriod::Day, base, 1), QDate(2026, 2, 1));
    QCOMPARE(Stats::shifted(StatsPeriod::Day, base, -1), QDate(2026, 1, 30));

    QCOMPARE(Stats::shifted(StatsPeriod::Week, base, 1), QDate(2026, 2, 7));
    // 31 Jan + 1 month clamps to end of Feb
    QCOMPARE(Stats::shifted(StatsPeriod::Month, base, 1), QDate(2026, 2, 28));
    QCOMPARE(Stats::shifted(StatsPeriod::Year, base, 1), QDate(2027, 1, 31));
}

void TestStatsAggregator::testSamePointInPreviousPeriod()
{
    const QTimeZone tz = QTimeZone::utc();
    const auto at = [tz](int y, int m, int d, int h) { return QDateTime(QDate(y, m, d), QTime(h, 0), tz); };
    const auto samePoint = [tz](StatsPeriod kind, const QDateTime &now) {
        return Stats::samePointInPreviousPeriod(kind, now.date(), Qt::Monday, tz, now);
    };

    QCOMPARE(samePoint(StatsPeriod::Day, at(2026, 10, 3, 9)), at(2026, 10, 2, 9).toMSecsSinceEpoch());
    QCOMPARE(samePoint(StatsPeriod::Day, at(2026, 10, 3, 0)), at(2026, 10, 2, 0).toMSecsSinceEpoch());
    // Saturday 12:00 matches last Saturday 12:00.
    QCOMPARE(samePoint(StatsPeriod::Week, at(2026, 10, 3, 12)), at(2026, 9, 26, 12).toMSecsSinceEpoch());
    // The month before is shorter: 29 to 31 March are clamped to 28 February, which is over by
    // then, so the whole of February counts.
    QCOMPARE(samePoint(StatsPeriod::Month, at(2026, 3, 28, 8)), at(2026, 2, 28, 8).toMSecsSinceEpoch());
    QCOMPARE(samePoint(StatsPeriod::Month, at(2026, 3, 29, 8)), at(2026, 3, 1, 0).toMSecsSinceEpoch());
    QCOMPARE(samePoint(StatsPeriod::Month, at(2026, 3, 31, 8)), at(2026, 3, 1, 0).toMSecsSinceEpoch());
    // 2028 has a 29 February: 1 March 2028 matches 1 March 2027, not 2 March (as many days in).
    QCOMPARE(samePoint(StatsPeriod::Year, at(2028, 3, 1, 12)), at(2027, 3, 1, 12).toMSecsSinceEpoch());
    QCOMPARE(samePoint(StatsPeriod::Year, at(2028, 2, 28, 23)), at(2027, 2, 28, 23).toMSecsSinceEpoch());
    QCOMPARE(samePoint(StatsPeriod::Year, at(2028, 2, 29, 10)), at(2027, 3, 1, 0).toMSecsSinceEpoch());
    QCOMPARE(samePoint(StatsPeriod::Year, at(2029, 3, 1, 12)), at(2028, 3, 1, 12).toMSecsSinceEpoch());

    // As time goes on the point never goes back, not even where a day is clamped.
    QVERIFY(samePoint(StatsPeriod::Month, at(2026, 3, 31, 0)) >= samePoint(StatsPeriod::Month, at(2026, 3, 30, 23)));
    QVERIFY(samePoint(StatsPeriod::Month, at(2026, 5, 31, 0)) >= samePoint(StatsPeriod::Month, at(2026, 5, 30, 23)));
    QVERIFY(samePoint(StatsPeriod::Year, at(2028, 2, 29, 0)) >= samePoint(StatsPeriod::Year, at(2028, 2, 28, 23)));
    for (const StatsPeriod kind : {StatsPeriod::Month, StatsPeriod::Year}) {
        qint64 last = samePoint(kind, at(2028, 1, 1, 0));
        for (QDateTime now = at(2028, 1, 1, 1); now < at(2029, 1, 1, 0); now = now.addSecs(3600)) {
            if (kind == StatsPeriod::Month && now.date().day() == 1 && now.time().hour() == 0) {
                last = samePoint(kind, now); // a new month: a new period before
                continue;
            }
            const qint64 point = samePoint(kind, now);
            QVERIFY2(point >= last, qPrintable(now.toString(Qt::ISODate)));
            last = point;
        }
    }
}

void TestStatsAggregator::testSamePointInPreviousPeriodAcrossClockChanges()
{
    const QTimeZone berlin("Europe/Berlin");
    if (!berlin.isValid()) {
        QSKIP("No time zone data for Europe/Berlin");
    }
    const auto at = [berlin](int m, int d, int h) { return QDateTime(QDate(2026, m, d), QTime(h, 0), berlin); };
    const auto samePoint = [berlin](StatsPeriod kind, const QDateTime &now) {
        return Stats::samePointInPreviousPeriod(kind, now.date(), Qt::Monday, berlin, now);
    };

    // The clocks go forward on Sunday 29 March (a day of 23 hours) and back on Sunday 25 October
    // (25 hours): the same clock time, not as many hours into the day or week.
    QCOMPARE(samePoint(StatsPeriod::Day, at(3, 30, 10)), at(3, 29, 10).toMSecsSinceEpoch());
    QCOMPARE(samePoint(StatsPeriod::Day, at(3, 29, 10)), at(3, 28, 10).toMSecsSinceEpoch());
    QCOMPARE(samePoint(StatsPeriod::Day, at(10, 26, 10)), at(10, 25, 10).toMSecsSinceEpoch());
    QCOMPARE(samePoint(StatsPeriod::Day, at(10, 25, 10)), at(10, 24, 10).toMSecsSinceEpoch());
    QCOMPARE(samePoint(StatsPeriod::Week, at(3, 29, 10)), at(3, 22, 10).toMSecsSinceEpoch());
    QCOMPARE(samePoint(StatsPeriod::Week, at(10, 25, 10)), at(10, 18, 10).toMSecsSinceEpoch());
}

void TestStatsAggregator::testSummarizeEmpty()
{
    const QTimeZone tz = QTimeZone::utc();
    const QLocale locale = QLocale::c();
    const QDate anchor(2026, 10, 3);
    const QDateTime now(anchor, QTime(12, 0), tz);

    const StatsSummary summary = Stats::summarize({}, StatsPeriod::Day, anchor, Qt::Monday, tz, locale, now);
    QCOMPARE(summary.workSeconds, 0);
    QCOMPARE(summary.breakSeconds, 0);
    QCOMPARE(summary.completedWorkSessions, 0);
    QCOMPARE(summary.interruptedWorkSessions, 0);
    QCOMPARE(summary.ratio, -1.0);
    QCOMPARE(summary.hasData(), false);
    QCOMPARE(summary.buckets.size(), 24);
    QVERIFY(summary.buckets[12].isCurrent);
}

void TestStatsAggregator::testSummarizeSessions()
{
    const QTimeZone tz = QTimeZone::utc();
    const QLocale locale = QLocale::c();
    const QDate anchor(2026, 10, 3);
    const QDateTime now(anchor, QTime(15, 0), tz);

    SessionRecord r1;
    r1.kind = SessionKind::Work;
    r1.startedAtMs = QDateTime(anchor, QTime(9, 0), tz).toMSecsSinceEpoch();
    r1.endedAtMs = QDateTime(anchor, QTime(9, 25), tz).toMSecsSinceEpoch();
    r1.durationSec = 1500;
    r1.plannedSec = 1500;
    r1.completed = true;
    r1.presetName = QStringLiteral("Pomodoro");
    r1.category = QStringLiteral("Code");

    SessionRecord r2;
    r2.kind = SessionKind::ShortBreak;
    r2.startedAtMs = QDateTime(anchor, QTime(9, 25), tz).toMSecsSinceEpoch();
    r2.endedAtMs = QDateTime(anchor, QTime(9, 30), tz).toMSecsSinceEpoch();
    r2.durationSec = 300;
    r2.plannedSec = 300;
    r2.completed = true;
    r2.presetName = QStringLiteral("Pomodoro");

    SessionRecord r3;
    r3.kind = SessionKind::Work;
    r3.startedAtMs = QDateTime(anchor, QTime(10, 0), tz).toMSecsSinceEpoch();
    r3.endedAtMs = QDateTime(anchor, QTime(10, 10), tz).toMSecsSinceEpoch();
    r3.durationSec = 600;
    r3.plannedSec = 1500;
    r3.completed = false; // Interrupted
    r3.presetName = QStringLiteral("Pomodoro");
    r3.category = QStringLiteral("Study");

    const StatsSummary summary = Stats::summarize({r1, r2, r3}, StatsPeriod::Day, anchor, Qt::Monday, tz, locale, now);
    QCOMPARE(summary.workSeconds, 2100);
    QCOMPARE(summary.breakSeconds, 300);
    QCOMPARE(summary.completedWorkSessions, 1);
    QCOMPARE(summary.interruptedWorkSessions, 1);
    QCOMPARE(summary.ratio, 7.0); // 2100 / 300
    QVERIFY(summary.hasData());

    QCOMPARE(summary.buckets[9].workSeconds, 1500);
    QCOMPARE(summary.buckets[9].breakSeconds, 300);
    QCOMPARE(summary.buckets[10].workSeconds, 600);
}

void TestStatsAggregator::testDayViewFollowsTheRealHoursOnClockChangeDays()
{
    const QTimeZone rome("Europe/Rome");
    if (!rome.isValid()) {
        QSKIP("No time zone data for Europe/Rome");
    }
    const QLocale locale = QLocale::c();
    auto work = [](qint64 startUtcMs) {
        SessionRecord r;
        r.kind = SessionKind::Work;
        r.startedAtMs = startUtcMs;
        r.durationSec = 1500;
        r.plannedSec = 1500;
        r.completed = true;
        return r;
    };
    auto utc = [](int y, int m, int d, int h, int min) {
        return QDateTime(QDate(y, m, d), QTime(h, min), QTimeZone::utc()).toMSecsSinceEpoch();
    };

    // 25 October 2026: 03:00 CEST becomes 02:00 CET, so 02:00-03:00 happens twice. Each one is
    // its own bar (labelled 2 both times), and no bar holds two hours of work.
    {
        const QDate day(2026, 10, 25);
        const QList<SessionRecord> records = {work(utc(2026, 10, 25, 0, 30)), work(utc(2026, 10, 25, 1, 30))};
        const StatsSummary s = Stats::summarize(records, StatsPeriod::Day, day, Qt::Monday, rome, locale,
                                                QDateTime(day, QTime(12, 0), rome));
        QCOMPARE(s.buckets.size(), 25);
        QCOMPARE(s.buckets[2].label, QStringLiteral("2"));
        QCOMPARE(s.buckets[3].label, QStringLiteral("2"));
        QCOMPARE(s.buckets[4].label, QStringLiteral("3"));
        QCOMPARE(s.buckets[24].label, QStringLiteral("23"));
        QCOMPARE(s.buckets[2].workSeconds, 1500);
        QCOMPARE(s.buckets[3].workSeconds, 1500);
        QCOMPARE(s.buckets[2].endHour, 2);
        QCOMPARE(s.buckets[12 + 1].isCurrent, true); // 12:00 CET is the 14th real hour of the day
    }

    // 29 March 2026: 02:00 CET becomes 03:00 CEST; there is no 2 o'clock bar.
    {
        const QDate day(2026, 3, 29);
        const QList<SessionRecord> records = {work(utc(2026, 3, 29, 1, 30))}; // 03:30 CEST
        const StatsSummary s = Stats::summarize(records, StatsPeriod::Day, day, Qt::Monday, rome, locale,
                                                QDateTime(day, QTime(23, 0), rome));
        QCOMPARE(s.buckets.size(), 23);
        QCOMPARE(s.buckets[1].label, QStringLiteral("1"));
        QCOMPARE(s.buckets[1].endHour, 3);
        QCOMPARE(s.buckets[2].label, QStringLiteral("3"));
        QCOMPARE(s.buckets[2].workSeconds, 1500);
    }

    // An ordinary day keeps its 24 bars, labelled 0..23.
    {
        const QDate day(2026, 10, 3);
        const StatsSummary s = Stats::summarize({}, StatsPeriod::Day, day, Qt::Monday, rome, locale,
                                                QDateTime(day, QTime(12, 0), rome));
        QCOMPARE(s.buckets.size(), 24);
        for (int i = 0; i < 24; ++i) {
            QCOMPARE(s.buckets[i].hour, i);
            QCOMPARE(s.buckets[i].endHour, (i + 1) % 24);
        }
    }
}

void TestStatsAggregator::testCategoriesIgnoreSurroundingWhitespace()
{
    const QTimeZone tz = QTimeZone::utc();
    const QDate day(2026, 10, 3);
    QList<SessionRecord> records;
    for (const QString &category : {QStringLiteral("Study"), QStringLiteral("Study "), QStringLiteral("\tStudy\n")}) {
        SessionRecord r;
        r.kind = SessionKind::Work;
        r.startedAtMs = QDateTime(day, QTime(9, 0), tz).toMSecsSinceEpoch();
        r.durationSec = 600;
        r.completed = true;
        r.category = category;
        records.append(r);
    }
    const StatsSummary s = Stats::summarize(records, StatsPeriod::Day, day, Qt::Monday, tz, QLocale::c(),
                                            QDateTime(day, QTime(12, 0), tz));
    QCOMPARE(s.byCategory.size(), 1);
    QCOMPARE(s.byCategory.first().name, QStringLiteral("Study"));
    QCOMPARE(s.byCategory.first().workSeconds, 1800);
    QCOMPARE(s.byCategory.first().sessions, 3);
}

void TestStatsAggregator::testBreakdownOrder()
{
    const QTimeZone tz = QTimeZone::utc();
    const QLocale locale = QLocale::c();
    const QDate anchor(2026, 10, 3);
    const QDateTime now(anchor, QTime(15, 0), tz);

    SessionRecord r1;
    r1.kind = SessionKind::Work;
    r1.startedAtMs = QDateTime(anchor, QTime(9, 0), tz).toMSecsSinceEpoch();
    r1.durationSec = 500;
    r1.completed = true;
    r1.presetName = QStringLiteral("Timer A");

    SessionRecord r2;
    r2.kind = SessionKind::Work;
    r2.startedAtMs = QDateTime(anchor, QTime(10, 0), tz).toMSecsSinceEpoch();
    r2.durationSec = 1500;
    r2.completed = true;
    r2.presetName = QStringLiteral("Timer B");

    const StatsSummary summary = Stats::summarize({r1, r2}, StatsPeriod::Day, anchor, Qt::Monday, tz, locale, now);
    QCOMPARE(summary.byTimer.size(), 2);
    // Largest work time first
    QCOMPARE(summary.byTimer.at(0).name, QStringLiteral("Timer B"));
    QCOMPARE(summary.byTimer.at(0).workSeconds, 1500);
    QCOMPARE(summary.byTimer.at(1).name, QStringLiteral("Timer A"));
    QCOMPARE(summary.byTimer.at(1).workSeconds, 500);
}

void TestStatsAggregator::testTimerBreakdownGroupsByPreset()
{
    const QTimeZone tz = QTimeZone::utc();
    const QDate anchor(2026, 10, 3);
    const QDateTime now(anchor, QTime(23, 0), tz);
    auto work = [&](int hour, const QString &uuid, const QString &name, int seconds) {
        SessionRecord r;
        r.kind = SessionKind::Work;
        r.startedAtMs = QDateTime(anchor, QTime(hour, 0), tz).toMSecsSinceEpoch();
        r.durationSec = seconds;
        r.completed = true;
        r.presetUuid = uuid;
        r.presetName = name;
        return r;
    };

    const QList<SessionRecord> records = {
        // Renamed between sessions: one entry, under the latest name (whatever the input order).
        work(10, QStringLiteral("u1"), QStringLiteral("Renamed"), 600),
        work(9, QStringLiteral("u1"), QStringLiteral("Original"), 600),
        // Same name, different timers: two entries.
        work(11, QStringLiteral("u2"), QStringLiteral("Study"), 300),
        work(12, QStringLiteral("u3"), QStringLiteral("Study"), 200),
        // Deleted timers (no uuid any more) fall back to the name.
        work(13, QString(), QStringLiteral("Gone"), 100),
        work(14, QString(), QStringLiteral("Gone"), 50),
    };
    const StatsSummary summary = Stats::summarize(records, StatsPeriod::Day, anchor, Qt::Monday, tz, QLocale::c(), now);
    QCOMPARE(summary.byTimer.size(), 4);
    QCOMPARE(summary.byTimer.at(0).name, QStringLiteral("Renamed"));
    QCOMPARE(summary.byTimer.at(0).workSeconds, 1200);
    QCOMPARE(summary.byTimer.at(0).sessions, 2);
    QCOMPARE(summary.byTimer.at(1).name, QStringLiteral("Study"));
    QCOMPARE(summary.byTimer.at(1).workSeconds, 300);
    QCOMPARE(summary.byTimer.at(2).name, QStringLiteral("Study"));
    QCOMPARE(summary.byTimer.at(2).workSeconds, 200);
    QCOMPARE(summary.byTimer.at(3).name, QStringLiteral("Gone"));
    QCOMPARE(summary.byTimer.at(3).workSeconds, 150);
}

void TestStatsAggregator::testTaskBreakdown()
{
    const QTimeZone tz = QTimeZone::utc();
    const QLocale locale = QLocale::c();
    const QDate anchor(2026, 10, 3);
    const QDateTime now(anchor, QTime(15, 0), tz);

    SessionRecord r1;
    r1.kind = SessionKind::Work;
    r1.startedAtMs = QDateTime(anchor, QTime(9, 0), tz).toMSecsSinceEpoch();
    r1.durationSec = 500;
    r1.completed = true;
    r1.note = QStringLiteral("Fix bug #123");

    SessionRecord r2;
    r2.kind = SessionKind::Work;
    r2.startedAtMs = QDateTime(anchor, QTime(10, 0), tz).toMSecsSinceEpoch();
    r2.durationSec = 1500;
    r2.completed = true;
    r2.note = QStringLiteral("Feature development");

    SessionRecord r3;
    r3.kind = SessionKind::Work;
    r3.startedAtMs = QDateTime(anchor, QTime(11, 0), tz).toMSecsSinceEpoch();
    r3.durationSec = 300;
    r3.completed = false;
    r3.note = QStringLiteral("   ");

    const StatsSummary summary = Stats::summarize({r1, r2, r3}, StatsPeriod::Day, anchor, Qt::Monday, tz, locale, now);
    QCOMPARE(summary.byTask.size(), 3);
    QCOMPARE(summary.byTask.at(0).name, QStringLiteral("Feature development"));
    QCOMPARE(summary.byTask.at(0).workSeconds, 1500);
    QCOMPARE(summary.byTask.at(0).sessions, 1);

    QCOMPARE(summary.byTask.at(1).name, QStringLiteral("Fix bug #123"));
    QCOMPARE(summary.byTask.at(1).workSeconds, 500);
    QCOMPARE(summary.byTask.at(1).sessions, 1);

    QCOMPARE(summary.byTask.at(2).name, QStringLiteral(""));
    QCOMPARE(summary.byTask.at(2).workSeconds, 300);
    QCOMPARE(summary.byTask.at(2).sessions, 0);
}

void TestStatsAggregator::testStreaks()
{
    const QDate today(2026, 10, 3);

    QSet<QDate> days;
    auto addDay = [&](const QDate &d) { days.insert(d); };

    addDay(today);
    addDay(today.addDays(-1));
    addDay(today.addDays(-2));
    // Gap on today - 3
    addDay(today.addDays(-4));
    addDay(today.addDays(-5));
    addDay(today.addDays(-6));
    addDay(today.addDays(-7));

    QCOMPARE(Stats::currentStreak(days, today), 3);
    QCOMPARE(Stats::bestStreak(days), 4);

    // If today has no sessions yet, streak continues from yesterday
    const QDate tomorrow = today.addDays(1);
    QCOMPARE(Stats::currentStreak(days, tomorrow), 3);
}

void TestStatsAggregator::testWeekendStreakProtection()
{
    const QDate friday(2026, 10, 2);
    const QDate sunday(2026, 10, 4);
    const QDate monday(2026, 10, 5);
    const QDate tuesday(2026, 10, 6);

    const QSet<QDate> days = {friday, monday};

    // On Monday (has session): streak is 2 if protected, 1 if not
    QCOMPARE(Stats::currentStreak(days, monday, true), 2);
    QCOMPARE(Stats::currentStreak(days, monday, false), 1);

    // On Tuesday (no session yet): continues from Monday -> 2 if protected, 1 if not
    QCOMPARE(Stats::currentStreak(days, tuesday, true), 2);
    QCOMPARE(Stats::currentStreak(days, tuesday, false), 1);

    // On Sunday (no session): if protected, continues from Friday -> 1
    QCOMPARE(Stats::currentStreak(days, sunday, true), 1);
    QCOMPARE(Stats::currentStreak(days, sunday, false), 0);

    // Best streak:
    QCOMPARE(Stats::bestStreak(days, true), 2);
    QCOMPARE(Stats::bestStreak(days, false), 1);
}

QTEST_MAIN(TestStatsAggregator)
#include "tst_statsaggregator.moc"
