// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <memory>

#include "PrivateDBus.h"
#include "SecretServiceStore.h"

namespace
{
const QString kServiceName = QStringLiteral("org.freedesktop.secrets");
const QString kCollectionPath = QStringLiteral("/org/freedesktop/secrets/collection/login");

/// What the fake keyring holds; shared between its D-Bus objects (service thread) and the test.
struct KeyringState {
    struct Item {
        SecretServiceAttributes attributes;
        QByteArray value;
        QString label;
    };
    QMutex mutex;
    QHash<QString, Item> items; ///< By object path.
    int nextItem = 1;
    bool locked = false;
    bool dismissPrompt = false;
    int prompts = 0;
    QString sessionAlgorithm;
    QSet<QString> sessions; ///< Open sessions; a restarted keyring forgets them.
    int sessionsOpened = 0;
    QString connectionName;
    bool answerPrompts = true; ///< false: a prompt stays open until it is dismissed.
    int dismissals = 0;        ///< Prompt.Dismiss calls.
    int answerAfterMs = 20;    ///< How long the user takes to answer a prompt.
};

class PromptObject : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Secret.Prompt")

public:
    PromptObject(KeyringState *state, QString path)
        : m_state(state)
        , m_path(std::move(path))
    {
    }

public Q_SLOTS:
    void Prompt(const QString &)
    {
        bool dismissed = false;
        int answerAfterMs = 0;
        {
            QMutexLocker lock(&m_state->mutex);
            ++m_state->prompts;
            if (!m_state->answerPrompts) {
                return; // the user is away: the dialog stays open
            }
            dismissed = m_state->dismissPrompt;
            answerAfterMs = m_state->answerAfterMs;
        }
        // The answer comes later, as a signal, like a real dialog; the keyring is unlocked then.
        QTimer::singleShot(answerAfterMs, this, [this, dismissed] {
            if (!dismissed) {
                QMutexLocker lock(&m_state->mutex);
                m_state->locked = false;
            }
            QDBusMessage signal = QDBusMessage::createSignal(m_path, QStringLiteral("org.freedesktop.Secret.Prompt"), QStringLiteral("Completed"));
            signal << dismissed << QVariant::fromValue(QDBusVariant(QVariant::fromValue(QList<QDBusObjectPath>())));
            QDBusConnection(m_state->connectionName).send(signal);
        });
    }

    void Dismiss()
    {
        {
            QMutexLocker lock(&m_state->mutex);
            ++m_state->dismissals;
        }
        QDBusMessage signal = QDBusMessage::createSignal(m_path, QStringLiteral("org.freedesktop.Secret.Prompt"), QStringLiteral("Completed"));
        signal << true << QVariant::fromValue(QDBusVariant(QVariant::fromValue(QList<QDBusObjectPath>())));
        QDBusConnection(m_state->connectionName).send(signal);
    }

private:
    KeyringState *m_state;
    QString m_path;
};

class ItemObject : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Secret.Item")

public:
    ItemObject(KeyringState *state, QString path)
        : m_state(state)
        , m_path(std::move(path))
    {
    }

public Q_SLOTS:
    SecretServiceSecret GetSecret(const QDBusObjectPath &session)
    {
        QMutexLocker lock(&m_state->mutex);
        if (!m_state->sessions.contains(session.path())) {
            sendErrorReply(QStringLiteral("org.freedesktop.Secret.Error.NoSession"), QStringLiteral("no such session"));
            return {};
        }
        return {session, QByteArray(), m_state->items.value(m_path).value, QStringLiteral("text/plain")};
    }

    QDBusObjectPath Delete()
    {
        QMutexLocker lock(&m_state->mutex);
        m_state->items.remove(m_path);
        return QDBusObjectPath(QStringLiteral("/"));
    }

private:
    KeyringState *m_state;
    QString m_path;
};

class CollectionObject : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Secret.Collection")

public:
    explicit CollectionObject(KeyringState *state)
        : m_state(state)
    {
    }

public Q_SLOTS:
    QDBusObjectPath CreateItem(const QVariantMap &properties, const SecretServiceSecret &secret, bool replace, QDBusObjectPath &prompt)
    {
        prompt = QDBusObjectPath(QStringLiteral("/"));
        const auto attributes = qdbus_cast<SecretServiceAttributes>(properties.value(QStringLiteral("org.freedesktop.Secret.Item.Attributes")));
        QMutexLocker lock(&m_state->mutex);
        if (!m_state->sessions.contains(secret.session.path())) {
            sendErrorReply(QStringLiteral("org.freedesktop.Secret.Error.NoSession"), QStringLiteral("no such session"));
            return {};
        }
        QString path;
        if (replace) {
            for (auto it = m_state->items.cbegin(); it != m_state->items.cend(); ++it) {
                if (it->attributes == attributes) {
                    path = it.key();
                }
            }
        }
        if (path.isEmpty()) {
            path = kCollectionPath + QStringLiteral("/") + QString::number(m_state->nextItem++);
            auto *item = new ItemObject(m_state, path);
            item->setParent(this);
            QDBusConnection(m_state->connectionName).registerObject(path, item, QDBusConnection::ExportAllSlots);
        }
        m_state->items.insert(path, {attributes, secret.value, properties.value(QStringLiteral("org.freedesktop.Secret.Item.Label")).toString()});
        return QDBusObjectPath(path);
    }

private:
    KeyringState *m_state;
};

class ServiceObject : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Secret.Service")

public:
    explicit ServiceObject(KeyringState *state)
        : m_state(state)
    {
    }

public Q_SLOTS:
    QDBusVariant OpenSession(const QString &algorithm, const QDBusVariant &, QDBusObjectPath &result)
    {
        QMutexLocker lock(&m_state->mutex);
        m_state->sessionAlgorithm = algorithm;
        result = QDBusObjectPath(QStringLiteral("/org/freedesktop/secrets/session/") + QString::number(++m_state->sessionsOpened));
        m_state->sessions.insert(result.path());
        return QDBusVariant(QString());
    }

    QList<QDBusObjectPath> SearchItems(const SecretServiceAttributes &attributes, QList<QDBusObjectPath> &locked)
    {
        QMutexLocker lock(&m_state->mutex);
        QList<QDBusObjectPath> found;
        for (auto it = m_state->items.cbegin(); it != m_state->items.cend(); ++it) {
            bool match = true;
            for (auto a = attributes.cbegin(); a != attributes.cend(); ++a) {
                match = match && it->attributes.value(a.key()) == a.value();
            }
            if (match) {
                found.append(QDBusObjectPath(it.key()));
            }
        }
        if (m_state->locked) {
            locked = found;
            return {};
        }
        return found;
    }

    QList<QDBusObjectPath> Unlock(const QList<QDBusObjectPath> &objects, QDBusObjectPath &prompt)
    {
        QMutexLocker lock(&m_state->mutex);
        if (!m_state->locked) {
            prompt = QDBusObjectPath(QStringLiteral("/"));
            return objects;
        }
        const QString path = QStringLiteral("/org/freedesktop/secrets/prompt/") + QString::number(m_state->prompts + 1);
        auto *object = new PromptObject(m_state, path);
        object->setParent(this);
        QDBusConnection(m_state->connectionName).registerObject(path, object, QDBusConnection::ExportAllSlots);
        prompt = QDBusObjectPath(path);
        return {};
    }

    QDBusObjectPath ReadAlias(const QString &name)
    {
        return QDBusObjectPath(name == QLatin1String("default") ? kCollectionPath : QStringLiteral("/"));
    }

private:
    KeyringState *m_state;
};

/// Runs the fake keyring on its own thread: the store's calls block the test's thread.
class FakeKeyring
{
public:
    explicit FakeKeyring(const QString &address)
    {
        registerSecretServiceTypes();
        state.connectionName = QStringLiteral("fake-keyring");
        m_thread.start();
        m_root = std::make_unique<QObject>();
        m_root->moveToThread(&m_thread);
        QMetaObject::invokeMethod(
            m_root.get(),
            [this, address] {
                QDBusConnection bus = QDBusConnection::connectToBus(address, state.connectionName);
                auto *service = new ServiceObject(&state);
                auto *collection = new CollectionObject(&state);
                service->setParent(m_root.get());
                collection->setParent(m_root.get());
                bus.registerObject(QStringLiteral("/org/freedesktop/secrets"), service, QDBusConnection::ExportAllSlots);
                bus.registerObject(kCollectionPath, collection, QDBusConnection::ExportAllSlots);
                m_registered = bus.registerService(kServiceName);
            },
            Qt::BlockingQueuedConnection);
    }

    ~FakeKeyring()
    {
        QMetaObject::invokeMethod(
            m_root.get(),
            [this] {
                QDBusConnection(state.connectionName).unregisterService(kServiceName);
                QDBusConnection::disconnectFromBus(state.connectionName);
                m_root.reset();
            },
            Qt::BlockingQueuedConnection);
        m_thread.quit();
        m_thread.wait();
    }

    bool registered() const { return m_registered; }

    KeyringState state;

private:
    QThread m_thread;
    std::unique_ptr<QObject> m_root;
    bool m_registered = false;
};
} // namespace

class SecretStoreTest : public QObject
{
    Q_OBJECT

private:
    PrivateDBus m_bus;
    std::unique_ptr<FakeKeyring> m_keyring;
    int m_client = 0;

    QDBusConnection client()
    {
        return QDBusConnection::connectToBus(m_bus.address(), QStringLiteral("client-%1").arg(++m_client));
    }

private Q_SLOTS:
    void initTestCase()
    {
        if (!m_bus.start()) {
            QSKIP("dbus-daemon is not available");
        }
    }

    void init()
    {
        m_keyring = std::make_unique<FakeKeyring>(m_bus.address());
        QVERIFY(m_keyring->registered());
    }

    void cleanup()
    {
        m_keyring.reset();
    }

    void writeReadAndRemove()
    {
        SecretServiceStore store(client(), QStringLiteral("io.github.mineraleyt.sugo"));
        QCOMPARE(store.read(QStringLiteral("calendar-nextcloud")), std::optional<QString>(QString()));
        QCOMPARE(m_keyring->state.sessionAlgorithm, QStringLiteral("plain"));
        QVERIFY(store.write(QStringLiteral("calendar-nextcloud"), QStringLiteral("pässword with spaces")));
        QCOMPARE(store.read(QStringLiteral("calendar-nextcloud")), std::optional<QString>(QStringLiteral("pässword with spaces")));

        // Items are tagged so they can be found again (and told apart from other apps').
        QCOMPARE(m_keyring->state.items.size(), 1);
        const auto item = m_keyring->state.items.cbegin().value();
        QCOMPARE(item.attributes.value(QStringLiteral("application")), QStringLiteral("io.github.mineraleyt.sugo"));
        QCOMPARE(item.attributes.value(QStringLiteral("key")), QStringLiteral("calendar-nextcloud"));
        QVERIFY(item.label.contains(QStringLiteral("Sugo")));

        // Writing again replaces; other keys are separate.
        QVERIFY(store.write(QStringLiteral("calendar-nextcloud"), QStringLiteral("second")));
        QVERIFY(store.write(QStringLiteral("calendar-caldav"), QStringLiteral("other")));
        QCOMPARE(m_keyring->state.items.size(), 2);
        QCOMPARE(store.read(QStringLiteral("calendar-nextcloud")), std::optional<QString>(QStringLiteral("second")));

        // An empty secret removes it.
        QVERIFY(store.write(QStringLiteral("calendar-nextcloud"), QString()));
        QCOMPARE(store.read(QStringLiteral("calendar-nextcloud")), std::optional<QString>(QString()));
        QCOMPARE(m_keyring->state.items.size(), 1);

        // A fresh client (the next run) still finds what was stored.
        SecretServiceStore again(client(), QStringLiteral("io.github.mineraleyt.sugo"));
        QCOMPARE(again.read(QStringLiteral("calendar-caldav")), std::optional<QString>(QStringLiteral("other")));
    }

    void lockedKeyringIsUnlockedThroughItsPrompt()
    {
        SecretServiceStore store(client(), QStringLiteral("io.github.mineraleyt.sugo"));
        QVERIFY(store.write(QStringLiteral("calendar-caldav"), QStringLiteral("pw")));
        m_keyring->state.locked = true;

        QCOMPARE(store.read(QStringLiteral("calendar-caldav")), std::optional<QString>(QStringLiteral("pw")));
        QCOMPARE(m_keyring->state.prompts, 1);

        m_keyring->state.locked = true;
        QVERIFY(store.write(QStringLiteral("calendar-caldav"), QStringLiteral("pw2")));
        QCOMPARE(m_keyring->state.prompts, 2);
        QCOMPARE(store.read(QStringLiteral("calendar-caldav")), std::optional<QString>(QStringLiteral("pw2")));
    }

    void dismissedPromptIsAFailureNotAnEmptySecret()
    {
        SecretServiceStore store(client(), QStringLiteral("io.github.mineraleyt.sugo"));
        QVERIFY(store.write(QStringLiteral("calendar-caldav"), QStringLiteral("pw")));
        m_keyring->state.locked = true;
        m_keyring->state.dismissPrompt = true;
        QVERIFY(!store.read(QStringLiteral("calendar-caldav")).has_value());
        QVERIFY(!store.write(QStringLiteral("calendar-caldav"), QStringLiteral("pw2")));
    }

    void aRestartedKeyringGetsANewSession()
    {
        SecretServiceStore store(client(), QStringLiteral("io.github.mineraleyt.sugo"));
        QVERIFY(store.write(QStringLiteral("calendar-caldav"), QStringLiteral("pw")));
        const auto restart = [this] {
            QMutexLocker lock(&m_keyring->state.mutex);
            m_keyring->state.sessions.clear(); // the items survive, the sessions do not
        };

        restart();
        QCOMPARE(store.read(QStringLiteral("calendar-caldav")), std::optional<QString>(QStringLiteral("pw")));
        restart();
        QVERIFY(store.write(QStringLiteral("calendar-caldav"), QStringLiteral("pw2")));
        QCOMPARE(store.read(QStringLiteral("calendar-caldav")), std::optional<QString>(QStringLiteral("pw2")));
        QCOMPARE(m_keyring->state.sessionsOpened, 3);
    }

    void noKeyringIsReportedAsUnavailable()
    {
        m_keyring.reset();
        SecretServiceStore store(client(), QStringLiteral("io.github.mineraleyt.sugo"));
        QVERIFY(!store.read(QStringLiteral("calendar-caldav")).has_value());
        QVERIFY(!store.write(QStringLiteral("calendar-caldav"), QStringLiteral("pw")));
        m_keyring = std::make_unique<FakeKeyring>(m_bus.address()); // for cleanup()
    }

    void aKeyringStartedLaterIsFoundOnALaterCall()
    {
        m_keyring.reset();
        SecretServiceStore store(client(), QStringLiteral("io.github.mineraleyt.sugo"));
        store.setRetryIntervalForTesting(200);
        QVERIFY(!store.read(QStringLiteral("calendar-caldav")).has_value());
        m_keyring = std::make_unique<FakeKeyring>(m_bus.address()); // started at login, after us
        QVERIFY(m_keyring->registered());
        QTRY_VERIFY(store.read(QStringLiteral("calendar-caldav")).has_value());
        QCOMPARE(store.read(QStringLiteral("calendar-caldav")), std::optional<QString>(QString()));
    }

    void anUnansweredPromptIsClosedWhenItTimesOut()
    {
        SecretServiceStore store(client(), QStringLiteral("io.github.mineraleyt.sugo"));
        store.setPromptTimeoutForTesting(200);
        QVERIFY(store.write(QStringLiteral("calendar-caldav"), QStringLiteral("pw")));
        m_keyring->state.locked = true;
        m_keyring->state.answerPrompts = false;

        QVERIFY(!store.read(QStringLiteral("calendar-caldav")).has_value());
        QCOMPARE(m_keyring->state.prompts, 1);
        // The dialog does not stay on screen after we stopped waiting for it.
        QCOMPARE(m_keyring->state.dismissals, 1);
    }

    void aSecondReadWhileAPromptIsOpenDoesNotPromptAgain()
    {
        SecretServiceStore store(client(), QStringLiteral("io.github.mineraleyt.sugo"));
        QVERIFY(store.write(QStringLiteral("calendar-caldav"), QStringLiteral("pw")));
        m_keyring->state.locked = true;
        m_keyring->state.answerAfterMs = 400;

        // While the first read waits for the prompt, something else (a timer, the settings page)
        // asks again: it fails at once, without a second prompt.
        bool nestedRan = false;
        std::optional<QString> nested = QStringLiteral("not read");
        bool nestedWrite = true;
        QTimer::singleShot(50, this, [&] {
            nestedRan = true;
            nested = store.read(QStringLiteral("calendar-caldav"));
            nestedWrite = store.write(QStringLiteral("calendar-caldav"), QStringLiteral("pw2"));
        });
        QCOMPARE(store.read(QStringLiteral("calendar-caldav")), std::optional<QString>(QStringLiteral("pw")));
        QVERIFY(nestedRan);
        QVERIFY(!nested.has_value());
        QVERIFY(!nestedWrite);
        QCOMPARE(m_keyring->state.prompts, 1);

        // Once the prompt is answered, reads work again.
        QCOMPARE(store.read(QStringLiteral("calendar-caldav")), std::optional<QString>(QStringLiteral("pw")));
        QCOMPARE(m_keyring->state.prompts, 1);
    }
};

QTEST_GUILESS_MAIN(SecretStoreTest)
#include "tst_secretstore.moc"
