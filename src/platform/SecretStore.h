// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QDeadlineTimer>
#include <QMap>
#include <QMetaType>
#include <QString>

#include <memory>
#include <optional>

/// The Secret Service "Secret" struct, (oayays).
struct SecretServiceSecret {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};
Q_DECLARE_METATYPE(SecretServiceSecret)

QDBusArgument &operator<<(QDBusArgument &argument, const SecretServiceSecret &secret);
const QDBusArgument &operator>>(const QDBusArgument &argument, SecretServiceSecret &secret);

using SecretServiceAttributes = QMap<QString, QString>;

/// Registers the D-Bus types above; safe to call more than once.
void registerSecretServiceTypes();

/**
 * Where secrets (the calendar app password) are kept, so they never have to sit in a
 * configuration file. Injectable so tests can use an in-memory store.
 */
class SecretStore
{
public:
    virtual ~SecretStore() = default;

    /// False when no keyring answers; the caller then keeps the secret somewhere else.
    virtual bool isAvailable() = 0;
    /// The stored secret, an empty string if there is none, or nothing on failure.
    virtual std::optional<QString> read(const QString &key) = 0;
    /// Stores `secret` under `key`; an empty secret removes it. False on failure.
    virtual bool write(const QString &key, const QString &secret) = 0;
};

/**
 * The freedesktop Secret Service (org.freedesktop.secrets: KWallet on Plasma, GNOME Keyring...),
 * spoken over D-Bus with the "plain" session algorithm: the secret only crosses the local
 * session bus. Items go into the default collection, tagged with the application id and `key`.
 *
 * The calls are synchronous. A locked collection is unlocked through the service's own prompt;
 * the call then waits (running the event loop) until the user answers it or it times out.
 */
class SecretServiceStore final : public SecretStore
{
public:
    explicit SecretServiceStore(const QDBusConnection &bus, const QString &applicationId);

    bool isAvailable() override;
    std::optional<QString> read(const QString &key) override;
    bool write(const QString &key, const QString &secret) override;

    void setRetryIntervalForTesting(int ms) { m_retryIntervalMs = ms; }

private:
    bool ensureSession();
    bool renewSession();
    std::optional<QList<QDBusObjectPath>> search(const QString &key);
    bool unlock(const QList<QDBusObjectPath> &objects);
    bool runPrompt(const QDBusObjectPath &prompt);
    bool deleteItems(const QList<QDBusObjectPath> &items);

    QDBusConnection m_bus;
    QString m_applicationId;
    QDBusObjectPath m_session;
    QDeadlineTimer m_retryAfter; ///< Running while a failed OpenSession is not tried again.
    int m_retryIntervalMs = 60 * 1000;
    int m_promptTimeoutMs = 120 * 1000;
};
