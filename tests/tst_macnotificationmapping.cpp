// SPDX-License-Identifier: GPL-3.0-or-later
#include <QTest>

#include "MacNotificationMapping.h"

using namespace MacNotificationMapping;

namespace
{
Notifier::Notification withActions(const QStringList &labels, bool reply)
{
    Notifier::Notification n;
    n.title = QStringLiteral("Title");
    for (const QString &label : labels) {
        n.actions.append({label, [](const QString &) {}});
    }
    if (reply) {
        Notifier::Reply r;
        r.label = QStringLiteral("Log Task");
        r.placeholder = QStringLiteral("What did you accomplish?");
        r.submitLabel = QStringLiteral("Save");
        n.reply = r;
    }
    return n;
}
} // namespace

class MacNotificationMappingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void actionsBecomeButtonsAndTheReplyATextField()
    {
        const QList<ActionSpec> specs = actionsFor(withActions({QStringLiteral("Start"), QStringLiteral("Skip")}, true));
        QCOMPARE(specs.size(), 3);
        QCOMPARE(specs.at(0).identifier, QStringLiteral("action-0"));
        QCOMPARE(specs.at(0).title, QStringLiteral("Start"));
        QVERIFY(!specs.at(0).textInput);
        QCOMPARE(specs.at(1).identifier, QStringLiteral("action-1"));
        QCOMPARE(specs.at(2).identifier, replyActionId());
        QVERIFY(specs.at(2).textInput);
        QCOMPARE(specs.at(2).title, QStringLiteral("Log Task"));
        QCOMPARE(specs.at(2).submitTitle, QStringLiteral("Save"));
        QCOMPARE(specs.at(2).placeholder, QStringLiteral("What did you accomplish?"));
    }

    void categoriesDependOnEverythingTheButtonsAreMadeOf()
    {
        QVERIFY(categoryIdentifier({}).isEmpty());
        const QString a = categoryIdentifier(actionsFor(withActions({QStringLiteral("Start"), QStringLiteral("Skip")}, false)));
        const QString same = categoryIdentifier(actionsFor(withActions({QStringLiteral("Start"), QStringLiteral("Skip")}, false)));
        const QString otherLabel = categoryIdentifier(actionsFor(withActions({QStringLiteral("Start"), QStringLiteral("Skip Break")}, false)));
        const QString withReply = categoryIdentifier(actionsFor(withActions({QStringLiteral("Start"), QStringLiteral("Skip")}, true)));
        // Field boundaries matter: "ab"+"c" is not "a"+"bc".
        const QString shifted1 = categoryIdentifier(actionsFor(withActions({QStringLiteral("ab"), QStringLiteral("c")}, false)));
        const QString shifted2 = categoryIdentifier(actionsFor(withActions({QStringLiteral("a"), QStringLiteral("bc")}, false)));
        QVERIFY(a.startsWith(QStringLiteral("io.github.mineraleyt.sugo.actions.")));
        QCOMPARE(a, same);
        QVERIFY(a != otherLabel);
        QVERIFY(a != withReply);
        QVERIFY(shifted1 != shifted2);
    }

    void responsesRunTheirHandlerOnce()
    {
        MacNotificationRegistry registry;
        QObject context;
        QStringList ran;
        Notifier::Notification n;
        n.actions.append({QStringLiteral("Start"), [&ran](const QString &) { ran << QStringLiteral("start"); }});
        n.defaultAction = [&ran](const QString &) { ran << QStringLiteral("default"); };
        Notifier::Reply reply;
        reply.replied = [&ran](const QString &text) { ran << QStringLiteral("reply:") + text; };
        n.reply = reply;

        const QString first = registry.add(&context, n);
        const QString second = registry.add(&context, n);
        const QString third = registry.add(&context, n);
        QVERIFY(first != second);

        QVERIFY(registry.respond(first, QStringLiteral("action-0")));
        QVERIFY(!registry.respond(first, QStringLiteral("action-0"))); // forgotten once answered
        QVERIFY(registry.respond(second, defaultActionId()));
        QVERIFY(registry.respond(third, replyActionId(), QStringLiteral("notes")));
        QCOMPARE(ran, (QStringList{QStringLiteral("start"), QStringLiteral("default"), QStringLiteral("reply:notes")}));
        QCOMPARE(registry.size(), 0);
    }

    void unknownAndDismissedResponsesRunNothing()
    {
        MacNotificationRegistry registry;
        QObject context;
        bool ran = false;
        Notifier::Notification n;
        n.defaultAction = [&ran](const QString &) { ran = true; };
        const QString id = registry.add(&context, n);
        QVERIFY(!registry.respond(QStringLiteral("someone-else"), defaultActionId()));
        QVERIFY(!registry.respond(id, dismissActionId()));
        QVERIFY(!ran);
        QVERIFY(!registry.contains(id));
    }

    void handlersRunOnlyWhileTheirContextExists()
    {
        MacNotificationRegistry registry;
        bool ran = false;
        Notifier::Notification n;
        n.defaultAction = [&ran](const QString &) { ran = true; };
        auto *context = new QObject;
        const QString id = registry.add(context, n);
        delete context;
        QVERIFY(!registry.respond(id, defaultActionId()));
        QVERIFY(!ran);
    }

    void unansweredNotificationsAreDroppedOldestFirst()
    {
        MacNotificationRegistry registry(2);
        QObject context;
        Notifier::Notification n;
        const QString a = registry.add(&context, n);
        const QString b = registry.add(&context, n);
        const QString c = registry.add(&context, n);
        QCOMPARE(registry.size(), 2);
        QVERIFY(!registry.contains(a));
        QVERIFY(registry.contains(b));
        QVERIFY(registry.contains(c));
        registry.remove(b);
        QCOMPARE(registry.size(), 1);
    }

    void anUnshownRequestIsHandedBackWhole()
    {
        MacNotificationRegistry registry;
        QObject context;
        QStringList ran;
        Notifier::Notification n = withActions({QStringLiteral("Start")}, true);
        n.text = QStringLiteral("Break is over");
        n.defaultAction = [&ran](const QString &) { ran << QStringLiteral("default"); };
        n.reply->replied = [&ran](const QString &text) { ran << QStringLiteral("reply:") + text; };
        const QString id = registry.add(&context, n);

        const std::optional<MacNotificationRegistry::Unshown> unshown = registry.takeUnshown(id);
        QVERIFY(unshown.has_value());
        QCOMPARE(unshown->context, &context);
        QCOMPARE(unshown->notification.title, n.title);
        QCOMPARE(unshown->notification.text, n.text);
        QCOMPARE(unshown->notification.actions.size(), 1);
        QCOMPARE(unshown->notification.actions.at(0).label, QStringLiteral("Start"));
        QVERIFY(unshown->notification.reply.has_value());
        QCOMPARE(unshown->notification.reply->label, QStringLiteral("Log Task"));
        // The handlers come back too, so the other notifier can run them.
        unshown->notification.defaultAction(QString());
        unshown->notification.reply->replied(QStringLiteral("notes"));
        QCOMPARE(ran, (QStringList{QStringLiteral("default"), QStringLiteral("reply:notes")}));

        // Forgotten: neither handed back twice nor answerable.
        QVERIFY(!registry.contains(id));
        QVERIFY(!registry.takeUnshown(id).has_value());
        QVERIFY(!registry.respond(id, defaultActionId()));
    }

    void anUnshownRequestWithoutItsContextIsDropped()
    {
        MacNotificationRegistry registry;
        auto *context = new QObject;
        const QString id = registry.add(context, withActions({}, false));
        delete context;
        QVERIFY(!registry.takeUnshown(id).has_value());
        QVERIFY(!registry.contains(id));
        QVERIFY(!registry.takeUnshown(QStringLiteral("someone-else")).has_value());
    }
};

QTEST_MAIN(MacNotificationMappingTest)

#include "tst_macnotificationmapping.moc"
