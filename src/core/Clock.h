// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QElapsedTimer>

/**
 * Time source for the TimerEngine. Injectable so tests can drive time by hand.
 *
 * Durations are always computed from monotonicMs(); wallTime() is only used to
 * label recorded sessions with a start/end timestamp.
 */
class Clock
{
public:
    virtual ~Clock() = default;

    /// Milliseconds on a monotonic clock. Unaffected by wall-clock adjustments.
    virtual qint64 monotonicMs() const = 0;

    virtual QDateTime wallTime() const
    {
        return QDateTime::currentDateTimeUtc();
    }
};

/// Production clock backed by QElapsedTimer (CLOCK_MONOTONIC on Linux).
class SystemClock final : public Clock
{
public:
    SystemClock()
    {
        m_timer.start();
    }

    qint64 monotonicMs() const override
    {
        return m_timer.elapsed();
    }

private:
    QElapsedTimer m_timer;
};
