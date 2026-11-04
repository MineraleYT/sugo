// SPDX-License-Identifier: GPL-3.0-or-later
// macOS only: secrets in the login Keychain.
#include <QTest>
#include <QUuid>

#include <Security/Security.h>

#include "MacKeychainStore.h"

class MacKeychainStoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void writeReadOverwriteAndRemove()
    {
        // A service of its own, so the user's real calendar password is never touched.
        MacKeychainStore store(QStringLiteral("io.github.mineraleyt.sugo.test-")
                               + QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString key = QStringLiteral("calendar-test");

        const auto missing = store.read(key);
        if (store.lastStatus() == errSecInteractionNotAllowed) {
            QSKIP("The Keychain is locked and no one can unlock it here (CI)");
        }
        QVERIFY(missing.has_value());
        QVERIFY(missing->isEmpty());

        if (!store.write(key, QStringLiteral("first pässword"))) {
            if (store.lastStatus() == errSecInteractionNotAllowed) {
                QSKIP("The Keychain is locked and no one can unlock it here (CI)");
            }
            QFAIL(qPrintable(QStringLiteral("SecItemAdd failed with OSStatus %1").arg(store.lastStatus())));
        }
        QCOMPARE(store.read(key).value_or(QStringLiteral("<read failed>")), QStringLiteral("first pässword"));

        QVERIFY(store.write(key, QStringLiteral("second"))); // SecItemUpdate of the same item
        QCOMPARE(store.read(key).value_or(QStringLiteral("<read failed>")), QStringLiteral("second"));

        QVERIFY(store.write(key, QString())); // an empty secret removes the item
        QCOMPARE(store.read(key).value_or(QStringLiteral("<read failed>")), QString());
        QVERIFY(store.write(key, QString())); // removing what is not there succeeds
    }
};

QTEST_GUILESS_MAIN(MacKeychainStoreTest)
#include "tst_mackeychainstore.moc"
