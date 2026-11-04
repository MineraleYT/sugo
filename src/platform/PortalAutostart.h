// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Linux only (QtDBus): see src/CMakeLists.txt.

#include "Autostart.h"

#include <QDBusConnection>
#include <QString>
#include <QStringList>

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
