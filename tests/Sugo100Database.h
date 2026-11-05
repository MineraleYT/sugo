// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A database as Sugo 1.0.0 wrote it, for the tests of opening, recovering and restoring one.

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QString>
#include <QTest>
#include <QUuid>

/// The application id Sugo 1.0.0 stamped its files with ("kTmt" in ASCII).
inline constexpr int kSugo100ApplicationId = 0x6B546D74;

/**
 * Writes a database exactly as Sugo 1.0.0 (tag v1.0.0) left it: its two migrations (version 1,
 * then version 2 adding session.note), user_version 2, the 1.0.0 application id stamped after
 * the migrations (here after the rows, which changes nothing), WAL mode, preset times in seconds
 * (session times in milliseconds). One timer with an option, one session with
 * a note, one meta key.
 */
inline void createSugo100Database(const QString &path)
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
                 "VALUES ('work', 1761876100000, 1761879100000, 3000, 3000, 1, 1, 'Deep work', 'Study', 'Chapter 3')",
                 "INSERT INTO meta (key, value) VALUES ('selectedPreset', 'mine')",
             }) {
            QVERIFY2(query.exec(QString::fromLatin1(statement)), statement);
        }
        QVERIFY(query.exec(QStringLiteral("PRAGMA application_id = %1").arg(kSugo100ApplicationId)));
        raw.close();
    }
    QSqlDatabase::removeDatabase(name);
}
