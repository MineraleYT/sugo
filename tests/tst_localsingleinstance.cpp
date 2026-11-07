// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QLockFile>
#include <QLoggingCategory>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTest>

#include "LocalSingleInstance.h"

class LocalSingleInstanceTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir dir;
    const QString name = QStringLiteral("sugo-test");

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        QLoggingCategory::setFilterRules(QStringLiteral("sugo.platform.localsingleinstance.warning=false"));
    }

    void theFirstLaunchBecomesThePrimaryInstance()
    {
        LocalSingleInstance primary(name, dir.path());
        QVERIFY(primary.acquire());
        QVERIFY(QFile::exists(primary.lockPath()));
        QVERIFY(QFile::exists(primary.socketPath()));
    }

    void aSecondLaunchAsksTheFirstToShowItselfAndShouldExit()
    {
        LocalSingleInstance primary(name, dir.path());
        QVERIFY(primary.acquire());
        QSignalSpy activated(&primary, &LocalSingleInstance::activateRequested);

        LocalSingleInstance later(name, dir.path());
        QVERIFY(!later.acquire());
        QTRY_COMPARE(activated.size(), 1);
    }

    void aBackgroundLaunchExitsWithoutShowingTheWindow()
    {
        LocalSingleInstance primary(name, dir.path());
        QVERIFY(primary.acquire());
        QSignalSpy activated(&primary, &LocalSingleInstance::activateRequested);

        LocalSingleInstance later(name, dir.path());
        QVERIFY(!later.acquire(false));
        QTest::qWait(50);
        QCOMPARE(activated.size(), 0);
    }

    void aRequestBeforeAnyoneListensIsKept()
    {
        LocalSingleInstance primary(name, dir.path());
        QVERIFY(primary.acquire());
        LocalSingleInstance later(name, dir.path());
        QVERIFY(!later.acquire());

        QSignalSpy activated(&primary, &LocalSingleInstance::activateRequested);
        QTRY_COMPARE(activated.size(), 1);
    }

    void theNextLaunchTakesOverOnceThePrimaryHasQuit()
    {
        {
            LocalSingleInstance primary(name, dir.path());
            QVERIFY(primary.acquire());
        }
        LocalSingleInstance next(name, dir.path());
        QVERIFY(next.acquire());
    }

    void whatACrashLeftBehindIsTakenOver()
    {
        const QString lockPath = dir.filePath(name + QStringLiteral(".lock"));
        const QString socketPath = dir.filePath(name + QStringLiteral(".sock"));
        // A lock file of a process that no longer runs (QLockFile's format: pid, app name, host),
        // and a socket file nobody listens on.
        {
            QFile lock(lockPath);
            QVERIFY(lock.open(QIODevice::WriteOnly | QIODevice::Truncate));
            lock.write("2147483646\nsugo\n" + QSysInfo::machineHostName().toUtf8() + '\n');
            QFile socket(socketPath);
            QVERIFY(socket.open(QIODevice::WriteOnly | QIODevice::Truncate));
        }
        LocalSingleInstance primary(name, dir.path());
        QVERIFY(primary.acquire());
        QSignalSpy activated(&primary, &LocalSingleInstance::activateRequested);
        LocalSingleInstance later(name, dir.path());
        QVERIFY(!later.acquire());
        QTRY_COMPARE(activated.size(), 1);
    }

    void aPrimaryThatDoesNotAnswerStillKeepsTheSecondOut()
    {
        // The lock is held, but nothing listens.
        QLockFile held(dir.filePath(QStringLiteral("silent.lock")));
        QVERIFY(held.tryLock(0));
        LocalSingleInstance later(QStringLiteral("silent"), dir.path());
        later.setReplyTimeout(200);
        QVERIFY(!later.acquire());
    }

    void aPrimaryThatHangsIsWaitedForOnlyOnce()
    {
        // The lock is held and the socket accepts connections, but nothing ever answers: the
        // first instance is alive but stuck. Asking it again would only double the wait.
        QLockFile held(dir.filePath(QStringLiteral("hung.lock")));
        QVERIFY(held.tryLock(0));
        LocalSingleInstance later(QStringLiteral("hung"), dir.path());
        QLocalServer hung;
        QLocalServer::removeServer(later.socketPath());
        QVERIFY(hung.listen(later.socketPath()));
        int connections = 0;
        connect(&hung, &QLocalServer::newConnection, &hung, [&]() {
            while (hung.nextPendingConnection()) {
                ++connections; // kept open (parented to the server), never answered
            }
        });

        constexpr int timeoutMs = 1000;
        later.setReplyTimeout(timeoutMs);
        QElapsedTimer elapsed;
        elapsed.start();
        QVERIFY(!later.acquire());
        const qint64 waited = elapsed.elapsed();
        // Two rounds would take at least twice the timeout, whatever the machine's load.
        QVERIFY2(waited < timeoutMs * 2, qPrintable(QStringLiteral("waited %1 ms").arg(waited)));
        QCOMPARE(connections, 1);
    }

    void aDirectoryTooLongForASocketStillReachesThePrimary()
    {
        // A socket path holds at most 103 bytes on macOS (107 on Linux): a long SUGO_DATA_DIR or
        // home directory must not leave the primary unreachable, and a second launch with nothing.
        const QString longDir = dir.filePath(QString(120, QLatin1Char('d')));
        QVERIFY(QDir().mkpath(longDir));
        LocalSingleInstance primary(name, longDir);
        QVERIFY(primary.acquire());
        QSignalSpy activated(&primary, &LocalSingleInstance::activateRequested);
        LocalSingleInstance later(name, longDir);
        QCOMPARE(later.socketPath(), primary.socketPath());
        QVERIFY(!later.acquire());
        QTRY_COMPARE(activated.size(), 1);

        QCOMPARE(QFileInfo(primary.lockPath()).absolutePath(), QFileInfo(longDir).absoluteFilePath());
        QVERIFY2(QFile::encodeName(primary.socketPath()).size() < 104, qPrintable(primary.socketPath()));
        QVERIFY(QFile::exists(primary.socketPath()));
        // Somewhere only this user can reach.
        const QFileInfo socketDir(QFileInfo(primary.socketPath()).absolutePath());
        QVERIFY(!socketDir.isSymLink());
        QCOMPARE(socketDir.permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup | QFile::ReadOther
                                            | QFile::WriteOther | QFile::ExeOther),
                 QFile::Permissions());

        // Another long directory is another instance (an isolated run): another socket.
        const QString otherDir = dir.filePath(QString(120, QLatin1Char('e')));
        QVERIFY(QDir().mkpath(otherDir));
        LocalSingleInstance other(name, otherDir);
        QVERIFY(other.socketPath() != primary.socketPath());
        QVERIFY(other.acquire());
    }

    void aFallbackDirectoryOthersCanEnterIsNotUsed()
    {
        // Someone made the short directory first, open to everyone: no socket goes there (anyone
        // could replace it and hear what later launches send).
        const QString longDir = dir.filePath(QString(120, QLatin1Char('f')));
        QVERIFY(QDir().mkpath(longDir));
        const QString openName = QStringLiteral("sugo-test-open");
        LocalSingleInstance primary(openName, longDir);
        const QString socketDir = QFileInfo(primary.socketPath()).absolutePath();
        QVERIFY(socketDir != QFileInfo(longDir).absoluteFilePath());
        QDir(socketDir).removeRecursively();
        QVERIFY(QDir().mkpath(socketDir));
        QVERIFY(QFile::setPermissions(socketDir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadOther
                                                     | QFile::ExeOther));
        QVERIFY(primary.acquire());
        QVERIFY(!QFile::exists(primary.socketPath()));
        QDir(socketDir).removeRecursively();
    }

    void defaultNamesDifferPerUser()
    {
        const QString n = LocalSingleInstance::defaultName(QStringLiteral("sugo"));
        QVERIFY(n.startsWith(QStringLiteral("sugo-")));
        QVERIFY(n.size() > 5);
        QVERIFY(!LocalSingleInstance::defaultDirectory().isEmpty());
    }

#ifdef Q_OS_MACOS
    void theDefaultDirectoryIsNotOneTheSystemCleans()
    {
        // The lock must not live in $TMPDIR: the system empties it, and the launch that then
        // finds no lock becomes a second primary on the same database.
        const QByteArray saved = qgetenv("SUGO_DATA_DIR");
        qunsetenv("SUGO_DATA_DIR");
        const QString directory = QDir::cleanPath(LocalSingleInstance::defaultDirectory());
        if (!saved.isNull()) {
            qputenv("SUGO_DATA_DIR", saved);
        }

        const QString support = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
        QVERIFY(!support.isEmpty());
        QVERIFY2(directory.startsWith(support + QLatin1Char('/')), qPrintable(directory));
        QVERIFY2(!directory.startsWith(QDir::cleanPath(QDir::tempPath())), qPrintable(directory));
    }
#endif
};

QTEST_MAIN(LocalSingleInstanceTest)

#include "tst_localsingleinstance.moc"
