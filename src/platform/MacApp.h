// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>

#ifdef Q_OS_MACOS
#include <QString>

/// Brings the application to the front, as a click on its Dock icon does. QWindow::requestActivate()
/// only makes a window key inside the application: when another one is active, the window
/// would stay behind it.
void macActivateApp();

/// Sets the text of the badge on the Dock icon; an empty text removes the badge.
void macSetDockBadge(const QString &label);

/// Hides the Dock icon (an accessory application: no Dock icon, no menu bar of its own) or shows
/// it again (a regular application). Showing it again does not activate the application.
void macSetDockIconHidden(bool hidden);

#include "QuitPolicy.h"

/// Where the request to quit being handled comes from, told by the Apple event Cocoa is handling
/// (call it while applicationShouldTerminate: runs, i.e. from the QEvent::Quit Qt sends then):
/// a `quit` event (the Dock's Quit, Activity Monitor, AppleScript) is External, or SessionEnding
/// when it carries a reason (kAEQuitReason: logging out, restarting, shutting down); no Apple
/// event means Sugo's own QCoreApplication::quit() (Application).
QuitPolicy::Origin macCurrentQuitOrigin();

class QWindow;
/// Hides the title text the system draws in the window's title bar (the traffic lights stay):
/// the title bar of the window already shows the name. The title still names the window in the Window menu and
/// Mission Control. Does nothing without the cocoa platform (offscreen runs).
void macHideWindowTitle(QWindow *window);
#endif
