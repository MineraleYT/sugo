// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QLockFile>
#include <QObject>
#include <QString>

#include <memory>

class QLocalServer;
class QLocalSocket;

/**
 * Keeps one running instance without a session bus (macOS): the same job as SingleInstance,
 * with a lock file and a local socket.
 *
 * The first process takes `<directory>/<name>.lock` (QLockFile: a lock left by a process that
 * died is taken over) and listens on `<directory>/<name>.sock`, reachable by the same user only.
 * A later launch finds the lock taken, connects to the socket, asks the first instance to show
 * itself and reports that it should exit. Under the lock, a socket file left behind by a crash
 * is removed before listening.
 */
class LocalSingleInstance : public QObject
{
    Q_OBJECT

public:
    /// `name` should differ per user when `directory` is shared (see defaultName()).
    LocalSingleInstance(const QString &name, const QString &directory = defaultDirectory(), QObject *parent = nullptr);
    ~LocalSingleInstance() override;

    /// A per-user directory for the lock and the socket: on macOS the application support
    /// directory (not $TMPDIR, which the system cleans), elsewhere $XDG_RUNTIME_DIR or the
    /// temporary directory.
    static QString defaultDirectory();
    /// `base` plus the user id, e.g. "sugo-501".
    static QString defaultName(const QString &base);

    /// True: this is the primary instance. False: another one runs and has been asked to
    /// come to the front (unless `activateExisting` is false, e.g. a `--background` login
    /// launch); the caller should exit. If another instance holds the lock but cannot be
    /// reached, the result is false too: two instances must never share the database. If the
    /// lock file cannot be used at all (the directory is not writable), there is nothing to
    /// coordinate with and the result is true.
    bool acquire(bool activateExisting = true);

    QString lockPath() const;
    QString socketPath() const;

    /// How long a later launch waits for the first instance's answer, which comes only once
    /// that one reaches its event loop (a cold start can take a while). For the tests.
    void setReplyTimeout(int milliseconds) { m_replyTimeoutMs = milliseconds; }

Q_SIGNALS:
    /// `activationToken` is what the later launch passed on (empty on macOS).
    /// A request that arrives before anything is connected is kept and delivered on connect.
    void activateRequested(const QString &activationToken);

protected:
    void connectNotify(const QMetaMethod &signal) override;

private:
    void listen();
    void onNewConnection();
    void onReadyRead(QLocalSocket *socket);
    void request(const QString &token);

    enum class Reply {
        Ok, ///< The first instance answered and will come to the front.
        Unreachable, ///< No connection, or it closed without an answer: the instance may be gone.
        NoAnswer, ///< Connected, but no answer in time: the instance is alive but busy or hung.
    };
    /// Asks the first instance to come to the front.
    Reply activateOther(const QString &token);

    QString m_directory;
    QString m_name;
    std::unique_ptr<QLockFile> m_lock;
    QLocalServer *m_server = nullptr;
    bool m_primary = false;
    bool m_hasPendingActivation = false;
    QString m_pendingToken;
    int m_replyTimeoutMs = 25000;
};
