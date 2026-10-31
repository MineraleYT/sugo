// SPDX-License-Identifier: GPL-3.0-or-later
#include "StatsAggregator.h"

#include <QHash>

#include <algorithm>

namespace Stats
{
namespace
{
constexpr qint64 kHourMs = 3600 * 1000;

QDate startOfWeek(const QDate &date, Qt::DayOfWeek firstDayOfWeek)
{
    const int back = (date.dayOfWeek() - int(firstDayOfWeek) + 7) % 7;
    return date.addDays(-back);
}

QList<StatsBucket> makeBuckets(StatsPeriod kind, const PeriodRange &range, const QLocale &locale, const QTimeZone &timeZone)
{
    QList<StatsBucket> buckets;
    switch (kind) {
    case StatsPeriod::Day:
        // One bar per real hour from midnight to midnight, labelled with the clock hour.
        for (qint64 startMs = range.startMs; startMs < range.endMs; startMs += kHourMs) {
            StatsBucket b;
            b.hour = QDateTime::fromMSecsSinceEpoch(startMs, timeZone).time().hour();
            b.endHour = QDateTime::fromMSecsSinceEpoch(std::min(startMs + kHourMs, range.endMs), timeZone).time().hour();
            b.date = range.firstDay;
            b.label = QString::number(b.hour);
            buckets.append(b);
        }
        break;
    case StatsPeriod::Week:
    case StatsPeriod::Month:
        for (QDate day = range.firstDay; day <= range.lastDay; day = day.addDays(1)) {
            StatsBucket b;
            b.date = day;
            b.label = kind == StatsPeriod::Week ? locale.dayName(day.dayOfWeek(), QLocale::ShortFormat)
                                                : QString::number(day.day());
            buckets.append(b);
        }
        break;
    case StatsPeriod::Year:
        for (int month = 1; month <= 12; ++month) {
            StatsBucket b;
            b.date = QDate(range.firstDay.year(), month, 1);
            b.label = locale.standaloneMonthName(month, QLocale::ShortFormat);
            buckets.append(b);
        }
        break;
    }
    return buckets;
}

int bucketIndex(StatsPeriod kind, const PeriodRange &range, const QDateTime &local)
{
    switch (kind) {
    case StatsPeriod::Day:
        return int((local.toMSecsSinceEpoch() - range.startMs) / kHourMs);
    case StatsPeriod::Week:
    case StatsPeriod::Month:
        return int(range.firstDay.daysTo(local.date()));
    case StatsPeriod::Year:
        return local.date().month() - 1;
    }
    Q_UNREACHABLE_RETURN(-1);
}

void sortLargestFirst(QList<BreakdownEntry> &entries)
{
    std::sort(entries.begin(), entries.end(), [](const BreakdownEntry &a, const BreakdownEntry &b) {
        return a.workSeconds != b.workSeconds ? a.workSeconds > b.workSeconds : a.name < b.name;
    });
}

QList<BreakdownEntry> toList(const QHash<QString, BreakdownEntry> &hash)
{
    QList<BreakdownEntry> list = hash.values();
    sortLargestFirst(list);
    return list;
}
} // namespace

PeriodRange rangeFor(StatsPeriod kind, const QDate &anchor, Qt::DayOfWeek firstDayOfWeek, const QTimeZone &timeZone)
{
    PeriodRange range;
    switch (kind) {
    case StatsPeriod::Day:
        range.firstDay = range.lastDay = anchor;
        break;
    case StatsPeriod::Week:
        range.firstDay = startOfWeek(anchor, firstDayOfWeek);
        range.lastDay = range.firstDay.addDays(6);
        break;
    case StatsPeriod::Month:
        range.firstDay = QDate(anchor.year(), anchor.month(), 1);
        range.lastDay = range.firstDay.addMonths(1).addDays(-1);
        break;
    case StatsPeriod::Year:
        range.firstDay = QDate(anchor.year(), 1, 1);
        range.lastDay = QDate(anchor.year(), 12, 31);
        break;
    }
    // startOfDay() copes with days that do not begin at 00:00 (DST changes at midnight).
    range.startMs = range.firstDay.startOfDay(timeZone).toMSecsSinceEpoch();
    range.endMs = range.lastDay.addDays(1).startOfDay(timeZone).toMSecsSinceEpoch();
    return range;
}

QDate shifted(StatsPeriod kind, const QDate &anchor, int steps)
{
    switch (kind) {
    case StatsPeriod::Day:
        return anchor.addDays(steps);
    case StatsPeriod::Week:
        return anchor.addDays(7 * steps);
    case StatsPeriod::Month:
        return anchor.addMonths(steps);
    case StatsPeriod::Year:
        return anchor.addYears(steps);
    }
    Q_UNREACHABLE_RETURN(anchor);
}

StatsSummary summarize(const QList<SessionRecord> &records,
                       StatsPeriod kind,
                       const QDate &anchor,
                       Qt::DayOfWeek firstDayOfWeek,
                       const QTimeZone &timeZone,
                       const QLocale &locale,
                       bool includeInterrupted,
                       const QDateTime &now)
{
    const PeriodRange range = rangeFor(kind, anchor, firstDayOfWeek, timeZone);

    StatsSummary summary;
    summary.buckets = makeBuckets(kind, range, locale, timeZone);

    // Mark the bucket that contains "now", if "now" falls in this period.
    const QDateTime localNow = now.toTimeZone(timeZone);
    if (localNow.toMSecsSinceEpoch() >= range.startMs && localNow.toMSecsSinceEpoch() < range.endMs) {
        const int index = bucketIndex(kind, range, localNow);
        if (index >= 0 && index < summary.buckets.size()) {
            summary.buckets[index].isCurrent = true;
        }
    }

    // Timers are keyed by preset uuid, so a renamed timer stays one entry and two timers with
    // the same name stay apart; the entry shows the name of its latest session. Sessions whose
    // preset was deleted (no uuid any more) fall back to their name snapshot.
    QHash<QString, BreakdownEntry> byTimer;
    QHash<QString, qint64> timerNameAt;
    QHash<QString, BreakdownEntry> byCategory;
    QHash<QString, BreakdownEntry> byTask;

    for (const SessionRecord &record : records) {
        if (record.startedAtMs < range.startMs || record.startedAtMs >= range.endMs) {
            continue;
        }
        if (!record.completed && !includeInterrupted) {
            continue;
        }

        const QDateTime local = QDateTime::fromMSecsSinceEpoch(record.startedAtMs, timeZone);
        const int index = bucketIndex(kind, range, local);
        if (index < 0 || index >= summary.buckets.size()) {
            continue; // cannot happen inside the range; never index out of bounds anyway
        }
        StatsBucket &bucket = summary.buckets[index];
        const qint64 seconds = record.durationSec;

        if (record.kind == SessionKind::Work) {
            bucket.workSeconds += seconds;
            summary.workSeconds += seconds;
            if (record.completed) {
                ++summary.completedWorkSessions;
            } else {
                ++summary.interruptedWorkSessions;
            }

            const QString timerKey = record.presetUuid.isEmpty() ? QStringLiteral("name:") + record.presetName
                                                                 : QStringLiteral("uuid:") + record.presetUuid;
            BreakdownEntry &timer = byTimer[timerKey];
            const auto nameAt = timerNameAt.constFind(timerKey);
            if (nameAt == timerNameAt.cend() || record.startedAtMs >= nameAt.value()) {
                timer.name = record.presetName;
                timerNameAt.insert(timerKey, record.startedAtMs);
            }
            timer.workSeconds += seconds;
            timer.sessions += record.completed ? 1 : 0;

            // Grouped like task notes: surrounding whitespace does not make another category.
            const QString categoryName = record.category.trimmed();
            BreakdownEntry &category = byCategory[categoryName];
            category.name = categoryName;
            category.workSeconds += seconds;
            category.sessions += record.completed ? 1 : 0;

            const QString taskNote = record.note.trimmed();
            BreakdownEntry &task = byTask[taskNote];
            task.name = taskNote;
            task.workSeconds += seconds;
            task.sessions += record.completed ? 1 : 0;
        } else {
            bucket.breakSeconds += seconds;
            summary.breakSeconds += seconds;
        }
    }

    summary.ratio = summary.breakSeconds > 0 ? double(summary.workSeconds) / double(summary.breakSeconds) : -1;
    summary.byTimer = toList(byTimer);
    summary.byCategory = toList(byCategory);
    summary.byTask = toList(byTask);
    return summary;
}

QSet<QDate> daysOf(const QList<qint64> &startMs, const QTimeZone &timeZone)
{
    QSet<QDate> days;
    for (const qint64 ms : startMs) {
        days.insert(QDateTime::fromMSecsSinceEpoch(ms, timeZone).date());
    }
    return days;
}

int currentStreak(const QSet<QDate> &days, const QDate &today, bool protectWeekends)
{
    QDate day = today;
    if (!days.contains(day)) {
        day = day.addDays(-1);
        if (protectWeekends) {
            while (!days.contains(day) && (day.dayOfWeek() == Qt::Saturday || day.dayOfWeek() == Qt::Sunday)) {
                day = day.addDays(-1);
            }
        }
    }

    int streak = 0;
    while (true) {
        if (days.contains(day)) {
            ++streak;
            day = day.addDays(-1);
        } else if (protectWeekends && (day.dayOfWeek() == Qt::Saturday || day.dayOfWeek() == Qt::Sunday)) {
            day = day.addDays(-1);
        } else {
            break;
        }
    }
    return streak;
}

namespace
{
bool onlyWeekendBetween(const QDate &from, const QDate &to)
{
    for (QDate d = from.addDays(1); d < to; d = d.addDays(1)) {
        if (d.dayOfWeek() != Qt::Saturday && d.dayOfWeek() != Qt::Sunday) {
            return false;
        }
    }
    return true;
}
} // namespace

int bestStreak(const QSet<QDate> &days, bool protectWeekends)
{
    if (days.isEmpty()) {
        return 0;
    }
    QList<QDate> sorted = days.values();
    std::sort(sorted.begin(), sorted.end());

    int best = 0;
    int run = 0;
    for (qsizetype i = 0; i < sorted.size(); ++i) {
        if (i == 0) {
            run = 1;
        } else {
            const int gap = int(sorted.at(i - 1).daysTo(sorted.at(i)));
            if (gap == 1) {
                run += 1;
            } else if (protectWeekends && gap > 1 && onlyWeekendBetween(sorted.at(i - 1), sorted.at(i))) {
                run += 1;
            } else {
                run = 1;
            }
        }
        best = std::max(best, run);
    }
    return best;
}
} // namespace Stats
