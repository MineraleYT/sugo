// SPDX-License-Identifier: GPL-3.0-or-later
#include "UnixSignals.h"

#include <QtGlobal>

#ifdef Q_OS_UNIX
#include <QCoreApplication>
#include <QSocketNotifier>

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
// A signal handler may only call async-signal-safe functions, so it writes one byte to a
// socket pair; the Qt event loop reads it and quits from a normal context.
int signalSocket[2] = {-1, -1};
constexpr int kQuitSignals[] = {SIGINT, SIGTERM, SIGHUP};

void onSignal(int)
{
    const int savedErrno = errno; // the interrupted code may be about to read errno
    // Back to the default action: if the graceful shutdown hangs, a second Ctrl+C or
    // SIGTERM ends the process for real.
    struct sigaction defaultAction {};
    defaultAction.sa_handler = SIG_DFL;
    sigemptyset(&defaultAction.sa_mask);
    for (const int signal : kQuitSignals) {
        ::sigaction(signal, &defaultAction, nullptr);
    }
    const char byte = 1;
    const ssize_t ignored = ::write(signalSocket[0], &byte, sizeof(byte)); // non-blocking
    Q_UNUSED(ignored)
    errno = savedErrno;
}
} // namespace

void installQuitSignalHandlers(QObject *parent)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, signalSocket) != 0) {
        return;
    }
    // The handler must never block, whatever is (not) reading the other end.
    ::fcntl(signalSocket[0], F_SETFL, ::fcntl(signalSocket[0], F_GETFL) | O_NONBLOCK);

    auto *notifier = new QSocketNotifier(signalSocket[1], QSocketNotifier::Read, parent);
    QObject::connect(notifier, &QSocketNotifier::activated, parent, [notifier]() {
        notifier->setEnabled(false);
        char byte;
        const ssize_t ignored = ::read(signalSocket[1], &byte, sizeof(byte));
        Q_UNUSED(ignored)
        QCoreApplication::quit();
    });

    struct sigaction action {};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    for (const int signal : kQuitSignals) {
        ::sigaction(signal, &action, nullptr);
    }
}
#else
void installQuitSignalHandlers(QObject *)
{
}
#endif
