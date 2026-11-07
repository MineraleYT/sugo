// SPDX-License-Identifier: GPL-3.0-or-later
#include "LocalSingleInstance.h"

#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLoggingCategory>
#include <QMetaMethod>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#endif

Q_LOGGING_CATEGORY(lcLocalSingleInstance, "sugo.platform.localsingleinstance")

namespace
{
const QByteArray kActivate = QByteArrayLiteral("activate");
const QByteArray kOk = QByteArrayLiteral("ok");
constexpr qint64 kMaxMessage = 4096;
/// How long the first instance keeps a connection that says nothing.
constexpr int kIdleConnectionMs = 5000;
/// How long a later launch keeps trying to connect: the first instance takes the lock a moment
/// before it listens.
constexpr int kConnectWindowMs = 2000;
constexpr int kConnectRetryMs = 100;

#ifdef Q_OS_UNIX
/// Whether a local socket can be bound at \a path: sun_path holds 104 bytes on macOS, 108 on
/// Linux, with the terminating NUL.
bool fitsSocketAddress(const QString &path)
{
    return size_t(QFile::encodeName(path).size()) < sizeof(sockaddr_un::sun_path);
}

/// Whether \a path is a directory of this user's that no one else can enter (created if it is
/// not there): not a link, owned by us, no access for group or others.
bool privateDirectory(const QString &path, bool create)
{
    const QByteArray encoded = QFile::encodeName(path);
    if (create && ::mkdir(encoded.constData(), S_IRWXU) != 0 && errno != EEXIST) {
        return false;
    }
    struct stat info = {};
    return ::lstat(encoded.constData(), &info) == 0 && S_ISDIR(info.st_mode) && info.st_uid == ::getuid()
        && (info.st_mode & (S_IRWXG | S_IRWXO)) == 0;
}
#endif
} // namespace

LocalSingleInstance::LocalSingleInstance(const QString &name, const QString &directory, QObject *parent)
    : QObject(parent)
    , m_directory(directory)
    , m_name(name)
    , m_socketPath(QDir(directory).filePath(name + QStringLiteral(".sock")))
{
#ifdef Q_OS_UNIX
    if (fitsSocketAddress(m_socketPath)) {
        return;
    }
    // Too long for a socket (a deep SUGO_DATA_DIR or home directory): in a directory of our own
    // under the temporary directory, named after the directory asked for, so that another
    // isolated run is still another instance. The lock stays where it was asked for.
    const QByteArray directoryHash =
        QCryptographicHash::hash(QFile::encodeName(QDir(directory).absolutePath()), QCryptographicHash::Sha256).toHex().left(16);
    const QString socketName = name + QLatin1Char('-') + QString::fromLatin1(directoryHash) + QStringLiteral(".sock");
    for (const QString &base : {QDir::tempPath(), QStringLiteral("/tmp")}) {
        const QString candidate = QDir(base).filePath(name) + QLatin1Char('/') + socketName;
        if (fitsSocketAddress(candidate)) {
            m_socketPath = candidate;
            m_socketDirectory = QDir(base).filePath(name);
            return;
        }
    }
#endif
}

LocalSingleInstance::~LocalSingleInstance()
{
    // The socket goes before the lock: once the lock is free a new instance may listen on the
    // same path, and closing our server then would remove its socket.
    if (m_server) {
        m_server->close();
        delete m_server;
        m_server = nullptr;
    }
    if (m_lock && m_primary) {
        m_lock->unlock();
    }
}

QString LocalSingleInstance::defaultDirectory()
{
    // An isolated run (tests, SUGO_DATA_DIR) is an instance of its own: it must not hand over to
    // the Sugo the user is running.
    const QString dataDir = qEnvironmentVariable("SUGO_DATA_DIR");
    if (!dataDir.isEmpty()) {
        return dataDir;
    }
#ifdef Q_OS_MACOS
    // Next to the database, the settings and the keychain handoff: the lock must outlive the
    // system's cleanup of temporary files, or a later launch would find no lock and become a
    // second primary on the same database.
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/sugo");
#else
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty() && QFileInfo(runtime).isDir()) {
        return runtime;
    }
    return QDir::tempPath();
#endif
}

QString LocalSingleInstance::defaultName(const QString &base)
{
#ifdef Q_OS_UNIX
    return base + QLatin1Char('-') + QString::number(::getuid());
#else
    return base + QLatin1Char('-') + qEnvironmentVariable("USERNAME");
#endif
}

QString LocalSingleInstance::lockPath() const
{
    return QDir(m_directory).filePath(m_name + QStringLiteral(".lock"));
}

QString LocalSingleInstance::socketPath() const
{
    return m_socketPath;
}

bool LocalSingleInstance::socketDirectoryIsOurs(bool create) const
{
#ifdef Q_OS_UNIX
    if (!m_socketDirectory.isEmpty() && !privateDirectory(m_socketDirectory, create)) {
        qCWarning(lcLocalSingleInstance) << "Cannot use" << m_socketDirectory << "for the socket: not a directory of this user's alone";
        return false;
    }
#else
    Q_UNUSED(create)
#endif
    return true;
}

bool LocalSingleInstance::acquire(bool activateExisting)
{
    if (m_primary) {
        return true;
    }
    QDir().mkpath(m_directory);
    m_lock = std::make_unique<QLockFile>(lockPath());
    // Never stale by age: only when the process that took it is gone.
    m_lock->setStaleLockTime(0);

    QString token;
#ifdef Q_OS_LINUX
    token = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
#endif

    // A second round: the other instance may have quit while this one tried to reach it. Not
    // after a connection that got no answer: that instance still runs and still holds the lock,
    // and asking again would only make this launch wait twice as long.
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (m_lock->tryLock(0)) {
            m_primary = true;
            listen();
            return true;
        }
        if (m_lock->error() != QLockFile::LockFailedError) {
            // Nothing to coordinate with (a directory we cannot write): run, as SingleInstance
            // does without a bus.
            qCWarning(lcLocalSingleInstance) << "Cannot use the lock file" << lockPath() << "- not checking for another instance";
            return true;
        }
        if (!activateExisting) {
            return false;
        }
        const Reply reply = activateOther(token);
        if (reply == Reply::Ok) {
            return false;
        }
        if (reply == Reply::NoAnswer) {
            break;
        }
    }
    qCWarning(lcLocalSingleInstance) << "Sugo is already running but did not answer";
    return false;
}

void LocalSingleInstance::listen()
{
    if (!socketDirectoryIsOurs(true)) {
        return;
    }
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    // We hold the lock, so a socket file there was left by an instance that ended badly.
    QLocalServer::removeServer(socketPath());
    connect(m_server, &QLocalServer::newConnection, this, &LocalSingleInstance::onNewConnection);
    if (!m_server->listen(socketPath())) {
        qCWarning(lcLocalSingleInstance) << "Cannot listen on" << socketPath() << ":" << m_server->errorString();
    }
}

void LocalSingleInstance::onNewConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
        QTimer::singleShot(kIdleConnectionMs, socket, [socket]() { socket->abort(); });
        if (socket->bytesAvailable() > 0) {
            onReadyRead(socket);
        }
    }
}

void LocalSingleInstance::onReadyRead(QLocalSocket *socket)
{
    if (!socket->canReadLine()) {
        if (socket->bytesAvailable() > kMaxMessage) {
            socket->abort();
        }
        return;
    }
    const QByteArray line = socket->readLine(kMaxMessage).trimmed();
    const QList<QByteArray> parts = line.split(' ');
    if (parts.value(0) != kActivate) {
        socket->abort();
        return;
    }
    socket->write(kOk + '\n');
    socket->disconnectFromServer(); // after the answer has been written
    request(QString::fromUtf8(QByteArray::fromBase64(parts.value(1))));
}

void LocalSingleInstance::request(const QString &token)
{
    if (!isSignalConnected(QMetaMethod::fromSignal(&LocalSingleInstance::activateRequested))) {
        // Nobody can show a window yet (still starting up): keep the request for later.
        m_hasPendingActivation = true;
        m_pendingToken = token;
        return;
    }
    Q_EMIT activateRequested(token);
}

LocalSingleInstance::Reply LocalSingleInstance::activateOther(const QString &token)
{
    if (!socketDirectoryIsOurs(false)) {
        return Reply::Unreachable;
    }
    QLocalSocket socket;
    const QDeadlineTimer connectDeadline(qMin(kConnectWindowMs, m_replyTimeoutMs));
    while (true) {
        socket.connectToServer(socketPath());
        if (socket.waitForConnected(kConnectRetryMs)) {
            break;
        }
        socket.abort();
        if (connectDeadline.hasExpired()) {
            return Reply::Unreachable;
        }
        QThread::msleep(kConnectRetryMs);
    }

    // The answer comes from the other instance's event loop, which may be this process' own
    // (the tests): wait in an event loop rather than blocking.
    QEventLoop loop;
    QByteArray received;
    bool answered = false;
    bool timedOut = false;
    connect(&socket, &QLocalSocket::readyRead, &loop, [&]() {
        received += socket.readAll();
        if (received.contains('\n')) {
            answered = received.trimmed() == kOk;
            loop.quit();
        }
    });
    connect(&socket, &QLocalSocket::disconnected, &loop, &QEventLoop::quit);
    connect(&socket, &QLocalSocket::errorOccurred, &loop, &QEventLoop::quit);
    QTimer::singleShot(m_replyTimeoutMs, &loop, [&]() {
        timedOut = true;
        loop.quit();
    });

    QByteArray message = kActivate;
    if (!token.isEmpty()) {
        message += ' ' + token.toUtf8().toBase64();
    }
    socket.write(message + '\n');
    socket.flush();
    if (socket.state() == QLocalSocket::ConnectedState) {
        loop.exec();
    }
    // The answer may have arrived together with the hang-up.
    if (!answered) {
        received += socket.readAll();
        answered = received.trimmed() == kOk;
    }
    if (answered) {
        return Reply::Ok;
    }
    return timedOut ? Reply::NoAnswer : Reply::Unreachable;
}

void LocalSingleInstance::connectNotify(const QMetaMethod &signal)
{
    if (signal == QMetaMethod::fromSignal(&LocalSingleInstance::activateRequested) && m_hasPendingActivation) {
        m_hasPendingActivation = false;
        // Queued, so the receiver's connect() has returned before it is called.
        QMetaObject::invokeMethod(
            this, [this, token = m_pendingToken]() { Q_EMIT activateRequested(token); }, Qt::QueuedConnection);
        m_pendingToken.clear();
    }
}
