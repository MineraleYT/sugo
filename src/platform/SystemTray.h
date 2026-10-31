// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// True if a system tray can show our icon: on Linux, a StatusNotifier watcher is running
/// (KDE Plasma's kded provides it; GNOME needs an extension). The tray host itself may still
/// be starting, which is fine: the item appears as soon as it is up.
bool systemTrayAvailable();
