// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <memory>

#include "Clock.h"

/**
 * Pomodoro state machine.
 *
 * Accuracy: the remaining time is always `deadline - clock.now()`. The internal
 * QTimer only drives UI refreshes and phase-end detection, so a delayed or
 * skipped tick (minimized window, system load) never makes the countdown drift.
 *
 * States and transitions
 * ----------------------
 *   Idle        --start-->            Working | ShortBreak | LongBreak   (the pending phase())
 *   running     --pause-->            Paused          (remembers its phase and remaining time)
 *   Paused      --resume-->           the phase it was paused from
 *   any active  --stop-->             Idle, phase = Work, cycles reset   (session saved as interrupted)
 *   any active  --skip-->             next phase, started immediately    (session saved as interrupted;
 *                                     a skipped Work phase still counts as a cycle)
 *   Idle        --skip-->             Idle, pending phase advanced       (nothing recorded; a pending break
 *                                     becomes a pending Work phase, a pending Work phase becomes a
 *                                     short break without counting a cycle: no work was done)
 *   Working     --time elapsed-->     ShortBreak, or LongBreak once `cyclesBeforeLong` work phases completed
 *   ShortBreak  --time elapsed-->     Working
 *   LongBreak   --time elapsed-->     Working, cycles reset
 *
 * When a phase elapses, the next phase starts by itself if the auto-start mode allows it;
 * otherwise the engine waits in Idle with phase() set to the upcoming phase. An auto-started
 * transition goes straight from one running state to the next (no Idle in between), and
 * phaseFinished() is emitted after the transition, so listeners see the real new state.
 * A long break with a zero duration still ends the set, with a short break in its place. A
 * break with a zero duration is skipped: the upcoming phase is Work right away. Skipping a
 * work phase counts as a cycle just like finishing it, so skipping through a set reaches the
 * long break.
 */
class TimerEngine : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // State and Phase share enumerator names: expose them only as TimerEngine.State.X / .Phase.X.
    Q_CLASSINFO("RegisterEnumClassesUnscoped", "false")

public:
    enum class State {
        Idle,
        Working,
        ShortBreak,
        LongBreak,
        Paused,
    };
    Q_ENUM(State)

    /// Kind of phase. While Idle it is the phase that start() will begin;
    /// while Paused it is the phase that was paused.
    enum class Phase {
        Work,
        ShortBreak,
        LongBreak,
    };
    Q_ENUM(Phase)

    Q_PROPERTY(State state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged FINAL)
    /// True while a phase is running or paused (i.e. not Idle).
    Q_PROPERTY(bool active READ isActive NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool paused READ isPaused NOTIFY stateChanged FINAL)

    Q_PROPERTY(int remainingSeconds READ remainingSeconds NOTIFY remainingChanged FINAL)
    Q_PROPERTY(int totalSeconds READ totalSeconds NOTIFY totalChanged FINAL)
    /// Elapsed fraction of the current phase, 0..1.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged FINAL)
    /// Work phases completed in the current set (towards the long break).
    Q_PROPERTY(int completedCycles READ completedCycles NOTIFY cyclesChanged FINAL)

    Q_PROPERTY(int workSeconds READ workSeconds WRITE setWorkSeconds NOTIFY workSecondsChanged FINAL)
    Q_PROPERTY(int shortBreakSeconds READ shortBreakSeconds WRITE setShortBreakSeconds NOTIFY shortBreakSecondsChanged FINAL)
    Q_PROPERTY(int longBreakSeconds READ longBreakSeconds WRITE setLongBreakSeconds NOTIFY longBreakSecondsChanged FINAL)
    enum class AutoStartMode {
        Manual = 0,
        BreaksOnly = 1,
        All = 2
    };
    Q_ENUM(AutoStartMode)

    Q_PROPERTY(int cyclesBeforeLong READ cyclesBeforeLong WRITE setCyclesBeforeLong NOTIFY cyclesBeforeLongChanged FINAL)

    /// A phase cut short before this much time ran is an accidental start: it is not recorded
    /// (SessionRecorder) and a Work phase skipped that early does not count a cycle.
    static constexpr qint64 kMinInterruptedMs = 5000;

    /// Uses the real monotonic clock. This is the constructor QML uses.
    explicit TimerEngine(QObject *parent = nullptr);
    /// Uses an externally owned clock (tests).
    TimerEngine(Clock *clock, QObject *parent);

    State state() const { return m_state; }
    Phase phase() const { return m_phase; }
    bool isActive() const { return m_state != State::Idle; }
    bool isRunning() const;
    bool isPaused() const { return m_state == State::Paused; }
    bool isIdle() const { return m_state == State::Idle; }

    int remainingSeconds() const;
    int totalSeconds() const;
    double progress() const;
    int completedCycles() const { return m_completedCycles; }

    int workSeconds() const { return m_workSeconds; }
    void setWorkSeconds(int seconds);
    int shortBreakSeconds() const { return m_shortBreakSeconds; }
    void setShortBreakSeconds(int seconds);
    int longBreakSeconds() const { return m_longBreakSeconds; }
    void setLongBreakSeconds(int seconds);
    int cyclesBeforeLong() const { return m_cyclesBeforeLong; }
    void setCyclesBeforeLong(int cycles);
    AutoStartMode autoStartMode() const { return m_autoStartMode; }
    void setAutoStartMode(AutoStartMode mode);
    void setAutoStartModeInt(int mode);

public Q_SLOTS:
    void start();
    void pause();
    void resume();
    /// Idle -> start, running -> pause, Paused -> resume.
    void toggle();
    void stop();
    void skip();
    /// Runs one Work phase of `seconds` (at least 1) instead of the preset's work length.
    /// An active phase is ended first (saved as interrupted). Later phases use the normal durations.
    Q_INVOKABLE void startCustomWork(int seconds);
    /// Back to a fresh Work phase with the cycle count cleared (e.g. after switching preset).
    /// An active phase is stopped first (saved as interrupted).
    void reset();

    /// Recomputes everything from the clock and ends the phase if due.
    /// Called by the internal ticker; also callable directly (tests).
    void refresh();

Q_SIGNALS:
    void stateChanged();
    void phaseChanged();
    void remainingChanged();
    void totalChanged();
    void progressChanged();
    void cyclesChanged();
    void workSecondsChanged();
    void shortBreakSecondsChanged();
    void longBreakSecondsChanged();
    void cyclesBeforeLongChanged();
    void oneMinuteRemaining(TimerEngine::Phase phase);

    /// A phase ended, either elapsed (completed) or cut short by stop/skip.
    /// `activeMs` excludes time spent paused; `endedAt - startedAt` does not.
    void sessionEnded(TimerEngine::Phase phase,
                      const QDateTime &startedAt,
                      const QDateTime &endedAt,
                      qint64 activeMs,
                      qint64 plannedMs,
                      bool completed);

    /// A phase began (start, auto-start or skip). Not emitted on resume.
    void phaseStarted(TimerEngine::Phase phase);

    /// A phase elapsed naturally. `next` is the upcoming phase. Use for notifications.
    /// Emitted after the transition: if `next` was auto-started, isRunning() is already true.
    void phaseFinished(TimerEngine::Phase finished, TimerEngine::Phase next);

private:
    qint64 durationMs(Phase phase) const;
    qint64 remainingMs() const;
    static State stateFor(Phase phase);

    /// `plannedMs` < 0 uses the configured duration of the phase.
    void startPhase(Phase phase, qint64 plannedMs = -1);
    bool shouldAutoStart(Phase next) const;
    void setIdle(Phase next);
    void completePhase();
    /// `endMonoMs`: when the phase ended, on the monotonic clock.
    void emitSessionEnded(bool completed, qint64 endMonoMs);
    /// Advances the cycle count when a Work phase ends (finished or skipped) and returns what follows.
    /// A zero long break becomes a short break (still ending the set); a zero break is passed over.
    Phase nextPhaseAfter(Phase finished);

    void setState(State state);
    /// Completes a running phase whose time is already up (before the ticker noticed); true if it did.
    bool finishIfDue();
    void setPhase(Phase phase);
    void setCycles(int cycles);
    void notifyTime(bool force);
    void refreshIdleDisplay();

    std::unique_ptr<Clock> m_ownedClock;
    Clock *m_clock = nullptr;
    QTimer m_ticker;

    State m_state = State::Idle;
    Phase m_phase = Phase::Work;
    int m_completedCycles = 0;

    // Active phase bookkeeping.
    qint64 m_plannedMs = 0;
    qint64 m_deadlineMs = 0;          // monotonic; valid while running
    qint64 m_remainingAtPauseMs = 0;  // valid while paused
    QDateTime m_startWall;
    qint64 m_startMonoMs = 0;
    int m_lastRemainingSeconds = -1;

    // Configuration.
    int m_workSeconds = 25 * 60;
    int m_shortBreakSeconds = 5 * 60;
    int m_longBreakSeconds = 15 * 60;
    int m_cyclesBeforeLong = 4;
    AutoStartMode m_autoStartMode = AutoStartMode::Manual;
    bool m_warnedOneMinute = false;
    bool m_completing = false; ///< sessionEnded of a completed phase is being emitted
};
