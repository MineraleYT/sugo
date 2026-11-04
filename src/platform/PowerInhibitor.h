// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <memory>

class TimerEngine;
class AppSettings;
#ifdef Q_OS_MACOS
class MacPowerAssertion;
#else
#include <QDBusConnection>

class QDBusPendingCallWatcher;
class QDBusServiceWatcher;
#endif

/// Prevents the screen from dimming/sleeping and system idle standby during work phases.
///
/// Inhibit() answers asynchronously, so the wanted state can change while a request is in
/// flight: at most one request is outstanding, and a cookie that arrives when inhibition is
/// no longer wanted is handed straight back. If the screensaver service restarts, the old
/// cookie is dropped and inhibition is requested again from the new owner.
///
/// On macOS an IOKit power assertion (MacPowerAssertion) is used instead of D-Bus; it is taken
/// and released synchronously, so none of the above applies there.
class PowerInhibitor : public QObject
{
    Q_OBJECT

public:
#ifdef Q_OS_MACOS
    explicit PowerInhibitor(QObject *parent = nullptr);
#else
    explicit PowerInhibitor(const QDBusConnection &bus = QDBusConnection::sessionBus(),
                            QObject *parent = nullptr);
#endif
    ~PowerInhibitor() override;

    void attach(TimerEngine *engine, AppSettings *settings);

public Q_SLOTS:
    void inhibit();
    void release();
    void update();
    /// For quitting: releases inhibition and waits briefly for an Inhibit call still on its way,
    /// so its cookie is handed back instead of arriving after we are gone.
    void shutdown();

private:
    void sync();

    TimerEngine *m_engine = nullptr;
    AppSettings *m_settings = nullptr;
    bool m_wanted = false;
#ifdef Q_OS_MACOS
    std::unique_ptr<MacPowerAssertion> m_macAssertion;
#else
    void sendInhibit();
    void sendUnInhibit(uint cookie);
    void onOwnerChanged(const QString &oldOwner, const QString &newOwner);

    QDBusConnection m_bus;
    QDBusServiceWatcher *m_watcher = nullptr;
    uint m_cookie = 0;
    bool m_hasCookie = false;   // m_cookie is held: 0 is a valid cookie (kscreenlocker starts there)
    bool m_pending = false;     // an Inhibit call is waiting for its reply
    QPointer<QDBusPendingCallWatcher> m_pendingWatcher;
    int m_retries = 0;          // failed Inhibit calls retried since inhibition was last not wanted
    QTimer m_retryTimer;        // stopped when inhibition stops being wanted
    quint64 m_generation = 0;   // bumped when the service owner changes
#endif
};
