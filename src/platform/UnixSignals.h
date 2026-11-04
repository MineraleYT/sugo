// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class QObject;

/// Turns SIGINT, SIGTERM and SIGHUP into a normal QCoreApplication::quit(), so that
/// `kill`, logout or shutdown end the app through aboutToQuit: the running phase is
/// recorded and notifications are switched back on. (SIGKILL cannot be caught; the
/// notification server drops the inhibition when our D-Bus connection closes.)
/// The same signal received a second time while shutting down terminates immediately
/// (default action); another of the three does not.
/// No-op on platforms without POSIX signals.
void installQuitSignalHandlers(QObject *parent);
