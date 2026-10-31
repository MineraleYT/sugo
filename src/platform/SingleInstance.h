// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QVariantMap>

/**
 * Keeps one running instance. The first process owns a name on the session bus and
 * implements the standard org.freedesktop.Application interface; a later launch finds the
 * name taken, asks the first instance to show itself via Activate() and reports that it
 * should exit. This matters most for a tray application: login autostart and a menu
 * launch must not open two programs on the same database.
 */
class SingleInstance : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Application")

public:
    SingleInstance(const QDBusConnection &bus, const QString &serviceName, QObject *parent = nullptr);
    ~SingleInstance() override;

    /// True: this is the primary instance. False: another one runs and has been asked to
    /// come to the front (unless `activateExisting` is false, e.g. a `--background` login
    /// launch); the caller should exit. Without a session bus there is nothing to
    /// coordinate with and the result is true.
    bool acquire(bool activateExisting = true);

public Q_SLOTS:
    /// org.freedesktop.Application.Activate: another launch wants the window shown.
    void Activate(const QVariantMap &platformData);

Q_SIGNALS:
    /// `activationToken` may be empty; on Wayland it lets the window take focus.
    /// A request that arrives before anything is connected is kept and delivered on connect.
    void activateRequested(const QString &activationToken);

protected:
    void connectNotify(const QMetaMethod &signal) override;

private:
    QString objectPath() const;

    QDBusConnection m_bus;
    QString m_service;
    bool m_primary = false;
    bool m_hasPendingActivation = false;
    QString m_pendingToken;
};
