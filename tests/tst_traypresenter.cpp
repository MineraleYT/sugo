// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "Localization.h"

#include "TrayPresenter.h"

using State = TimerEngine::State;
using Phase = TimerEngine::Phase;

class TrayPresenterTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
    }

    void formatClockHandlesMinutesAndHours()
    {
        QCOMPARE(formatClock(0), QStringLiteral("0:00"));
        QCOMPARE(formatClock(5), QStringLiteral("0:05"));
        QCOMPARE(formatClock(61), QStringLiteral("1:01"));
        QCOMPARE(formatClock(25 * 60), QStringLiteral("25:00"));
        QCOMPARE(formatClock(59 * 60 + 59), QStringLiteral("59:59"));
        QCOMPARE(formatClock(3600), QStringLiteral("1:00:00"));
        QCOMPARE(formatClock(2 * 3600 + 5 * 60 + 9), QStringLiteral("2:05:09"));
        QCOMPARE(formatClock(-30), QStringLiteral("0:00")); // never negative
    }

    void aRunningPhaseShowsTimeNameAndABadge()
    {
        const TrayStatus s = describeTray(State::Working, Phase::Work, 24 * 60 + 12, QStringLiteral("Deep study"));
        QVERIFY(s.toolTipTitle.contains(QStringLiteral("Work")));
        QVERIFY(s.toolTipText.contains(QStringLiteral("24:12")));
        QVERIFY(s.toolTipText.contains(QStringLiteral("Deep study")));
        QVERIFY(s.title.contains(QStringLiteral("Work")));
        QCOMPARE(s.badgeMinutes, 25); // rounded up: 24:12 left is still "25" until it drops below 24:01
    }

    void theBadgeRoundsUpSoItNeverShowsZeroBeforeTheEnd()
    {
        QCOMPARE(describeTray(State::Working, Phase::Work, 60, {}).badgeMinutes, 1);
        QCOMPARE(describeTray(State::Working, Phase::Work, 61, {}).badgeMinutes, 2);
        QCOMPARE(describeTray(State::Working, Phase::Work, 1, {}).badgeMinutes, 1);
        QCOMPARE(describeTray(State::Working, Phase::Work, 25 * 60, {}).badgeMinutes, 25);
        QCOMPARE(describeTray(State::Working, Phase::Work, 24 * 60, {}).badgeMinutes, 24);
    }

    void breaksAreNamedAsBreaks()
    {
        QVERIFY(describeTray(State::ShortBreak, Phase::ShortBreak, 300, {}).toolTipTitle.contains(QStringLiteral("Short break")));
        QVERIFY(describeTray(State::LongBreak, Phase::LongBreak, 900, {}).toolTipTitle.contains(QStringLiteral("Long break")));
        QCOMPARE(describeTray(State::LongBreak, Phase::LongBreak, 900, {}).badgeMinutes, 15);
    }

    void pausedIsLabelledAndKeepsTheBadge()
    {
        const TrayStatus s = describeTray(State::Paused, Phase::Work, 10 * 60, QStringLiteral("Default"));
        QVERIFY(s.toolTipTitle.contains(QStringLiteral("paused")));
        QVERIFY(s.title.contains(QStringLiteral("paused")));
        QVERIFY(s.toolTipText.contains(QStringLiteral("10:00")));
        QCOMPARE(s.badgeMinutes, 10); // the time is frozen but still worth seeing
    }

    void idleShowsWhatComesNextAndHasNoBadge()
    {
        const TrayStatus work = describeTray(State::Idle, Phase::Work, 25 * 60, QStringLiteral("Default"));
        QCOMPARE(work.badgeMinutes, -1);
        QVERIFY(work.toolTipTitle.contains(QStringLiteral("ready")));
        QVERIFY(work.toolTipText.contains(QStringLiteral("Work")));
        QVERIFY(work.toolTipText.contains(QStringLiteral("25:00")));
        QVERIFY(work.toolTipText.contains(QStringLiteral("Default")));

        const TrayStatus pendingBreak = describeTray(State::Idle, Phase::ShortBreak, 5 * 60, QStringLiteral("Default"));
        QVERIFY(pendingBreak.toolTipText.contains(QStringLiteral("Short break")));
        QCOMPARE(pendingBreak.badgeMinutes, -1);
    }

    void everyStateHasANonEmptyTitleAndTooltip()
    {
        for (const State state : {State::Idle, State::Working, State::ShortBreak, State::LongBreak, State::Paused}) {
            const TrayStatus s = describeTray(state, Phase::Work, 100, QStringLiteral("x"));
            QVERIFY(!s.title.isEmpty());
            QVERIFY(!s.toolTipTitle.isEmpty());
            QVERIFY(!s.toolTipText.isEmpty());
        }
    }

    void menuStatusLineShowsPhaseClockAndName()
    {
        QCOMPARE(menuStatusLine(State::Working, Phase::Work, 12 * 60 + 30, QStringLiteral("Pomodoro")),
                 QStringLiteral("Work \u00b7 12:30 \u00b7 Pomodoro"));
        QCOMPARE(menuStatusLine(State::Working, Phase::ShortBreak, 4 * 60, QStringLiteral("Pomodoro")),
                 QStringLiteral("Short break \u00b7 4:00 \u00b7 Pomodoro"));
        QCOMPARE(menuStatusLine(State::Working, Phase::LongBreak, 3600 + 5, QStringLiteral("Deep")),
                 QStringLiteral("Long break \u00b7 1:00:05 \u00b7 Deep"));
    }

    void menuStatusLineMarksPausedAndShowsTheNextPhaseWhenIdle()
    {
        QCOMPARE(menuStatusLine(State::Paused, Phase::Work, 90, QStringLiteral("Pomodoro")),
                 QStringLiteral("Work (paused) \u00b7 1:30 \u00b7 Pomodoro"));
        QCOMPARE(menuStatusLine(State::Idle, Phase::LongBreak, 15 * 60, QStringLiteral("Pomodoro")),
                 QStringLiteral("Long break \u00b7 15:00 \u00b7 Pomodoro"));
    }

    void menuStatusLineFallsBackWhenTheNameIsEmpty()
    {
        QCOMPARE(menuStatusLine(State::Idle, Phase::Work, 25 * 60, QString()), QStringLiteral("Work \u00b7 25:00 \u00b7 Sugo"));
    }
};

QTEST_GUILESS_MAIN(TrayPresenterTest)
#include "tst_traypresenter.moc"
