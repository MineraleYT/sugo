// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "NotificationInhibitor.h"

/**
 * Picks the notification inhibitor for the running system.
 *
 * - Linux with a session bus whose notification server implements `Inhibit`
 *   (KDE Plasma): FreedesktopInhibitor.
 * - A session bus without a notification server yet: FreedesktopInhibitor as well, since
 *   the server may simply not have started (the inhibitor copes with errors and restarts).
 *   So too a server that does not answer the question within 2 s (busy starting at login).
 * - macOS: ShortcutsFocusInhibitor (a Focus turned on and off by two of the user's shortcuts).
 * - Anything else (other platforms, a desktop whose server has no `Inhibit`): NoopInhibitor,
 *   so the option is accepted but reported as unavailable.
 */
std::unique_ptr<NotificationInhibitor> createNotificationInhibitor(QObject *parent = nullptr);

#ifdef Q_OS_LINUX
class QDBusConnection;

/// The same choice on a given bus (the session bus above; tests use a private one).
std::unique_ptr<NotificationInhibitor> createNotificationInhibitor(const QDBusConnection &bus, QObject *parent = nullptr);
#endif
