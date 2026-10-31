// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class QObject;

/// Turns SIGINT, SIGTERM and SIGHUP into a normal QCoreApplication::quit(), so that
/// `kill`, logout or shutdown end the app through aboutToQuit: the running phase is
/// recorded and notifications are switched back on. (SIGKILL cannot be caught; the
/// notification server drops the inhibition when our D-Bus connection closes.)
/// A second signal while shutting down terminates immediately (default action).
/// No-op on platforms without POSIX signals.
void installQuitSignalHandlers(QObject *parent);
