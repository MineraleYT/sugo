// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSqlDatabase>
#include <QString>

/**
 * Owns the SQLite connection and brings the schema up to date.
 *
 * Ownership: our files carry `PRAGMA application_id` = kApplicationId, or, as Sugo 1.0.0 wrote
 * them, kSugo100ApplicationId (such a file is converted on open: see migrate()). A file that
 * already has tables but is not ours (another program that
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
    /// Identifies Sugo databases: "Sugo" in ASCII.
    static constexpr int kApplicationId = 0x5375676F;
    /// The id Sugo 1.0.0 stamped its files with ("kTmt"). Its files are ours: open() gives them
    /// kApplicationId and today's schema numbering.
    static constexpr int kSugo100ApplicationId = 0x6B546D74;
    /// The last user_version Sugo 1.0.0 wrote: its version 2, which added session.note, is the
    /// schema of today's version 1.
    static constexpr int kSugo100SchemaVersion = 2;

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

    /**
     * Takes back the timers and history that Sugo 1.0.1 to 1.0.x moved aside. Those versions did
     * not know Sugo 1.0.0's application id, so they moved its database to `<path>.foreign-*` and
     * started an empty one. Call after open() succeeded.
     *
     * Does something only if this database holds nothing the user made (no session, no timer of
     * their own, no deleted timer), nothing was moved aside by this open(), and exactly one
     * `<path>.foreign-*` file is a readable database of Sugo 1.0.0 with sessions or timers of
     * its own. A converted copy of that file then takes this one's place, which is kept as
     * `<path>.replaced-<timestamp>`; the `.foreign-*` file itself is only copied, never changed.
     *
     * It looks once per database: the first look leaves the meta key `sugo100_recovery` (the name of
     * the file taken back, or "checked"), and a `<path>.replaced-*` file next to it also ends the
     * search, so that data the user cleared afterwards does not come back.
     *
     * Returns false only if no database is open any more (lastError() says why); whether the data
     * was taken back is recoveredFromPath().
     */
    bool recoverSugo100Database();
    /// The `.foreign-*` file whose data recoverSugo100Database() took back; empty if none.
    QString recoveredFromPath() const { return m_recoveredFromPath; }
    /// Where recoverSugo100Database() kept the database it replaced; empty if none.
    QString replacedPath() const { return m_replacedPath; }

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
    /// Renames a database and its journal files; all or nothing, never over an existing file.
    static bool moveFiles(const QString &from, const QString &to);
    bool finishOpen();
    bool hasSqliteHeader() const;
    static bool isCorruptionError(const QString &nativeCode);
    void closeConnection();
    bool migrate(QSqlDatabase &db);
    bool convertSugo100File(QSqlDatabase &db);

    QString m_path;
    QString m_connectionName;
    QString m_error;
    QString m_movedAsidePath;
    bool m_movedAsideCorrupt = false;
    QString m_recoveredFromPath;
    QString m_replacedPath;
};
