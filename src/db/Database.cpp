// SPDX-License-Identifier: GPL-3.0-or-later
#include "Database.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QStringList>
#include <QUuid>
#include <QtGlobal>

#include <algorithm>

namespace
{
/// One entry per schema version, in order. Never edit a released entry: append a new one.
/// Sugo 1.0.0 numbered its versions differently; Database::convertSugo100File() renumbers its files.
const QList<QStringList> &migrations()
{
    static const QList<QStringList> list = {
        // Version 1
        {
            QStringLiteral(R"(
CREATE TABLE meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
))"),
            QStringLiteral(R"(
CREATE TABLE preset (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    uuid               TEXT    NOT NULL UNIQUE,
    name               TEXT    NOT NULL,
    category           TEXT    NOT NULL DEFAULT '',
    work_sec           INTEGER NOT NULL CHECK (work_sec > 0),
    short_break_sec    INTEGER NOT NULL CHECK (short_break_sec >= 0),
    long_break_sec     INTEGER NOT NULL CHECK (long_break_sec >= 0),
    cycles_before_long INTEGER NOT NULL DEFAULT 4 CHECK (cycles_before_long >= 0),
    is_builtin         INTEGER NOT NULL DEFAULT 0,
    sort_order         INTEGER NOT NULL DEFAULT 0,
    created_at         INTEGER NOT NULL
))"),
            QStringLiteral(R"(
CREATE TABLE preset_option (
    preset_id INTEGER NOT NULL REFERENCES preset(id) ON DELETE CASCADE,
    key       TEXT    NOT NULL,
    value     TEXT    NOT NULL,
    PRIMARY KEY (preset_id, key)
))"),
            QStringLiteral(R"(
CREATE TABLE session (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    kind         TEXT    NOT NULL CHECK (kind IN ('work', 'short_break', 'long_break')),
    started_at   INTEGER NOT NULL,
    ended_at     INTEGER NOT NULL,
    duration_sec INTEGER NOT NULL,
    planned_sec  INTEGER NOT NULL,
    completed    INTEGER NOT NULL,
    preset_id    INTEGER REFERENCES preset(id) ON DELETE SET NULL,
    preset_name  TEXT    NOT NULL,
    category     TEXT    NOT NULL DEFAULT '',
    note         TEXT    NOT NULL DEFAULT ''
))"),
            QStringLiteral("CREATE INDEX idx_session_started ON session(started_at)"),
            QStringLiteral("CREATE INDEX idx_session_kind_started ON session(kind, started_at)"),
        },
        // Version 2: what Sugo Sync needs to merge timers (docs/sync-plan.md). Preset times are
        // milliseconds since the epoch (UTC) from here on; version 1 stored created_at in seconds.
        // updated_at 0: not changed since this version. updated_by / deleted_by: the Sugo Sync
        // device that made the change, empty for this computer.
        {
            QStringLiteral("ALTER TABLE preset ADD COLUMN updated_at INTEGER NOT NULL DEFAULT 0"),
            QStringLiteral("ALTER TABLE preset ADD COLUMN updated_by TEXT NOT NULL DEFAULT ''"),
            QStringLiteral("UPDATE preset SET created_at = created_at * 1000"),
            QStringLiteral(R"(
CREATE TABLE preset_tombstone (
    uuid       TEXT    PRIMARY KEY,
    deleted_at INTEGER NOT NULL,
    deleted_by TEXT    NOT NULL DEFAULT ''
))"),
        },
    };
    return list;
}

bool run(QSqlDatabase &db, const QString &sql, QString *error)
{
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        *error = query.lastError().text();
        return false;
    }
    return true;
}

int pragmaInt(QSqlDatabase &db, const QString &name)
{
    QSqlQuery query(db);
    return query.exec(QStringLiteral("PRAGMA ") + name) && query.next() ? query.value(0).toInt() : -1;
}
} // namespace

Database::Database(const QString &path)
    : m_path(path)
    , m_connectionName(QStringLiteral("sugo-") + QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}

Database::~Database()
{
    closeConnection();
}

int Database::latestSchemaVersion()
{
    return int(migrations().size());
}

QString Database::defaultPath()
{
    // Tests and smoke runs (see Settings::defaultPath()).
    const QString dataDir = qEnvironmentVariable("SUGO_DATA_DIR");
    if (!dataDir.isEmpty()) {
        return dataDir + QStringLiteral("/sugo.db");
    }
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/sugo/sugo.db");
}

void Database::closeConnection()
{
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
        if (db.isValid() && db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool Database::connect(bool *corrupt)
{
    if (corrupt) {
        *corrupt = false;
    }
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    db.setDatabaseName(m_path);
    if (!db.open()) {
        m_error = db.lastError().text();
        if (corrupt) {
            *corrupt = isCorruptionError(db.lastError().nativeErrorCode());
        }
        return false;
    }
    // Foreign keys are off by default in SQLite and must be enabled per connection.
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        m_error = query.lastError().text();
        if (corrupt) {
            *corrupt = isCorruptionError(query.lastError().nativeErrorCode());
        }
        return false;
    }
    // Deleted rows (cleared history, private notes) are overwritten rather than left readable in
    // free pages. Like foreign_keys, a setting of the connection: it writes nothing by itself.
    if (!query.exec(QStringLiteral("PRAGMA secure_delete = ON"))) {
        m_error = query.lastError().text();
        if (corrupt) {
            *corrupt = isCorruptionError(query.lastError().nativeErrorCode());
        }
        return false;
    }
    return true;
}

bool Database::hasSqliteHeader() const
{
    QFile file(m_path);
    if (!file.exists() || file.size() == 0) {
        return true; // nothing there yet: SQLite creates the file
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return true; // unreadable is not the same as damaged; let SQLite report it
    }
    static const QByteArray magic("SQLite format 3\0", 16);
    return file.read(magic.size()) == magic;
}

bool Database::isCorruptionError(const QString &nativeCode)
{
    // SQLITE_CORRUPT (11) and SQLITE_NOTADB (26), possibly as extended codes. Anything else
    // (busy, locked, permissions...) says nothing about the file and must not move it.
    bool ok = false;
    const int code = nativeCode.toInt(&ok) & 0xff;
    return ok && (code == 11 || code == 26);
}

bool Database::open()
{
    const bool inMemory = isTemporary();
    if (!inMemory && !QDir().mkpath(QFileInfo(m_path).absolutePath())) {
        m_error = QStringLiteral("Cannot create the data directory for %1").arg(m_path);
        return false;
    }

    if (!inMemory && !hasSqliteHeader() && !moveFileAside(QStringLiteral("corrupt"))) {
        return false;
    }

    bool unopenable = false;
    if (!connect(&unopenable)) {
        if (inMemory || !unopenable || !moveFileAside(QStringLiteral("corrupt")) || !connect()) {
            return false;
        }
    }

    if (!inMemory) {
        // Decide ownership before any write or pragma that would modify a foreign file.
        // The handle must go out of scope before the connection is removed below.
        Ownership verdict = Ownership::Unknown;
        {
            QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
            verdict = ownership(db);
        }
        switch (verdict) {
        case Ownership::Ours:
            break;
        case Ownership::Corrupt:
            if (!moveFileAside(QStringLiteral("corrupt")) || !connect()) {
                return false;
            }
            break;
        case Ownership::Foreign:
            if (!moveFileAside(QStringLiteral("foreign")) || !connect()) {
                return false;
            }
            break;
        case Ownership::Unknown:
            // m_error says why; the caller falls back to a temporary database for this run.
            closeConnection();
            return false;
        }
    }

    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    if (!inMemory) {
        // WAL keeps writes cheap and readers unblocked; a crash cannot corrupt the file.
        if (!run(db, QStringLiteral("PRAGMA journal_mode = WAL"), &m_error)
            || !run(db, QStringLiteral("PRAGMA synchronous = NORMAL"), &m_error)) {
            return false;
        }
    }
    return migrate(db);
}

Database::Ownership Database::ownership(QSqlDatabase &db)
{
    // Unreadable schema or check: damaged or not a database. Other errors (busy, locked...) say
    // nothing about whose file it is.
    const auto failed = [this](const QSqlError &error) {
        m_error = error.text();
        return isCorruptionError(error.nativeErrorCode()) ? Ownership::Corrupt : Ownership::Unknown;
    };

    bool hasTables = false;
    {
        QSqlQuery query(db);
        if (!query.exec(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' LIMIT 1"))) {
            return failed(query.lastError());
        }
        hasTables = query.next();
    }

    if (!hasTables) {
        return Ownership::Ours; // new or empty file: ours to initialise
    }

    // A readable schema can still sit on top of damaged pages. The files are small, so the
    // quick check costs a few milliseconds.
    QSqlQuery check(db);
    if (!check.exec(QStringLiteral("PRAGMA quick_check(1)"))) {
        return failed(check.lastError());
    }
    if (!check.next()) {
        m_error = QStringLiteral("The integrity check returned no result");
        return Ownership::Corrupt;
    }
    const QString integrity = check.value(0).toString();
    if (integrity != QLatin1String("ok")) {
        m_error = QStringLiteral("The integrity check failed: %1").arg(integrity);
        return Ownership::Corrupt;
    }

    QSqlQuery idQuery(db);
    if (!idQuery.exec(QStringLiteral("PRAGMA application_id")) || !idQuery.next()) {
        return failed(idQuery.lastError());
    }
    const int applicationId = idQuery.value(0).toInt();
    if (applicationId == kApplicationId) {
        return Ownership::Ours;
    }
    if (applicationId == kSugo100ApplicationId) {
        // Written by Sugo 1.0.0, which stamped its files after creating the tables, so its files
        // have version 1 or 2. Any other version under that id was never written by a Sugo.
        QSqlQuery versionQuery(db);
        if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next()) {
            return failed(versionQuery.lastError());
        }
        const int version = versionQuery.value(0).toInt();
        return version >= 1 && version <= kSugo100SchemaVersion ? Ownership::Ours : Ownership::Foreign;
    }
    return Ownership::Foreign;
}

bool Database::moveFileAside(const QString &reason)
{
    closeConnection();

    const QString target = m_path + QLatin1Char('.') + reason + QLatin1Char('-')
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));

    // The journal files belong to the same database and must travel with it.
    QStringList moved;
    for (const QString &suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")}) {
        if (!QFile::exists(m_path + suffix)) {
            continue;
        }
        if (!QFile::rename(m_path + suffix, target + suffix)) {
            // Put back what was moved already: a journal must never be left with another file.
            for (const QString &done : std::as_const(moved)) {
                QFile::rename(target + done, m_path + done);
            }
            m_error = QStringLiteral("%1 is not a usable Sugo database and cannot be moved aside").arg(m_path);
            return false;
        }
        moved.append(suffix);
    }
    m_movedAsidePath = target;
    m_movedAsideCorrupt = reason == QLatin1String("corrupt");
    return true;
}

int Database::schemaVersion() const
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName, false);
    QSqlQuery query(db);
    if (query.exec(QStringLiteral("PRAGMA user_version")) && query.next()) {
        return query.value(0).toInt();
    }
    return -1;
}

bool Database::convertSugo100File(QSqlDatabase &db)
{
    // One transaction: the new id must never sit on top of 1.0.0's numbering, where version 2
    // means "session.note added" rather than "ready for Sugo Sync".
    if (!db.transaction()) {
        m_error = db.lastError().text();
        return false;
    }
    const int version = pragmaInt(db, QStringLiteral("user_version"));
    bool ok = version >= 1 && version <= kSugo100SchemaVersion;
    if (!ok) {
        m_error = QStringLiteral("Unexpected schema version %1 in a Sugo 1.0.0 database").arg(version);
    }
    // 1.0.0's version 2 added session.note, which today's version 1 creates with the table.
    if (ok && version < 2) {
        ok = run(db, QStringLiteral("ALTER TABLE session ADD COLUMN note TEXT NOT NULL DEFAULT ''"), &m_error);
    }
    ok = ok && run(db, QStringLiteral("PRAGMA user_version = 1"), &m_error)
        && run(db, QStringLiteral("PRAGMA application_id = %1").arg(kApplicationId), &m_error);
    if (!ok) {
        db.rollback();
        return false;
    }
    if (!db.commit()) {
        m_error = db.lastError().text();
        return false;
    }
    return true;
}

bool Database::migrate(QSqlDatabase &db)
{
    // A file of Sugo 1.0.0 (ownership() accepted it): renumber it first. One without tables or
    // version is simply stamped below, like any new file.
    if (pragmaInt(db, QStringLiteral("application_id")) == kSugo100ApplicationId && schemaVersion() > 0
        && !convertSugo100File(db)) {
        return false;
    }

    const int current = schemaVersion();
    if (current < 0) {
        m_error = QStringLiteral("Cannot read the schema version");
        return false;
    }
    if (current > latestSchemaVersion()) {
        m_error = QStringLiteral("The database was created by a newer version of Sugo (schema %1)").arg(current);
        return false;
    }

    // Stamp before the first table exists, so a file Sugo began to fill always reads as ours.
    if (pragmaInt(db, QStringLiteral("application_id")) != kApplicationId
        && !run(db, QStringLiteral("PRAGMA application_id = %1").arg(kApplicationId), &m_error)) {
        return false;
    }

    for (int version = current; version < latestSchemaVersion(); ++version) {
        if (!db.transaction()) {
            m_error = db.lastError().text();
            return false;
        }
        bool ok = true;
        for (const QString &statement : migrations().at(version)) {
            if (!run(db, statement, &m_error)) {
                ok = false;
                break;
            }
        }
        // PRAGMA takes no bound parameters; the value is an int we control.
        ok = ok && run(db, QStringLiteral("PRAGMA user_version = %1").arg(version + 1), &m_error);

        if (!ok) {
            db.rollback();
            return false;
        }
        if (!db.commit()) {
            m_error = db.lastError().text();
            return false;
        }
    }
    return true;
}
