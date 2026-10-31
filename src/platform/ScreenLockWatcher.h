// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QtQml/qqmlregistration.h>

class AppSettings;
class TimerEngine;

/**
 * Listens for system screen lock/unlock signals via FreeDesktop ScreenSaver D-Bus.
 * Automatically pauses the active work phase when the screen is locked, and
 * signals when the user returns so they can resume or review their session.
 *
 * Also pauses whatever phase is running when the computer goes to sleep (logind's
 * PrepareForSleep, on the system bus). The countdown cannot run while the machine sleeps, and
 * the time away must not count as work; after waking up the timer waits, visibly paused, for
 * the user to resume it instead of carrying on by itself.
 */
class ScreenLockWatcher : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool pausedByScreenLock READ isPausedByScreenLock NOTIFY pausedByScreenLockChanged FINAL)
    Q_PROPERTY(bool pausedBySleep READ isPausedBySleep NOTIFY pausedBySleepChanged FINAL)

public:
    explicit ScreenLockWatcher(QObject *parent = nullptr);
    ScreenLockWatcher(const QDBusConnection &bus, QObject *parent = nullptr);
    /// `systemBus` carries logind's sleep signal.
    ScreenLockWatcher(const QDBusConnection &bus, const QDBusConnection &systemBus, QObject *parent = nullptr);
    ~ScreenLockWatcher() override;

    void attach(TimerEngine *engine, AppSettings *settings);

    bool isPausedByScreenLock() const { return m_pausedByScreenLock; }
    bool isPausedBySleep() const { return m_pausedBySleep; }

    Q_INVOKABLE void clearLockPauseNotice();

public Q_SLOTS:
    void onActiveChanged(bool active);
    void onPrepareForSleep(bool start);

Q_SIGNALS:
    void pausedByScreenLockChanged();
    void screenUnlockedAfterPause();
    void pausedBySleepChanged();
    /// The computer woke up while the phase it paused is still paused.
    void wokeUpAfterPause();

private:
    void setPausedBySleep(bool paused);

    QDBusConnection m_bus;
    QDBusConnection m_systemBus;
    TimerEngine *m_engine = nullptr;
    AppSettings *m_settings = nullptr;
    bool m_pausedByScreenLock = false;
    bool m_pausedBySleep = false;
    bool m_screenLocked = false; // last state seen, to ignore duplicate signals
};
