// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include <memory>

#include "Database.h"

namespace
{
QVariant scalar(const QString &connection, const QString &sql)
{
    QSqlQuery query(QSqlDatabase::database(connection, false));
    return query.exec(sql) && query.next() ? query.value(0) : QVariant();
}

QStringList tableNames(const QString &connection)
{
    QSqlQuery query(QSqlDatabase::database(connection, false));
    query.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%'"));
    QStringList names;
    while (query.next()) {
        names.append(query.value(0).toString());
    }
    names.sort();
    return names;
}

/// Creates a SQLite file with arbitrary content, as another program would have.
void createForeignDatabase(const QString &path, const QString &schemaSql, int userVersion, int applicationId = 0)
{
    const QString name = QStringLiteral("foreign-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        // The SQLite driver runs one statement per exec().
        for (const QString &statement : schemaSql.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            QVERIFY(query.exec(statement));
        }
        QVERIFY(query.exec(QStringLiteral("INSERT INTO notes (text) VALUES ('precious')")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = %1").arg(userVersion)));
        QVERIFY(query.exec(QStringLiteral("PRAGMA application_id = %1").arg(applicationId)));
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
}

QString foreignNotes(const QString &path)
{
    const QString name = QStringLiteral("check-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString text;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        if (db.open()) {
            QSqlQuery query(db);
            if (query.exec(QStringLiteral("SELECT text FROM notes")) && query.next()) {
                text = query.value(0).toString();
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return text;
}

QStringList movedAsideFiles(const QDir &dir)
{
    return dir.entryList({QStringLiteral("*.foreign-*")}, QDir::Files);
}

/// The application id Sugo 1.0.0 stamped its files with ("kTmt" in ASCII).
constexpr int kSugo100ApplicationId = 0x6B546D74;

/**
 * Writes a database exactly as Sugo 1.0.0 (tag v1.0.0) left it: its two migrations (version 1,
 * then version 2 adding session.note), user_version 2, the 1.0.0 application id stamped after
 * the migrations (here after the rows, which changes nothing), WAL mode, preset times in seconds. One timer with an option, one session with
 * a note, one meta key.
 */
void createSugo100Database(const QString &path)
{
    const QString name = QStringLiteral("v100-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        raw.setDatabaseName(path);
        QVERIFY(raw.open());
        QSqlQuery query(raw);
        for (const char *statement : {
                 "PRAGMA foreign_keys = ON",
                 "PRAGMA journal_mode = WAL",
                 // v1.0.0, migration 1
                 "CREATE TABLE meta (\n    key   TEXT PRIMARY KEY,\n    value TEXT NOT NULL\n)",
                 "CREATE TABLE preset (\n"
                 "    id                 INTEGER PRIMARY KEY AUTOINCREMENT,\n"
                 "    uuid               TEXT    NOT NULL UNIQUE,\n"
                 "    name               TEXT    NOT NULL,\n"
                 "    category           TEXT    NOT NULL DEFAULT '',\n"
                 "    work_sec           INTEGER NOT NULL CHECK (work_sec > 0),\n"
                 "    short_break_sec    INTEGER NOT NULL CHECK (short_break_sec >= 0),\n"
                 "    long_break_sec     INTEGER NOT NULL CHECK (long_break_sec >= 0),\n"
                 "    cycles_before_long INTEGER NOT NULL DEFAULT 4 CHECK (cycles_before_long >= 0),\n"
                 "    is_builtin         INTEGER NOT NULL DEFAULT 0,\n"
                 "    sort_order         INTEGER NOT NULL DEFAULT 0,\n"
                 "    created_at         INTEGER NOT NULL\n)",
                 "CREATE TABLE preset_option (\n"
                 "    preset_id INTEGER NOT NULL REFERENCES preset(id) ON DELETE CASCADE,\n"
                 "    key       TEXT    NOT NULL,\n"
                 "    value     TEXT    NOT NULL,\n"
                 "    PRIMARY KEY (preset_id, key)\n)",
                 "CREATE TABLE session (\n"
                 "    id           INTEGER PRIMARY KEY AUTOINCREMENT,\n"
                 "    kind         TEXT    NOT NULL CHECK (kind IN ('work', 'short_break', 'long_break')),\n"
                 "    started_at   INTEGER NOT NULL,\n"
                 "    ended_at     INTEGER NOT NULL,\n"
                 "    duration_sec INTEGER NOT NULL,\n"
                 "    planned_sec  INTEGER NOT NULL,\n"
                 "    completed    INTEGER NOT NULL,\n"
                 "    preset_id    INTEGER REFERENCES preset(id) ON DELETE SET NULL,\n"
                 "    preset_name  TEXT    NOT NULL,\n"
                 "    category     TEXT    NOT NULL DEFAULT ''\n)",
                 "CREATE INDEX idx_session_started ON session(started_at)",
                 "CREATE INDEX idx_session_kind_started ON session(kind, started_at)",
                 "PRAGMA user_version = 1",
                 // v1.0.0, migration 2
                 "ALTER TABLE session ADD COLUMN note TEXT NOT NULL DEFAULT ''",
                 "PRAGMA user_version = 2",
                 // What a user of 1.0.0 had saved.
                 "INSERT INTO preset (uuid, name, category, work_sec, short_break_sec, long_break_sec, "
                 "cycles_before_long, is_builtin, sort_order, created_at) "
                 "VALUES ('mine', 'Deep work', 'Study', 3000, 600, 1200, 3, 0, 0, 1761876000)",
                 "INSERT INTO preset_option (preset_id, key, value) VALUES (1, 'autoStartBreaks', 'true')",
                 "INSERT INTO session (kind, started_at, ended_at, duration_sec, planned_sec, completed, "
                 "preset_id, preset_name, category, note) "
                 "VALUES ('work', 1761876100, 1761879100, 3000, 3000, 1, 1, 'Deep work', 'Study', 'Chapter 3')",
                 "INSERT INTO meta (key, value) VALUES ('selectedPreset', 'mine')",
             }) {
            QVERIFY2(query.exec(QString::fromLatin1(statement)), statement);
        }
        QVERIFY(query.exec(QStringLiteral("PRAGMA application_id = %1").arg(kSugo100ApplicationId)));
        raw.close();
    }
    QSqlDatabase::removeDatabase(name);
}
} // namespace

class DatabaseTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void freshDatabaseIsMigratedToLatestSchema()
    {
        Database db(QStringLiteral(":memory:"));
        QVERIFY2(db.open(), qPrintable(db.lastError()));

        QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());
        QCOMPARE(tableNames(db.connectionName()),
                 (QStringList{QStringLiteral("meta"), QStringLiteral("preset"), QStringLiteral("preset_option"),
                              QStringLiteral("preset_tombstone"), QStringLiteral("session")}));
    }

    void foreignKeysAreEnforced()
    {
        Database db(QStringLiteral(":memory:"));
        QVERIFY(db.open());
        QCOMPARE(scalar(db.connectionName(), QStringLiteral("PRAGMA foreign_keys")).toInt(), 1);

        // An option must belong to an existing preset.
        QSqlQuery query(QSqlDatabase::database(db.connectionName(), false));
        QVERIFY(!query.exec(QStringLiteral("INSERT INTO preset_option (preset_id, key, value) VALUES (999, 'k', 'v')")));
    }

    void checkConstraintsRejectInvalidRows()
    {
        Database db(QStringLiteral(":memory:"));
        QVERIFY(db.open());
        QSqlQuery query(QSqlDatabase::database(db.connectionName(), false));
        QVERIFY(!query.exec(QStringLiteral(
            "INSERT INTO preset (uuid, name, work_sec, short_break_sec, long_break_sec, created_at) "
            "VALUES ('u', 'n', 0, 300, 900, 0)"))); // work_sec must be > 0
        QVERIFY(!query.exec(QStringLiteral(
            "INSERT INTO session (kind, started_at, ended_at, duration_sec, planned_sec, completed, preset_name) "
            "VALUES ('nap', 0, 0, 0, 0, 1, 'x')"))); // unknown kind
    }

    void fileDatabaseUsesWalAndKeepsDataAcrossReopen()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("sub/dir/test.db")); // directory is created

        {
            Database db(path);
            QVERIFY2(db.open(), qPrintable(db.lastError()));
            QCOMPARE(scalar(db.connectionName(), QStringLiteral("PRAGMA journal_mode")).toString().toLower(),
                     QStringLiteral("wal"));
            QSqlQuery query(QSqlDatabase::database(db.connectionName(), false));
            QVERIFY(query.exec(QStringLiteral("INSERT INTO meta (key, value) VALUES ('k', 'v')")));
        }
        {
            Database db(path);
            QVERIFY2(db.open(), qPrintable(db.lastError()));
            QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());
            QCOMPARE(scalar(db.connectionName(), QStringLiteral("SELECT value FROM meta WHERE key = 'k'")).toString(),
                     QStringLiteral("v")); // migrations did not run twice or wipe data
        }
    }

    void version1TimersAreMigratedForSync()
    {
        // A file as Sugo 1.0 left it: schema 1, created_at in seconds.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("v1.db"));
        const QString name = QStringLiteral("v1-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        {
            QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
            raw.setDatabaseName(path);
            QVERIFY(raw.open());
            QSqlQuery query(raw);
            for (const char *statement : {
                     "CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)",
                     "CREATE TABLE preset (id INTEGER PRIMARY KEY AUTOINCREMENT, uuid TEXT NOT NULL UNIQUE, "
                     "name TEXT NOT NULL, category TEXT NOT NULL DEFAULT '', work_sec INTEGER NOT NULL CHECK (work_sec > 0), "
                     "short_break_sec INTEGER NOT NULL CHECK (short_break_sec >= 0), "
                     "long_break_sec INTEGER NOT NULL CHECK (long_break_sec >= 0), "
                     "cycles_before_long INTEGER NOT NULL DEFAULT 4 CHECK (cycles_before_long >= 0), "
                     "is_builtin INTEGER NOT NULL DEFAULT 0, sort_order INTEGER NOT NULL DEFAULT 0, created_at INTEGER NOT NULL)",
                     "CREATE TABLE preset_option (preset_id INTEGER NOT NULL REFERENCES preset(id) ON DELETE CASCADE, "
                     "key TEXT NOT NULL, value TEXT NOT NULL, PRIMARY KEY (preset_id, key))",
                     "CREATE TABLE session (id INTEGER PRIMARY KEY AUTOINCREMENT, kind TEXT NOT NULL, started_at INTEGER NOT NULL, "
                     "ended_at INTEGER NOT NULL, duration_sec INTEGER NOT NULL, planned_sec INTEGER NOT NULL, "
                     "completed INTEGER NOT NULL, preset_id INTEGER REFERENCES preset(id) ON DELETE SET NULL, "
                     "preset_name TEXT NOT NULL, category TEXT NOT NULL DEFAULT '', note TEXT NOT NULL DEFAULT '')",
                     "INSERT INTO preset (uuid, name, work_sec, short_break_sec, long_break_sec, created_at) "
                     "VALUES ('old', 'Old timer', 1500, 300, 900, 1700000000)",
                 }) {
                QVERIFY2(query.exec(QString::fromLatin1(statement)), statement);
            }
            QVERIFY(query.exec(QStringLiteral("PRAGMA application_id = %1").arg(Database::kApplicationId)));
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 1")));
            raw.close();
        }
        QSqlDatabase::removeDatabase(name);

        Database db(path);
        QVERIFY2(db.open(), qPrintable(db.lastError()));
        QVERIFY(db.movedAsidePath().isEmpty());
        QCOMPARE(db.schemaVersion(), 2);
        QCOMPARE(scalar(db.connectionName(), QStringLiteral("SELECT created_at FROM preset WHERE uuid = 'old'")).toLongLong(),
                 1700000000000LL); // now milliseconds
        QCOMPARE(scalar(db.connectionName(), QStringLiteral("SELECT updated_at FROM preset WHERE uuid = 'old'")).toLongLong(), 0);
        QCOMPARE(scalar(db.connectionName(), QStringLiteral("SELECT updated_by FROM preset WHERE uuid = 'old'")).toString(), QString());
        QVERIFY(tableNames(db.connectionName()).contains(QStringLiteral("preset_tombstone")));

        QSqlQuery query(QSqlDatabase::database(db.connectionName(), false));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO preset_tombstone (uuid, deleted_at) VALUES ('gone', 5)")));
        QCOMPARE(scalar(db.connectionName(), QStringLiteral("SELECT deleted_by FROM preset_tombstone")).toString(), QString());
        QVERIFY(!query.exec(QStringLiteral("INSERT INTO preset_tombstone (uuid, deleted_at) VALUES ('gone', 6)"))); // one per uuid
    }

    void sugo100DatabaseIsKeptAndUpgraded()
    {
        // Sugo 1.0.0 was released with another application id and its own numbering of
        // user_version: its version 2 (session.note) is today's version 1. Such a file is ours.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("sugo.db"));
        createSugo100Database(path);

        {
            Database db(path);
            QVERIFY2(db.open(), qPrintable(db.lastError()));
            const QString c = db.connectionName();

            QVERIFY2(db.movedAsidePath().isEmpty(), qPrintable(db.movedAsidePath()));
            QVERIFY(movedAsideFiles(QDir(dir.path())).isEmpty());
            QCOMPARE(scalar(c, QStringLiteral("PRAGMA application_id")).toInt(), Database::kApplicationId);
            QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());

            // The timer, now with what Sugo Sync needs, and its times in milliseconds.
            QCOMPARE(scalar(c, QStringLiteral("SELECT count(*) FROM preset")).toInt(), 1);
            QCOMPARE(scalar(c, QStringLiteral("SELECT name FROM preset WHERE uuid = 'mine'")).toString(),
                     QStringLiteral("Deep work"));
            QCOMPARE(scalar(c, QStringLiteral("SELECT work_sec FROM preset WHERE uuid = 'mine'")).toInt(), 3000);
            QCOMPARE(scalar(c, QStringLiteral("SELECT created_at FROM preset WHERE uuid = 'mine'")).toLongLong(),
                     1761876000000LL);
            QCOMPARE(scalar(c, QStringLiteral("SELECT updated_at FROM preset WHERE uuid = 'mine'")).toLongLong(), 0);
            QCOMPARE(scalar(c, QStringLiteral("SELECT updated_by FROM preset WHERE uuid = 'mine'")).toString(), QString());
            QCOMPARE(scalar(c, QStringLiteral("SELECT value FROM preset_option WHERE key = 'autoStartBreaks'")).toString(),
                     QStringLiteral("true"));
            QVERIFY(tableNames(c).contains(QStringLiteral("preset_tombstone")));

            // The history, untouched (session times were and are seconds).
            QCOMPARE(scalar(c, QStringLiteral("SELECT count(*) FROM session")).toInt(), 1);
            QCOMPARE(scalar(c, QStringLiteral("SELECT note FROM session")).toString(), QStringLiteral("Chapter 3"));
            QCOMPARE(scalar(c, QStringLiteral("SELECT started_at FROM session")).toLongLong(), 1761876100LL);
            QCOMPARE(scalar(c, QStringLiteral("SELECT preset_id FROM session")).toInt(), 1);
            QCOMPARE(scalar(c, QStringLiteral("SELECT value FROM meta WHERE key = 'selectedPreset'")).toString(),
                     QStringLiteral("mine"));
            QCOMPARE(scalar(c, QStringLiteral("PRAGMA foreign_key_check")), QVariant());
        }
        {
            // Opening it again changes nothing more (milliseconds are not multiplied twice).
            Database db(path);
            QVERIFY2(db.open(), qPrintable(db.lastError()));
            QVERIFY(db.movedAsidePath().isEmpty());
            QCOMPARE(scalar(db.connectionName(), QStringLiteral("SELECT created_at FROM preset WHERE uuid = 'mine'")).toLongLong(),
                     1761876000000LL);
        }
    }

    void databaseFromNewerVersionIsRefusedAndUntouched()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("future.db"));
        {
            Database db(path);
            QVERIFY(db.open());
            QSqlQuery query(QSqlDatabase::database(db.connectionName(), false));
            QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 99")));
        }
        {
            Database db(path);
            QVERIFY(!db.open());
            QVERIFY(db.lastError().contains(QStringLiteral("newer")));
        }
    }


    // --- Files that are not ours ---------------------------------------------

    void foreignDatabaseWithSameSchemaVersionIsMovedAsideNotMigrated()
    {
        // The real-world case: another program's file at our path, user_version already 1,
        // different tables. Trusting user_version would skip creating our tables.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sugo.db"));
        createForeignDatabase(path, QStringLiteral("CREATE TABLE notes (text TEXT)"), 1);

        Database db(path);
        QVERIFY2(db.open(), qPrintable(db.lastError()));

        // The new database is complete and stamped.
        QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());
        QCOMPARE(tableNames(db.connectionName()),
                 (QStringList{QStringLiteral("meta"), QStringLiteral("preset"), QStringLiteral("preset_option"),
                              QStringLiteral("preset_tombstone"), QStringLiteral("session")}));
        QCOMPARE(scalar(db.connectionName(), QStringLiteral("PRAGMA application_id")).toInt(), Database::kApplicationId);

        // The old file survives untouched under a new name.
        QVERIFY(!db.movedAsidePath().isEmpty());
        QVERIFY(QFile::exists(db.movedAsidePath()));
        QCOMPARE(foreignNotes(db.movedAsidePath()), QStringLiteral("precious"));
        QCOMPARE(movedAsideFiles(QDir(dir.path())).size(), 1);
    }

    void foreignDatabaseWithoutVersionIsAlsoMovedAside()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sugo.db"));
        createForeignDatabase(path, QStringLiteral("CREATE TABLE notes (text TEXT)"), 0);

        Database db(path);
        QVERIFY2(db.open(), qPrintable(db.lastError()));
        QVERIFY(!db.movedAsidePath().isEmpty());
        QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());
    }

    void foreignDatabaseWithOtherApplicationIdIsMovedAside()
    {
        // Even if it happens to contain tables named like ours.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sugo.db"));
        createForeignDatabase(path,
                              QStringLiteral("CREATE TABLE meta (key TEXT); CREATE TABLE preset (id INTEGER); "
                                             "CREATE TABLE preset_option (x); CREATE TABLE session (y); "
                                             "CREATE TABLE notes (text TEXT)"),
                              1, 0x12345678);
        Database db(path);
        QVERIFY2(db.open(), qPrintable(db.lastError()));
        QVERIFY(!db.movedAsidePath().isEmpty());
    }

    void freshDatabaseIsStampedAndNothingIsMovedAside()
    {
        QTemporaryDir dir;
        Database db(dir.filePath(QStringLiteral("sugo.db")));
        QVERIFY(db.open());
        QCOMPARE(scalar(db.connectionName(), QStringLiteral("PRAGMA application_id")).toInt(), Database::kApplicationId);
        QVERIFY(db.movedAsidePath().isEmpty());
    }

    void emptyExistingFileIsAdoptedNotMovedAside()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sugo.db"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly)); // zero-byte file, e.g. created by `touch`
        file.close();

        Database db(path);
        QVERIFY2(db.open(), qPrintable(db.lastError()));
        QVERIFY(db.movedAsidePath().isEmpty());
        QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());
    }

    void unstampedDatabaseWithOurTablesIsMovedAside()
    {
        // Sugo stamps a file before creating any table, so tables without the stamp are not ours.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sugo.db"));
        {
            Database db(path);
            QVERIFY(db.open());
            QSqlQuery query(QSqlDatabase::database(db.connectionName(), false));
            QVERIFY(query.exec(QStringLiteral("PRAGMA application_id = 0")));
        }
        Database db(path);
        QVERIFY2(db.open(), qPrintable(db.lastError()));
        QVERIFY(!db.movedAsidePath().isEmpty());
    }

    void reopeningOwnDatabaseNeverMovesIt()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sugo.db"));
        for (int i = 0; i < 3; ++i) {
            Database db(path);
            QVERIFY(db.open());
            QVERIFY(db.movedAsidePath().isEmpty());
        }
        QVERIFY(movedAsideFiles(QDir(dir.path())).isEmpty());
    }

    void nonSqliteFileIsMovedAsideAsCorrupt()
    {
        // Must not silently run on :memory: at every launch.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sugo.db"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("this is a text file, not a database\n");
        file.close();

        Database db(path);
        QVERIFY2(db.open(), qPrintable(db.lastError()));
        QVERIFY(!db.isTemporary());
        QVERIFY(db.movedAsideWasCorrupt());
        QVERIFY(db.movedAsidePath().contains(QStringLiteral(".corrupt-")));
        QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());

        QFile moved(db.movedAsidePath());
        QVERIFY(moved.open(QIODevice::ReadOnly));
        QCOMPARE(moved.readAll(), QByteArray("this is a text file, not a database\n"));
    }

    void aFailedMoveAsidePutsBackWhatItMoved()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sugo.db"));
        for (const QString &suffix : {QString(), QStringLiteral("-wal")}) {
            QFile file(path + suffix);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("not a database");
        }
        // The journal cannot follow: its destination is taken (for any second the move may use).
        const QDateTime now = QDateTime::currentDateTime();
        for (int s = 0; s < 5; ++s) {
            QFile taken(path + QStringLiteral(".corrupt-") + now.addSecs(s).toString(QStringLiteral("yyyyMMdd-HHmmss")) + QStringLiteral("-wal"));
            QVERIFY(taken.open(QIODevice::WriteOnly));
        }

        Database db(path);
        QVERIFY(!db.open());
        QVERIFY(db.movedAsidePath().isEmpty());
        QFile main(path);
        QVERIFY(main.open(QIODevice::ReadOnly));
        QCOMPARE(main.readAll(), QByteArray("not a database"));
        QVERIFY(QFile::exists(path + QStringLiteral("-wal")));
    }

    void damagedDatabaseIsMovedAsideAsCorrupt()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sugo.db"));
        {
            Database db(path);
            QVERIFY(db.open());
        }
        // Keep the 100-byte file header, wreck the schema page behind it.
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.seek(100));
        file.write(QByteArray(1000, '\xAB'));
        file.close();

        Database db(path);
        QVERIFY2(db.open(), qPrintable(db.lastError()));
        QVERIFY(db.movedAsideWasCorrupt());
        QVERIFY(db.movedAsidePath().contains(QStringLiteral(".corrupt-")));
        QCOMPARE(db.schemaVersion(), Database::latestSchemaVersion());
        QVERIFY(QFile::exists(db.movedAsidePath()));
    }

    void lockedOwnDatabaseIsNeitherMovedNorReplaced()
    {
        // Another process holding the file (a backup tool, an sqlite3 shell) says nothing about
        // whose file it is: opening must fail and leave the history where it is.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sugo.db"));
        {
            Database db(path);
            QVERIFY(db.open());
        }

        const QString lockName = QStringLiteral("locker");
        {
            QSqlDatabase locker = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), lockName);
            locker.setDatabaseName(path);
            locker.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=0"));
            QVERIFY(locker.open());
            QSqlQuery query(locker);
            QVERIFY(query.exec(QStringLiteral("PRAGMA locking_mode = EXCLUSIVE")));
            QVERIFY(query.exec(QStringLiteral("BEGIN EXCLUSIVE")));
            QVERIFY(query.exec(QStringLiteral("SELECT count(*) FROM sqlite_master")));
        }

        {
            Database db(path);
            QVERIFY(!db.open());
            QVERIFY(!db.lastError().isEmpty());
            QVERIFY(db.movedAsidePath().isEmpty());
        }
        QVERIFY(movedAsideFiles(QDir(dir.path())).isEmpty());
        QVERIFY(QFile::exists(path));

        {
            QSqlDatabase locker = QSqlDatabase::database(lockName, false);
            QSqlQuery(locker).exec(QStringLiteral("ROLLBACK"));
            locker.close();
        }
        QSqlDatabase::removeDatabase(lockName);

        // Once the lock is gone the same file opens normally.
        Database db(path);
        QVERIFY2(db.open(), qPrintable(db.lastError()));
        QVERIFY(db.movedAsidePath().isEmpty());
    }

    void foreignDatabaseIsNotReportedAsCorrupt()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sugo.db"));
        createForeignDatabase(path, QStringLiteral("CREATE TABLE notes (text TEXT)"), 0, 42);
        Database db(path);
        QVERIFY(db.open());
        QVERIFY(!db.movedAsidePath().isEmpty());
        QVERIFY(!db.movedAsideWasCorrupt());
    }

    void inMemoryDatabaseIsTemporary()
    {
        Database memory(QStringLiteral(":memory:"));
        QVERIFY(memory.open());
        QVERIFY(memory.isTemporary());

        QTemporaryDir dir;
        Database file(dir.filePath(QStringLiteral("sugo.db")));
        QVERIFY(file.open());
        QVERIFY(!file.isTemporary());
    }

    void dataDirVariableReplacesDefaultLocation()
    {
        const QByteArray saved = qgetenv("SUGO_DATA_DIR");
        const bool wasSet = qEnvironmentVariableIsSet("SUGO_DATA_DIR");
        QTemporaryDir dir;
        qputenv("SUGO_DATA_DIR", QFile::encodeName(dir.path()));
        QCOMPARE(Database::defaultPath(), dir.filePath(QStringLiteral("sugo.db")));
        qunsetenv("SUGO_DATA_DIR");
        QVERIFY(Database::defaultPath().endsWith(QLatin1String("/sugo/sugo.db")));
        QVERIFY(Database::defaultPath() != dir.filePath(QStringLiteral("sugo.db")));
        if (wasSet) {
            qputenv("SUGO_DATA_DIR", saved);
        }
    }

    void unusablePathReportsAnError()
    {
        QTemporaryDir dir;
        // A regular file where a directory is needed.
        QFile blocker(dir.filePath(QStringLiteral("blocker")));
        QVERIFY(blocker.open(QIODevice::WriteOnly));
        blocker.close();

        Database db(dir.filePath(QStringLiteral("blocker/sugo.db")));
        QVERIFY(!db.open());
        QVERIFY(!db.lastError().isEmpty());
    }
};

QTEST_GUILESS_MAIN(DatabaseTest)
#include "tst_database.moc"
