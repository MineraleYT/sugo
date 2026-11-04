// SPDX-License-Identifier: GPL-3.0-or-later
#include "SecretStore.h"

#ifdef Q_OS_MACOS
#include "MacKeychainStore.h"
#else
#include "SecretServiceStore.h"
#include <QDBusConnection>
#endif

std::unique_ptr<SecretStore> createSecretStore()
{
    const QString applicationId = QStringLiteral("io.github.mineraleyt.sugo");
#ifdef Q_OS_MACOS
    return std::make_unique<MacKeychainStore>(applicationId);
#else
    return std::make_unique<SecretServiceStore>(QDBusConnection::sessionBus(), applicationId);
#endif
}
