// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QLocale>
#include <QSet>
#include <QTimeZone>

#include "SessionRepository.h"
#include "StatsTypes.h"

/// Turning stored sessions into numbers for the statistics page. Pure functions: no database,
/// no clock and no locale of their own, so everything is deterministic and testable. All
/// calendar decisions (which day a session belongs to, where a week starts) use the time zone
/// and locale that are passed in.
namespace Stats
{
/// The calendar period of `kind` that contains `anchor`.
PeriodRange rangeFor(StatsPeriod kind, const QDate &anchor, Qt::DayOfWeek firstDayOfWeek, const QTimeZone &timeZone);

/// `anchor` moved by whole periods. Months and years clamp the day (31 Jan + 1 month = 28/29 Feb).
QDate shifted(StatsPeriod kind, const QDate &anchor, int steps);

/**
 * Totals, chart buckets and breakdowns of `records` within the period around `anchor`.
 *
 * - A session belongs to the bucket in which it *started* (in local time), even if it ends later.
 * - Work time counts the active time (pauses excluded), including interrupted phases unless
 *   `includeInterrupted` is false, in which case those are ignored completely.
 * - `completedWorkSessions` counts finished work phases only; an interrupted one is not a pomodoro.
 * - Records outside the period are ignored, so callers may pass a larger set.
 * - `now` marks the current bucket.
 */
StatsSummary summarize(const QList<SessionRecord> &records,
                       StatsPeriod kind,
                       const QDate &anchor,
                       Qt::DayOfWeek firstDayOfWeek,
                       const QTimeZone &timeZone,
                       const QLocale &locale,
                       bool includeInterrupted,
                       const QDateTime &now);

/// The local days on which a session started, from start instants in Unix milliseconds.
QSet<QDate> daysOf(const QList<qint64> &startMs, const QTimeZone &timeZone);

/// Consecutive days with activity ending today. A day that has not had any yet does not break
/// the streak: it continues from yesterday until the day is over.
/// If `protectWeekends` is true, Saturdays and Sundays without activity do not break an active streak.
int currentStreak(const QSet<QDate> &days, const QDate &today, bool protectWeekends = true);

/// The longest run of consecutive days with activity, ever.
int bestStreak(const QSet<QDate> &days, bool protectWeekends = true);
} // namespace Stats
