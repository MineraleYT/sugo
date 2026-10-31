// SPDX-License-Identifier: GPL-3.0-or-later
#include "PowerInhibitor.h"

#include "AppSettings.h"
#include "TimerEngine.h"

#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QEventLoop>
#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcPowerInhibit, "sugo.platform.power")
const QString kService = QStringLiteral("org.freedesktop.ScreenSaver");
const QString kPath = QStringLiteral("/org/freedesktop/ScreenSaver");
const QString kInterface = QStringLiteral("org.freedesktop.ScreenSaver");
constexpr int kMaxRetries = 3;
constexpr int kRetryDelayMs = 1000;
constexpr int kShutdownWaitMs = 1000;
} // namespace

PowerInhibitor::PowerInhibitor(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(kRetryDelayMs);
    connect(&m_retryTimer, &QTimer::timeout, this, &PowerInhibitor::sync);
    if (m_bus.isConnected()) {
        m_watcher = new QDBusServiceWatcher(kService, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
        connect(m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
                [this](const QString &, const QString &, const QString &newOwner) { onOwnerChanged(newOwner); });
    }
}

PowerInhibitor::~PowerInhibitor()
{
    release();
}

void PowerInhibitor::attach(TimerEngine *engine, AppSettings *settings)
{
    m_engine = engine;
    m_settings = settings;

    if (m_engine) {
        connect(m_engine, &TimerEngine::stateChanged, this, &PowerInhibitor::update);
    }
    if (m_settings) {
        connect(m_settings, &AppSettings::keepScreenAwakeChanged, this, &PowerInhibitor::update);
    }

    update();
}

void PowerInhibitor::update()
{
    m_wanted = m_engine && m_settings
        && m_settings->keepScreenAwake()
        && m_engine->state() == TimerEngine::State::Working;
    sync();
}

void PowerInhibitor::inhibit()
{
    m_wanted = true;
    sync();
}

void PowerInhibitor::release()
{
    m_wanted = false;
    sync();
}

void PowerInhibitor::shutdown()
{
    m_engine = nullptr; // turns update() off: nothing may inhibit again while quitting
    release();
    if (!m_pending || !m_pendingWatcher) {
        return;
    }
    QEventLoop loop;
    // The reply handler, connected first, has handed the cookie back by then.
    connect(m_pendingWatcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(kShutdownWaitMs, &loop, &QEventLoop::quit);
    loop.exec();
}

void PowerInhibitor::sync()
{
    if (!m_wanted) {
        m_retries = 0;
        m_retryTimer.stop();
    }
    if (m_wanted && m_cookie == 0 && !m_pending) {
        sendInhibit();
    } else if (!m_wanted && m_cookie != 0) {
        const uint cookie = m_cookie;
        m_cookie = 0;
        sendUnInhibit(cookie);
    }
    // !m_wanted while a request is pending: the reply handler returns the cookie.
}

void PowerInhibitor::sendInhibit()
{
    if (!m_bus.isConnected()) {
        return;
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kInterface, QStringLiteral("Inhibit"));
    msg << QStringLiteral("Sugo") << QStringLiteral("Pomodoro work session in progress");

    m_pending = true;
    const quint64 generation = m_generation;
    QDBusPendingCall call = m_bus.asyncCall(msg);
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    m_pendingWatcher = watcher;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, generation](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (generation != m_generation) {
            // Answer from a service owner that has gone away; onOwnerChanged() already reset.
            return;
        }
        m_pending = false;
        QDBusPendingReply<uint> reply = *w;
        if (reply.isError()) {
            qCWarning(lcPowerInhibit) << "Failed to inhibit screen sleep:" << reply.error().message();
            // A few delayed retries while still wanted, no loop; the next state change tries again.
            if (m_wanted && m_retries < kMaxRetries) {
                ++m_retries;
                m_retryTimer.start();
            }
            return;
        }
        const uint cookie = reply.value();
        if (!m_wanted) {
            // Paused, stopped or setting turned off while the call was in flight.
            sendUnInhibit(cookie);
            return;
        }
        m_cookie = cookie;
        qCDebug(lcPowerInhibit) << "Screen sleep inhibited with cookie" << m_cookie;
    });
}

void PowerInhibitor::sendUnInhibit(uint cookie)
{
    if (cookie == 0 || !m_bus.isConnected()) {
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kInterface, QStringLiteral("UnInhibit"));
    msg << cookie;
    m_bus.asyncCall(msg);
    qCDebug(lcPowerInhibit) << "Screen sleep inhibition released for cookie" << cookie;
}

void PowerInhibitor::onOwnerChanged(const QString &newOwner)
{
    // A cookie belongs to the process that issued it; the new owner does not know it.
    ++m_generation;
    m_pending = false;
    m_cookie = 0;
    if (!newOwner.isEmpty()) {
        sync();
    }
}
