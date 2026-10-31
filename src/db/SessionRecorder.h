// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QTimer>

#include "TimerEngine.h"
#include "SessionRepository.h"
#include "TimerPreset.h"

class PresetModel;

/**
 * Stores every finished phase of the TimerEngine.
 *
 * The preset is captured when a phase *starts*, not when it ends, so a session is
 * always attributed to the timer it ran with even if the user switches timers
 * (which stops the running phase) or edits the preset afterwards.
 *
 * Phases cut short by stop/skip are kept only if they lasted at least
 * kMinInterruptedMs, so accidental starts do not clutter the statistics.
 *
 * A session that cannot be stored (database busy, disk full...) is not dropped: it is kept in
 * memory and stored again at the next phase end or retry, and isSaveFailing() says so meanwhile.
 */
class SessionRecorder : public QObject
{
    Q_OBJECT

public:
    static constexpr qint64 kMinInterruptedMs = TimerEngine::kMinInterruptedMs;

    SessionRecorder(TimerEngine *engine, PresetModel *presets, SessionRepository *repository, QObject *parent = nullptr);

    /// Row id of the most recently ended Work phase, or -1 if that phase was not recorded
    /// (too short, or the insert failed) or no Work phase has ended yet. Breaks leave it as is,
    /// so a note typed during the following break still lands on the right session.
    qint64 lastWorkSessionId() const { return m_lastWorkSessionId; }

    /// Called when the stored history is replaced (cleared or restored). Forgets that id, so a
    /// later note cannot land on an unrelated row that happens to reuse the id, and drops the
    /// sessions still waiting to be stored, so they do not reappear in the replaced history.
    void forgetHistory();

    /// True while some ended sessions could not be stored yet.
    bool isSaveFailing() const { return !m_unsaved.isEmpty(); }
    void setRetryIntervalForTesting(int ms) { m_retryTimer.setInterval(ms); }

Q_SIGNALS:
    void sessionRecorded();
    /// A Work phase ran to its end and was stored; record.id is its new row id.
    void workSessionRecorded(const SessionRecord &record);
    void saveFailingChanged();

private Q_SLOTS:
    void onPhaseStarted(TimerEngine::Phase phase);
    void onSessionEnded(TimerEngine::Phase phase,
                        const QDateTime &startedAt,
                        const QDateTime &endedAt,
                        qint64 activeMs,
                        qint64 plannedMs,
                        bool completed);

private:
    bool store(const SessionRecord &record);
    void storeUnsaved();

    PresetModel *m_presets;
    SessionRepository *m_repository;
    TimerPreset m_active; ///< Preset captured at the start of the running phase.
    qint64 m_lastWorkSessionId = -1;
    QList<SessionRecord> m_unsaved; ///< Ended sessions the database refused, oldest first.
    QTimer m_retryTimer;
};
