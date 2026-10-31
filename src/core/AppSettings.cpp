// SPDX-License-Identifier: GPL-3.0-or-later
#include "AppSettings.h"

#include "Autostart.h"
#include "FocusSoundController.h"
#include "SystemTray.h"

#include <QAudioOutput>
#include <QDBusConnection>
#include <QDebug>
#include <QDBusServiceWatcher>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QMediaPlayer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTimer>
#include <QUrl>
#include "Localization.h"
#include "Settings.h"
#include <algorithm>


namespace
{
const char kShowTrayIcon[] = "ShowTrayIcon";
const char kCloseToTray[] = "CloseToTray";
const char kAllowExternalControl[] = "AllowExternalControl";
const char kStartMinimized[] = "StartMinimized";
const char kTrayBadgeStyle[] = "TrayBadgeStyle";
const char kSoundTheme[] = "SoundTheme";
const char kSoundVolume[] = "SoundVolume";
const char kPreAlarmEnabled[] = "PreAlarmEnabled";
const char kPreAlarmSound[] = "PreAlarmSound";
const char kFocusSoundMode[] = "FocusSoundMode";
const char kFocusSoundVolume[] = "FocusSoundVolume";
const char kKeepScreenAwake[] = "KeepScreenAwake";
const char kAutoPauseOnScreenLock[] = "AutoPauseOnScreenLock";
const char kProtectWeekendStreak[] = "ProtectWeekendStreak";
const char kFirstRunCompleted[] = "FirstRunCompleted";
const char kPromptTaskNote[] = "PromptTaskNote";
const char kColorScheme[] = "ColorScheme";
const char kUpdateChannel[] = "UpdateChannel";
const char kAutoCheckUpdates[] = "AutoCheckUpdates";

QString validColorScheme(const QString &scheme)
{
    const QString s = scheme.trimmed().toLower();
    if (s == QLatin1String("light") || s == QLatin1String("dark")) {
        return s;
    }
    // Follow the desktop's light or dark mode unless told otherwise.
    return QStringLiteral("system");
}

/// A sound file for an event sound name, looked up in the XDG data dirs (so it also works
/// inside Flatpak), then in the host's /usr/share. An absolute path is used as is.
QString findSoundFile(const QString &soundName)
{
    if (soundName.startsWith(QLatin1Char('/'))) {
        return QFileInfo(soundName).isFile() ? soundName : QString();
    }
    for (const char *theme : {"ocean", "freedesktop"}) {
        for (const char *ext : {"oga", "ogg", "wav"}) {
            const QString relative = QStringLiteral("sounds/%1/stereo/%2.%3").arg(QLatin1String(theme), soundName, QLatin1String(ext));
            QString file = QStandardPaths::locate(QStandardPaths::GenericDataLocation, relative);
            if (file.isEmpty()) {
                for (const QString &prefix : {QStringLiteral("/usr/share/"), QStringLiteral("/run/host/usr/share/")}) {
                    if (QFile::exists(prefix + relative)) {
                        file = prefix + relative;
                        break;
                    }
                }
            }
            if (!file.isEmpty()) {
                return file;
            }
        }
    }
    return {};
}

constexpr qint64 kAlertFadeInMs = 300;
constexpr qint64 kAlertFadeOutMs = 600;

/// Plays a file with pw-play or paplay, at a fixed volume.
void playWithPlayerProgram(const QString &file, int vol)
{
    if (!QStandardPaths::findExecutable(QStringLiteral("pw-play")).isEmpty()) {
        const double volFactor = vol / 100.0;
        if (QProcess::startDetached(QStringLiteral("pw-play"), {QStringLiteral("--volume"), QString::number(volFactor, 'f', 2), file})) {
            return;
        }
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("paplay")).isEmpty()) {
        // paplay's volume is linear, 65536 = 100 %.
        const int paVolume = vol * 65536 / 100;
        if (QProcess::startDetached(QStringLiteral("paplay"), {QStringLiteral("--volume=%1").arg(paVolume), file})) {
            return;
        }
    }
    qWarning() << "Cannot play" << file << "- neither pw-play nor paplay could be started";
}

/// Plays a file through Qt Multimedia, fading it in and out. False if Qt Multimedia has no
/// backend; a file it cannot decode falls back to playWithPlayerProgram().
bool playWithFade(const QString &file, int vol, QObject *parent)
{
    auto *player = new QMediaPlayer(parent);
    if (!player->isAvailable()) {
        delete player;
        return false;
    }
    auto *output = new QAudioOutput(player);
    output->setVolume(0.0f);
    player->setAudioOutput(output);

    auto *fade = new QTimer(player);
    fade->setInterval(20);
    QObject::connect(fade, &QTimer::timeout, player, [player, output, vol]() {
        output->setVolume(float(vol / 100.0 * AppSettings::alertFadeGain(player->position(), player->duration())));
    });
    QObject::connect(player, &QMediaPlayer::mediaStatusChanged, player, [player](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            player->deleteLater();
        }
    });
    QObject::connect(player, &QMediaPlayer::errorOccurred, player, [player, file, vol]() {
        player->disconnect(); // errors can come more than once: play the fallback only once
        player->deleteLater();
        playWithPlayerProgram(file, vol);
    });
    player->setSource(QUrl::fromLocalFile(file));
    fade->start();
    player->play();
    return true;
}
} // namespace

AppSettings::AppSettings(QObject *parent)
    : AppSettings(Settings::openDefault(), createAutostart(), systemTrayAvailable(), parent)
{
    watchTray();
}

AppSettings::AppSettings(Settings::Ptr config, std::unique_ptr<Autostart> autostart, bool trayAvailable, QObject *parent)
    : QObject(parent)
    , m_config(std::move(config))
    , m_autostart(std::move(autostart))
    , m_trayAvailable(trayAvailable)
{
    const QSettings &settings = *m_config;
    m_showTrayIcon = settings.value(QLatin1String(kShowTrayIcon), true).toBool();
    m_closeToTray = settings.value(QLatin1String(kCloseToTray), true).toBool();
    m_allowExternalControl = settings.value(QLatin1String(kAllowExternalControl), true).toBool();
    m_startMinimized = settings.value(QLatin1String(kStartMinimized), true).toBool();
    m_trayBadgeStyle = std::clamp(settings.value(QLatin1String(kTrayBadgeStyle), 0).toInt(), 0, 2);
    m_soundTheme = settings.value(QLatin1String(kSoundTheme), QStringLiteral("alarm-clock-elapsed")).toString();
    m_soundVolume = std::clamp(settings.value(QLatin1String(kSoundVolume), 80).toInt(), 0, 100);
    m_preAlarmEnabled = settings.value(QLatin1String(kPreAlarmEnabled), true).toBool();
    m_preAlarmSound = settings.value(QLatin1String(kPreAlarmSound), QStringLiteral("dialog-information")).toString();
    m_focusSoundMode = std::clamp(settings.value(QLatin1String(kFocusSoundMode), 0).toInt(), 0, FocusSoundController::LastMode);
    m_focusSoundVolume = std::clamp(settings.value(QLatin1String(kFocusSoundVolume), 30).toInt(), 0, 100);
    m_keepScreenAwake = settings.value(QLatin1String(kKeepScreenAwake), true).toBool();
    m_autoPauseOnScreenLock = settings.value(QLatin1String(kAutoPauseOnScreenLock), true).toBool();
    m_protectWeekendStreak = settings.value(QLatin1String(kProtectWeekendStreak), true).toBool();
    m_firstRunCompleted = settings.value(QLatin1String(kFirstRunCompleted), false).toBool();
    m_promptTaskNote = settings.value(QLatin1String(kPromptTaskNote), true).toBool();
    m_colorScheme = validColorScheme(settings.value(QLatin1String(kColorScheme), QStringLiteral("system")).toString());
    applyColorScheme();
    m_updateChannel = settings.value(QLatin1String(kUpdateChannel)).toString() == QLatin1String("nightly") ? QStringLiteral("nightly") : QStringLiteral("stable");
    m_autoCheckUpdates = settings.value(QLatin1String(kAutoCheckUpdates), true).toBool();

    // The program may have moved since the entry was written (new install prefix).
    if (m_autostart) {
        m_autostart->repair();
    }
}

AppSettings::~AppSettings() = default;

void AppSettings::store(const char *key, bool value)
{
    m_config->setValue(QLatin1String(key), value);
    Settings::sync(*m_config);
}

void AppSettings::storeInt(const char *key, int value)
{
    m_config->setValue(QLatin1String(key), value);
    Settings::sync(*m_config);
}

void AppSettings::storeString(const char *key, const QString &value)
{
    m_config->setValue(QLatin1String(key), value);
    Settings::sync(*m_config);
}

void AppSettings::watchTray()
{
    // The tray host can appear after us (login) or restart (plasmashell crash).
    auto *watcher = new QDBusServiceWatcher(QStringLiteral("org.kde.StatusNotifierWatcher"),
                                            QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration,
                                            this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        setTrayAvailable(true);
    });
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        setTrayAvailable(false);
    });
}

void AppSettings::setTrayAvailable(bool available)
{
    if (m_trayAvailable == available) {
        return;
    }
    m_trayAvailable = available;
    Q_EMIT trayAvailableChanged();
    Q_EMIT keepRunningInTrayChanged();
    Q_EMIT startHiddenChanged();
}

bool AppSettings::autostartSupported() const
{
    return m_autostart && m_autostart->isSupported();
}

void AppSettings::setShowTrayIcon(bool show)
{
    if (m_showTrayIcon == show) {
        return;
    }
    m_showTrayIcon = show;
    store(kShowTrayIcon, show);
    Q_EMIT showTrayIconChanged();
    Q_EMIT keepRunningInTrayChanged();
    Q_EMIT startHiddenChanged();
}

void AppSettings::setCloseToTray(bool close)
{
    if (m_closeToTray == close) {
        return;
    }
    m_closeToTray = close;
    store(kCloseToTray, close);
    Q_EMIT closeToTrayChanged();
    Q_EMIT keepRunningInTrayChanged();
}

void AppSettings::setAllowExternalControl(bool allow)
{
    if (m_allowExternalControl == allow) {
        return;
    }
    m_allowExternalControl = allow;
    store(kAllowExternalControl, allow);
    Q_EMIT allowExternalControlChanged();
}

bool AppSettings::startAtLogin() const
{
    return m_autostart && m_autostart->isEnabled();
}

void AppSettings::setStartAtLogin(bool enabled)
{
    if (!m_autostart) {
        return;
    }
    if (enabled != startAtLogin() && !m_autostart->setEnabled(enabled)) {
        Q_EMIT autostartFailed(enabled ? i18n("Could not set up starting Sugo at login.")
                                       : i18n("Could not remove the entry that starts Sugo at login."));
    } else if (!enabled) {
        // "Start minimized" only means something for the start at login: switched off with it,
        // it never stays on unseen behind the disabled row.
        setStartMinimized(false);
    }
    // Always announced: after a failure this makes views show the real state again.
    Q_EMIT startAtLoginChanged();
}

void AppSettings::setStartMinimized(bool minimized)
{
    if (m_startMinimized == minimized) {
        return;
    }
    m_startMinimized = minimized;
    store(kStartMinimized, minimized);
    Q_EMIT startMinimizedChanged();
    Q_EMIT startHiddenChanged();
}

void AppSettings::setTrayBadgeStyle(int style)
{
    style = std::clamp(style, 0, 2);
    if (m_trayBadgeStyle == style) {
        return;
    }
    m_trayBadgeStyle = style;
    storeInt(kTrayBadgeStyle, style);
    Q_EMIT trayBadgeStyleChanged();
}

void AppSettings::setSoundTheme(const QString &theme)
{
    if (m_soundTheme == theme) {
        return;
    }
    m_soundTheme = theme;
    storeString(kSoundTheme, theme);
    Q_EMIT soundThemeChanged();
}

void AppSettings::setSoundVolume(int volume)
{
    volume = std::clamp(volume, 0, 100);
    if (m_soundVolume == volume) {
        return;
    }
    m_soundVolume = volume;
    storeInt(kSoundVolume, volume);
    Q_EMIT soundVolumeChanged();
}

void AppSettings::setPreAlarmEnabled(bool enabled)
{
    if (m_preAlarmEnabled == enabled) {
        return;
    }
    m_preAlarmEnabled = enabled;
    store(kPreAlarmEnabled, enabled);
    Q_EMIT preAlarmEnabledChanged();
}

void AppSettings::setPreAlarmSound(const QString &sound)
{
    if (m_preAlarmSound == sound) {
        return;
    }
    m_preAlarmSound = sound;
    storeString(kPreAlarmSound, sound);
    Q_EMIT preAlarmSoundChanged();
}

void AppSettings::setFocusSoundMode(int mode)
{
    mode = std::clamp(mode, 0, FocusSoundController::LastMode);
    if (m_focusSoundMode == mode) {
        return;
    }
    m_focusSoundMode = mode;
    storeInt(kFocusSoundMode, mode);
    Q_EMIT focusSoundModeChanged();
}

void AppSettings::setFocusSoundVolume(int volume)
{
    volume = std::clamp(volume, 0, 100);
    if (m_focusSoundVolume == volume) {
        return;
    }
    m_focusSoundVolume = volume;
    storeInt(kFocusSoundVolume, volume);
    Q_EMIT focusSoundVolumeChanged();
}

void AppSettings::setKeepScreenAwake(bool awake)
{
    if (m_keepScreenAwake == awake) {
        return;
    }
    m_keepScreenAwake = awake;
    store(kKeepScreenAwake, awake);
    Q_EMIT keepScreenAwakeChanged();
}

void AppSettings::setAutoPauseOnScreenLock(bool pause)
{
    if (m_autoPauseOnScreenLock == pause) {
        return;
    }
    m_autoPauseOnScreenLock = pause;
    store(kAutoPauseOnScreenLock, pause);
    Q_EMIT autoPauseOnScreenLockChanged();
}

void AppSettings::setProtectWeekendStreak(bool protect)
{
    if (m_protectWeekendStreak == protect) {
        return;
    }
    m_protectWeekendStreak = protect;
    store(kProtectWeekendStreak, protect);
    Q_EMIT protectWeekendStreakChanged();
}

void AppSettings::setFirstRunCompleted(bool completed)
{
    if (m_firstRunCompleted == completed) {
        return;
    }
    m_firstRunCompleted = completed;
    store(kFirstRunCompleted, completed);
    Q_EMIT firstRunCompletedChanged();
}

void AppSettings::setPromptTaskNote(bool prompt)
{
    if (m_promptTaskNote == prompt) {
        return;
    }
    m_promptTaskNote = prompt;
    store(kPromptTaskNote, prompt);
    Q_EMIT promptTaskNoteChanged();
}

void AppSettings::setColorScheme(const QString &scheme)
{
    const QString valid = validColorScheme(scheme);
    if (m_colorScheme == valid) {
        return;
    }
    m_colorScheme = valid;
    storeString(kColorScheme, valid);
    applyColorScheme();
    Q_EMIT colorSchemeChanged();
}

void AppSettings::setUpdateChannel(const QString &channel)
{
    const QString valid = channel == QLatin1String("nightly") ? channel : QStringLiteral("stable");
    if (m_updateChannel == valid) {
        return;
    }
    m_updateChannel = valid;
    storeString(kUpdateChannel, valid);
    Q_EMIT updateChannelChanged();
}

void AppSettings::setAutoCheckUpdates(bool enabled)
{
    if (m_autoCheckUpdates == enabled) {
        return;
    }
    m_autoCheckUpdates = enabled;
    store(kAutoCheckUpdates, enabled);
    Q_EMIT autoCheckUpdatesChanged();
}

void AppSettings::applyColorScheme()
{
    // The interface (Theme.qml) follows the style hints; "system" lets the platform decide.
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        return;
    }
    Qt::ColorScheme scheme = Qt::ColorScheme::Unknown;
    if (m_colorScheme == QLatin1String("dark")) {
        scheme = Qt::ColorScheme::Dark;
    } else if (m_colorScheme == QLatin1String("light")) {
        scheme = Qt::ColorScheme::Light;
    }
    QGuiApplication::styleHints()->setColorScheme(scheme);
}

void AppSettings::playSoundPreview(const QString &soundName, int volume)
{
    const int vol = (volume >= 0) ? std::clamp(volume, 0, 100) : m_soundVolume;
    if (!isUsableSoundName(soundName)) {
        qWarning() << "Ignoring the sound" << soundName << ": not a sound name or an absolute path";
        return;
    }
    if (m_soundPlayer) {
        m_soundPlayer(soundName, vol);
        return;
    }
    const QString file = findSoundFile(soundName);
    if (file.isEmpty()) {
        // Not a file we can find: let libcanberra resolve the event sound name.
        if (QStandardPaths::findExecutable(QStringLiteral("canberra-gtk-play")).isEmpty()
            || !QProcess::startDetached(QStringLiteral("canberra-gtk-play"), canberraArguments(soundName))) {
            qWarning() << "Sound" << soundName << "not found and canberra-gtk-play is unavailable";
        }
        return;
    }

    if (!playWithFade(file, vol, this)) {
        playWithPlayerProgram(file, vol);
    }
}

double AppSettings::alertFadeGain(qint64 positionMs, qint64 durationMs)
{
    // A short sound gets shorter fades, so that it still reaches full volume.
    const qint64 fadeIn = durationMs > 0 ? std::min(kAlertFadeInMs, durationMs / 3) : kAlertFadeInMs;
    double gain = fadeIn > 0 ? std::clamp(double(positionMs) / fadeIn, 0.0, 1.0) : 1.0;
    if (durationMs > 0) {
        const qint64 fadeOut = std::min(kAlertFadeOutMs, durationMs / 3);
        if (fadeOut > 0) {
            gain = std::min(gain, std::clamp(double(durationMs - positionMs) / fadeOut, 0.0, 1.0));
        }
    }
    return gain;
}

bool AppSettings::isUsableSoundName(const QString &soundName)
{
    if (soundName.startsWith(QLatin1Char('/'))) {
        return !soundName.contains(QStringLiteral("/../")) && !soundName.endsWith(QStringLiteral("/.."));
    }
    static const QRegularExpression name(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9._-]*$"));
    return name.match(soundName).hasMatch() && !soundName.contains(QStringLiteral(".."));
}

QStringList AppSettings::canberraArguments(const QString &soundName)
{
    return {QStringLiteral("--id=") + soundName};
}

void AppSettings::openShortcutsSettings()
{
    if (!QProcess::startDetached(QStringLiteral("systemsettings"), {QStringLiteral("kcm_keys")})) {
        qWarning() << "Failed to launch systemsettings kcm_keys";
    }
}

bool AppSettings::keepRunningInTray() const
{
    return m_showTrayIcon && m_closeToTray && m_trayAvailable;
}

bool AppSettings::startHidden() const
{
    return m_launchedInBackground && m_startMinimized && m_showTrayIcon && m_trayAvailable;
}

void AppSettings::setLaunchedInBackground(bool background)
{
    m_launchedInBackground = background;
    Q_EMIT startHiddenChanged();
}
