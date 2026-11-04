// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>

#include <csignal>

#include "UnixSignals.h"

namespace
{
bool hasDefaultAction(int number)
{
    struct sigaction current {};
    ::sigaction(number, nullptr, &current);
    return current.sa_handler == SIG_DFL;
}
} // namespace

class UnixSignalsTest : public QObject
{
    Q_OBJECT

private:
    QObject owner;

private Q_SLOTS:
    void initTestCase()
    {
        installQuitSignalHandlers(&owner);
    }

    void theQuitSignalsAreCaught()
    {
        QVERIFY(!hasDefaultAction(SIGINT));
        QVERIFY(!hasDefaultAction(SIGTERM));
        QVERIFY(!hasDefaultAction(SIGHUP));
    }

    void aSignalOnlyGivesItsOwnDefaultActionBack()
    {
        // The event loop never runs here, so the quit the handler asks for never happens.
        QCOMPARE(std::raise(SIGTERM), 0);
        QVERIFY(hasDefaultAction(SIGTERM)); // a second SIGTERM ends a shutdown that hangs
        QVERIFY(!hasDefaultAction(SIGINT));
        QVERIFY(!hasDefaultAction(SIGHUP));

        // systemd follows SIGTERM with SIGHUP: it must not end the process before
        // aboutToQuit has run (this test would die here).
        QCOMPARE(std::raise(SIGHUP), 0);
        QVERIFY(hasDefaultAction(SIGHUP));
        QVERIFY(!hasDefaultAction(SIGINT));
    }
};

QTEST_GUILESS_MAIN(UnixSignalsTest)
#include "tst_unixsignals.moc"
