// SPDX-License-Identifier: GPL-3.0-or-later
#include <QApplication>
#include <QSystemTrayIcon>
#include <QTest>

#include "TrayNotifier.h"

/// The fallback notifier: a message from the tray icon, its click running the default action.
class TrayNotifierTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void nothingWithoutAnIcon()
    {
        int clicks = 0;
        Notifier::Notification n;
        n.title = QStringLiteral("Work done");
        n.defaultAction = [&clicks](const QString &) { ++clicks; };

        TrayNotifier none([]() { return nullptr; });
        none.notify(this, n); // must not crash

        QSystemTrayIcon hidden;
        TrayNotifier notShown([&hidden]() { return &hidden; });
        notShown.notify(this, n);
        Q_EMIT hidden.messageClicked();
        QCOMPARE(clicks, 0); // a hidden icon shows no message, so nothing can be clicked
    }

    void clickRunsTheLatestDefaultAction()
    {
        QSystemTrayIcon icon(QIcon(QStringLiteral(":/nothing.png")));
        icon.show();
        if (!icon.isVisible() || !QSystemTrayIcon::supportsMessages()) {
            QSKIP("No tray icon with messages on this platform");
        }
        QString ran;
        TrayNotifier notifier([&icon]() { return &icon; });
        Notifier::Notification first;
        first.title = QStringLiteral("First");
        first.defaultAction = [&ran](const QString &) { ran = QStringLiteral("first"); };
        Notifier::Notification second;
        second.title = QStringLiteral("Second");
        second.defaultAction = [&ran](const QString &) { ran = QStringLiteral("second"); };

        notifier.notify(this, first);
        notifier.notify(this, second);
        Q_EMIT icon.messageClicked();
        QCOMPARE(ran, QStringLiteral("second"));

        // The object the message was about is gone: the click does nothing.
        ran.clear();
        auto *context = new QObject;
        notifier.notify(context, first);
        delete context;
        Q_EMIT icon.messageClicked();
        QVERIFY(ran.isEmpty());
    }
};

QTEST_MAIN(TrayNotifierTest)
#include "tst_traynotifier.moc"
