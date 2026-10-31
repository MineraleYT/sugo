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
 * - Anything else (other platforms, a desktop whose server has no `Inhibit`): NoopInhibitor,
 *   so the option is accepted but reported as unavailable.
 */
std::unique_ptr<NotificationInhibitor> createNotificationInhibitor(QObject *parent = nullptr);
