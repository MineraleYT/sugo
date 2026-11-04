// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>

#include "Settings.h"
#include "SyncAccount.h"
#include "TimerPreset.h"

class Notifier;
class PresetModel;
class PresetPersistence;
class PresetRepository;
class SecretStore;
class SyncApi;
class SyncSecrets;

/**
 * Sugo Sync for the interface: the account (SyncAccount) and the sync of the timers.
 *
 * The timers travel as one encrypted blob (PresetSyncFormat, SyncCrypto). One operation runs at
 * a time:
 * - Pull: when the device becomes ready, when the window is activated (at most once a minute),
 *   every 30 minutes and on syncNow(), with If-None-Match. A new blob is decrypted, merged with
 *   the local timers, applied here if it changes them, and uploaded again if the merge changes
 *   it. 404: there is no blob yet, the first upload follows.
 * - Push: 3 s after PresetPersistence::saved() (not for what a merge applied), with If-Match,
 *   or If-None-Match: * without a blob. 412: download, merge, apply, try again, three times at
 *   most; then Error, and again in 5 minutes.
 * - A blob this version cannot read (newer format: FormatTooNew; wrong key or damaged:
 *   UnreadableData) is never overwritten. Network failures are tried again after 1, 5 and
 *   15 minutes; the program works as usual meanwhile.
 *
 * The keyring is read once per run, a few seconds after attach(), and only when the settings say
 * this computer is linked: never while signed out.
 *
 * No Qt Gui here: main.cpp forwards the window's activation (windowActivated()) and gives the
 * desktop actions (setDesktopActions()); tests run with a QCoreApplication.
 */
class SyncController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(State state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged FINAL)
    Q_PROPERTY(ErrorKind errorKind READ errorKind NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool keyringAvailable READ keyringAvailable NOTIFY keyringAvailableChanged FINAL)
    Q_PROPERTY(QDateTime lastSync READ lastSync NOTIFY lastSyncChanged FINAL)

    Q_PROPERTY(QString userCode READ userCode NOTIFY signInCodeChanged FINAL)
    Q_PROPERTY(QUrl verificationUri READ verificationUri NOTIFY signInCodeChanged FINAL)
    Q_PROPERTY(QUrl verificationUriComplete READ verificationUriComplete NOTIFY signInCodeChanged FINAL)
    Q_PROPERTY(bool deviceLimitReached READ deviceLimitReached NOTIFY deviceLimitReachedChanged FINAL)
    Q_PROPERTY(QString verificationCode READ verificationCode NOTIFY verificationCodeChanged FINAL)

    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged FINAL)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY deviceNameChanged FINAL)
    Q_PROPERTY(bool recoveryCodeShowable READ recoveryCodeShowable NOTIFY recoveryCodeShowableChanged FINAL)
    Q_PROPERTY(QVariantMap pendingApproval READ pendingApproval NOTIFY pendingApprovalChanged FINAL)

public:
    enum State {
        Off,                ///< Not linked (keyringAvailable says whether signing in can work).
        SigningIn,          ///< A code is shown; deviceLimitReached while waiting for a free slot.
        WaitingForApproval, ///< Linked, without the data key: verificationCode, or a recovery code.
        Idle,
        Syncing,            ///< A transfer runs (or the keyring is being read).
        Error,              ///< errorKind says what; it is tried again by itself.
    };
    Q_ENUM(State)

    enum ErrorKind {
        NoError,
        Network,            ///< Out of reach; tried again after 1, 5, 15 minutes.
        SignedOutRemotely,  ///< (state Off) Disconnected on sugo.stream: sign in again.
        FormatTooNew,       ///< A newer Sugo wrote the timers: update to keep syncing.
        UnreadableData,     ///< The blob does not open with this account's key.
        TooLarge,
        KeyringUnavailable, ///< The keyring did not answer; tried again when the window is activated.
        KeyringWriteFailed,
        RateLimited,
        Server,             ///< The server failed, or the blob kept changing during an upload.
    };
    Q_ENUM(ErrorKind)

    /// For QML: the settings file, the system keyring and SyncApi::defaultBaseUrl().
    explicit SyncController(QObject *parent = nullptr);
    /// For tests: `store` is not owned and must outlive this object.
    SyncController(Settings::Ptr settings, SecretStore *store, const QUrl &baseUrl, QObject *parent = nullptr);
    ~SyncController() override;

    /**
     * Starts Sugo Sync for these timers (not called in smoke runs: the controller stays Off).
     * Reads the keyring a few seconds later if this computer is linked. Not owned; they must
     * outlive this object, or be detached with attach(nullptr, nullptr, nullptr).
     */
    void attach(PresetModel *presets, PresetPersistence *persistence, PresetRepository *repository);

    /// How the desktop opens a page and copies text (main.cpp; QML may also do it itself).
    void setDesktopActions(std::function<bool(const QUrl &)> openUrl, std::function<void(const QString &)> copyText);

    /**
     * Desktop notifications: a device asking for approval while the window is hidden (as
     * `windowVisible` says) is announced, and a click on it emits showSyncPageRequested().
     * `notifier` is not owned; nullptr stops the notifications.
     */
    void attachNotifier(Notifier *notifier, std::function<bool()> windowVisible);

    /// One second in ms for every wait (tests make it short).
    void setTimeUnitMs(int ms);

    State state() const { return m_state; }
    QString statusText() const;
    ErrorKind errorKind() const { return m_errorKind; }
    bool keyringAvailable() const;
    QDateTime lastSync() const;

    QString userCode() const;
    QUrl verificationUri() const;
    QUrl verificationUriComplete() const;
    bool deviceLimitReached() const;
    QString verificationCode() const;

    QVariantList devices() const;
    /// This device's name; while signed out, the name sign-in suggests (the host name).
    QString deviceName() const;
    bool recoveryCodeShowable() const;
    /// The first device asking for the key: id, name, platform, verificationCode; empty if none.
    QVariantMap pendingApproval() const;

    SyncAccount *account() const { return m_account; }

    // --- Sign-in ------------------------------------------------------------------------------
    Q_INVOKABLE void signIn(const QString &deviceName);
    Q_INVOKABLE void cancelSignIn();
    /// Opens sugo.stream with the code filled in; false without a code or a way to open it.
    Q_INVOKABLE bool openVerificationPage();
    /// Copies the code; false without a code or a clipboard.
    Q_INVOKABLE bool copyUserCode();

    // --- Sync ---------------------------------------------------------------------------------
    /// Downloads now (and uploads what waits), also after an error.
    Q_INVOKABLE void syncNow();

    // --- Devices ------------------------------------------------------------------------------
    Q_INVOKABLE void approveDevice(const QString &id);
    Q_INVOKABLE void ignoreDevice(const QString &id);
    Q_INVOKABLE void disconnectDevice(const QString &id);
    Q_INVOKABLE void refreshDevices();

    // --- Recovery code ------------------------------------------------------------------------
    Q_INVOKABLE void useRecoveryCode(const QString &code);
    /// The recovery code, when recoveryCodeShowable (from memory, no keyring access).
    Q_INVOKABLE QString recoveryCode() const;
    Q_INVOKABLE void regenerateRecoveryCode();
    /// Whether `text` is group `index` (from 0) of the code last given by recoveryCodeToConfirm
    /// (else of recoveryCode()), in any case, with O for 0 and I or L for 1.
    Q_INVOKABLE bool confirmRecoveryGroup(int index, const QString &text) const;

    Q_INVOKABLE void signOut();

public Q_SLOTS:
    /// The window became active: key polling speeds up, the timers are pulled (at most once a
    /// minute), and a keyring that failed is read again.
    void windowActivated();
    void windowDeactivated();

Q_SIGNALS:
    void stateChanged();
    void statusTextChanged();
    void keyringAvailableChanged();
    void lastSyncChanged();
    void signInCodeChanged();
    void deviceLimitReachedChanged();
    void verificationCodeChanged();
    void devicesChanged();
    void deviceNameChanged();
    void recoveryCodeShowableChanged();
    void pendingApprovalChanged();

    /// A device asks for the key: pendingApproval holds it.
    void approvalRequested();
    /// A new recovery code (first device, or regenerated): show it once and have it confirmed.
    void recoveryCodeToConfirm(const QString &code);
    /// Signed out here; `remoteDone` false when the server could not be told (the device keeps
    /// its slot until disconnected on sugo.stream).
    void signedOut(bool remoteDone);
    /// Something worth a toast.
    void message(const QString &text);
    /// The approval notification was clicked: show the window on the Sync settings.
    /// `activationToken` (may be empty) lets the window take the focus on Wayland.
    void showSyncPageRequested(const QString &activationToken);

private:
    enum class Op { None, Pull, Push };

    SyncController(Settings::Ptr settings, std::unique_ptr<SecretStore> ownedStore, SecretStore *store, const QUrl &baseUrl,
                   QObject *parent);
    void connectAccount();
    void onSignedOut();
    void loadSecrets();
    void onPhaseChanged();
    void onReady();
    void resetCycle();

    void onSaved();
    void requestPull();
    void requestPush();
    void runNext();
    void pull(bool unconditional);
    void push();
    void takeRemote(const SyncReply &reply);
    void applyLocally(const QList<TimerPreset> &presets, const QList<PresetTombstone> &tombstones);
    void succeeded();
    void failed(const SyncError &error, Op op);
    void scheduleRetry(int seconds);

    void setError(ErrorKind kind);
    void updateState();
    void setLastSyncNow();
    QByteArray etag() const;
    void setEtag(const QByteArray &etag);
    bool dirty() const;
    void setDirty(bool dirty);
    void writeSetting(const char *name, const QVariant &value);
    bool readyToSync() const;
    void showNextApproval();
    void notifyApproval();
    void dropApproval(const QString &id);

    Settings::Ptr m_settings;
    std::unique_ptr<SecretStore> m_ownedStore;
    std::unique_ptr<SyncSecrets> m_secrets;
    SyncApi *m_api = nullptr;
    SyncAccount *m_account = nullptr;

    QPointer<PresetModel> m_presets;
    QPointer<PresetPersistence> m_persistence;
    PresetRepository *m_repository = nullptr;
    bool m_attached = false;

    std::function<bool(const QUrl &)> m_openUrl;
    std::function<void(const QString &)> m_copyText;
    QPointer<Notifier> m_notifier;
    std::function<bool()> m_windowVisible;
    int m_unitMs = 1000;

    State m_state = Off;
    ErrorKind m_errorKind = NoError;
    ErrorKind m_lastErrorKind = NoError; ///< As last announced by stateChanged().
    QString m_lastStatusText;

    enum class Load { NotYet, Waiting, Done, Failed };
    Load m_load = Load::NotYet;
    QTimer m_loadTimer;

    Op m_running = Op::None;
    bool m_pullWanted = false;
    bool m_pullUnconditional = false; ///< The next pull downloads even an unchanged blob (after 412).
    bool m_pushWanted = false;
    bool m_remoteUnreadable = false;  ///< The blob does not open: never overwritten until it does.
    quint64 m_saves = 0;              ///< saved() count, so an upload clears Dirty only if nothing came after.
    bool m_applyingRemote = false;
    bool m_formatTooNew = false;
    bool m_signingOut = false;
    bool m_keyRejected = false;
    int m_conflicts = 0;
    int m_failures = 0;
    QTimer m_pushTimer;
    QTimer m_pullTimer;
    QTimer m_retryTimer;
    QElapsedTimer m_sinceLastPull;

    QList<QVariantMap> m_approvals;
    QString m_recoveryToConfirm;
};
