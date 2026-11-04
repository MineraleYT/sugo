// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "Settings.h"

#include <memory>

#include "AppSettings.h"
#include "Autostart.h"
#include "FakeShortcutTools.h"
#include "FakeShortcuts.h"
#include "ShortcutsFocusInhibitor.h"
#include "ShortcutsFocusInstaller.h"

namespace
{
/// Autostart whose state the test controls; shared with the AppSettings under test.
struct FakeState {
    bool supported = true;
    bool enabled = false;
    bool failNext = false;
    int setCalls = 0;
    int repairCalls = 0;
};

class FakeAutostart final : public Autostart
{
public:
    explicit FakeAutostart(std::shared_ptr<FakeState> state)
        : m_state(std::move(state))
    {
    }

    bool isSupported() const override { return m_state->supported; }
    bool isEnabled() const override { return m_state->enabled; }
    bool setEnabled(bool enabled) override
    {
        ++m_state->setCalls;
        if (m_state->failNext) {
            m_state->failNext = false;
            return false;
        }
        m_state->enabled = enabled;
        return true;
    }
    void repair() override { ++m_state->repairCalls; }

private:
    std::shared_ptr<FakeState> m_state;
};
} // namespace

class AppSettingsTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir dir;
    std::shared_ptr<FakeState> state;

    QString configPath() const { return dir.filePath(QStringLiteral("settings.conf")); }

    std::unique_ptr<AppSettings> make(bool trayAvailable = true)
    {
        return std::make_unique<AppSettings>(Settings::open(configPath()),
                                             std::make_unique<FakeAutostart>(state), trayAvailable);
    }

    // Reads the file directly, not through the object under test.
    static QString missing() { return QStringLiteral("<missing>"); }

    QString stored(const char *key) const
    {
        const QSettings config(configPath(), QSettings::IniFormat);
        return config.value(QLatin1String(key), missing()).toString();
    }

private Q_SLOTS:
    void initTestCase()
    {
    }

    void init()
    {
        QFile::remove(configPath());
        state = std::make_shared<FakeState>();
    }

    void defaultsKeepTheProgramInTheTray()
    {
        const auto settings = make();
        QVERIFY(settings->showTrayIcon());
        QVERIFY(settings->closeToTray());
        QVERIFY(settings->startMinimized());
        QVERIFY(!settings->startAtLogin());
        QVERIFY(settings->keepRunningInTray());
        QVERIFY(settings->trayAvailable());
        QVERIFY(settings->autostartSupported());
        QCOMPARE(settings->trayBadgeStyle(), 0);
        QVERIFY(settings->showDockBadge());
        QVERIFY(!settings->hideDockIcon());
        QCOMPARE(settings->soundTheme(), QStringLiteral("alarm-clock-elapsed"));
        QVERIFY(settings->protectWeekendStreak());
        QCOMPARE(settings->soundVolume(), 80);
        QVERIFY(settings->preAlarmEnabled());
        QCOMPARE(settings->preAlarmSound(), QStringLiteral("dialog-information"));
        QCOMPARE(settings->focusSoundMode(), 0);
        QCOMPARE(settings->focusSoundVolume(), 30);
        QVERIFY(settings->keepScreenAwake());
    }

    void changesAreWrittenToTheConfigFile()
    {
        {
            const auto settings = make();
            settings->setShowTrayIcon(false);
            settings->setCloseToTray(false);
            settings->setStartMinimized(false);
            settings->setTrayBadgeStyle(1);
            settings->setShowDockBadge(false);
            settings->setHideDockIcon(true);
            settings->setSoundTheme(QStringLiteral("complete"));
            settings->setSoundVolume(12);
            settings->setProtectWeekendStreak(false);
        }
        QCOMPARE(stored("ShowTrayIcon"), QStringLiteral("false"));
        QCOMPARE(stored("CloseToTray"), QStringLiteral("false"));
        QCOMPARE(stored("StartMinimized"), QStringLiteral("false"));
        QCOMPARE(stored("TrayBadgeStyle"), QStringLiteral("1"));
        QCOMPARE(stored("ShowDockBadge"), QStringLiteral("false"));
        QCOMPARE(stored("HideDockIcon"), QStringLiteral("true"));
        QCOMPARE(stored("SoundTheme"), QStringLiteral("complete"));
        QCOMPARE(stored("SoundVolume"), QStringLiteral("12"));
        QCOMPARE(stored("ProtectWeekendStreak"), QStringLiteral("false"));

        const auto reloaded = make();
        QVERIFY(!reloaded->showTrayIcon());
        QVERIFY(!reloaded->closeToTray());
        QVERIFY(!reloaded->startMinimized());
        QCOMPARE(reloaded->trayBadgeStyle(), 1);
        QVERIFY(!reloaded->showDockBadge());
        QVERIFY(reloaded->hideDockIcon());
        QCOMPARE(reloaded->soundTheme(), QStringLiteral("complete"));
        QCOMPARE(reloaded->soundVolume(), 12);
        QVERIFY(!reloaded->protectWeekendStreak());
    }

    void installingFocusShortcutsWithoutAnInstallerFails()
    {
        auto settings = make();
        QVERIFY(!settings->focusShortcutsInstallable());
        QSignalSpy changed(settings.get(), &AppSettings::focusShortcutsInstallChanged);
        settings->installFocusShortcuts();
        QCOMPARE(settings->focusShortcutsInstallStatus(), QStringLiteral("failed"));
        QVERIFY(!settings->focusShortcutsInstallDetail().isEmpty());
        QCOMPARE(changed.count(), 1);
    }

    void installingFocusShortcutsReportsAndUsesTheShippedNames()
    {
        FakeShortcutTools tools;
        QVERIFY(tools.isValid());
        FakeShortcuts shortcuts;
        QVERIFY(shortcuts.isValid());
        ShortcutsFocusInstaller installer(tools.programs(), tools.directory());
        installer.setTimingForTesting(20, 2000, 2000);
        ShortcutsFocusInhibitor inhibitor(shortcuts.program());
        auto settings = make();
        settings->attachFocusShortcuts(&inhibitor);
        settings->attachFocusShortcutsInstaller(&installer);
        QVERIFY(settings->focusShortcutsInstallable());
        QTRY_VERIFY(!shortcuts.calls().isEmpty()); // the first list
        // Other names give way to those of the shipped shortcuts.
        settings->setFocusOnShortcut(QStringLiteral("My Focus"));
        QTRY_VERIFY(shortcuts.calls().size() >= 2);
        shortcuts.setList({QStringLiteral("Sugo Focus On"), QStringLiteral("Sugo Focus Off")});

        QStringList statuses;
        connect(settings.get(), &AppSettings::focusShortcutsInstallChanged, this, [&]() { statuses.append(settings->focusShortcutsInstallStatus()); });
        settings->installFocusShortcuts();
        QCOMPARE(settings->focusOnShortcut(), QStringLiteral("Sugo Focus On"));
        QCOMPARE(settings->focusOffShortcut(), QStringLiteral("Sugo Focus Off"));
        QTRY_COMPARE(settings->focusShortcutsInstallStatus(), QStringLiteral("done"));
        QCOMPARE(statuses, (QStringList{QStringLiteral("installing"), QStringLiteral("waitingOn"), QStringLiteral("waitingOff"), QStringLiteral("done")}));
        QVERIFY(settings->focusShortcutsInstallDetail().isEmpty());
        // Done: the inhibitor reads its list again and finds both.
        QTRY_VERIFY(inhibitor.isAvailable());

        // A failure carries its detail.
        tools.setFails(QStringLiteral("shortcuts"), "broken");
        settings->installFocusShortcuts();
        QTRY_COMPARE(settings->focusShortcutsInstallStatus(), QStringLiteral("failed"));
        QVERIFY(settings->focusShortcutsInstallDetail().contains(QStringLiteral("broken")));
    }

    void focusAudioAndPowerSettingsPersistAndClamp()
    {
        {
            const auto settings = make();
            settings->setSoundVolume(130);
            settings->setPreAlarmEnabled(false);
            settings->setPreAlarmSound(QStringLiteral("bell"));
            settings->setFocusSoundMode(2);
            settings->setFocusSoundVolume(-10);
            settings->setKeepScreenAwake(false);
        }
        QCOMPARE(stored("SoundVolume"), QStringLiteral("100"));
        QCOMPARE(stored("PreAlarmEnabled"), QStringLiteral("false"));
        QCOMPARE(stored("PreAlarmSound"), QStringLiteral("bell"));
        QCOMPARE(stored("FocusSoundMode"), QStringLiteral("2"));
        QCOMPARE(stored("FocusSoundVolume"), QStringLiteral("0"));
        QCOMPARE(stored("KeepScreenAwake"), QStringLiteral("false"));

        const auto reloaded = make();
        QCOMPARE(reloaded->soundVolume(), 100);
        QVERIFY(!reloaded->preAlarmEnabled());
        QCOMPARE(reloaded->preAlarmSound(), QStringLiteral("bell"));
        QCOMPARE(reloaded->focusSoundMode(), 2);
        QCOMPARE(reloaded->focusSoundVolume(), 0);
        QVERIFY(!reloaded->keepScreenAwake());
    }

    void startAtLoginIsNotStoredInTheConfigFile()
    {
        const auto settings = make();
        settings->setStartAtLogin(true);
        settings->setShowTrayIcon(false); // forces a write
        QCOMPARE(stored("StartAtLogin"), missing());
        QCOMPARE(stored("startAtLogin"), missing());
    }

    void signalsFireOnlyOnRealChanges()
    {
        const auto settings = make();
        QSignalSpy tray(settings.get(), &AppSettings::showTrayIconChanged);
        QSignalSpy keep(settings.get(), &AppSettings::keepRunningInTrayChanged);
        QSignalSpy close(settings.get(), &AppSettings::closeToTrayChanged);
        QSignalSpy minimized(settings.get(), &AppSettings::startMinimizedChanged);
        QSignalSpy badge(settings.get(), &AppSettings::trayBadgeStyleChanged);
        QSignalSpy sound(settings.get(), &AppSettings::soundThemeChanged);
        QSignalSpy streak(settings.get(), &AppSettings::protectWeekendStreakChanged);

        settings->setShowTrayIcon(true); // unchanged
        settings->setCloseToTray(true);
        settings->setStartMinimized(true);
        settings->setTrayBadgeStyle(0);
        settings->setSoundTheme(QStringLiteral("alarm-clock-elapsed"));
        settings->setProtectWeekendStreak(true);
        QCOMPARE(tray.count(), 0);
        QCOMPARE(close.count(), 0);
        QCOMPARE(minimized.count(), 0);
        QCOMPARE(badge.count(), 0);
        QCOMPARE(sound.count(), 0);
        QCOMPARE(streak.count(), 0);

        settings->setShowTrayIcon(false);
        QCOMPARE(tray.count(), 1);
        QCOMPARE(keep.count(), 1);
        settings->setCloseToTray(false);
        QCOMPARE(close.count(), 1);
        QCOMPARE(keep.count(), 2);
        settings->setStartMinimized(false);
        QCOMPARE(minimized.count(), 1);
        settings->setTrayBadgeStyle(2);
        QCOMPARE(badge.count(), 1);
        settings->setSoundTheme(QStringLiteral("bell"));
        QCOMPARE(sound.count(), 1);
        settings->setProtectWeekendStreak(false);
        QCOMPARE(streak.count(), 1);
    }

    void keepRunningInTrayNeedsTheIconTheOptionAndATray()
    {
        {
            const auto settings = make(true);
            QVERIFY(settings->keepRunningInTray());
            settings->setCloseToTray(false);
            QVERIFY(!settings->keepRunningInTray());
            settings->setCloseToTray(true);
            settings->setShowTrayIcon(false);
            QVERIFY(!settings->keepRunningInTray());
        }
        QFile::remove(configPath());
        const auto noTray = make(false); // nothing to hide into: closing must quit
        QVERIFY(!noTray->trayAvailable());
        QVERIFY(!noTray->keepRunningInTray());
    }

    void startHiddenNeedsEveryConditionAtOnce()
    {
        const auto settings = make(true);
        QVERIFY(!settings->startHidden()); // not launched with --background

        QSignalSpy spy(settings.get(), &AppSettings::startHiddenChanged);
        settings->setLaunchedInBackground(true);
        QVERIFY(settings->startHidden());
        QCOMPARE(spy.count(), 1);

        settings->setStartMinimized(false);
        QVERIFY(!settings->startHidden());
        settings->setStartMinimized(true);
        QVERIFY(settings->startHidden());
        settings->setShowTrayIcon(false);
        QVERIFY(!settings->startHidden()); // hidden with no icon would leave the program unreachable
    }

    void startHiddenIsOffWithoutATray()
    {
        const auto settings = make(false);
        settings->setLaunchedInBackground(true);
        QVERIFY(!settings->startHidden()); // never start invisible when there is no tray to find it in
    }

    void startAtLoginFollowsTheEntryOnDisk()
    {
        const auto settings = make();
        QVERIFY(!settings->startAtLogin());
        state->enabled = true; // another tool (System Settings) enabled it
        QVERIFY(settings->startAtLogin());
        state->enabled = false;
        QVERIFY(!settings->startAtLogin());
    }

    void settingStartAtLoginCreatesAndRemovesTheEntry()
    {
        const auto settings = make();
        settings->setStartAtLogin(true);
        QVERIFY(state->enabled);
        QVERIFY(settings->startAtLogin());
        settings->setStartAtLogin(false);
        QVERIFY(!state->enabled);
    }

    void turningStartAtLoginOffAlsoTurnsStartMinimizedOff()
    {
        const auto settings = make();
        settings->setStartAtLogin(true);
        settings->setStartMinimized(true);
        settings->setStartAtLogin(false);
        QVERIFY(!settings->startMinimized());
        QCOMPARE(stored("StartMinimized"), QStringLiteral("false"));

        // It stays as chosen while the start at login stays on, and when removing the entry fails.
        settings->setStartAtLogin(true);
        settings->setStartMinimized(true);
        state->failNext = true;
        settings->setStartAtLogin(false);
        QVERIFY(settings->startAtLogin());
        QVERIFY(settings->startMinimized());
    }

    void settingTheCurrentValueDoesNotTouchTheEntry()
    {
        const auto settings = make();
        settings->setStartAtLogin(false);
        QCOMPARE(state->setCalls, 0);
    }

    void aRefusedChangeIsReportedAndTheStateStaysTrue()
    {
        const auto settings = make();
        QSignalSpy failed(settings.get(), &AppSettings::autostartFailed);
        QSignalSpy changed(settings.get(), &AppSettings::startAtLoginChanged);

        state->failNext = true;
        settings->setStartAtLogin(true);

        QCOMPARE(failed.count(), 1);
        QVERIFY(!failed.first().first().toString().isEmpty());
        QVERIFY(!settings->startAtLogin());  // the real state, not what was asked for
        QCOMPARE(changed.count(), 1);        // announced, so a checkbox shows the real state again
    }

    void aStaleEntryIsRepairedAtStartup()
    {
        make();
        QCOMPARE(state->repairCalls, 1);
    }

    // A test run or a build being tried (SUGO_DATA_DIR) must not point the user's login entry at itself.
    void anIsolatedRunLeavesTheLoginEntryAlone()
    {
        const QByteArray saved = qgetenv("SUGO_DATA_DIR");
        const bool wasSet = qEnvironmentVariableIsSet("SUGO_DATA_DIR");
        QTemporaryDir dir;
        qputenv("SUGO_DATA_DIR", QFile::encodeName(dir.path()));
        make();
        if (wasSet) {
            qputenv("SUGO_DATA_DIR", saved);
        } else {
            qunsetenv("SUGO_DATA_DIR");
        }
        QCOMPARE(state->repairCalls, 0);
    }

    void unsupportedPlatformsAreReported()
    {
        state->supported = false;
        const auto settings = make();
        QVERIFY(!settings->autostartSupported());
    }

    void firstRunCompletedDefaultsToFalseAndPersists()
    {
        {
            const auto settings = make();
            QVERIFY(!settings->firstRunCompleted());
            QSignalSpy spy(settings.get(), &AppSettings::firstRunCompletedChanged);
            settings->setFirstRunCompleted(true);
            QVERIFY(settings->firstRunCompleted());
            QCOMPARE(spy.count(), 1);
        }
        // Re-read from disk
        {
            const auto settings = make();
            QVERIFY(settings->firstRunCompleted());
        }
    }

    void autoPauseOnScreenLockDefaultsToTrueAndPersists()
    {
        {
            const auto settings = make();
            QVERIFY(settings->autoPauseOnScreenLock());
            QSignalSpy spy(settings.get(), &AppSettings::autoPauseOnScreenLockChanged);
            settings->setAutoPauseOnScreenLock(false);
            QVERIFY(!settings->autoPauseOnScreenLock());
            QCOMPARE(spy.count(), 1);
        }
        // Re-read from disk
        {
            const auto settings = make();
            QVERIFY(!settings->autoPauseOnScreenLock());
        }
    }

    void promptTaskNoteDefaultsToTrueAndPersists()
    {
        {
            const auto settings = make();
            QVERIFY(settings->promptTaskNote());
            QSignalSpy spy(settings.get(), &AppSettings::promptTaskNoteChanged);
            settings->setPromptTaskNote(false);
            QVERIFY(!settings->promptTaskNote());
            QCOMPARE(spy.count(), 1);
        }
        // Re-read from disk
        {
            const auto settings = make();
            QVERIFY(!settings->promptTaskNote());
        }
    }

    void invalidStoredValuesAreClamped()
    {
        {
            QSettings config(configPath(), QSettings::IniFormat);
            config.setValue(QStringLiteral("TrayBadgeStyle"), 9);
            config.setValue(QStringLiteral("FocusSoundMode"), 99);
            config.setValue(QStringLiteral("SoundVolume"), 500);
            config.setValue(QStringLiteral("ColorScheme"), QStringLiteral("purple"));
            config.sync();
        }
        {
            const auto settings = make();
            QCOMPARE(settings->trayBadgeStyle(), 2);
            QCOMPARE(settings->focusSoundMode(), 6);
            QCOMPARE(settings->soundVolume(), 100);
            QCOMPARE(settings->colorScheme(), QStringLiteral("system"));
        } // releases the shared config, so the next one rereads the file

        {
            QSettings config(configPath(), QSettings::IniFormat);
            config.setValue(QStringLiteral("TrayBadgeStyle"), -1);
            config.setValue(QStringLiteral("FocusSoundMode"), -4);
            config.setValue(QStringLiteral("SoundVolume"), -3);
            config.sync();
        }
        const auto reloaded = make();
        QCOMPARE(reloaded->trayBadgeStyle(), 0);
        QCOMPARE(reloaded->focusSoundMode(), 0);
        QCOMPARE(reloaded->soundVolume(), 0);
    }

    void colorSchemeDefaultsToSystemAndPersists()
    {
        {
            const auto settings = make();
            QCOMPARE(settings->colorScheme(), QStringLiteral("system"));
            QSignalSpy spy(settings.get(), &AppSettings::colorSchemeChanged);
            settings->setColorScheme(QStringLiteral("light"));
            QCOMPARE(settings->colorScheme(), QStringLiteral("light"));
            QCOMPARE(spy.count(), 1);
            settings->setColorScheme(QStringLiteral("bogus")); // falls back to the system scheme
            QCOMPARE(settings->colorScheme(), QStringLiteral("system"));
            settings->setColorScheme(QStringLiteral("dark"));
            QCOMPARE(spy.count(), 3);
        }
        QCOMPARE(stored("ColorScheme"), QStringLiteral("dark"));
        QCOMPARE(make()->colorScheme(), QStringLiteral("dark"));
    }

    void updatesDefaultToStableAndAutomatic()
    {
        {
            const auto settings = make();
            QCOMPARE(settings->updateChannel(), QStringLiteral("stable"));
            QVERIFY(settings->autoCheckUpdates());
            QSignalSpy channelSpy(settings.get(), &AppSettings::updateChannelChanged);
            QSignalSpy autoSpy(settings.get(), &AppSettings::autoCheckUpdatesChanged);
            settings->setUpdateChannel(QStringLiteral("nightly"));
            settings->setUpdateChannel(QStringLiteral("nightly"));
            QCOMPARE(channelSpy.count(), 1);
            settings->setUpdateChannel(QStringLiteral("beta")); // unknown: stable
            QCOMPARE(settings->updateChannel(), QStringLiteral("stable"));
            settings->setUpdateChannel(QStringLiteral("nightly"));
            settings->setAutoCheckUpdates(false);
            QCOMPARE(autoSpy.count(), 1);
        }
        QCOMPARE(stored("UpdateChannel"), QStringLiteral("nightly"));
        const auto reloaded = make();
        QCOMPARE(reloaded->updateChannel(), QStringLiteral("nightly"));
        QVERIFY(!reloaded->autoCheckUpdates());
    }

    void verboseLoggingIsOffByDefaultAndPersists()
    {
        {
            const auto settings = make();
            QVERIFY(!settings->verboseLogging());
            QSignalSpy spy(settings.get(), &AppSettings::verboseLoggingChanged);
            settings->setVerboseLogging(false);
            QCOMPARE(spy.count(), 0);
            settings->setVerboseLogging(true);
            settings->setVerboseLogging(true);
            QCOMPARE(spy.count(), 1);
            QVERIFY(settings->verboseLogging());
        }
        QCOMPARE(stored("VerboseLogging"), QStringLiteral("true"));
        QVERIFY(make()->verboseLogging());
    }

    void resetToDefaultsRestoresEveryPreferenceThroughTheSetters()
    {
        {
            // Not preferences of this class: they must survive a reset.
            QSettings other(configPath(), QSettings::IniFormat);
            other.setValue(QStringLiteral("CalendarSync/Username"), QStringLiteral("anna"));
            other.sync();
        }
        const auto settings = make();
        settings->setShowTrayIcon(false);
        settings->setCloseToTray(false);
        settings->setAllowExternalControl(false);
        settings->setStartAtLogin(true);
        settings->setStartMinimized(false);
        settings->setTrayBadgeStyle(2);
        settings->setShowDockBadge(false);
        settings->setHideDockIcon(true);
        settings->setSoundTheme(QStringLiteral("bell"));
        settings->setSoundVolume(10);
        settings->setPreAlarmEnabled(false);
        settings->setPreAlarmSound(QStringLiteral("bell"));
        settings->setFocusSoundMode(2);
        settings->setFocusSoundVolume(90);
        settings->setKeepScreenAwake(false);
        settings->setAutoPauseOnScreenLock(false);
        settings->setProtectWeekendStreak(false);
        settings->setFirstRunCompleted(true);
        settings->setPromptTaskNote(false);
        settings->setColorScheme(QStringLiteral("light"));
        settings->setUpdateChannel(QStringLiteral("nightly"));
        settings->setAutoCheckUpdates(false);
        settings->setVerboseLogging(true);
        settings->setFocusOnShortcut(QStringLiteral("Mine On"));
        settings->setFocusOffShortcut(QStringLiteral("Mine Off"));

        QSignalSpy trayIcon(settings.get(), &AppSettings::showTrayIconChanged);
        QSignalSpy volume(settings.get(), &AppSettings::soundVolumeChanged);
        QSignalSpy scheme(settings.get(), &AppSettings::colorSchemeChanged);
        QSignalSpy verbose(settings.get(), &AppSettings::verboseLoggingChanged);
        QSignalSpy channel(settings.get(), &AppSettings::updateChannelChanged);
        QSignalSpy firstRun(settings.get(), &AppSettings::firstRunCompletedChanged);
        QSignalSpy login(settings.get(), &AppSettings::startAtLoginChanged);

        settings->resetToDefaults();

        QVERIFY(settings->showTrayIcon());
        QVERIFY(settings->closeToTray());
        QVERIFY(settings->allowExternalControl());
        QVERIFY(settings->startMinimized());
        QCOMPARE(settings->trayBadgeStyle(), 0);
        QVERIFY(settings->showDockBadge());
        QVERIFY(!settings->hideDockIcon());
        QCOMPARE(settings->soundTheme(), QStringLiteral("alarm-clock-elapsed"));
        QCOMPARE(settings->soundVolume(), 80);
        QVERIFY(settings->preAlarmEnabled());
        QCOMPARE(settings->preAlarmSound(), QStringLiteral("dialog-information"));
        QCOMPARE(settings->focusSoundMode(), 0);
        QCOMPARE(settings->focusSoundVolume(), 30);
        QVERIFY(settings->keepScreenAwake());
        QVERIFY(settings->autoPauseOnScreenLock());
        QVERIFY(settings->protectWeekendStreak());
        QVERIFY(settings->promptTaskNote());
        QCOMPARE(settings->colorScheme(), QStringLiteral("system"));
        QCOMPARE(settings->updateChannel(), QStringLiteral("stable"));
        QVERIFY(settings->autoCheckUpdates());
        QVERIFY(!settings->verboseLogging());

        // Views are told ...
        QCOMPARE(trayIcon.count(), 1);
        QCOMPARE(volume.count(), 1);
        QCOMPARE(scheme.count(), 1);
        QCOMPARE(verbose.count(), 1);
        QCOMPARE(channel.count(), 1);
        // ... and what is not a preference of this class stays.
        QVERIFY(settings->firstRunCompleted());
        QCOMPARE(firstRun.count(), 0);
        QVERIFY(settings->startAtLogin());
        QVERIFY(state->enabled);
        QCOMPARE(login.count(), 0);

        // The file has the defaults, not the old values.
        QCOMPARE(stored("ShowTrayIcon"), QStringLiteral("true"));
        QCOMPARE(stored("SoundVolume"), QStringLiteral("80"));
        QCOMPARE(stored("SoundTheme"), QStringLiteral("alarm-clock-elapsed"));
        QCOMPARE(stored("ColorScheme"), QStringLiteral("system"));
        QCOMPARE(stored("UpdateChannel"), QStringLiteral("stable"));
        QCOMPARE(stored("VerboseLogging"), QStringLiteral("false"));
        QCOMPARE(stored("FirstRunCompleted"), QStringLiteral("true"));
        QCOMPARE(stored("CalendarSync/Username"), QStringLiteral("anna"));
        const auto reloaded = make();
        QCOMPARE(reloaded->soundVolume(), 80);
        QCOMPARE(reloaded->colorScheme(), QStringLiteral("system"));
        QVERIFY(reloaded->showTrayIcon());
        QVERIFY(!reloaded->verboseLogging());

        // Nothing left to reset: no change is announced.
        settings->resetToDefaults();
        QCOMPARE(trayIcon.count(), 1);
        QCOMPARE(volume.count(), 1);
        QCOMPARE(scheme.count(), 1);
    }

    void trayAvailabilityChangesUpdateDependentProperties()
    {
        const auto settings = make(false);
        QVERIFY(!settings->keepRunningInTray());
        QSignalSpy tray(settings.get(), &AppSettings::trayAvailableChanged);
        QSignalSpy keep(settings.get(), &AppSettings::keepRunningInTrayChanged);
        QSignalSpy hidden(settings.get(), &AppSettings::startHiddenChanged);

        settings->setTrayAvailable(true);
        QVERIFY(settings->trayAvailable());
        QVERIFY(settings->keepRunningInTray());
        QCOMPARE(tray.count(), 1);
        QCOMPARE(keep.count(), 1);
        QCOMPARE(hidden.count(), 1);

        settings->setTrayAvailable(true); // no change, no signal
        QCOMPARE(tray.count(), 1);

        settings->setTrayAvailable(false);
        QVERIFY(!settings->keepRunningInTray());
        QCOMPARE(tray.count(), 2);
    }

    void defaultSettingsAreSharedAndPrivate()
    {
        QStandardPaths::setTestModeEnabled(true);
        QFile::remove(Settings::defaultPath());
        QVERIFY(QDir().mkpath(QFileInfo(Settings::defaultPath()).absolutePath()));
        {
            const Settings::Ptr settings = Settings::openDefault();
            QCOMPARE(Settings::openDefault(), settings); // one object for everyone
            settings->setValue(QStringLiteral("SoundVolume"), 7);
            Settings::sync(*settings);
        }
        QCOMPARE(QFile::permissions(Settings::defaultPath()) & (QFile::ReadGroup | QFile::ReadOther), QFile::Permissions());
        QCOMPARE(Settings::openDefault()->value(QStringLiteral("SoundVolume")).toInt(), 7);

        QFile::remove(Settings::defaultPath());
        QStandardPaths::setTestModeEnabled(false);
    }

    void dataDirVariableReplacesDefaultLocation()
    {
        const QByteArray saved = qgetenv("SUGO_DATA_DIR");
        const bool wasSet = qEnvironmentVariableIsSet("SUGO_DATA_DIR");
        QTemporaryDir dir;
        qputenv("SUGO_DATA_DIR", QFile::encodeName(dir.path()));
        QCOMPARE(Settings::defaultPath(), dir.filePath(QStringLiteral("sugo.conf")));
        qunsetenv("SUGO_DATA_DIR");
        QVERIFY(Settings::defaultPath().endsWith(QLatin1String("/sugo/sugo.conf")));
        if (wasSet) {
            qputenv("SUGO_DATA_DIR", saved);
        }
    }

    void onlySoundNamesAndAbsolutePathsArePlayed()
    {
        for (const QString &good : {QStringLiteral("complete"), QStringLiteral("alarm-clock-elapsed"), QStringLiteral("bell.window"),
                                    QStringLiteral("/usr/share/sounds/x.oga")}) {
            QVERIFY2(AppSettings::isUsableSoundName(good), qPrintable(good));
        }
        for (const QString &bad : {QStringLiteral("../../../etc/passwd"), QStringLiteral("a/b"), QStringLiteral("-x"),
                                   QStringLiteral("--file=/etc/passwd"), QStringLiteral(""), QStringLiteral(".hidden"),
                                   QStringLiteral("/usr/share/../../etc/shadow")}) {
            QVERIFY2(!AppSettings::isUsableSoundName(bad), qPrintable(bad));
        }
    }

    void externalControlIsAllowedByDefaultAndRemembered()
    {
        {
            const auto settings = make();
            QVERIFY(settings->allowExternalControl());
            QSignalSpy changed(settings.get(), &AppSettings::allowExternalControlChanged);
            settings->setAllowExternalControl(false);
            QCOMPARE(changed.count(), 1);
        }
        QCOMPARE(stored("AllowExternalControl"), QStringLiteral("false"));
        QVERIFY(!make()->allowExternalControl());
    }

    void canberraGetsTheSoundNameAsOneOptionValue()
    {
        QCOMPARE(AppSettings::canberraArguments(QStringLiteral("complete")), QStringList{QStringLiteral("--id=complete")});
        // A name read from the configuration that looks like an option stays a value.
        QCOMPARE(AppSettings::canberraArguments(QStringLiteral("--file=/etc/passwd")),
                 QStringList{QStringLiteral("--id=--file=/etc/passwd")});
    }

    void alertSoundsFadeInAndOut()
    {
        // A 3 s sound: 300 ms in, 600 ms out.
        QCOMPARE(AppSettings::alertFadeGain(0, 3000), 0.0);
        QCOMPARE(AppSettings::alertFadeGain(150, 3000), 0.5);
        QCOMPARE(AppSettings::alertFadeGain(1500, 3000), 1.0);
        QCOMPARE(AppSettings::alertFadeGain(2700, 3000), 0.5);
        QCOMPARE(AppSettings::alertFadeGain(3000, 3000), 0.0);
        // A 600 ms sound: both fades shrink to 200 ms, so it still reaches full volume.
        QCOMPARE(AppSettings::alertFadeGain(300, 600), 1.0);
        QCOMPARE(AppSettings::alertFadeGain(500, 600), 0.5);
        // The duration is not known yet: only the fade-in applies.
        QCOMPARE(AppSettings::alertFadeGain(150, 0), 0.5);
        QCOMPARE(AppSettings::alertFadeGain(5000, 0), 1.0);
    }
    void soundsAreFoundUnderTheGivenRootsInThemeOrder()
    {
        QTemporaryDir first;
        QTemporaryDir second;
        QVERIFY(first.isValid() && second.isValid());
        const auto touch = [](const QString &file) {
            QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
        };
        touch(first.filePath(QStringLiteral("sounds/freedesktop/stereo/bell.oga")));
        touch(second.filePath(QStringLiteral("sounds/ocean/stereo/bell.wav")));
        touch(second.filePath(QStringLiteral("sounds/freedesktop/stereo/complete.oga")));
        const QStringList roots = {first.path(), second.path()};

        // Any theme's file in Ocean comes before freedesktop's, whatever root holds it.
        QCOMPARE(AppSettings::findSoundFile(QStringLiteral("bell"), roots), second.filePath(QStringLiteral("sounds/ocean/stereo/bell.wav")));
        QCOMPARE(AppSettings::findSoundFile(QStringLiteral("complete"), roots),
                 second.filePath(QStringLiteral("sounds/freedesktop/stereo/complete.oga")));
        QVERIFY(AppSettings::findSoundFile(QStringLiteral("missing"), roots).isEmpty());
        QVERIFY(AppSettings::findSoundFile(QStringLiteral("bell"), {}).isEmpty());
        // An absolute path is used as is, if it is a file.
        const QString absolute = first.filePath(QStringLiteral("sounds/freedesktop/stereo/bell.oga"));
        QCOMPARE(AppSettings::findSoundFile(absolute, {}), absolute);
        QVERIFY(AppSettings::findSoundFile(first.filePath(QStringLiteral("nothing.oga")), {}).isEmpty());
    }

    void theSearchRootsIncludeTheSystemSounds()
    {
        const QStringList roots = AppSettings::soundSearchRoots();
        QVERIFY(roots.contains(QStringLiteral("/usr/share")));
        QVERIFY(roots.contains(QStringLiteral("/run/host/usr/share")));
    }

    void everyOfferedSoundIsBundled()
    {
        // The names in the two sound lists of AppSettingsPage.qml; macOS has only these files.
        for (const char *name : {"alarm-clock-elapsed", "complete", "message-new-instant", "dialog-information", "bell",
                                 "completion-success", "service-login", "window-attention", "phone-incoming-call", "message"}) {
            const QString file = AppSettings::findSoundFile(QLatin1String(name), {QStringLiteral(SUGO_BUNDLED_SOUNDS_ROOT)});
            QVERIFY2(!file.isEmpty(), name);
            QVERIFY2(QFileInfo::exists(file + QStringLiteral(".license")), name);
        }
    }

    void afplayGetsOnlyFormatsItReads()
    {
        QVERIFY(AppSettings::afplayCanPlay(QStringLiteral("/x/bell.wav")));
        QVERIFY(AppSettings::afplayCanPlay(QStringLiteral("/x/bell.AIFF")));
        QVERIFY(AppSettings::afplayCanPlay(QStringLiteral("/x/bell.caf")));
        QVERIFY(!AppSettings::afplayCanPlay(QStringLiteral("/x/bell.oga")));
        QVERIFY(!AppSettings::afplayCanPlay(QStringLiteral("/x/bell.ogg")));
        QVERIFY(!AppSettings::afplayCanPlay(QStringLiteral("/x/bell")));
        QCOMPARE(AppSettings::afplayArguments(QStringLiteral("/x/bell.wav"), 80),
                 (QStringList{QStringLiteral("-v"), QStringLiteral("0.80"), QStringLiteral("/x/bell.wav")}));
        QCOMPARE(AppSettings::afplayArguments(QStringLiteral("/x/bell.wav"), 150).at(1), QStringLiteral("1.00"));
        QCOMPARE(AppSettings::afplayArguments(QStringLiteral("/x/bell.wav"), -5).at(1), QStringLiteral("0.00"));
    }
};

QTEST_GUILESS_MAIN(AppSettingsTest)
#include "tst_appsettings.moc"
