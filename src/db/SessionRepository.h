// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

enum class SessionKind {
    Work,
    ShortBreak,
    LongBreak,
};

/// One finished (or interrupted) phase, as stored. Times are Unix milliseconds, UTC.
struct SessionRecord {
    qint64 id = 0;
    SessionKind kind = SessionKind::Work;
    qint64 startedAtMs = 0;
    qint64 endedAtMs = 0;
    int durationSec = 0; ///< Active time, excluding pauses.
    int plannedSec = 0;
    bool completed = false;
    QString presetUuid;  ///< Used to link to the preset; empty or unknown links to nothing.
    QString presetName;  ///< Snapshot, so statistics survive renames and deletion.
    QString category;    ///< Snapshot.
    QString note;        ///< User task note or activity summary.
};
Q_DECLARE_METATYPE(SessionRecord)

class SessionRepository
{
public:
    explicit SessionRepository(const QString &connectionName);

    /// Returns false (and logs) on failure. On success `insertedId` (if given) receives the row id.
    bool insert(const SessionRecord &record, qint64 *insertedId = nullptr);

    /// Updates the task note for an existing session.
    bool updateNote(qint64 sessionId, const QString &note);

    /// ID of the newest work session, or 0. Not necessarily the one that just ended: prefer
    /// SessionRecorder::lastWorkSessionId() to attach a note to a finished phase.
    qint64 lastSessionId() const;

    /// Recent unique non-empty task notes (trimmed), most recently used first.
    QStringList recentNotes(int limit = 5) const;

    /// Sessions that started in [fromMs, toMs), oldest first.
    QList<SessionRecord> between(qint64 fromMs, qint64 toMs) const;

    /// Started timestamps (Unix ms) of completed work sessions, oldest first.
    QList<qint64> completedWorkStarts() const;

    /// Like completedWorkStarts(), but only rows with an id greater than `afterId`, so callers
    /// can keep a cache up to date. `maxId` (if given) receives the highest id seen, or
    /// `afterId` when there are no new rows.
    QList<qint64> completedWorkStartsAfter(qint64 afterId, qint64 *maxId = nullptr) const;

    int count() const;
    bool deleteAll();

    static QString kindToString(SessionKind kind);
    static SessionKind kindFromString(const QString &text);

private:
    QString m_connection;
};
