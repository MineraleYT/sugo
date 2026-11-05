// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A database as Sugo 1.0.0 wrote it, for the tests of opening, recovering and restoring one.

#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QTest>
#include <QUuid>

/// The application id Sugo 1.0.0 stamped its files with ("kTmt" in ASCII).
inline constexpr int kSugo100ApplicationId = 0x6B546D74;

/// Where the last changes of a Sugo 1.0.0 file are.
enum class Sugo100Stamp {
    /// Folded into the file, as when 1.0.0 was closed.
    InFile,
    /// Still only in the write-ahead log: the file's header has no application id yet (1.0.0
    /// ended without a checkpoint, e.g. killed, or the file was copied while it ran).
    OnlyInWal,
};

/// The application id in a SQLite file's header (big-endian at offset 68), read without SQLite.
inline qint64 sqliteHeaderApplicationId(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return -1;
    }
    const QByteArray header = file.read(72);
    if (header.size() < 72) {
        return -1;
    }
    qint64 id = 0;
    for (int i = 68; i < 72; ++i) {
        id = (id << 8) | quint8(header.at(i));
    }
    return id;
}

/**
 * Writes a database exactly as Sugo 1.0.0 (tag v1.0.0) left it.
 *
 * Schema: 1.0.0's two migrations (version 1, then version 2 adding session.note), user_version 2,
 * the 1.0.0 application id stamped after the migrations, WAL mode.
 *
 * Content, as 1.0.0's PresetRepository and SessionRepository stored it: the built-in timer
 * `default` and one of the user's (`mine`), preset times in seconds; options as JSON arrays
 * (`[true]`), with the pair autoStartMode/autoStart that 1.0.0 mirrored; one session with a note,
 * its times in milliseconds; the meta keys `currentPresetUuid` and `presets_initialized`.
 */
inline void createSugo100Database(const QString &path, Sugo100Stamp stamp = Sugo100Stamp::InFile)
{
    // For OnlyInWal the file is written elsewhere and copied, with its log, while a second
    // connection keeps SQLite from folding the log into the file.
    const QString target = stamp == Sugo100Stamp::InFile ? path : path + QStringLiteral(".building");
    const QString name = QStringLiteral("v100-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString holder = name + QStringLiteral("-holder");
    {
        QSqlDatabase raw = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        raw.setDatabaseName(target);
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
             }) {
            QVERIFY2(query.exec(QString::fromLatin1(statement)), statement);
        }
        // v1.0.0 stamped the file once the migrations had run.
        QVERIFY(query.exec(QStringLiteral("PRAGMA application_id = %1").arg(kSugo100ApplicationId)));
        for (const char *statement : {
                 // What a user of 1.0.0 had saved: PresetRepository::saveAll() (sort_order is the
                 // position, created_at seconds) and setCurrentUuid(), PresetPersistence's flag.
                 "INSERT INTO preset (uuid, name, category, work_sec, short_break_sec, long_break_sec, "
                 "cycles_before_long, is_builtin, sort_order, created_at) "
                 "VALUES ('default', 'Pomodoro', 'Work', 1500, 300, 900, 4, 1, 0, 1761875000)",
                 "INSERT INTO preset (uuid, name, category, work_sec, short_break_sec, long_break_sec, "
                 "cycles_before_long, is_builtin, sort_order, created_at) "
                 "VALUES ('mine', 'Deep work', 'Study', 3000, 600, 1200, 3, 0, 1, 1761876000)",
                 "INSERT INTO preset_option (preset_id, key, value) VALUES (1, 'iconName', '[\"sugo-timer\"]')",
                 "INSERT INTO preset_option (preset_id, key, value) VALUES (1, 'silenceWhileWorking', '[true]')",
                 "INSERT INTO preset_option (preset_id, key, value) VALUES (2, 'iconName', '[\"sugo-focus\"]')",
                 "INSERT INTO preset_option (preset_id, key, value) VALUES (2, 'autoStartMode', '[2]')",
                 "INSERT INTO preset_option (preset_id, key, value) VALUES (2, 'autoStart', '[true]')",
                 "INSERT INTO preset_option (preset_id, key, value) VALUES (2, 'soundOnEnd', '[false]')",
                 "INSERT INTO session (kind, started_at, ended_at, duration_sec, planned_sec, completed, "
                 "preset_id, preset_name, category, note) "
                 "VALUES ('work', 1761876100000, 1761879100000, 3000, 3000, 1, 2, 'Deep work', 'Study', 'Chapter 3')",
                 "INSERT INTO meta (key, value) VALUES ('presets_initialized', '1')",
                 "INSERT INTO meta (key, value) VALUES ('currentPresetUuid', 'mine')",
             }) {
            QVERIFY2(query.exec(QString::fromLatin1(statement)), statement);
        }

        if (stamp == Sugo100Stamp::OnlyInWal) {
            QSqlDatabase second = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), holder);
            second.setDatabaseName(target);
            QVERIFY(second.open());
            QSqlQuery read(second);
            QVERIFY(read.exec(QStringLiteral("SELECT count(*) FROM sqlite_master")) && read.next());
            read.finish();
            raw.close(); // not the last connection: no checkpoint, the log stays
            QVERIFY(QFile::exists(target + QStringLiteral("-wal")));
            QVERIFY(QFile::copy(target, path));
            QVERIFY(QFile::copy(target + QStringLiteral("-wal"), path + QStringLiteral("-wal")));
            second.close();
        } else {
            raw.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    if (stamp == Sugo100Stamp::OnlyInWal) {
        QSqlDatabase::removeDatabase(holder);
        for (const QString &suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")}) {
            QFile::remove(target + suffix);
        }
        // The case is only worth testing if the stamp really is not in the file.
        QCOMPARE(sqliteHeaderApplicationId(path), 0);
    } else {
        QCOMPARE(sqliteHeaderApplicationId(path), qint64(kSugo100ApplicationId));
    }
}
