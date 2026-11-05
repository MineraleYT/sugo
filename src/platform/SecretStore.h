// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QSet>
#include <QString>

#include <memory>
#include <optional>

/**
 * Where secrets (the calendar app password) are kept, so they never have to sit in a
 * configuration file. Injectable so tests can use an in-memory store.
 */
class SecretStore
{
public:
    virtual ~SecretStore() = default;

    /// The stored secret, an empty string if there is none, or nothing on failure.
    virtual std::optional<QString> read(const QString &key) = 0;
    /// Stores `secret` under `key`; an empty secret removes it. False on failure.
    virtual bool write(const QString &key, const QString &secret) = 0;
    /// Whether the last read() failed because the user, asked by the system, refused access
    /// (as opposed to a keyring that is missing, locked where no one can unlock it, or not
    /// started yet, which is worth trying again).
    virtual bool lastReadRefused() const { return false; }
};

/**
 * A store that does not ask again for a secret the user refused to give: after a refused read
 * of a key, read() of that key fails at once, without asking, until the secret is written again
 * (the user entered it anew) or the program restarts. Other failures are tried again as usual.
 * macOS asks with a dialog at every read an update made unreadable (MacKeychainStore), and the
 * calendar reads again before each send: without this, every finished work session would ask.
 */
class AskOnceSecretStore final : public SecretStore
{
public:
    explicit AskOnceSecretStore(std::unique_ptr<SecretStore> store);

    std::optional<QString> read(const QString &key) override;
    bool write(const QString &key, const QString &secret) override;
    bool lastReadRefused() const override { return m_lastReadRefused; }

private:
    std::unique_ptr<SecretStore> m_store;
    QSet<QString> m_refused;
    bool m_lastReadRefused = false;
};

/// The service (Keychain) or application (Secret Service) under which this application keeps its
/// secrets: its application id.
QString secretStoreService();

/// The platform's keyring for this application: the Keychain on macOS (MacKeychainStore), the
/// freedesktop Secret Service on the session bus elsewhere (SecretServiceStore), in an
/// AskOnceSecretStore.
std::unique_ptr<SecretStore> createSecretStore();
