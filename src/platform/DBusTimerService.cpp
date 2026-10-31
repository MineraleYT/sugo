// SPDX-License-Identifier: GPL-3.0-or-later
#include "DBusTimerService.h"

#include "Localization.h"

DBusTimerService::DBusTimerService(TimerEngine *engine, TimerDBusObject *host)
    : QDBusAbstractAdaptor(host)
    , m_engine(engine)
{
}

bool DBusTimerService::mayControl()
{
    if (!m_engine) {
        return false;
    }
    if (!m_allowed || m_allowed()) {
        return true;
    }
    auto *host = static_cast<TimerDBusObject *>(parent());
    if (host->calledFromDBus()) {
        host->sendErrorReply(ControlDisabledError, i18n("Control by other programs is turned off in Sugo's settings."));
    }
    return false;
}

void DBusTimerService::toggle()
{
    if (mayControl()) {
        m_engine->toggle();
    }
}

void DBusTimerService::start()
{
    if (mayControl()) {
        m_engine->start();
    }
}

void DBusTimerService::pause()
{
    if (mayControl()) {
        m_engine->pause();
    }
}

void DBusTimerService::stop()
{
    if (mayControl()) {
        m_engine->stop();
    }
}

void DBusTimerService::skip()
{
    if (mayControl()) {
        m_engine->skip();
    }
}

QString DBusTimerService::status() const
{
    if (!m_engine) {
        return QStringLiteral("unavailable");
    }
    QString stateStr;
    if (m_engine->state() == TimerEngine::State::Idle) {
        stateStr = QStringLiteral("idle");
    } else if (m_engine->isPaused()) {
        stateStr = QStringLiteral("paused");
    } else {
        stateStr = QStringLiteral("running");
    }

    QString phaseStr;
    switch (m_engine->phase()) {
    case TimerEngine::Phase::Work:
        phaseStr = QStringLiteral("work");
        break;
    case TimerEngine::Phase::ShortBreak:
        phaseStr = QStringLiteral("short_break");
        break;
    case TimerEngine::Phase::LongBreak:
        phaseStr = QStringLiteral("long_break");
        break;
    }

    return QStringLiteral("%1:%2:%3").arg(stateStr, phaseStr, QString::number(m_engine->remainingSeconds()));
}
