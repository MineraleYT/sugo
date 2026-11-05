// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QFile>
#include <QSqlDatabase>
#include <QString>

/**
 * A database file with a damaged page, to make a query fail partway through its rows, as a bad
 * sector or an I/O error would. Database::open() would move such a file aside (its quick check
 * reads every page), so the damage is done after the file was written and closed, and the file is
 * opened again here without any check.
 */
namespace DamagedDatabase
{
/// Overwrites the header of every page of the closed SQLite file `path` that holds `marker`, so
/// reading one of them fails with SQLITE_CORRUPT. Returns how many pages were damaged.
inline int damagePagesHolding(const QString &path, const QByteArray &marker)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite)) {
        return 0;
    }
    const QByteArray data = file.readAll();
    if (data.size() < 100) {
        return 0;
    }
    // Bytes 16-17 of the file header: the page size, big endian (1 stands for 65536).
    int pageSize = (quint8(data.at(16)) << 8) | quint8(data.at(17));
    if (pageSize == 1) {
        pageSize = 65536;
    }
    int damaged = 0;
    qint64 lastPage = -1;
    for (qsizetype at = data.indexOf(marker); at >= 0; at = data.indexOf(marker, at + 1)) {
        const qint64 page = at / pageSize;
        if (page == 0 || page == lastPage) {
            continue; // page 1 holds the schema: damaging it would fail every query
        }
        lastPage = page;
        // The first byte of a b-tree page header is the page type: 0xff is none.
        if (!file.seek(page * pageSize) || file.write(QByteArray(8, '\xff')) != 8) {
            return 0;
        }
        ++damaged;
    }
    return damaged;
}

/// An SQLite connection to `path`, opened without Database's checks; removed when destroyed.
class Connection
{
public:
    Connection(const QString &path, const QString &name)
        : m_name(name)
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_name);
        db.setDatabaseName(path);
        m_open = db.open();
    }
    ~Connection()
    {
        {
            QSqlDatabase db = QSqlDatabase::database(m_name, false);
            db.close();
        }
        QSqlDatabase::removeDatabase(m_name);
    }
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    bool isOpen() const { return m_open; }
    QString name() const { return m_name; }

private:
    QString m_name;
    bool m_open = false;
};
} // namespace DamagedDatabase
