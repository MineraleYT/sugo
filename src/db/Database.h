// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSqlDatabase>
#include <QString>

/**
 * Owns the SQLite connection and brings the schema up to date.
 *
 * Ownership: our files carry `PRAGMA application_id` = kApplicationId. A file that
 * already has tables but is not ours (another program, or an old prototype that
 * happens to use the same path) is never migrated or modified: it is moved aside to
 * `<path>.foreign-<timestamp>` and a fresh database is created. See movedAsidePath().
 * A file that is not SQLite at all, or a damaged one, is likewise moved to
 * `<path>.corrupt-<timestamp>`, so a broken file never forces every later launch onto
 * a throw-away in-memory database.
 *
 * Schema versions live in `PRAGMA user_version`; each migration runs in its own
 * transaction. A database written by a newer Sugo is refused rather than
 * modified. Repositories receive only the connection *name* and look the
 * connection up on use, so no QSqlDatabase copy outlives this object.
 */
class Database
{
public:
    /// "kTmt" in ASCII; identifies Sugo databases.
    static constexpr int kApplicationId = 0x6B546D74;

    /// `path` may be ":memory:" (tests, or fallback when the file is unusable).
    explicit Database(const QString &path);
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    /// Opens the file (creating its directory), checks ownership, sets pragmas and migrates.
    bool open();

    QString connectionName() const { return m_connectionName; }
    QString path() const { return m_path; }
    QString lastError() const { return m_error; }
    int schemaVersion() const;
    /// Where a foreign or damaged database found at the path was moved to; empty if none was.
    QString movedAsidePath() const { return m_movedAsidePath; }
    /// True if the file moved aside was damaged or not SQLite, false if it belonged to another program.
    bool movedAsideWasCorrupt() const { return m_movedAsideCorrupt; }
    /// True for an in-memory database: nothing is saved when the program ends.
    bool isTemporary() const { return m_path == QLatin1String(":memory:"); }

    static int latestSchemaVersion();
    /// ~/.local/share/sugo/sugo.db (inside the sandbox when packaged as Flatpak).
    static QString defaultPath();

private:
    bool connect(bool *corrupt = nullptr);
    enum class Ownership {
        Ours,
        Foreign,
        Corrupt,
        /// The file could not be read (busy, locked, I/O): no verdict, so it must not be touched.
        Unknown,
    };
    Ownership ownership(QSqlDatabase &db);
    bool moveFileAside(const QString &reason);
    bool hasSqliteHeader() const;
    static bool isCorruptionError(const QString &nativeCode);
    void closeConnection();
    bool migrate(QSqlDatabase &db);

    QString m_path;
    QString m_connectionName;
    QString m_error;
    QString m_movedAsidePath;
    bool m_movedAsideCorrupt = false;
};
