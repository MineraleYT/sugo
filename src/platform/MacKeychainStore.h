// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SecretStore.h"

/**
 * macOS only: secrets in the user's login Keychain (Security.framework, SecItem* API), as
 * generic passwords with service `service` and account `key`.
 *
 * Unsigned or re-signed builds may make macOS ask the user to allow access after an update;
 * where no user can answer (errSecInteractionNotAllowed) the call fails and the caller falls
 * back as for any other failure.
 */
class MacKeychainStore final : public SecretStore
{
public:
    explicit MacKeychainStore(const QString &service);

    std::optional<QString> read(const QString &key) override;
    bool write(const QString &key, const QString &secret) override;

    /// The OSStatus of the last call (errSecSuccess = 0), for tests and diagnostics.
    int lastStatus() const { return m_lastStatus; }

private:
    bool remove(const QString &key);

    QString m_service;
    int m_lastStatus = 0;
};
