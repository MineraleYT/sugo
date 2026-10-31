// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusConnection>
#include <QMap>
#include <QString>
#include <QStringList>

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

/**
 * Inside a Flatpak sandbox: the XDG Background portal (org.freedesktop.portal.Background),
 * which writes the login entry on the host side.
 *
 * The portal cannot be asked whether the entry exists, so the last confirmed choice is
 * kept in `stateFile`. setEnabled() sends RequestBackground and waits (without blocking the
 * event loop, but up to `responseTimeoutMs`) for the portal's Response; a refusal, an
 * error or no answer in time returns false, so the caller's existing failure path reports it.
 */
class PortalAutostart final : public Autostart
{
public:
    /// `command` is the program inside the sandbox plus arguments, e.g. {"sugo", "--background"}.
    PortalAutostart(const QDBusConnection &bus, const QString &stateFile, const QString &reason, const QStringList &command,
                    int responseTimeoutMs = 60000);

    bool isSupported() const override { return m_bus.isConnected(); }
    bool isEnabled() const override;
    bool setEnabled(bool enabled) override;

private:
    bool storeState(bool enabled) const;

    QDBusConnection m_bus;
    QString m_stateFile;
    QString m_reason;
    QStringList m_command;
    int m_responseTimeoutMs;
};

/// XDG autostart on Linux, the Background portal inside a Flatpak sandbox, unsupported on
/// other platforms.
std::unique_ptr<Autostart> createAutostart();
