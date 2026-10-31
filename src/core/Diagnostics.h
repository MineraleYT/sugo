// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

class AppSettings;
class DataManager;

/**
 * Provides system diagnostics and redacted log export for issue reporting.
 *
 * Automatically redacts usernames, personal paths, and sensitive data so
 * users can safely attach system details and recent application logs
 * to bug reports.
 */
class Diagnostics : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Diagnostics(QObject *parent = nullptr);
    ~Diagnostics() override;

    void attach(AppSettings *settings, DataManager *dataManager);

    /// Formats the complete markdown diagnostics report (system + config + logs).
    Q_INVOKABLE QString generateReport() const;

    /// Writes the report to a local file (a file:// URL or an absolute path). Returns false and
    /// sets lastExportError() if the location is not local or the file cannot be written.
    Q_INVOKABLE bool exportReport(const QUrl &fileUrl) const;

    /// Why the last exportReport() failed, readable by the user; empty after a success.
    Q_INVOKABLE QString lastExportError() const { return m_lastExportError; }

    /// Copies the full markdown report to the system clipboard.
    Q_INVOKABLE void copyToClipboard() const;

    /// Returns the concatenated recent logs (already redacted).
    QString recentLogs() const;

    /// Redacts personal paths (the home directory, /home/<name>), the user name and the
    /// user part of user@host from text.
    static QString redact(const QString &text);
    /// Words that identify the user's accounts (the calendar login and server), redacted from
    /// every report and log line from now on; replaces the previous list.
    static void setAccountRedactions(const QStringList &words);

    /// Installs Qt message handler to capture application log messages into a ring buffer.
    static void installLogHandler();

    /// Appends a log line to the internal buffer (for testing and handler).
    static void addLogEntry(const QString &level, const QString &category, const QString &message);

    /// Clears the log buffer (primarily for testing).
    static void clearLogs();

    /// Verbose logging: Sugo's debug messages are let through (sugo.* categories) and the log
    /// buffer holds more lines. Off restores Qt's default rules; QT_LOGGING_RULES from the
    /// environment wins over both. Follows AppSettings::verboseLogging once attached.
    static void setVerboseLogging(bool verbose);
    static bool verboseLogging();
    /// How many log lines the buffer keeps now.
    static int logCapacity();

private:
    AppSettings *m_settings = nullptr;
    DataManager *m_dataManager = nullptr;
    mutable QString m_lastExportError;
    QMetaObject::Connection m_verboseConnection;
};
