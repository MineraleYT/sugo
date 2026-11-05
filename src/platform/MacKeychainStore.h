// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "SecretStore.h"

#include <CoreFoundation/CoreFoundation.h>
#include <QHash>

/**
 * macOS only: secrets in the user's login Keychain (Security.framework, SecItem* API), as
 * generic passwords with service `service` and account `key`.
 *
 * An item trusts the build that created it. A build signed ad hoc is known by its code hash,
 * which every update changes, so after an update the first read of each item makes macOS ask
 * the user (the call waits for the answer); where no user can answer (errSecInteractionNotAllowed,
 * no login session) the call fails and the caller falls back as for any other failure. write()
 * replaces the item, so what this build writes it can read again without asking. If storing the
 * new item fails after the old one was deleted, write() puts the old secret back when it knows it
 * without asking (this store read or wrote it last); otherwise it is lost with the failed write.
 * (tests/manual-review-checklist.md, "macOS: Keychain".)
 */
class MacKeychainStore final : public SecretStore
{
public:
    explicit MacKeychainStore(const QString &service);

    std::optional<QString> read(const QString &key) override;
    bool write(const QString &key, const QString &secret) override;
    /// The user refused when macOS asked: "Deny" gives errSecUserCanceled (-128, seen on macOS 27);
    /// errSecAuthFailed (-25293) is what a refused access gives where no dialog is shown.
    bool lastReadRefused() const override { return m_lastReadRefused; }

    /// The OSStatus of the last call (errSecSuccess = 0), for tests and diagnostics.
    int lastStatus() const { return m_lastStatus; }

    /// Tests: the next write() fails with \a status where it would add the new item, after the old
    /// one was deleted (as a Keychain locked meanwhile would).
    void failNextAddForTest(int status) { m_failNextAdd = status; }

private:
    bool remove(const QString &key);
    OSStatus addItem(const QString &key, CFDataRef data) const;
    void restore(const QString &key, const std::optional<QString> &previous);

    /// What each item holds as far as this store knows (its last read or write; empty: no item),
    /// to put back when replacing it fails. Unknown keys are not read again: that may ask the user.
    QHash<QString, QString> m_known;
    int m_failNextAdd = 0;

    QString m_service;
    int m_lastStatus = 0;
    bool m_lastReadRefused = false;
};
