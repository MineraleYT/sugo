// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QObject>

#include <functional>

#include "TimerEngine.h"

/**
 * Exposes TimerEngine controls over D-Bus at object path /Timer
 * with interface io.github.mineraleyt.sugo.Timer.
 *
 * This allows KDE Plasma Global Shortcuts, command line calls (qdbus),
 * scripts, and hardware buttons to start, pause, skip, or stop the timer.
 *
 * The commands can be switched off (Settings: "Allow other programs to control the timer");
 * they then answer with the D-Bus error ControlDisabled and change nothing. status() stays
 * available: it only reads.
 */
class DBusTimerService;

/// The object registered at /Timer: it carries the adaptor and the D-Bus call context (for an
/// adaptor's slot, QtDBus gives the context to the adaptor's parent, so it must live here).
class TimerDBusObject : public QObject, public QDBusContext
{
    Q_OBJECT

public:
    using QObject::QObject;
};

class DBusTimerService : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.mineraleyt.sugo.Timer")

public:
    /// Register `host` (not the engine) at /Timer with QDBusConnection::ExportAdaptors.
    DBusTimerService(TimerEngine *engine, TimerDBusObject *host);

    static inline const QString ControlDisabledError = QStringLiteral("io.github.mineraleyt.sugo.Error.ControlDisabled");

    /// Asked before every command; without one, commands are always allowed.
    void setControlAllowed(std::function<bool()> allowed) { m_allowed = std::move(allowed); }

public Q_SLOTS:
    void toggle();
    void start();
    void pause();
    void stop();
    void skip();
    QString status() const;

private:
    /// True if a command may run; otherwise answers the caller with ControlDisabled.
    bool mayControl();

    TimerEngine *m_engine;
    std::function<bool()> m_allowed;
};
