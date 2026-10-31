// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

#include <functional>

/**
 * Silences the desktop's notifications on request ("Do Not Disturb").
 *
 * Implementations must be idempotent: calling inhibit() twice holds one inhibition,
 * and release() with nothing held does nothing. Calls may complete asynchronously;
 * isInhibited() stays true from the first inhibit() until the release is confirmed,
 * so "not inhibited" always means "notifications are back".
 *
 * A safe implementation never outlives its process: the platform must drop the
 * inhibition when the application exits or crashes (verified for Plasma 6, where the
 * notification server ties it to the client's D-Bus connection).
 */
class NotificationInhibitor : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    /// False for the no-op fallback: silencing is requested but cannot be honoured.
    virtual bool isAvailable() const = 0;

    /// Starts (or keeps) the inhibition. `reason` may be shown to the user by the desktop.
    virtual void inhibit(const QString &reason) = 0;

    /// Ends the inhibition. Safe to call at any time, including while inhibit() is in flight.
    virtual void release() = 0;

    /// True while an inhibition is held or an acquire/release is still in flight.
    virtual bool isInhibited() const = 0;

    /// True while the silencing is being taken down (released, or about to be). False when it is
    /// wanted, since waiting for a release that is not coming would only delay the caller.
    virtual bool isReleasing() const { return isInhibited(); }

    /// Runs `callback` as soon as nothing is inhibited, or after `timeoutMs` at the latest.
    /// Immediate if already released, or if the silencing is wanted again (nothing to wait for).
    /// Dropped if `context` is destroyed first.
    /// Used to post a notification only after the silencing has really ended.
    void whenReleased(QObject *context, std::function<void()> callback, int timeoutMs = 2000);

    /// Waits in a local event loop until released, up to `timeoutMs`. For shutdown.
    bool waitUntilReleased(int timeoutMs);

Q_SIGNALS:
    void inhibitedChanged();
};
