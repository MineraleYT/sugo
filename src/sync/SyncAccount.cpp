// SPDX-License-Identifier: GPL-3.0-or-later
#include "SyncAccount.h"

#include "SyncCrypto.h"
#include "SyncSecrets.h"

#include <QJsonObject>
#include <QSysInfo>

#include <algorithm>

namespace
{
const QLatin1String kGroup("Sync");
const char kDeviceId[] = "DeviceId";
const char kUserId[] = "UserId";
const char kDeviceName[] = "DeviceName";

QString key(const char *name)
{
    return kGroup + QLatin1Char('/') + QLatin1String(name);
}

constexpr int kMaxNameLength = 64;
} // namespace

SyncAccount::SyncAccount(SyncApi *api, SyncSecrets *secrets, Settings::Ptr settings, QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_secrets(secrets)
    , m_settings(std::move(settings))
{
    m_pollTimer.setSingleShot(true);
    connect(&m_pollTimer, &QTimer::timeout, this, &SyncAccount::pollToken);
    connect(m_api, &SyncApi::signedOutRemotely, this, &SyncAccount::onSignedOutRemotely);
}

SyncAccount::~SyncAccount() = default;

QString SyncAccount::platformName()
{
    return QSysInfo::kernelType() == QLatin1String("darwin") ? QStringLiteral("macos") : QStringLiteral("linux");
}

bool SyncAccount::isLinked() const
{
    return !deviceId().isEmpty();
}

QString SyncAccount::deviceId() const
{
    return m_settings->value(key(kDeviceId)).toString();
}

QString SyncAccount::userId() const
{
    return m_settings->value(key(kUserId)).toString();
}

QString SyncAccount::deviceName() const
{
    return m_settings->value(key(kDeviceName)).toString();
}

void SyncAccount::writeSetting(const char *name, const QVariant &value)
{
    m_settings->setValue(key(name), value);
}

void SyncAccount::setPhase(Phase phase)
{
    if (m_phase == phase) {
        return;
    }
    m_phase = phase;
    Q_EMIT phaseChanged(phase);
}

void SyncAccount::setKeyringAvailable(bool available)
{
    if (m_keyringAvailable == available) {
        return;
    }
    m_keyringAvailable = available;
    Q_EMIT keyringAvailableChanged(available);
}

void SyncAccount::setDeviceLimitReached(bool reached)
{
    if (m_deviceLimitReached == reached) {
        return;
    }
    m_deviceLimitReached = reached;
    Q_EMIT deviceLimitReachedChanged(reached);
}

// ---------------------------------------------------------------------------------------------
// Restore
// ---------------------------------------------------------------------------------------------

bool SyncAccount::restore()
{
    if (m_phase != Phase::SignedOut || !isLinked()) {
        return false;
    }
    const SyncSecrets::Values &values = m_secrets->values();
    if (m_secrets->refreshToken().isEmpty() || values.deviceKey.isEmpty()) {
        return false;
    }
    m_publicKey = SyncCrypto::publicKeyFor(values.deviceKey);
    setPhase(Phase::WaitingForKey);
    startKeyPhase();
    return true;
}

// ---------------------------------------------------------------------------------------------
// Sign-in
// ---------------------------------------------------------------------------------------------

void SyncAccount::startSignIn(const QString &deviceName)
{
    if (m_phase != Phase::SignedOut) {
        return;
    }
    // Rule S4: no keyring, no sync. Checked before anything goes out.
    if (!m_secrets->probe()) {
        setKeyringAvailable(false);
        Q_EMIT keyringUnavailable();
        return;
    }
    setKeyringAvailable(true);

    SyncCrypto::KeyPair keys = SyncCrypto::generateKeyPair();
    if (keys.publicKey.isEmpty()) {
        SyncError e;
        e.kind = SyncError::Kind::Format;
        Q_EMIT signInFailed(e);
        return;
    }
    SyncSecrets::Values values;
    values.deviceKey = std::move(keys.secretKey);
    if (!m_secrets->replace(std::move(values))) {
        Q_EMIT keyringWriteFailed();
        return;
    }
    m_publicKey = keys.publicKey;
    m_deviceName = deviceName.simplified().left(kMaxNameLength);
    if (m_deviceName.isEmpty()) {
        m_deviceName = QSysInfo::machineHostName().left(kMaxNameLength);
    }
    if (m_deviceName.isEmpty()) {
        m_deviceName = QStringLiteral("Sugo");
    }
    setPhase(Phase::SigningIn);

    m_api->startDevice(m_deviceName, platformName(), m_publicKey, [this](const SyncReply &reply) {
        if (m_phase != Phase::SigningIn) {
            return;
        }
        const QString deviceCode = reply.json.value(QLatin1String("device_code")).toString();
        if (!reply.ok() || deviceCode.isEmpty()) {
            SyncError error = reply.error;
            if (error.ok()) {
                error.kind = SyncError::Kind::Format;
            }
            abandonSignIn();
            Q_EMIT signInFailed(error);
            return;
        }
        m_deviceCode = deviceCode;
        m_userCode = reply.json.value(QLatin1String("user_code")).toString();
        m_verificationUri = QUrl(reply.json.value(QLatin1String("verification_uri")).toString());
        m_verificationUriComplete = QUrl(reply.json.value(QLatin1String("verification_uri_complete")).toString());
        m_interval = std::max(1, reply.json.value(QLatin1String("interval")).toInt(5));
        m_codeExpiresAt = QDateTime::currentDateTimeUtc().addSecs(reply.json.value(QLatin1String("expires_in")).toInt(600));
        Q_EMIT codeReady(m_userCode, m_verificationUri, m_verificationUriComplete, m_codeExpiresAt);
        schedulePoll(m_interval);
    });
}

void SyncAccount::schedulePoll(int seconds)
{
    m_pollTimer.start(std::max(1, seconds) * m_unitMs);
}

void SyncAccount::pollToken()
{
    if (m_phase != Phase::SigningIn || m_deviceCode.isEmpty()) {
        return;
    }
    m_api->pollToken(m_deviceCode, [this](const SyncReply &reply) {
        if (m_phase != Phase::SigningIn) {
            return;
        }
        if (reply.ok()) {
            finishSignIn(reply.json);
            return;
        }
        const SyncError &error = reply.error;
        switch (error.kind) {
        case SyncError::Kind::AuthorizationPending:
            setDeviceLimitReached(error.reason == QLatin1String("device_limit_reached"));
            break;
        case SyncError::Kind::SlowDown:
            m_interval = error.interval > m_interval ? error.interval : m_interval + 5;
            break;
        case SyncError::Kind::ExpiredToken:
            abandonSignIn();
            Q_EMIT signInExpired();
            return;
        case SyncError::Kind::Network:
        case SyncError::Kind::RateLimited:
        case SyncError::Kind::Server:
            Q_EMIT requestFailed(error);
            break;
        default:
            abandonSignIn();
            Q_EMIT signInFailed(error);
            return;
        }
        if (m_codeExpiresAt.isValid() && QDateTime::currentDateTimeUtc() >= m_codeExpiresAt) {
            abandonSignIn();
            Q_EMIT signInExpired();
            return;
        }
        schedulePoll(std::max(m_interval, error.retryAfter));
    });
}

void SyncAccount::finishSignIn(const QJsonObject &json)
{
    // SyncApi::pollToken() has already written the refresh token (first of all).
    if (m_secrets->hasUnsavedRefreshToken()) {
        Q_EMIT keyringWriteFailed();
    }
    writeSetting(kDeviceId, json.value(QLatin1String("device_id")).toString());
    writeSetting(kUserId, json.value(QLatin1String("user_id")).toString());
    writeSetting(kDeviceName, m_deviceName);
    Settings::sync(*m_settings);
    clearSignInCode();
    setDeviceLimitReached(false);
    setPhase(Phase::WaitingForKey);
    Q_EMIT signedIn();
    if (m_phase == Phase::WaitingForKey) {
        startKeyPhase();
    }
}

void SyncAccount::clearSignInCode()
{
    m_pollTimer.stop();
    m_deviceCode.clear();
    m_userCode.clear();
    m_verificationUri.clear();
    m_verificationUriComplete.clear();
    m_codeExpiresAt = QDateTime();
}

void SyncAccount::abandonSignIn()
{
    clearSignInCode();
    m_api->reset();
    m_secrets->clear();
    m_publicKey.clear();
    setDeviceLimitReached(false);
    setPhase(Phase::SignedOut);
}

void SyncAccount::cancelSignIn()
{
    if (m_phase == Phase::SigningIn) {
        abandonSignIn();
    }
}

// ---------------------------------------------------------------------------------------------
// Key
// ---------------------------------------------------------------------------------------------

void SyncAccount::startKeyPhase()
{
    // The data key arrives in the next change.
}

// ---------------------------------------------------------------------------------------------
// Sign out
// ---------------------------------------------------------------------------------------------

void SyncAccount::signOutLocally()
{
    clearSignInCode();
    m_api->reset();
    m_secrets->clear();
    m_settings->remove(kGroup);
    Settings::sync(*m_settings);
    m_publicKey.clear();
    setDeviceLimitReached(false);
    setPhase(Phase::SignedOut);
}

void SyncAccount::onSignedOutRemotely()
{
    if (m_phase == Phase::SignedOut || m_phase == Phase::SigningIn) {
        return;
    }
    signOutLocally();
    Q_EMIT signedOutRemotely();
}
