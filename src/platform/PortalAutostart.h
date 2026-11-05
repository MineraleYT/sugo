// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Linux only (QtDBus): see src/CMakeLists.txt.

#include "Autostart.h"

#include <QDBusConnection>
#include <QString>
#include <QStringList>

#include <memory>

class PortalResponse;

/**
 * Inside a Flatpak sandbox: the XDG Background portal (org.freedesktop.portal.Background),
 * which writes the login entry on the host side.
 *
 * The portal cannot be asked whether the entry exists, so the last confirmed choice is
 * kept in `stateFile`. setEnabled() sends RequestBackground and waits (without blocking the
 * event loop, but up to `responseTimeoutMs`) for the portal's Response; a refusal, an
 * error or no answer in time returns false, so the caller's existing failure path reports it.
 *
 * The portal's dialog may still be open when setEnabled() stops waiting: the request is
 * still listened to, and if the user answers yes after all (the portal then changes the
 * login entry), the choice is stored and the changed callback called. A newer request
 * replaces it, and so does destroying this object (quitting): the old request is then
 * closed (Request.Close), so its dialog cannot change the entry unseen.
 */
class PortalAutostart final : public Autostart
{
public:
    /// `command` is the program inside the sandbox plus arguments, e.g. {"sugo", "--background"}.
    PortalAutostart(const QDBusConnection &bus, const QString &stateFile, const QString &reason, const QStringList &command,
                    int responseTimeoutMs = 60000);
    ~PortalAutostart() override;

    bool isSupported() const override { return m_bus.isConnected(); }
    bool isEnabled() const override;
    bool setEnabled(bool enabled) override;

private:
    bool storeState(bool enabled) const;
    /// Whether the portal's answer to a request for `enabled` confirms the change (logged if not).
    static bool confirms(const PortalResponse &response, bool enabled);
    void listen(const QString &handle, PortalResponse *response);
    void stopListening(const QString &handle, PortalResponse *response);
    /// The answer to the request setEnabled() gave up waiting for.
    void onLateResponse();
    void dropPendingRequest();

    QDBusConnection m_bus;
    QString m_stateFile;
    QString m_reason;
    QStringList m_command;
    int m_responseTimeoutMs;
    /// The request still waiting for an answer after setEnabled() returned, if any.
    std::unique_ptr<PortalResponse> m_pending;
};
