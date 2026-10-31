// SPDX-License-Identifier: GPL-3.0-or-later
#include "SessionRecorder.h"

#include "PresetModel.h"
#include "SessionRepository.h"

namespace
{
SessionKind kindFor(TimerEngine::Phase phase)
{
    switch (phase) {
    case TimerEngine::Phase::Work:
        return SessionKind::Work;
    case TimerEngine::Phase::ShortBreak:
        return SessionKind::ShortBreak;
    case TimerEngine::Phase::LongBreak:
        return SessionKind::LongBreak;
    }
    Q_UNREACHABLE_RETURN(SessionKind::Work);
}
} // namespace

SessionRecorder::SessionRecorder(TimerEngine *engine, PresetModel *presets, SessionRepository *repository, QObject *parent)
    : QObject(parent)
    , m_presets(presets)
    , m_repository(repository)
    , m_active(presets->currentPreset())
{
    connect(engine, &TimerEngine::phaseStarted, this, &SessionRecorder::onPhaseStarted);
    connect(engine, &TimerEngine::sessionEnded, this, &SessionRecorder::onSessionEnded);
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(60 * 1000);
    connect(&m_retryTimer, &QTimer::timeout, this, &SessionRecorder::storeUnsaved);
}

bool SessionRecorder::store(const SessionRecord &record)
{
    qint64 id = -1;
    if (!m_repository->insert(record, &id)) {
        return false;
    }
    Q_EMIT sessionRecorded();
    if (record.kind == SessionKind::Work && record.completed) {
        SessionRecord stored = record;
        stored.id = id;
        Q_EMIT workSessionRecorded(stored);
    }
    return true;
}

void SessionRecorder::storeUnsaved()
{
    const bool wasFailing = isSaveFailing();
    while (!m_unsaved.isEmpty() && store(m_unsaved.first())) {
        m_unsaved.removeFirst();
    }
    if (isSaveFailing()) {
        m_retryTimer.start();
    } else if (wasFailing) {
        Q_EMIT saveFailingChanged();
    }
}

void SessionRecorder::forgetHistory()
{
    m_lastWorkSessionId = -1;
    m_retryTimer.stop();
    if (isSaveFailing()) {
        m_unsaved.clear();
        Q_EMIT saveFailingChanged();
    }
}

void SessionRecorder::onPhaseStarted(TimerEngine::Phase)
{
    m_active = m_presets->currentPreset();
}

void SessionRecorder::onSessionEnded(TimerEngine::Phase phase,
                                     const QDateTime &startedAt,
                                     const QDateTime &endedAt,
                                     qint64 activeMs,
                                     qint64 plannedMs,
                                     bool completed)
{
    const bool isWork = phase == TimerEngine::Phase::Work;
    if (!completed && activeMs < kMinInterruptedMs) {
        if (isWork) {
            m_lastWorkSessionId = -1;
        }
        return;
    }

    SessionRecord record;
    record.kind = kindFor(phase);
    record.startedAtMs = startedAt.toMSecsSinceEpoch();
    record.endedAtMs = endedAt.toMSecsSinceEpoch();
    record.durationSec = int((activeMs + 500) / 1000);
    record.plannedSec = int((plannedMs + 500) / 1000);
    record.completed = completed;
    record.presetUuid = m_active.uuid;
    record.presetName = m_active.name;
    record.category = m_active.category;

    // Older sessions that could not be stored go first, so the history stays in order.
    storeUnsaved();
    qint64 id = -1;
    const bool inserted = !isSaveFailing() && m_repository->insert(record, &id);
    if (isWork) {
        m_lastWorkSessionId = inserted ? id : -1;
    }
    if (inserted) {
        Q_EMIT sessionRecorded();
        if (isWork && completed) {
            record.id = id;
            Q_EMIT workSessionRecorded(record);
        }
        return;
    }
    const bool wasFailing = isSaveFailing();
    m_unsaved.append(record);
    m_retryTimer.start();
    if (!wasFailing) {
        Q_EMIT saveFailingChanged();
    }
}
