// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include "Settings.h"
#include "SyncApi.h"

class SyncSecrets;

/**
 * The Sugo Sync account of this computer: sign-in with a code shown on sugo.stream.
 *
 * Every "store X in the keyring" is a SyncSecrets write of the one `sync-secrets` item. The
 * Settings group `Sync` holds what is not secret: DeviceId, UserId, DeviceName.
 *
 * Rule S4: before sign-in sends anything, a test write proves the keyring can keep secrets;
 * without a keyring there is no sync (no file fallback), the same on every system.
 */
class SyncAccount : public QObject
{
    Q_OBJECT

public:
    enum class Phase {
        SignedOut,     ///< Not linked.
        SigningIn,     ///< A code is shown; polling until the site approves it.
        WaitingForKey, ///< Linked, without the data key yet.
        Ready,         ///< Linked, with the data key.
    };
    Q_ENUM(Phase)

    /// `api` and `secrets` are not owned and must outlive this object.
    SyncAccount(SyncApi *api, SyncSecrets *secrets, Settings::Ptr settings, QObject *parent = nullptr);
    ~SyncAccount() override;

    /// "linux" or "macos", as the server knows platforms.
    static QString platformName();

    Phase phase() const { return m_phase; }
    /// The Settings say this computer is linked (it may still lack the secrets: restore()).
    bool isLinked() const;
    /// False after a failed test write of the keyring (sign-in is then impossible).
    bool keyringAvailable() const { return m_keyringAvailable; }

    QString deviceId() const;
    QString userId() const;
    QString deviceName() const;

    // --- While signing in -------------------------------------------------------------------------
    QString userCode() const { return m_userCode; }
    QUrl verificationUri() const { return m_verificationUri; }
    QUrl verificationUriComplete() const { return m_verificationUriComplete; }
    QDateTime codeExpiresAt() const { return m_codeExpiresAt; }
    /// The site could not link this device: the account has its most devices.
    bool deviceLimitReached() const { return m_deviceLimitReached; }

    /// One second in ms for every wait (polling, retries); tests make it short.
    void setTimeUnitMs(int ms) { m_unitMs = ms; }

public Q_SLOTS:
    /**
     * After SyncSecrets::load() of a linked computer: takes the secrets in memory and resumes
     * where the account was. False (nothing changes) when the Settings say signed out or the
     * secrets lack the refresh token or device key: then they are lost, signOutLocally().
     */
    bool restore();

    /**
     * Proves the keyring (keyringUnavailable() and nothing sent if it fails), makes the device's
     * key pair, stores it, and asks the server for a code: codeReady(), then polling.
     */
    void startSignIn(const QString &deviceName);
    /// Stops polling and removes the secrets of the sign-in.
    void cancelSignIn();

    /// Forgets everything here: secrets item, Settings group `Sync`, pending requests.
    void signOutLocally();

Q_SIGNALS:
    void phaseChanged(SyncAccount::Phase phase);
    void keyringAvailableChanged(bool available);
    /// The keyring test write failed: "Sugo Sync needs the system keyring".
    void keyringUnavailable();
    /// A write of the secrets failed after the keyring had worked.
    void keyringWriteFailed();

    void codeReady(const QString &userCode, const QUrl &verificationUri, const QUrl &verificationUriComplete, const QDateTime &expiresAt);
    void deviceLimitReachedChanged(bool reached);
    /// The code expired before the site approved it.
    void signInExpired();
    /// Sign-in ended without success (invalid_grant, or the server refused to start).
    void signInFailed(const SyncError &error);
    /// The device is linked: the key phase begins.
    void signedIn();
    /// The server refused the refresh token (disconnected on the site, or token reused): this
    /// computer was signed out locally.
    void signedOutRemotely();
    /// A request failed in a way worth showing (network, rate limit, server); it is retried.
    void requestFailed(const SyncError &error);

private:
    void setPhase(Phase phase);
    void setKeyringAvailable(bool available);
    void setDeviceLimitReached(bool reached);
    void pollToken();
    void schedulePoll(int seconds);
    void finishSignIn(const QJsonObject &json);
    void abandonSignIn();
    void clearSignInCode();
    void startKeyPhase();
    void onSignedOutRemotely();
    void writeSetting(const char *key, const QVariant &value);

    SyncApi *m_api;
    SyncSecrets *m_secrets;
    Settings::Ptr m_settings;
    Phase m_phase = Phase::SignedOut;
    bool m_keyringAvailable = true;
    int m_unitMs = 1000;

    QByteArray m_publicKey;

    // Sign-in
    QTimer m_pollTimer;
    QString m_deviceName;
    QString m_deviceCode;
    QString m_userCode;
    QUrl m_verificationUri;
    QUrl m_verificationUriComplete;
    QDateTime m_codeExpiresAt;
    int m_interval = 5;
    bool m_deviceLimitReached = false;
};
