// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>

#include "Notifier.h"

class QSystemTrayIcon;

/**
 * Picks the notifier for the running system.
 *
 * - Linux with a session bus: FreedesktopNotifier, even if no notification server is running
 *   yet (it asks the server anew for every notification); a notification no server is there
 *   for becomes a message from the tray icon `trayIcon` returns.
 * - Anything else: TrayNotifier, a message from the tray icon `trayIcon` returns.
 */
std::unique_ptr<Notifier> createNotifier(std::function<QSystemTrayIcon *()> trayIcon, QObject *parent = nullptr);
