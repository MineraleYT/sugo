// SPDX-License-Identifier: GPL-3.0-or-later
// AskOnceSecretStore: a secret the user refused is not asked for again (macOS asks with a dialog).
#include <QTest>

#include "SecretStore.h"

namespace
{
/// A keyring that counts how often it is asked, and can refuse or be out of reach.
class FakeKeyring : public SecretStore
{
public:
    enum class Mode { Ok, Refuses, Unavailable };

    struct State {
        Mode mode = Mode::Ok;
        int reads = 0;
        QHash<QString, QString> secrets;
    };
    std::shared_ptr<State> state = std::make_shared<State>();

    std::optional<QString> read(const QString &key) override
    {
        ++state->reads;
        m_refused = state->mode == Mode::Refuses;
        if (state->mode != Mode::Ok) {
            return std::nullopt;
        }
        return state->secrets.value(key);
    }
    bool write(const QString &key, const QString &secret) override
    {
        if (state->mode == Mode::Unavailable) {
            return false;
        }
        state->mode = Mode::Ok; // a new item is this program's own (MacKeychainStore::write)
        state->secrets.insert(key, secret);
        return true;
    }
    bool lastReadRefused() const override { return m_refused; }

private:
    bool m_refused = false;
};
} // namespace

class AskOnceSecretStoreTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aRefusedSecretIsNotAskedForAgain()
    {
        FakeKeyring keyring;
        keyring.state->secrets.insert(QStringLiteral("calendar-caldav"), QStringLiteral("pw"));
        keyring.state->mode = FakeKeyring::Mode::Refuses;
        AskOnceSecretStore store(std::make_unique<FakeKeyring>(keyring));

        QVERIFY(!store.read(QStringLiteral("calendar-caldav")));
        QVERIFY(store.lastReadRefused());
        QCOMPARE(keyring.state->reads, 1);

        // Asked again (each send of the calendar): no new question, the same answer.
        for (int i = 0; i < 5; ++i) {
            QVERIFY(!store.read(QStringLiteral("calendar-caldav")));
            QVERIFY(store.lastReadRefused());
        }
        QCOMPARE(keyring.state->reads, 1);

        // Another secret is still asked for.
        QVERIFY(!store.read(QStringLiteral("sync-data-key")));
        QCOMPARE(keyring.state->reads, 2);
    }

    void writingTheSecretAnewMakesItReadableAgain()
    {
        FakeKeyring keyring;
        keyring.state->mode = FakeKeyring::Mode::Refuses;
        AskOnceSecretStore store(std::make_unique<FakeKeyring>(keyring));
        QVERIFY(!store.read(QStringLiteral("calendar-caldav")));

        QVERIFY(store.write(QStringLiteral("calendar-caldav"), QStringLiteral("new")));
        QCOMPARE(store.read(QStringLiteral("calendar-caldav")).value_or(QStringLiteral("<failed>")), QStringLiteral("new"));
        QVERIFY(!store.lastReadRefused());
        QCOMPARE(keyring.state->reads, 2);
    }

    void aRestartAsksAgain()
    {
        FakeKeyring keyring;
        keyring.state->mode = FakeKeyring::Mode::Refuses;
        {
            AskOnceSecretStore store(std::make_unique<FakeKeyring>(keyring));
            QVERIFY(!store.read(QStringLiteral("calendar-caldav")));
        }
        keyring.state->mode = FakeKeyring::Mode::Ok; // "Allow" this time
        keyring.state->secrets.insert(QStringLiteral("calendar-caldav"), QStringLiteral("pw"));
        AskOnceSecretStore store(std::make_unique<FakeKeyring>(keyring));
        QCOMPARE(store.read(QStringLiteral("calendar-caldav")).value_or(QStringLiteral("<failed>")), QStringLiteral("pw"));
        QCOMPARE(keyring.state->reads, 2);
    }

    void aKeyringOutOfReachIsTriedAgain()
    {
        // Not started yet, or no login session: nobody was asked, so it is worth trying again.
        FakeKeyring keyring;
        keyring.state->mode = FakeKeyring::Mode::Unavailable;
        AskOnceSecretStore store(std::make_unique<FakeKeyring>(keyring));
        QVERIFY(!store.read(QStringLiteral("calendar-caldav")));
        QVERIFY(!store.lastReadRefused());
        QVERIFY(!store.read(QStringLiteral("calendar-caldav")));
        QCOMPARE(keyring.state->reads, 2);

        keyring.state->mode = FakeKeyring::Mode::Ok;
        keyring.state->secrets.insert(QStringLiteral("calendar-caldav"), QStringLiteral("pw"));
        QCOMPARE(store.read(QStringLiteral("calendar-caldav")).value_or(QStringLiteral("<failed>")), QStringLiteral("pw"));
    }
};

QTEST_GUILESS_MAIN(AskOnceSecretStoreTest)
#include "tst_askoncesecretstore.moc"
