// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>

#include <algorithm>

/**
 * The Retry-After header of an HTTP answer, as Sugo Sync (SyncApi) and the calendar
 * (CalendarSync) take it.
 */
namespace RetryAfter
{
/// The longest wait kept, in seconds: more would overflow the retry timers' milliseconds and
/// make the retry fire at once.
constexpr int MaxSeconds = 3600;

/// Seconds to wait, at most MaxSeconds; 0 when there is none, or for an HTTP date or anything
/// unreadable (the caller's own delay then).
inline int seconds(const QByteArray &header)
{
    return std::clamp(header.trimmed().toInt(), 0, MaxSeconds);
}
} // namespace RetryAfter
