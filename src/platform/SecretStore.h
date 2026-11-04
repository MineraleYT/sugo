// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

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
};

/// The platform's keyring for this application: the Keychain on macOS (MacKeychainStore), the
/// freedesktop Secret Service on the session bus elsewhere (SecretServiceStore).
std::unique_ptr<SecretStore> createSecretStore();
