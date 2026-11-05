// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <functional>

#include "NotificationInhibitor.h"

/**
 * Do Not Disturb on macOS, through two shortcuts of the Shortcuts app.
 *
 * macOS has no public interface to turn a Focus on or off; the Shortcuts app's "Set Focus"
 * action can, and `/usr/bin/shortcuts run "<name>"` (macOS 12 and later) runs a shortcut from
 * another program. The user creates one shortcut that turns the Focus on and one that turns it
 * off, and picks them in the settings (AppSettings::focusOnShortcut / focusOffShortcut).
 *
 * Available when the tool exists and both shortcuts are among those `shortcuts list` printed.
 * The list is read again whenever the names change and when refreshShortcutList() is called
 * (the settings page, the application becoming active).
 *
 * Like FreedesktopInhibitor, a small asynchronous state machine: it remembers what is wanted
 * and, one `shortcuts run` at a time, makes the Focus match. A run that fails leaves the state
 * as it was (logged) and is not retried until the next inhibit() or a new list; one that
 * times out is killed. An "on" whose outcome is unknown (timed out)
 * counts as on, so the release still runs the "off" shortcut: turning a Focus off twice is
 * harmless, leaving it on is not. Killing the `shortcuts` client may not stop the shortcut
 * inside the Shortcuts app, so after a timeout the next run waits retryDelayMs (a late "on"
 * must not land after the "off"). An "off" that times out is run once more after that pause,
 * and then given up on: no retry loop, and quitting waits at most releaseTimeoutMs().
 *
 * Unlike the freedesktop inhibition, nothing ties the Focus to our process: if Sugo crashes
 * while working, the Focus stays on until the user turns it off. Quitting releases it
 * (SilenceController::shutdown waits up to releaseTimeoutMs()).
 *
 * Compiled everywhere (plain Qt and QProcess) so it is tested on Linux with a fake program.
 */
class ShortcutsFocusInhibitor final : public NotificationInhibitor
{
    Q_OBJECT

public:
    static inline const QString DefaultProgram = QStringLiteral("/usr/bin/shortcuts");

    explicit ShortcutsFocusInhibitor(const QString &program = DefaultProgram, QObject *parent = nullptr);

    bool isAvailable() const override;
    void inhibit(const QString &reason) override;
    void release() override;
    bool isInhibited() const override { return m_on || m_busy; }
    bool isReleasing() const override { return isInhibited() && !m_wanted; }
    /// An "on" in flight, the pause after it if it timed out, then the "off".
    int releaseTimeoutMs() const override { return 2 * m_runTimeoutMs + m_retryDelayMs + 500; }

    /// The shortcuts to run; reads the list again when they change.
    void setShortcutNames(const QString &onShortcut, const QString &offShortcut);
    QString onShortcut() const { return m_onShortcut; }
    QString offShortcut() const { return m_offShortcut; }

    /// The `shortcuts` program exists and can be run.
    bool toolFound() const;
    /// The names of the user's shortcuts, as last read (sorted, without duplicates).
    QStringList shortcutList() const { return m_list; }
    /// Reads the list of shortcuts again (asynchronously; shortcutListChanged() when done).
    void refreshShortcutList();

    /// Tests: shorter timeouts than the real ones.
    void setTimeoutsForTesting(int runMs, int listMs, int retryDelayMs = 500)
    {
        m_runTimeoutMs = runMs;
        m_listTimeoutMs = listMs;
        m_retryDelayMs = retryDelayMs;
    }

    /// The names of the shortcuts in the output of `shortcuts list`: one per line.
    static QStringList parseShortcutList(const QByteArray &output);

Q_SIGNALS:
    /// The list was read (it may be the same as before).
    void shortcutListChanged();

private:
    using Done = std::function<void(bool ok, bool timedOut, const QByteArray &output, const QString &error)>;
    void runProgram(const QStringList &arguments, int timeoutMs, Done done);
    void sync();
    void pauseThenSync();
    void report();

    QString m_program;
    QString m_onShortcut;
    QString m_offShortcut;
    QStringList m_list;
    bool m_listing = false;      ///< `shortcuts list` is running.
    bool m_listAgain = false;    ///< Asked for a refresh while one was running.
    bool m_wasAvailable = false; ///< Last value announced through availableChanged().

    bool m_wanted = false; ///< What the caller asked for.
    bool m_on = false;     ///< The "on" shortcut ran (or may have) and "off" has not run since.
    bool m_busy = false;   ///< A `shortcuts run` is in flight; only one at a time.
    bool m_pausing = false; ///< Waiting retryDelayMs after a timed-out run before the next one.
    bool m_offRetried = false; ///< The "off" timed out once already: the next timeout gives up.
    bool m_onFailed = false; ///< The "on" shortcut failed; not run again until asked anew.
    bool m_reported = false;
    bool m_warnedUnavailable = false;

    int m_runTimeoutMs = 5000;
    int m_listTimeoutMs = 10000;
    int m_retryDelayMs = 2000;
};
