// SPDX-License-Identifier: GPL-3.0-or-later
#include "SecretServiceStore.h"

#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVariant>
#include <QEventLoop>
#include <QLoggingCategory>
#include <QScopedValueRollback>
#include <QTimer>

Q_LOGGING_CATEGORY(lcSecretStore, "sugo.platform.secretstore")

namespace
{
const QString kService = QStringLiteral("org.freedesktop.secrets");
const QString kServicePath = QStringLiteral("/org/freedesktop/secrets");
const QString kServiceInterface = QStringLiteral("org.freedesktop.Secret.Service");
const QString kCollectionInterface = QStringLiteral("org.freedesktop.Secret.Collection");
const QString kItemInterface = QStringLiteral("org.freedesktop.Secret.Item");
const QString kPromptInterface = QStringLiteral("org.freedesktop.Secret.Prompt");
const QString kNoObject = QStringLiteral("/");
constexpr int kCallTimeoutMs = 10 * 1000;

/// Waits for the Completed signal of one prompt.
class PromptWaiter : public QObject
{
    Q_OBJECT

public:
    bool dismissed = true;
    bool completed = false;

Q_SIGNALS:
    void done();

public Q_SLOTS:
    void onCompleted(bool wasDismissed, const QDBusVariant &)
    {
        dismissed = wasDismissed;
        completed = true;
        Q_EMIT done();
    }
};
} // namespace

QDBusArgument &operator<<(QDBusArgument &argument, const SecretServiceSecret &secret)
{
    argument.beginStructure();
    argument << secret.session << secret.parameters << secret.value << secret.contentType;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, SecretServiceSecret &secret)
{
    argument.beginStructure();
    argument >> secret.session >> secret.parameters >> secret.value >> secret.contentType;
    argument.endStructure();
    return argument;
}

void registerSecretServiceTypes()
{
    static const bool registered = [] {
        qDBusRegisterMetaType<SecretServiceSecret>();
        qDBusRegisterMetaType<SecretServiceAttributes>();
        return true;
    }();
    Q_UNUSED(registered);
}

SecretServiceStore::SecretServiceStore(const QDBusConnection &bus, const QString &applicationId)
    : m_bus(bus)
    , m_applicationId(applicationId)
{
    registerSecretServiceTypes();
}

bool SecretServiceStore::ensureSession()
{
    if (!m_session.path().isEmpty()) {
        return true;
    }
    if (!m_retryAfter.hasExpired() || !m_bus.isConnected()) {
        return false;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(kService, kServicePath, kServiceInterface, QStringLiteral("OpenSession"));
    call << QStringLiteral("plain") << QVariant::fromValue(QDBusVariant(QString()));
    const QDBusMessage reply = m_bus.call(call, QDBus::Block, kCallTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
        // Asked again after a while, not on every call: a keyring started after us (at login) is
        // found then.
        qCInfo(lcSecretStore) << "No Secret Service available:" << reply.errorMessage();
        m_retryAfter.setRemainingTime(m_retryIntervalMs);
        return false;
    }
    m_session = qvariant_cast<QDBusObjectPath>(reply.arguments().at(1));
    return !m_session.path().isEmpty();
}

// After a failed call that used the session: a restarted keyring no longer knows it.
bool SecretServiceStore::renewSession()
{
    m_session = QDBusObjectPath();
    return ensureSession();
}

std::optional<SecretServiceStore::Matches> SecretServiceStore::search(const QString &key)
{
    QDBusMessage call = QDBusMessage::createMethodCall(kService, kServicePath, kServiceInterface, QStringLiteral("SearchItems"));
    const SecretServiceAttributes attributes{{QStringLiteral("application"), m_applicationId}, {QStringLiteral("key"), key}};
    call << QVariant::fromValue(attributes);
    const QDBusMessage reply = m_bus.call(call, QDBus::Block, kCallTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
        qCWarning(lcSecretStore) << "Searching the keyring failed:" << reply.errorMessage();
        return std::nullopt;
    }
    return Matches{qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(0)), qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(1))};
}

bool SecretServiceStore::unlock(const QList<QDBusObjectPath> &objects)
{
    // The user said no (or was not there): reads do not ask again on every call, each of which
    // would show the prompt and wait for it.
    if (!m_unlockRetryAfter.hasExpired()) {
        return false;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(kService, kServicePath, kServiceInterface, QStringLiteral("Unlock"));
    call << QVariant::fromValue(objects);
    const QDBusMessage reply = m_bus.call(call, QDBus::Block, kCallTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
        qCWarning(lcSecretStore) << "Unlocking the keyring failed:" << reply.errorMessage();
        m_unlockRetryAfter.setRemainingTime(m_retryIntervalMs);
        return false;
    }
    const auto prompt = qvariant_cast<QDBusObjectPath>(reply.arguments().at(1));
    if (prompt.path() == kNoObject || runPrompt(prompt)) {
        return true;
    }
    m_unlockRetryAfter.setRemainingTime(m_retryIntervalMs);
    return false;
}

bool SecretServiceStore::runPrompt(const QDBusObjectPath &prompt)
{
    const QScopedValueRollback<bool> prompting(m_prompting, true);
    PromptWaiter waiter;
    QEventLoop loop;
    QObject::connect(&waiter, &PromptWaiter::done, &loop, &QEventLoop::quit);
    if (!m_bus.connect(kService, prompt.path(), kPromptInterface, QStringLiteral("Completed"), &waiter,
                       SLOT(onCompleted(bool, QDBusVariant)))) {
        return false;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(kService, prompt.path(), kPromptInterface, QStringLiteral("Prompt"));
    call << QString(); // no parent window id
    const QDBusMessage reply = m_bus.call(call, QDBus::Block, kCallTimeoutMs);
    bool timedOut = false;
    if (reply.type() == QDBusMessage::ReplyMessage && !waiter.completed) {
        QTimer::singleShot(m_promptTimeoutMs, &loop, &QEventLoop::quit);
        loop.exec();
        timedOut = !waiter.completed;
    }
    m_bus.disconnect(kService, prompt.path(), kPromptInterface, QStringLiteral("Completed"), &waiter,
                     SLOT(onCompleted(bool, QDBusVariant)));
    if (timedOut) {
        // Not answered in time: the dialog is closed, or it would stay on screen and the next
        // unlock would open a second one on top of it.
        const QDBusMessage dismiss = QDBusMessage::createMethodCall(kService, prompt.path(), kPromptInterface, QStringLiteral("Dismiss"));
        const QDBusMessage dismissed = m_bus.call(dismiss, QDBus::Block, kCallTimeoutMs);
        if (dismissed.type() != QDBusMessage::ReplyMessage) {
            qCWarning(lcSecretStore) << "Closing the keyring prompt failed:" << dismissed.errorMessage();
        }
    }
    if (!waiter.completed || waiter.dismissed) {
        qCWarning(lcSecretStore) << "The keyring prompt was dismissed or not answered";
        return false;
    }
    return true;
}

bool SecretServiceStore::deleteItems(const QList<QDBusObjectPath> &items)
{
    bool ok = true;
    for (const QDBusObjectPath &item : items) {
        const QDBusMessage call = QDBusMessage::createMethodCall(kService, item.path(), kItemInterface, QStringLiteral("Delete"));
        const QDBusMessage reply = m_bus.call(call, QDBus::Block, kCallTimeoutMs);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            ok = false;
            continue;
        }
        const auto prompt = qvariant_cast<QDBusObjectPath>(reply.arguments().at(0));
        ok = (prompt.path() == kNoObject || runPrompt(prompt)) && ok;
    }
    return ok;
}

std::optional<QString> SecretServiceStore::read(const QString &key)
{
    if (!ensureSession()) {
        return std::nullopt;
    }
    if (m_prompting) {
        // Called from the event loop that waits for an open prompt: asking now would stack a
        // second prompt on top of it.
        return std::nullopt;
    }
    const auto matches = search(key);
    if (!matches) {
        return std::nullopt;
    }
    // A readable copy is enough: a locked one (say a stale copy in a second keyring) is only
    // unlocked when there is no other, and a refused unlock then does not hide the readable one.
    QDBusObjectPath item;
    if (!matches->unlocked.isEmpty()) {
        item = matches->unlocked.first();
    } else if (matches->locked.isEmpty()) {
        return QString();
    } else if (unlock(matches->locked)) {
        item = matches->locked.first();
    } else {
        return std::nullopt;
    }
    const auto getSecret = [&]() {
        QDBusMessage call = QDBusMessage::createMethodCall(kService, item.path(), kItemInterface, QStringLiteral("GetSecret"));
        call << QVariant::fromValue(m_session);
        return m_bus.call(call, QDBus::Block, kCallTimeoutMs);
    };
    QDBusMessage reply = getSecret();
    if (reply.type() == QDBusMessage::ErrorMessage && renewSession()) {
        reply = getSecret();
    }
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        qCWarning(lcSecretStore) << "Reading from the keyring failed:" << reply.errorMessage();
        return std::nullopt;
    }
    const auto secret = qdbus_cast<SecretServiceSecret>(reply.arguments().at(0));
    return QString::fromUtf8(secret.value);
}

bool SecretServiceStore::write(const QString &key, const QString &secret)
{
    // A write is the user's own doing (a password saved, an account disconnected): it asks again
    // even right after a refused unlock, or the password would end up in the settings file and a
    // disconnected account's password would stay in the keyring.
    m_unlockRetryAfter = QDeadlineTimer();
    if (!ensureSession()) {
        return false;
    }
    if (m_prompting) {
        return false; // as in read(): no second prompt while one is open
    }
    if (secret.isEmpty()) {
        const auto matches = search(key);
        if (!matches) {
            return false;
        }
        // What can be reached is removed even if a locked copy stays: the removal still fails
        // then, as the secret may be read from that copy later.
        const bool removedUnlocked = deleteItems(matches->unlocked);
        const bool removedLocked = matches->locked.isEmpty() || (unlock(matches->locked) && deleteItems(matches->locked));
        return removedUnlocked && removedLocked;
    }

    QDBusMessage alias = QDBusMessage::createMethodCall(kService, kServicePath, kServiceInterface, QStringLiteral("ReadAlias"));
    alias << QStringLiteral("default");
    const QDBusMessage aliasReply = m_bus.call(alias, QDBus::Block, kCallTimeoutMs);
    if (aliasReply.type() != QDBusMessage::ReplyMessage || aliasReply.arguments().isEmpty()) {
        qCWarning(lcSecretStore) << "The keyring has no default collection:" << aliasReply.errorMessage();
        return false;
    }
    const auto collection = qvariant_cast<QDBusObjectPath>(aliasReply.arguments().at(0));
    if (collection.path() == kNoObject || !unlock({collection})) {
        return false;
    }

    const SecretServiceAttributes attributes{{QStringLiteral("application"), m_applicationId}, {QStringLiteral("key"), key}};
    const QVariantMap properties{
        {QStringLiteral("org.freedesktop.Secret.Item.Label"), QStringLiteral("Sugo: %1").arg(key)},
        {QStringLiteral("org.freedesktop.Secret.Item.Attributes"), QVariant::fromValue(attributes)},
    };
    const auto createItem = [&]() {
        const SecretServiceSecret value{m_session, QByteArray(), secret.toUtf8(), QStringLiteral("text/plain; charset=utf8")};
        QDBusMessage create = QDBusMessage::createMethodCall(kService, collection.path(), kCollectionInterface, QStringLiteral("CreateItem"));
        create << properties << QVariant::fromValue(value) << true; // replace an item with the same attributes
        return m_bus.call(create, QDBus::Block, kCallTimeoutMs);
    };
    QDBusMessage reply = createItem();
    if (reply.type() == QDBusMessage::ErrorMessage && renewSession()) {
        reply = createItem();
    }
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
        qCWarning(lcSecretStore) << "Writing to the keyring failed:" << reply.errorMessage();
        return false;
    }
    const auto prompt = qvariant_cast<QDBusObjectPath>(reply.arguments().at(1));
    return prompt.path() == kNoObject || runPrompt(prompt);
}

#include "SecretServiceStore.moc"
