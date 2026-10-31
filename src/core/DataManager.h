// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QUrl>

class Database;
class PresetModel;
class PresetPersistence;
class PresetRepository;
class SessionRecorder;
class SessionRepository;
class StatsModel;

/// Backup, restore, export and clear the user's recorded timer sessions.
class DataManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString databasePath READ databasePath CONSTANT FINAL)
    Q_PROPERTY(int sessionCount READ sessionCount NOTIFY dataChanged FINAL)
    /// True when running on an in-memory database (the file could not be used): nothing is saved.
    Q_PROPERTY(bool usingTemporaryDatabase READ usingTemporaryDatabase CONSTANT FINAL)
    /// True while finished sessions or timer changes could not be stored (they are retried).
    Q_PROPERTY(bool saveFailing READ saveFailing NOTIFY saveFailingChanged FINAL)

public:
    DataManager(Database *database, SessionRepository *sessions, PresetRepository *presetsRepository,
                PresetModel *presets, StatsModel *stats, QObject *parent = nullptr);
    QString databasePath() const;
    int sessionCount() const;
    int presetCount() const;
    bool usingTemporaryDatabase() const;
    bool saveFailing() const;

    /// Tracks newly recorded sessions: keeps sessionCount current and provides lastWorkSessionId().
    void setSessionRecorder(SessionRecorder *recorder);
    /// Reports failed timer saves through saveFailing.
    void setPresetPersistence(PresetPersistence *persistence);
    /// Row id of the most recently ended Work phase, or -1 if it was not recorded or there is no recorder.
    Q_INVOKABLE qint64 lastWorkSessionId() const;

    Q_INVOKABLE bool clearHistory();
    /// Writes every session to a .csv or .json file (chosen by the file's extension).
    Q_INVOKABLE bool exportHistory(const QUrl &fileUrl);
    /// Opens the folder holding the database in the file manager.
    Q_INVOKABLE void openDatabaseFolder() const;
    Q_INVOKABLE bool backupDatabase(const QUrl &fileUrl);
    Q_INVOKABLE bool restoreDatabase(const QUrl &fileUrl);
    Q_INVOKABLE bool updateSessionNote(qint64 sessionId, const QString &note);
    Q_INVOKABLE QStringList recentTaskNotes(int limit = 5) const;

Q_SIGNALS:
    void operationFailed(const QString &message);
    void operationSucceeded(const QString &message);
    void dataChanged();
    void saveFailingChanged();
    /// A session note was stored (also emitted for an empty note).
    void sessionNoteChanged(qint64 sessionId, const QString &note);

private:
    bool validateBackup(const QString &path, QString *error) const;
    Database *m_database;
    SessionRepository *m_sessions;
    PresetRepository *m_presetsRepository;
    PresetModel *m_presets;
    StatsModel *m_stats;
    SessionRecorder *m_recorder = nullptr;
    PresetPersistence *m_presetPersistence = nullptr;
};
