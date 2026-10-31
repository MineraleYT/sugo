// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QDate>
#include <QHash>
#include <QList>
#include <QLocale>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QTimeZone>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

#include "SessionRepository.h"
#include "StatsAggregator.h"
#include "StatsTypes.h"

class SessionRecorder;

/**
 * Presentation model for the statistics page.
 *
 * Provides aggregated session data across Day, Week, Month, and Year periods,
 * grouped bar chart data (work vs. breaks), category/timer breakdowns,
 * and streak tracking.
 *
 * By default it follows the system: the time zone, the default QLocale and the first day of
 * the week are re-read by checkDateChange()
 * and when the date changes. "Today" is kept current by a timer armed for the next local
 * midnight, backed by a coarse periodic check that also catches suspend and clock changes.
 * Tests inject a fixed Environment with setEnvironment().
 */
class StatsModel : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    enum class Period {
        Day,
        Week,
        Month,
        Year,
    };
    Q_ENUM(Period)

    enum class BreakdownMode {
        ByTimer,
        ByCategory,
        ByTask,
    };
    Q_ENUM(BreakdownMode)

    struct Environment {
        std::function<QDateTime()> now = []() { return QDateTime::currentDateTime(); };
        QTimeZone timeZone = QTimeZone::systemTimeZone();
        QLocale locale = QLocale();
        Qt::DayOfWeek firstDayOfWeek = QLocale().firstDayOfWeek();
        /// If true, the date checks (checkDateChange()) re-read the time zone, locale and first
        /// day of the week from the system (keeping `now`). setEnvironment() honours it.
        bool followSystem = false;
    };

    Q_PROPERTY(Period period READ period WRITE setPeriod NOTIFY periodChanged FINAL)
    Q_PROPERTY(BreakdownMode breakdownMode READ breakdownMode WRITE setBreakdownMode NOTIFY breakdownModeChanged FINAL)

    Q_PROPERTY(QString title READ title NOTIFY periodChanged FINAL)
    Q_PROPERTY(bool isCurrent READ isCurrent NOTIFY periodChanged FINAL)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY periodChanged FINAL)

    Q_PROPERTY(qint64 workSeconds READ workSeconds NOTIFY statsChanged FINAL)
    Q_PROPERTY(qint64 breakSeconds READ breakSeconds NOTIFY statsChanged FINAL)
    /// Work time of the period before, filtered the same way; while the shown period is still
    /// under way, only up to the same point of it (Tuesday 10:00 compares with last Tuesday 10:00).
    Q_PROPERTY(qint64 previousWorkSeconds READ previousWorkSeconds NOTIFY statsChanged FINAL)
    Q_PROPERTY(int completedWorkSessions READ completedWorkSessions NOTIFY statsChanged FINAL)
    Q_PROPERTY(int interruptedWorkSessions READ interruptedWorkSessions NOTIFY statsChanged FINAL)
    Q_PROPERTY(bool hasData READ hasData NOTIFY statsChanged FINAL)

    Q_PROPERTY(int currentStreak READ currentStreak NOTIFY streaksChanged FINAL)
    Q_PROPERTY(int bestStreak READ bestStreak NOTIFY streaksChanged FINAL)

    Q_PROPERTY(bool protectWeekendStreak READ protectWeekendStreak WRITE setProtectWeekendStreak NOTIFY protectWeekendStreakChanged FINAL)

    Q_PROPERTY(QVariantList buckets READ buckets NOTIFY statsChanged FINAL)
    Q_PROPERTY(QVariantList breakdown READ breakdown NOTIFY statsChanged FINAL)
    /// Every day of the shown period, in order:
    /// { date, weekday (0 = Sunday), dayOfMonth, month (1-12), workSeconds, isToday, tooltip }.
    /// Work time counts by the day the session started, like the chart.
    Q_PROPERTY(QVariantList days READ days NOTIFY statsChanged FINAL)

    explicit StatsModel(QObject *parent = nullptr);

    void attach(SessionRepository *repository, SessionRecorder *recorder = nullptr);
    void setEnvironment(const Environment &env);

    /// Re-checks the current date: on a new day it updates "today" and the
    /// streaks, and moves the shown period along if it was showing the old today. Called by the
    /// timers; public for tests.
    void checkDateChange();

    Period period() const { return m_period; }
    void setPeriod(Period period);

    BreakdownMode breakdownMode() const { return m_breakdownMode; }
    void setBreakdownMode(BreakdownMode mode);


    QString title() const;
    bool isCurrent() const;
    bool canGoNext() const;

    qint64 workSeconds() const { return m_summary.workSeconds; }
    qint64 previousWorkSeconds() const { return m_previousWorkSeconds; }
    qint64 breakSeconds() const { return m_summary.breakSeconds; }
    int completedWorkSessions() const { return m_summary.completedWorkSessions; }
    int interruptedWorkSessions() const { return m_summary.interruptedWorkSessions; }
    bool hasData() const { return m_summary.hasData(); }

    int currentStreak() const { return m_currentStreak; }
    int bestStreak() const { return m_bestStreak; }


    bool protectWeekendStreak() const { return m_protectWeekendStreak; }
    void setProtectWeekendStreak(bool protect);

    QVariantList buckets() const { return m_bucketsVariant; }
    QVariantList breakdown() const;
    QVariantList days() const { return m_daysVariant; }

    Q_INVOKABLE void previousPeriod();
    Q_INVOKABLE void nextPeriod();
    Q_INVOKABLE void today();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString formatDuration(qint64 seconds) const;

Q_SIGNALS:
    void periodChanged();
    void breakdownModeChanged();
    void statsChanged();
    void streaksChanged();
    void protectWeekendStreakChanged();

private:
    void recompute();
    void recomputeStreaks();
    void reloadCompletedStarts();
    void loadNewCompletedStarts();
    void rebuildCompletedDays();
    void onSessionRecorded();
    void readSystemEnvironment();
    void armMidnightTimer();
    QDate todayDate() const;
    StatsPeriod toStatsPeriod(Period p) const;

    SessionRepository *m_repository = nullptr;
    Environment m_env;

    Period m_period = Period::Week;
    BreakdownMode m_breakdownMode = BreakdownMode::ByTimer;
    QDate m_anchorDate;

    StatsSummary m_summary;
    qint64 m_previousWorkSeconds = 0;
    QList<SessionRecord> m_currentRecords;
    QVariantList m_bucketsVariant;
    QVariantList m_daysVariant;

    int m_currentStreak = 0;
    int m_bestStreak = 0;
    bool m_protectWeekendStreak = true;

    QDate m_today;                       ///< "Today" as of the last check, in m_env.timeZone.
    QTimer m_midnightTimer;              ///< Single shot, armed for the next local midnight.
    QTimer m_dateCheckTimer;             ///< Coarse periodic check (suspend, clock changes).
    QList<qint64> m_completedStarts;     ///< Cache: start instants of completed work sessions.
    qint64 m_completedStartsMaxId = 0;   ///< Highest session id in the cache.
    QHash<QDate, int> m_completedPerDay; ///< Cache: completed work sessions per local day.
};
