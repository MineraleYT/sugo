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

class QWindow;
/// Hides the title text the system draws in the window's title bar (the traffic lights stay):
/// the title bar of the window already shows the name. The title still names the window in the Window menu and
/// Mission Control. Does nothing without the cocoa platform (offscreen runs).
void macHideWindowTitle(QWindow *window);
#endif
