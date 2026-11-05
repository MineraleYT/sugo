// SPDX-License-Identifier: GPL-3.0-or-later
#include <QObject>
#include <QTest>

#include "QuitPolicy.h"

using QuitPolicy::Origin;

Q_DECLARE_METATYPE(QuitPolicy::Origin)

class QuitPolicyTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void needsConfirmation_data()
    {
        QTest::addColumn<Origin>("origin");
        QTest::addColumn<bool>("phaseActive");
        QTest::addColumn<bool>("asks");

        // The Dock's Quit (or AppleScript's) with a phase running or paused: ask first.
        QTest::newRow("external, phase active") << Origin::External << true << true;
        QTest::newRow("external, idle") << Origin::External << false << false;
        // Sugo's own quit: the window asked already, or it is a signal.
        QTest::newRow("application, phase active") << Origin::Application << true << false;
        QTest::newRow("application, idle") << Origin::Application << false << false;
        // Logging out or shutting down: refusing would cancel it.
        QTest::newRow("session ending, phase active") << Origin::SessionEnding << true << false;
        QTest::newRow("session ending, idle") << Origin::SessionEnding << false << false;
    }

    void needsConfirmation()
    {
        QFETCH(Origin, origin);
        QFETCH(bool, phaseActive);
        QFETCH(bool, asks);
        QCOMPARE(QuitPolicy::needsConfirmation(origin, phaseActive), asks);
    }
};

QTEST_MAIN(QuitPolicyTest)
#include "tst_quitpolicy.moc"
