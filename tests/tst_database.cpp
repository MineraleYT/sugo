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
                 (QStringList{QStringLiteral("meta"), QStringLiteral("preset"),
                              QStringLiteral("preset_option"), QStringLiteral("session")}));
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
                 (QStringList{QStringLiteral("meta"), QStringLiteral("preset"),
                              QStringLiteral("preset_option"), QStringLiteral("session")}));
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

    void ownDatabaseWithoutStampIsAdopted()
    {
        // Databases written before the stamp existed must keep their data.
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("sugo.db"));
        {
            Database db(path);
            QVERIFY(db.open());
            QSqlQuery query(QSqlDatabase::database(db.connectionName(), false));
            QVERIFY(query.exec(QStringLiteral("INSERT INTO meta (key, value) VALUES ('k', 'v')")));
            QVERIFY(query.exec(QStringLiteral("PRAGMA application_id = 0"))); // as an M4 development build left it
        }
        Database db(path);
        QVERIFY2(db.open(), qPrintable(db.lastError()));
        QVERIFY(db.movedAsidePath().isEmpty());
        QCOMPARE(scalar(db.connectionName(), QStringLiteral("SELECT value FROM meta WHERE key = 'k'")).toString(),
                 QStringLiteral("v"));
        QCOMPARE(scalar(db.connectionName(), QStringLiteral("PRAGMA application_id")).toInt(), Database::kApplicationId);
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
