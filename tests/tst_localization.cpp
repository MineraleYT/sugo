// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "Localization.h"

/// The i18n functions: English texts, plural forms and placeholders.
class LocalizationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void englishTexts()
    {
        QCOMPARE(i18n("Work"), QStringLiteral("Work"));
        QCOMPARE(i18nc("clock hour", "%1:00", 14), QStringLiteral("14:00"));
    }

    void pluralForms()
    {
        QCOMPARE(i18np("%1 session", "%1 sessions", 1), QStringLiteral("1 session"));
        QCOMPARE(i18np("%1 session", "%1 sessions", 0), QStringLiteral("0 sessions"));
        QCOMPARE(i18np("%1 session", "%1 sessions", 3), QStringLiteral("3 sessions"));
        QCOMPARE(i18ncp("work sessions in a cycle", "%1 session of %2", "%1 sessions of %2", 2, 4), QStringLiteral("2 sessions of 4"));
    }

    void placeholdersAreFilledOnce()
    {
        QCOMPARE(Localization::substitute(QStringLiteral("%2 then %1"), {QStringLiteral("%2"), QStringLiteral("b")}),
                 QStringLiteral("b then %2"));
        QCOMPARE(Localization::substitute(QStringLiteral("100% of %1, %3 unknown"), {QStringLiteral("x")}),
                 QStringLiteral("100% of x, %3 unknown"));
        QCOMPARE(i18n("%1 / %2", QStringLiteral("a"), 2.5), QStringLiteral("a / 2.5"));
    }

    void qmlFunctions()
    {
        const LocalizedContext context;
        QCOMPARE(context.i18n(QStringLiteral("%1 min"), 25), QStringLiteral("25 min"));
        QCOMPARE(context.i18np(QStringLiteral("%1 timer"), QStringLiteral("%1 timers"), 1), QStringLiteral("1 timer"));
        QCOMPARE(context.i18np(QStringLiteral("%1 timer"), QStringLiteral("%1 timers"), 4), QStringLiteral("4 timers"));
    }
};

QTEST_GUILESS_MAIN(LocalizationTest)
#include "tst_localization.moc"
