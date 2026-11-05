// SPDX-License-Identifier: GPL-3.0-or-later
// macOS only: secrets in the login Keychain.
#include <QTest>
#include <QUuid>

#include <Security/Security.h>

#include "MacKeychainStore.h"

namespace
{
/// A locked Keychain no one can unlock here (CI), or no login session to ask in (an SSH shell:
/// errAuthorizationInternal).
bool unreachableKeychain(OSStatus status)
{
    return status == errSecInteractionNotAllowed || status == errAuthorizationInternal;
}
} // namespace

class MacKeychainStoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void writeReadOverwriteAndRemove()
    {
        // Writing to the Keychain can make macOS show a dialog (no Keychain in an SSH session,
        // a locked one...), so the test runs only when asked for, in a logged-in desktop session.
        if (qEnvironmentVariable("SUGO_TEST_KEYCHAIN") != QStringLiteral("1")) {
            QSKIP("Set SUGO_TEST_KEYCHAIN=1 to run it, from a terminal of the logged-in desktop session");
        }
        // A service of its own, so the user's real calendar password is never touched.
        MacKeychainStore store(QStringLiteral("io.github.mineraleyt.sugo.test-")
                               + QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString key = QStringLiteral("calendar-test");

        const auto missing = store.read(key);
        if (unreachableKeychain(store.lastStatus())) {
            QSKIP("The Keychain cannot be reached here (locked on CI, or no login session)");
        }
        QVERIFY(missing.has_value());
        QVERIFY(missing->isEmpty());

        if (!store.write(key, QStringLiteral("first pässword"))) {
            if (unreachableKeychain(store.lastStatus())) {
                QSKIP("The Keychain cannot be reached here (locked on CI, or no login session)");
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

    void failedWriteKeepsThePreviousSecret()
    {
        if (qEnvironmentVariable("SUGO_TEST_KEYCHAIN") != QStringLiteral("1")) {
            QSKIP("Set SUGO_TEST_KEYCHAIN=1 to run it, from a terminal of the logged-in desktop session");
        }
        const QString service = QStringLiteral("io.github.mineraleyt.sugo.test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        const QString key = QStringLiteral("calendar-test");
        MacKeychainStore store(service);
        if (!store.write(key, QStringLiteral("old"))) {
            if (unreachableKeychain(store.lastStatus())) {
                QSKIP("The Keychain cannot be reached here (locked on CI, or no login session)");
            }
            QFAIL(qPrintable(QStringLiteral("SecItemAdd failed with OSStatus %1").arg(store.lastStatus())));
        }

        // The old item is deleted, then adding the new one fails: the old secret is put back.
        store.failNextAddForTest(errSecInteractionNotAllowed);
        QVERIFY(!store.write(key, QStringLiteral("new")));
        QCOMPARE(store.lastStatus(), static_cast<int>(errSecInteractionNotAllowed));
        QCOMPARE(store.read(key).value_or(QStringLiteral("<read failed>")), QStringLiteral("old"));

        // A store that has only read the item knows it as well (Sugo reads its secrets at start).
        MacKeychainStore reader(service);
        QCOMPARE(reader.read(key).value_or(QStringLiteral("<read failed>")), QStringLiteral("old"));
        reader.failNextAddForTest(errSecDuplicateItem);
        QVERIFY(!reader.write(key, QStringLiteral("new")));
        QCOMPARE(reader.read(key).value_or(QStringLiteral("<read failed>")), QStringLiteral("old"));

        // Known to be absent: nothing is put back.
        QVERIFY(store.write(key, QString()));
        store.failNextAddForTest(errSecInteractionNotAllowed);
        QVERIFY(!store.write(key, QStringLiteral("new")));
        QCOMPARE(store.read(key).value_or(QStringLiteral("<read failed>")), QString());

        // After a failed write the store works as before.
        QVERIFY(store.write(key, QStringLiteral("third")));
        QCOMPARE(store.read(key).value_or(QStringLiteral("<read failed>")), QStringLiteral("third"));
        QVERIFY(store.write(key, QString()));
    }
};

QTEST_GUILESS_MAIN(MacKeychainStoreTest)
#include "tst_mackeychainstore.moc"
