// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

/**
 * Installs the two shortcuts ShortcutsFocusInhibitor runs, "Sugo Focus On" and "Sugo Focus Off",
 * so the user does not have to build them by hand in the Shortcuts app.
 *
 * Apple lets no app add a shortcut silently: a signed .shortcut file, opened, shows Shortcuts'
 * "Add Shortcut" sheet, which the user confirms. The two files ship inside the app
 * (Sugo.app/Contents/Resources/shortcuts/, from data/shortcuts/), signed by Apple for "Anyone"
 * when the maintainer exported them (see docs/DEVELOPMENT.md), so installing needs neither
 * iCloud nor signing on the user's Mac. install():
 *  1. checks both files are there and reads the user's shortcuts (`shortcuts list`);
 *  2. for "On", then "Off", unless the user already has it: opens its file (`open`), then reads
 *     the list again every second until the shortcut appears (the user clicked Add Shortcut), for
 *     at most three minutes. The second file is opened only once the first shortcut is there, so
 *     its sheet cannot be lost behind the first one.
 *
 * Every program runs as an asynchronous QProcess with a timeout, one at a time; the state is
 * reported as it changes (stateChanged()) and finished() reports the outcome once.
 *
 * Compiled everywhere (plain Qt) and tested on Linux with fake programs and files.
 */
class ShortcutsFocusInstaller final : public QObject
{
    Q_OBJECT

public:
    enum class State {
        Idle,          ///< Never started.
        Installing,    ///< Checking the files and reading the user's shortcuts.
        WaitingForOn,  ///< Shortcuts shows the sheet of "Sugo Focus On": the user clicks Add Shortcut.
        WaitingForOff, ///< The same for "Sugo Focus Off".
        Done,          ///< Both shortcuts are among the user's shortcuts.
        Failed,        ///< Missing files, a tool's error, or no shortcut within the time allowed.
    };
    Q_ENUM(State)

    struct Programs {
        QString shortcuts = QStringLiteral("/usr/bin/shortcuts");
        QString open = QStringLiteral("/usr/bin/open");
    };

    /// The real programs and the files of the app bundle.
    explicit ShortcutsFocusInstaller(QObject *parent = nullptr);
    /// Tests: other programs, and the files in @p directory.
    ShortcutsFocusInstaller(const Programs &programs, const QString &directory, QObject *parent = nullptr);
    ~ShortcutsFocusInstaller() override;

    /// The names of the shipped shortcuts (also the files' names, without ".shortcut").
    static QString onName();
    static QString offName();
    /// Where the files are in the app bundle: <app>/Contents/Resources/shortcuts.
    static QString bundledDirectory();

    /// Starts installing; finished() when done. Ignored (returns false) while one runs.
    bool install();
    bool isRunning() const { return m_state == State::Installing || m_state == State::WaitingForOn || m_state == State::WaitingForOff; }
    State state() const { return m_state; }
    /// Of a failed installation: what went wrong, for the log and the status line.
    QString message() const { return m_message; }
    QString directory() const { return m_directory; }

    /// Tests: how often the list is read while waiting, for how long, and every tool's timeout.
    void setTimingForTesting(int pollMs, int waitMs, int toolTimeoutMs)
    {
        m_pollMs = pollMs;
        m_waitMs = waitMs;
        m_toolTimeoutMs = toolTimeoutMs;
    }

Q_SIGNALS:
    void stateChanged(ShortcutsFocusInstaller::State state);
    /// The installation ended (Done or Failed); @p message as message().
    void finished(bool ok, const QString &message);

private:
    using Done = std::function<void(bool ok, const QByteArray &output, const QString &error)>;

    void installNext();
    void poll();
    void runProgram(const QString &program, const QStringList &arguments, Done done);
    void setState(State state);
    void fail(const QString &message);
    QString filePath(const QString &name) const;

    Programs m_programs;
    QString m_directory;
    State m_state = State::Idle;
    QString m_message;
    QStringList m_pending;   ///< Shortcuts still to install, in order.
    QString m_waitingFor;    ///< The shortcut whose sheet is open.
    QElapsedTimer m_waited;
    int m_pollMs = 1000;
    int m_waitMs = 3 * 60 * 1000;
    int m_toolTimeoutMs = 15000;
};
