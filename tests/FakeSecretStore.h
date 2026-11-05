// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include <memory>
#include <optional>

#include "SecretStore.h"

/// A counter shared by the fakes of one test process (FakeSecretStore, FakeSyncServer): each
/// keyring access and each request takes the next number, so a test can tell which came first.
inline qint64 nextFakeEventSequence()
{
    static qint64 sequence = 0;
    return ++sequence;
}

/**
 * An in-memory keyring; copies share their contents, like a real keyring across restarts.
 *
 * Like the macOS Keychain, reading a secret that does not exist gives an empty string, not a
 * failure. Every read and write is logged in order.
 */
class FakeSecretStore : public SecretStore
{
public:
    struct Access {
        enum Kind { Read, Write };
        Kind kind = Read;
        QString key;
        QString value;   ///< What was written (empty: removed); empty for reads.
        bool ok = false;
        qint64 sequence = 0; ///< nextFakeEventSequence() at the time.
    };

    std::shared_ptr<QHash<QString, QString>> secrets = std::make_shared<QHash<QString, QString>>();
    /// False: no keyring at all, reads and writes fail.
    std::shared_ptr<bool> available = std::make_shared<bool>(true);
    /// Writes fail while reads still answer (an empty string for a missing secret): a keyring
    /// that cannot store anything, which a read alone cannot reveal.
    std::shared_ptr<bool> failWrites = std::make_shared<bool>(false);
    /// The user answers "Deny" when asked (macOS after an update); writing a new item works.
    std::shared_ptr<bool> refuses = std::make_shared<bool>(false);
    std::shared_ptr<int> reads = std::make_shared<int>(0);
    std::shared_ptr<QList<Access>> log = std::make_shared<QList<Access>>();

    std::optional<QString> read(const QString &key) override
    {
        ++*reads;
        m_refused = *available && *refuses;
        const bool ok = *available && !*refuses;
        log->append({Access::Read, key, QString(), ok, nextFakeEventSequence()});
        if (!ok) {
            return std::nullopt;
        }
        return secrets->value(key);
    }
    bool write(const QString &key, const QString &secret) override
    {
        const bool ok = *available && !*failWrites;
        log->append({Access::Write, key, secret, ok, nextFakeEventSequence()});
        if (!ok) {
            return false;
        }
        *refuses = false; // the new item is this program's own
        if (secret.isEmpty()) {
            secrets->remove(key);
        } else {
            secrets->insert(key, secret);
        }
        return true;
    }
    bool lastReadRefused() const override { return m_refused; }

    /// The logged accesses to `key` of one kind.
    QList<Access> accesses(const QString &key, Access::Kind kind) const
    {
        QList<Access> out;
        for (const Access &a : std::as_const(*log)) {
            if (a.key == key && a.kind == kind) {
                out.append(a);
            }
        }
        return out;
    }

private:
    bool m_refused = false;
};
