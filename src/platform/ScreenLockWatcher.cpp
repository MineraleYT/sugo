// SPDX-License-Identifier: GPL-3.0-or-later
#include "ScreenLockWatcher.h"

#include "AppSettings.h"
#include "TimerEngine.h"

#ifdef Q_OS_MACOS
#include "MacSessionEvents.h"
#endif

#include <QLoggingCategory>

namespace
{
Q_LOGGING_CATEGORY(lcScreenLock, "sugo.platform.screenlock")
#ifndef Q_OS_MACOS
const QString kInterface = QStringLiteral("org.freedesktop.ScreenSaver");
const QString kSignal = QStringLiteral("ActiveChanged");
#endif
} // namespace

ScreenLockWatcher::ScreenLockWatcher(QObject *parent)
    : ScreenLockWatcher(Source::System, parent)
{
}

#ifdef Q_OS_MACOS
ScreenLockWatcher::ScreenLockWatcher(Source source, QObject *parent)
    : QObject(parent)
{
    if (source == Source::System) {
        // Both callbacks run on the main thread, from the run loop Qt's event loop spins.
        m_macEvents = std::make_unique<MacSessionEvents>([this](bool locked) { onActiveChanged(locked); },
                                                         [this](bool sleeping) { onPrepareForSleep(sleeping); });
    }
}
#else
ScreenLockWatcher::ScreenLockWatcher(Source source, QObject *parent)
    : ScreenLockWatcher(source == Source::System ? QDBusConnection::sessionBus() : QDBusConnection(QStringLiteral("sugo-no-session-bus")),
                        source == Source::System ? QDBusConnection::systemBus() : QDBusConnection(QStringLiteral("sugo-no-system-bus")),
                        parent)
{
}

ScreenLockWatcher::ScreenLockWatcher(const QDBusConnection &bus, QObject *parent)
    : ScreenLockWatcher(bus, QDBusConnection(QStringLiteral("sugo-no-system-bus")), parent)
{
}

ScreenLockWatcher::ScreenLockWatcher(const QDBusConnection &bus, const QDBusConnection &systemBus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_systemBus(systemBus)
{
    if (m_systemBus.isConnected()) {
        m_systemBus.connect(QStringLiteral("org.freedesktop.login1"),
                            QStringLiteral("/org/freedesktop/login1"),
                            QStringLiteral("org.freedesktop.login1.Manager"),
                            QStringLiteral("PrepareForSleep"),
                            this,
                            SLOT(onPrepareForSleep(bool)));
    }

    // Plasma emits ActiveChanged on both paths; onActiveChanged() drops the repeat. Only the owner
    // of the screen saver's name is listened to: another program on the bus cannot fake a lock.
    if (m_bus.isConnected()) {
        m_bus.connect(kInterface,
                      QStringLiteral("/org/freedesktop/ScreenSaver"),
                      kInterface,
                      kSignal,
                      this,
                      SLOT(onActiveChanged(bool)));
        m_bus.connect(kInterface,
                      QStringLiteral("/ScreenSaver"),
                      kInterface,
                      kSignal,
                      this,
                      SLOT(onActiveChanged(bool)));
    }
}
#endif

ScreenLockWatcher::~ScreenLockWatcher() = default;

bool ScreenLockWatcher::available() const
{
#ifdef Q_OS_MACOS
    return m_macEvents != nullptr;
#else
    return m_bus.isConnected();
#endif
}

void ScreenLockWatcher::attach(TimerEngine *engine, AppSettings *settings)
{
    m_engine = engine;
    m_settings = settings;

    if (m_engine) {
        connect(m_engine, &TimerEngine::stateChanged, this, [this]() {
            if (m_engine && m_engine->state() != TimerEngine::State::Paused) {
                if (m_pausedByScreenLock) {
                    m_pausedByScreenLock = false;
                    Q_EMIT pausedByScreenLockChanged();
                }
                setPausedBySleep(false);
            }
        });
    }
}

void ScreenLockWatcher::clearLockPauseNotice()
{
    if (m_pausedByScreenLock) {
        m_pausedByScreenLock = false;
        Q_EMIT pausedByScreenLockChanged();
    }
    setPausedBySleep(false);
}

void ScreenLockWatcher::setPausedBySleep(bool paused)
{
    if (m_pausedBySleep != paused) {
        m_pausedBySleep = paused;
        Q_EMIT pausedBySleepChanged();
    }
}

void ScreenLockWatcher::onPrepareForSleep(bool start)
{
    if (!m_engine) {
        return;
    }
    if (start) {
        // A phase already due ends first: the pause, and its notice, apply to the one that follows.
        m_engine->refresh();
        // Any running phase: a break slept through is not a break taken either.
        if (m_engine->isRunning()) {
            qCDebug(lcScreenLock) << "Pausing the timer: the system is going to sleep";
            setPausedBySleep(true);
            m_engine->pause();
        }
    } else if (m_pausedBySleep && m_engine->isPaused()) {
        qCDebug(lcScreenLock) << "Woke up with the timer paused by the sleep";
        Q_EMIT wokeUpAfterPause();
    }
}

void ScreenLockWatcher::onActiveChanged(bool active)
{
    if (active == m_screenLocked) {
        return; // the same change, reported again on the other object path
    }
    m_screenLocked = active;
    qCDebug(lcScreenLock) << "Screen lock state changed:" << active;

    if (active) {
        if (m_settings && m_settings->autoPauseOnScreenLock() && m_engine) {
            // A work phase already due ends first; only a Work still running is paused.
            m_engine->refresh();
            if (m_engine->state() == TimerEngine::State::Working) {
                qCDebug(lcScreenLock) << "Auto-pausing active timer due to screen lock";
                m_pausedByScreenLock = true;
                Q_EMIT pausedByScreenLockChanged();
                m_engine->pause();
            }
        }
    } else {
        if (m_pausedByScreenLock) {
            qCDebug(lcScreenLock) << "Screen unlocked after auto-pause";
            Q_EMIT screenUnlockedAfterPause();
        }
    }
}
