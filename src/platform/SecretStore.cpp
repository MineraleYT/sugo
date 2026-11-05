// SPDX-License-Identifier: GPL-3.0-or-later
#include "SecretStore.h"

#include <QLoggingCategory>

#ifdef Q_OS_MACOS
#include "MacKeychainStore.h"
#else
#include "SecretServiceStore.h"
#include <QDBusConnection>
#endif

namespace
{
Q_LOGGING_CATEGORY(lcSecrets, "sugo.platform.secrets")
}

AskOnceSecretStore::AskOnceSecretStore(std::unique_ptr<SecretStore> store)
    : m_store(std::move(store))
{
}

std::optional<QString> AskOnceSecretStore::read(const QString &key)
{
    if (m_refused.contains(key)) {
        m_lastReadRefused = true;
        return std::nullopt;
    }
    auto secret = m_store->read(key);
    m_lastReadRefused = !secret && m_store->lastReadRefused();
    if (m_lastReadRefused) {
        qCInfo(lcSecrets) << "Access to" << key << "was refused: not asking again until it is entered anew or Sugo restarts";
        m_refused.insert(key);
    }
    return secret;
}

bool AskOnceSecretStore::write(const QString &key, const QString &secret)
{
    // Written anew (the user entered it again): this program's own from now on, worth reading.
    m_refused.remove(key);
    return m_store->write(key, secret);
}

QString secretStoreService()
{
    // The app ID before 1.0.3 (now stream.sugo.app), kept: the keyring's items are found by it, and
    // a new name would lose the calendar password and Sugo Sync's keys.
    return QStringLiteral("io.github.mineraleyt.sugo");
}

std::unique_ptr<SecretStore> createSecretStore()
{
    const QString applicationId = secretStoreService();
#ifdef Q_OS_MACOS
    return std::make_unique<AskOnceSecretStore>(std::make_unique<MacKeychainStore>(applicationId));
#else
    return std::make_unique<AskOnceSecretStore>(std::make_unique<SecretServiceStore>(QDBusConnection::sessionBus(), applicationId));
#endif
}
