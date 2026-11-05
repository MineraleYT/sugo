// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

/// Starting the application automatically when the user logs in.
class Autostart
{
public:
    virtual ~Autostart() = default;

    /// False where the platform offers no way to do it (the UI then explains why).
    virtual bool isSupported() const = 0;
    virtual bool isEnabled() const = 0;
    /// Returns false if the change could not be made.
    virtual bool setEnabled(bool enabled) = 0;
    /// Brings an existing entry up to date, e.g. after the program moved. No-op if disabled.
    virtual void repair() {}
    /// This copy of the program will be gone by the next login (macOS: run from the disk image
    /// or a translocated copy), so an entry starting it cannot be set up: setEnabled(true) fails.
    virtual bool runsFromTemporaryLocation() const { return false; }

    /// Called when isEnabled() changes outside setEnabled(), e.g. when the answer to a request
    /// that setEnabled() gave up waiting for comes after all.
    void setChangedCallback(std::function<void()> callback) { m_changed = std::move(callback); }

protected:
    void notifyChanged() const
    {
        if (m_changed) {
            m_changed();
        }
    }

private:
    std::function<void()> m_changed;
};

/**
 * The freedesktop Autostart specification: a .desktop file in ~/.config/autostart.
 *
 * "Enabled" follows what other tools do to that file: KDE System Settings disables an
 * entry by writing `Hidden=true`, GNOME by `X-GNOME-Autostart-enabled=false`. Both are
 * honoured, and enabling again from here clears them.
 */
class XdgAutostart final : public Autostart
{
public:
    /// `command` is the program followed by its arguments, e.g. {"/usr/bin/sugo", "--background"}.
    XdgAutostart(const QString &directory, const QString &appId, const QString &displayName, const QString &comment, const QStringList &command);

    bool isSupported() const override { return true; }
    bool isEnabled() const override;
    bool setEnabled(bool enabled) override;
    void repair() override;

    QString filePath() const;
    /// The Exec= value for `command`, quoted and escaped as the Desktop Entry spec requires.
    static QString execLine(const QStringList &command);

private:
    QMap<QString, QString> readEntries() const;
    QByteArray contents() const;
    bool write() const;

    QString m_directory;
    QString m_appId;
    QString m_displayName;
    QString m_comment;
    QStringList m_command;
};

/// For environments where the entry cannot be managed from inside the application.
class UnsupportedAutostart final : public Autostart
{
public:
    bool isSupported() const override { return false; }
    bool isEnabled() const override { return false; }
    bool setEnabled(bool) override { return false; }
};

/// XDG autostart on Linux, the Background portal inside a Flatpak sandbox, a launchd agent
/// (LaunchAgentAutostart) on macOS, unsupported on other platforms.
std::unique_ptr<Autostart> createAutostart();

/// The program a login entry starts: the AppImage file when running from one (AppImage::path()),
/// else this executable.
QString autostartProgram();
