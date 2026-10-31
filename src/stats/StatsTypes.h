// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDate>
#include <QList>
#include <QString>

enum class StatsPeriod {
    Day,
    Week,
    Month,
    Year,
};

/// A calendar period in local time. The days are inclusive; the instants are [start, end).
struct PeriodRange {
    QDate firstDay;
    QDate lastDay;
    qint64 startMs = 0; ///< Unix milliseconds, UTC.
    qint64 endMs = 0;
};

/// One bar of the chart: an hour (day view), a day (week and month views) or a month (year view).
///
/// The day view has one bar per real hour of the day: 23 or 25 on the days the clocks change,
/// where an hour is skipped or happens twice (two bars with the same label).
struct StatsBucket {
    QString label;      ///< Short axis label, localized ("Mon", "5", "Oct", "14").
    QDate date;         ///< The day, or the first day of the month; invalid for hours.
    int hour = -1;      ///< Clock hour at the start of the bar (0..23) in the day view, else -1.
    int endHour = -1;   ///< Clock hour at its end in the day view (3 for 1:00-3:00 when the clocks go forward), else -1.
    qint64 workSeconds = 0;
    qint64 breakSeconds = 0;
    bool isCurrent = false; ///< Contains "now": today, this month, this hour...
};

/// Work time of one timer, category or task. Timer entries group by preset (not by name).
struct BreakdownEntry {
    QString name;       ///< May be empty (no category); the presentation layer words that.
    qint64 workSeconds = 0;
    int sessions = 0;   ///< Completed work sessions.
};

struct StatsSummary {
    qint64 workSeconds = 0;
    qint64 breakSeconds = 0;       ///< Short and long breaks together.
    int completedWorkSessions = 0;
    int interruptedWorkSessions = 0; ///< Only counted while interrupted phases are included.
    double ratio = -1;             ///< work / breaks; -1 when there were no breaks.
    QList<StatsBucket> buckets;
    QList<BreakdownEntry> byTimer;    ///< Largest first.
    QList<BreakdownEntry> byCategory; ///< Largest first.
    QList<BreakdownEntry> byTask;     ///< Largest first.

    bool hasData() const { return workSeconds > 0 || breakSeconds > 0; }
};
