// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Settings.h"

#include <QLatin1String>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>

class Autostart;

/**
 * Settings of the program itself (as opposed to the per-timer options in PresetSettings):
 * how it lives in the desktop session.
 *
 * Stored in the settings file (see Settings). "Start at login" is not stored there: its source of truth
 * is the autostart entry on disk, so it stays right when another tool (System Settings)
 * enables or disables the entry.
 */
class AppSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// A system tray exists on this desktop (the settings below that need one are disabled otherwise).
    /// Follows the tray host coming and going (e.g. plasmashell restarting).
    Q_PROPERTY(bool trayAvailable READ trayAvailable NOTIFY trayAvailableChanged FINAL)
    /// This platform can manage a login entry from inside the application.
    Q_PROPERTY(bool autostartSupported READ autostartSupported CONSTANT FINAL)
    /// Other programs can drive the timer: D-Bus, KRunner, global shortcuts (not on macOS).
    Q_PROPERTY(bool externalControlSupported READ externalControlSupported CONSTANT FINAL)

    Q_PROPERTY(bool showTrayIcon READ showTrayIcon WRITE setShowTrayIcon NOTIFY showTrayIconChanged FINAL)
    /// Closing the window hides it instead of quitting (needs the tray icon).
    Q_PROPERTY(bool closeToTray READ closeToTray WRITE setCloseToTray NOTIFY closeToTrayChanged FINAL)
    /// Other programs (global shortcuts, scripts over D-Bus, KRunner) may start, pause, skip and stop the timer.
    Q_PROPERTY(bool allowExternalControl READ allowExternalControl WRITE setAllowExternalControl NOTIFY allowExternalControlChanged FINAL)
    Q_PROPERTY(bool startAtLogin READ startAtLogin WRITE setStartAtLogin NOTIFY startAtLoginChanged FINAL)
    /// When started at login, start without showing the window.
    Q_PROPERTY(bool startMinimized READ startMinimized WRITE setStartMinimized NOTIFY startMinimizedChanged FINAL)

    /// Tray badge style: 0 = Minutes, 1 = Pie, 2 = Plain.
    Q_PROPERTY(int trayBadgeStyle READ trayBadgeStyle WRITE setTrayBadgeStyle NOTIFY trayBadgeStyleChanged FINAL)

    /// Notification sound theme / event sound name.
    Q_PROPERTY(QString soundTheme READ soundTheme WRITE setSoundTheme NOTIFY soundThemeChanged FINAL)
    /// Sound volume percentage (0..100).
    Q_PROPERTY(int soundVolume READ soundVolume WRITE setSoundVolume NOTIFY soundVolumeChanged FINAL)

    /// Pre-alarm at 1 minute before phase ends.
    Q_PROPERTY(bool preAlarmEnabled READ preAlarmEnabled WRITE setPreAlarmEnabled NOTIFY preAlarmEnabledChanged FINAL)
    Q_PROPERTY(QString preAlarmSound READ preAlarmSound WRITE setPreAlarmSound NOTIFY preAlarmSoundChanged FINAL)

    /// Ambient focus background sound mode (0=Off, 1=Clock, 2=Rain, 3=WhiteNoise).
    Q_PROPERTY(int focusSoundMode READ focusSoundMode WRITE setFocusSoundMode NOTIFY focusSoundModeChanged FINAL)
    Q_PROPERTY(int focusSoundVolume READ focusSoundVolume WRITE setFocusSoundVolume NOTIFY focusSoundVolumeChanged FINAL)

    /// Keep screen awake / prevent system idle sleep during work sessions.
    Q_PROPERTY(bool keepScreenAwake READ keepScreenAwake WRITE setKeepScreenAwake NOTIFY keepScreenAwakeChanged FINAL)

    /// Automatically pause active timer when screen is locked.
    Q_PROPERTY(bool autoPauseOnScreenLock READ autoPauseOnScreenLock WRITE setAutoPauseOnScreenLock NOTIFY autoPauseOnScreenLockChanged FINAL)


    /// Whether weekends without sessions do not break the streak.
    Q_PROPERTY(bool protectWeekendStreak READ protectWeekendStreak WRITE setProtectWeekendStreak NOTIFY protectWeekendStreakChanged FINAL)

    /// Whether the first-launch onboarding walkthrough has been completed.
    Q_PROPERTY(bool firstRunCompleted READ firstRunCompleted WRITE setFirstRunCompleted NOTIFY firstRunCompletedChanged FINAL)

    /// Whether to prompt the user to log a task note when a work session finishes.
    Q_PROPERTY(bool promptTaskNote READ promptTaskNote WRITE setPromptTaskNote NOTIFY promptTaskNoteChanged FINAL)

    /// Window colours: "dark", "light" or "system" (the default: the desktop's light or dark mode).
    Q_PROPERTY(QString colorScheme READ colorScheme WRITE setColorScheme NOTIFY colorSchemeChanged FINAL)
    /// "stable" (releases) or "nightly" (a build of every change), for the AppImage's updates.
    Q_PROPERTY(QString updateChannel READ updateChannel WRITE setUpdateChannel NOTIFY updateChannelChanged FINAL)
    /// The AppImage looks for updates by itself (see UpdateChecker); it installs them when asked.
    Q_PROPERTY(bool autoCheckUpdates READ autoCheckUpdates WRITE setAutoCheckUpdates NOTIFY autoCheckUpdatesChanged FINAL)
    /// Sugo's debug messages are recorded (in the diagnostic report). Applied by Diagnostics.
    Q_PROPERTY(bool verboseLogging READ verboseLogging WRITE setVerboseLogging NOTIFY verboseLoggingChanged FINAL)


    /// The close button really keeps the program running: tray icon on, tray present, option set.
    Q_PROPERTY(bool keepRunningInTray READ keepRunningInTray NOTIFY keepRunningInTrayChanged FINAL)
    /// Start with the window hidden: launched with --background and everything for it is in place.
    Q_PROPERTY(bool startHidden READ startHidden NOTIFY startHiddenChanged FINAL)

public:
    /// Real configuration file, real autostart entry, real tray detection. Used by QML.
    explicit AppSettings(QObject *parent = nullptr);
    AppSettings(Settings::Ptr config, std::unique_ptr<Autostart> autostart, bool trayAvailable, QObject *parent = nullptr);
    ~AppSettings() override;


    bool trayAvailable() const { return m_trayAvailable; }
    /// Normally driven by the D-Bus watcher of the tray host; public for tests.
    void setTrayAvailable(bool available);
    bool autostartSupported() const;
    static constexpr bool externalControlSupported()
    {
#ifdef Q_OS_MACOS
        return false;
#else
        return true;
#endif
    }

    bool showTrayIcon() const { return m_showTrayIcon; }
    void setShowTrayIcon(bool show);
    bool closeToTray() const { return m_closeToTray; }
    void setCloseToTray(bool close);
    bool allowExternalControl() const { return m_allowExternalControl; }
    void setAllowExternalControl(bool allow);
    bool startAtLogin() const;
    void setStartAtLogin(bool enabled);
    bool startMinimized() const { return m_startMinimized; }
    void setStartMinimized(bool minimized);

    int trayBadgeStyle() const { return m_trayBadgeStyle; }
    void setTrayBadgeStyle(int style);

    QString soundTheme() const { return m_soundTheme; }
    void setSoundTheme(const QString &theme);

    int soundVolume() const { return m_soundVolume; }
    void setSoundVolume(int volume);

    bool preAlarmEnabled() const { return m_preAlarmEnabled; }
    void setPreAlarmEnabled(bool enabled);

    QString preAlarmSound() const { return m_preAlarmSound; }
    void setPreAlarmSound(const QString &sound);

    int focusSoundMode() const { return m_focusSoundMode; }
    void setFocusSoundMode(int mode);

    int focusSoundVolume() const { return m_focusSoundVolume; }
    void setFocusSoundVolume(int volume);

    bool keepScreenAwake() const { return m_keepScreenAwake; }
    void setKeepScreenAwake(bool awake);

    bool autoPauseOnScreenLock() const { return m_autoPauseOnScreenLock; }
    void setAutoPauseOnScreenLock(bool pause);


    bool protectWeekendStreak() const { return m_protectWeekendStreak; }
    void setProtectWeekendStreak(bool protect);

    bool firstRunCompleted() const { return m_firstRunCompleted; }
    void setFirstRunCompleted(bool completed);

    bool promptTaskNote() const { return m_promptTaskNote; }
    void setPromptTaskNote(bool prompt);

    QString colorScheme() const { return m_colorScheme; }
    void setColorScheme(const QString &scheme);

    QString updateChannel() const { return m_updateChannel; }
    void setUpdateChannel(const QString &channel);
    bool autoCheckUpdates() const { return m_autoCheckUpdates; }
    void setAutoCheckUpdates(bool enabled);
    bool verboseLogging() const { return m_verboseLogging; }
    void setVerboseLogging(bool verbose);
    /// Puts every preference back to its default, through the setters (so views are told and the
    /// file is updated). Kept: whether the welcome tour was seen, the login entry (it lives on
    /// disk, outside the settings file) and everything that is not a preference of this class
    /// (timers, history, the calendar account).
    Q_INVOKABLE void resetToDefaults();
    /// Installs the chosen colour scheme as the application palette (needs a GUI application).
    void applyColorScheme();


    /// Plays an event sound or a file at the given volume, fading it in and out where Qt
    /// Multimedia can decode it.
    Q_INVOKABLE void playSoundPreview(const QString &soundName, int volume = -1);
    /// The fade level (0..1) of an alert sound at a position; the duration is 0 while unknown.
    static double alertFadeGain(qint64 positionMs, qint64 durationMs);
    /// Tests: called with the sound name and volume instead of starting a player.
    void setSoundPlayerForTesting(std::function<void(const QString &, int)> player) { m_soundPlayer = std::move(player); }
    /// canberra-gtk-play arguments for an event sound name. The name is glued to its option, so
    /// one that starts with '-' can never be read as another option.
    static QStringList canberraArguments(const QString &soundName);
    /// An event sound name ("complete", "alarm-clock-elapsed") or an absolute path to a file.
    /// Anything else from the configuration ("../x", "-x", "a/b") is not looked up or played.
    static bool isUsableSoundName(const QString &soundName);
    Q_INVOKABLE void openShortcutsSettings();

    bool keepRunningInTray() const;
    bool startHidden() const;

    /// Set once at startup from the --background command line option.
    void setLaunchedInBackground(bool background);

Q_SIGNALS:
    void showTrayIconChanged();
    void closeToTrayChanged();
    void allowExternalControlChanged();
    void startAtLoginChanged();
    void startMinimizedChanged();
    void trayBadgeStyleChanged();
    void soundThemeChanged();
    void soundVolumeChanged();
    void preAlarmEnabledChanged();
    void preAlarmSoundChanged();
    void focusSoundModeChanged();
    void focusSoundVolumeChanged();
    void keepScreenAwakeChanged();
    void autoPauseOnScreenLockChanged();
    void protectWeekendStreakChanged();
    void firstRunCompletedChanged();
    void promptTaskNoteChanged();
    void colorSchemeChanged();
    void updateChannelChanged();
    void autoCheckUpdatesChanged();
    void verboseLoggingChanged();
    void trayAvailableChanged();
    void keepRunningInTrayChanged();
    void startHiddenChanged();
    /// The login entry could not be changed (read-only home, full disk, ...).
    void autostartFailed(const QString &message);

private:
    /// The value of every preference when nothing is stored: used when loading the file and by
    /// resetToDefaults().
    struct Defaults {
        static constexpr bool showTrayIcon = true;
        static constexpr bool closeToTray = true;
        static constexpr bool allowExternalControl = true;
        static constexpr bool startMinimized = true;
        static constexpr int trayBadgeStyle = 0;
        static constexpr QLatin1String soundTheme{"alarm-clock-elapsed"};
        static constexpr int soundVolume = 80;
        static constexpr bool preAlarmEnabled = true;
        static constexpr QLatin1String preAlarmSound{"dialog-information"};
        static constexpr int focusSoundMode = 0;
        static constexpr int focusSoundVolume = 30;
        static constexpr bool keepScreenAwake = true;
        static constexpr bool autoPauseOnScreenLock = true;
        static constexpr bool protectWeekendStreak = true;
        static constexpr bool firstRunCompleted = false;
        static constexpr bool promptTaskNote = true;
        static constexpr QLatin1String colorScheme{"system"};
        static constexpr QLatin1String updateChannel{"stable"};
        static constexpr bool autoCheckUpdates = true;
        static constexpr bool verboseLogging = false;
    };

    void store(const char *key, bool value);
    void storeInt(const char *key, int value);
    void storeString(const char *key, const QString &value);
    void watchTray();

    Settings::Ptr m_config;
    std::unique_ptr<Autostart> m_autostart;
    std::function<void(const QString &, int)> m_soundPlayer;
    bool m_trayAvailable;

    bool m_showTrayIcon = Defaults::showTrayIcon;
    bool m_closeToTray = Defaults::closeToTray;
    bool m_allowExternalControl = Defaults::allowExternalControl;
    bool m_startMinimized = Defaults::startMinimized;
    int m_trayBadgeStyle = Defaults::trayBadgeStyle;
    QString m_soundTheme = QString(Defaults::soundTheme);
    int m_soundVolume = Defaults::soundVolume;
    bool m_preAlarmEnabled = Defaults::preAlarmEnabled;
    QString m_preAlarmSound = QString(Defaults::preAlarmSound);
    int m_focusSoundMode = Defaults::focusSoundMode;
    int m_focusSoundVolume = Defaults::focusSoundVolume;
    bool m_keepScreenAwake = Defaults::keepScreenAwake;
    bool m_autoPauseOnScreenLock = Defaults::autoPauseOnScreenLock;
    bool m_protectWeekendStreak = Defaults::protectWeekendStreak;
    bool m_firstRunCompleted = Defaults::firstRunCompleted;
    bool m_promptTaskNote = Defaults::promptTaskNote;
    QString m_colorScheme = QString(Defaults::colorScheme);
    QString m_updateChannel = QString(Defaults::updateChannel);
    bool m_autoCheckUpdates = Defaults::autoCheckUpdates;
    bool m_verboseLogging = Defaults::verboseLogging;
    bool m_launchedInBackground = false;
};
