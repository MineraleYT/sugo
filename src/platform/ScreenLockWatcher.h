// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <memory>

class AppSettings;
#ifdef Q_OS_MACOS
class MacSessionEvents;
#else
#include <QDBusConnection>
#include <QString>
#endif
class TimerEngine;

/**
 * Listens for system screen lock/unlock signals on D-Bus: org.freedesktop.ScreenSaver (Plasma)
 * and org.gnome.ScreenSaver (GNOME).
 * Automatically pauses the active work phase when the screen is locked, and
 * signals when the user returns so they can resume or review their session.
 *
 * If the name that reported the lock loses its owner (the locker crashed or restarted), its
 * unlock will never come: the screen is then taken as unlocked, so the next lock is not dropped
 * as a repeat. The locker going away most likely means the session is usable again, and a locker
 * that comes back locked reports a new lock, which pauses nothing (the phase is already paused).
 *
 * Also pauses whatever phase is running when the computer goes to sleep (logind's
 * PrepareForSleep, on the system bus). The countdown cannot run while the machine sleeps, and
 * the time away must not count as work; after waking up the timer waits, visibly paused, for
 * the user to resume it instead of carrying on by itself.
 *
 * On macOS the default constructor listens through MacSessionEvents instead (the login window's
 * lock notifications and IOKit's system power messages), which call the same slots; the D-Bus
 * constructors exist on the other systems only.
 */
class ScreenLockWatcher : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool pausedByScreenLock READ isPausedByScreenLock NOTIFY pausedByScreenLockChanged FINAL)
    Q_PROPERTY(bool pausedBySleep READ isPausedBySleep NOTIFY pausedBySleepChanged FINAL)
    /// Screen locks can be noticed at all: the session bus is connected on Linux; always on macOS.
    Q_PROPERTY(bool available READ available CONSTANT FINAL)

public:
    /// What the watcher listens to: the system's own lock and sleep events, or nothing (the
    /// slots are then only called directly, as tests do; available() is false).
    enum class Source { System, Nothing };

    explicit ScreenLockWatcher(QObject *parent = nullptr);
    explicit ScreenLockWatcher(Source source, QObject *parent = nullptr);
#ifndef Q_OS_MACOS
    ScreenLockWatcher(const QDBusConnection &bus, QObject *parent = nullptr);
    /// `systemBus` carries logind's sleep signal.
    ScreenLockWatcher(const QDBusConnection &bus, const QDBusConnection &systemBus, QObject *parent = nullptr);
#endif
    ~ScreenLockWatcher() override;

    void attach(TimerEngine *engine, AppSettings *settings);

    bool isPausedByScreenLock() const { return m_pausedByScreenLock; }
    bool isPausedBySleep() const { return m_pausedBySleep; }
    bool available() const;

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

#ifndef Q_OS_MACOS
private Q_SLOTS:
    // ActiveChanged from the owner of each name, which is remembered with the lock.
    void onFreedesktopActiveChanged(bool active);
    void onGnomeActiveChanged(bool active);
#endif

private:
    void setPausedBySleep(bool paused);
    /// `source`: the D-Bus name that reported the change, or empty (macOS, direct calls).
    void setScreenLocked(bool locked, const QString &source);

#ifndef Q_OS_MACOS
    void onLockerGone(const QString &name);

    QDBusConnection m_bus;
    QDBusConnection m_systemBus;
    QString m_lockSource; // the name whose owner reported the current lock
#endif
    TimerEngine *m_engine = nullptr;
    AppSettings *m_settings = nullptr;
    bool m_pausedByScreenLock = false;
    bool m_pausedBySleep = false;
    bool m_screenLocked = false; // last state seen, to ignore duplicate signals
#ifdef Q_OS_MACOS
    std::unique_ptr<MacSessionEvents> m_macEvents;
#endif
};
