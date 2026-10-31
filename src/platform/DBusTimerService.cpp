// SPDX-License-Identifier: GPL-3.0-or-later
#include "DBusTimerService.h"

#include "Localization.h"
#include "PresetModel.h"

DBusTimerService::DBusTimerService(TimerEngine *engine, PresetModel *presets, TimerDBusObject *host)
    : QDBusAbstractAdaptor(host)
    , m_engine(engine)
    , m_presets(presets)
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
    replyError(ControlDisabledError, i18n("Control by other programs is turned off in Sugo's settings."));
    return false;
}

void DBusTimerService::replyError(const QString &name, const QString &message)
{
    auto *host = static_cast<TimerDBusObject *>(parent());
    if (host->calledFromDBus()) {
        host->sendErrorReply(name, message);
    }
}

QString DBusTimerService::resolvePreset(const QString &nameOrUuid) const
{
    if (!m_presets) {
        return {};
    }
    const QList<TimerPreset> &list = m_presets->presets();
    for (const TimerPreset &preset : list) {
        if (preset.uuid == nameOrUuid) {
            return preset.uuid;
        }
    }
    for (const TimerPreset &preset : list) {
        if (preset.name.compare(nameOrUuid, Qt::CaseInsensitive) == 0) {
            return preset.uuid;
        }
    }
    return {};
}

bool DBusTimerService::switchTo(const QString &uuid)
{
    if (uuid == m_presets->currentUuid()) {
        return true;
    }
    if (m_engine->isActive()) {
        replyError(TimerActiveError, i18n("A timer is running. Stop it before choosing another timer."));
        return false;
    }
    m_presets->setCurrentUuid(uuid);
    return true;
}

QStringList DBusTimerService::presets() const
{
    QStringList names;
    if (m_presets) {
        for (const TimerPreset &preset : m_presets->presets()) {
            names.append(preset.name);
        }
    }
    return names;
}

bool DBusTimerService::selectPreset(const QString &nameOrUuid)
{
    if (!mayControl()) {
        return false;
    }
    const QString uuid = resolvePreset(nameOrUuid);
    if (uuid.isEmpty()) {
        replyError(UnknownPresetError, i18n("There is no timer named \"%1\".", nameOrUuid));
        return false;
    }
    return switchTo(uuid);
}

void DBusTimerService::stepPreset(int step)
{
    if (!mayControl() || !m_presets || m_presets->count() == 0) {
        return;
    }
    const int count = m_presets->count();
    const int row = ((m_presets->currentIndex() + step) % count + count) % count;
    switchTo(m_presets->uuidAt(row));
}

void DBusTimerService::nextPreset()
{
    stepPreset(1);
}

void DBusTimerService::previousPreset()
{
    stepPreset(-1);
}

bool DBusTimerService::startPreset(const QString &nameOrUuid)
{
    if (!selectPreset(nameOrUuid)) {
        return false;
    }
    if (m_engine->isPaused()) {
        m_engine->resume();
    } else {
        m_engine->start();
    }
    return true;
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
