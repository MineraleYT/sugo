// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtGlobal>

#ifdef Q_OS_MACOS
/// Brings the application to the front, as a click on its Dock icon does. QWindow::requestActivate()
/// only makes a window key inside the application: when another one is active, the window
/// would stay behind it.
void macActivateApp();
#endif
