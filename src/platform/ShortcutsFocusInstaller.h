// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QTemporaryDir;

/**
 * Installs the two shortcuts ShortcutsFocusInhibitor runs, so the user does not have to build
 * them by hand in the Shortcuts app.
 *
 * Apple lets no app add a shortcut silently: a signed .shortcut file, opened, shows Shortcuts'
 * "Add Shortcut" sheet, which the user confirms. So install():
 *  1. writes, in a private temporary directory, one shortcut per name as an XML property list
 *     ("<name>.shortcut": the file name becomes the name of the imported shortcut), each with a
 *     single "Set Focus" action (is.workflow.actions.dnd.set) that turns Do Not Disturb on (until
 *     turned off) or off;
 *  2. converts each to a binary property list (`plutil -convert binary1`);
 *  3. signs each (`shortcuts sign --mode anyone`), which needs the Mac to be signed into iCloud:
 *     without it, the tool says so and the result is NeedsICloud;
 *  4. opens the signed files one after the other (`open`, each once the previous `open` has
 *     returned): Shortcuts shows an "Add Shortcut" sheet for each. That the second sheet waits
 *     behind the first could not be checked on a Mac without iCloud (see the manual checklist).
 *
 * Every step is an asynchronous QProcess with a timeout, run one at a time; finished() reports
 * the outcome once. Opened only means the sheets were shown: whether the user added the
 * shortcuts shows in the next `shortcuts list`. The files stay until the next install() or the
 * installer's destruction, since Shortcuts reads them after `open` has returned.
 *
 * Compiled everywhere (plain Qt) and tested on Linux with fake programs.
 */
class ShortcutsFocusInstaller final : public QObject
{
    Q_OBJECT

public:
    enum class Result {
        Opened,      ///< Shortcuts was asked to import both; the user adds them.
        NeedsICloud, ///< Signing needs the Mac to be signed into iCloud.
        Failed,      ///< Any other error (logged, with the tool's error output).
    };
    Q_ENUM(Result)

    struct Programs {
        QString plutil = QStringLiteral("/usr/bin/plutil");
        QString shortcuts = QStringLiteral("/usr/bin/shortcuts");
        QString open = QStringLiteral("/usr/bin/open");
    };

    explicit ShortcutsFocusInstaller(QObject *parent = nullptr);
    explicit ShortcutsFocusInstaller(const Programs &programs, QObject *parent = nullptr);
    ~ShortcutsFocusInstaller() override;

    /// Starts installing the shortcuts named @p onName and @p offName; finished() when done.
    /// Ignored (returns false) while an installation runs.
    bool install(const QString &onName, const QString &offName);
    bool isRunning() const { return m_running; }

    /// The shortcut, as an XML property list: one "Set Focus" action turning Do Not Disturb
    /// on (until turned off) or off.
    static QByteArray shortcutPlist(bool turnOn);
    /// Whether @p name can be a shortcut's file name (and so its name).
    static bool isValidName(const QString &name);
    /// Whether the error output of `shortcuts sign` says the Mac is not signed into iCloud.
    static bool isICloudError(const QString &error);

    /// The directory of the last installation's files (empty before one).
    QString workDirectory() const;

    /// Tests: a shorter timeout for every step.
    void setTimeoutForTesting(int ms) { m_timeoutMs = m_signTimeoutMs = ms; }

Q_SIGNALS:
    /// The installation ended. @p message is a technical detail (the tool's error), for the log
    /// and the "failed" status; empty when it went well.
    void finished(ShortcutsFocusInstaller::Result result, const QString &message);

private:
    struct Step {
        QString program;
        QStringList arguments;
        int timeoutMs;
        bool isSign;
    };
    using Done = std::function<void(bool ok, const QString &error)>;

    void runNext();
    void runProgram(const Step &step, Done done);
    void finish(Result result, const QString &message);

    Programs m_programs;
    std::unique_ptr<QTemporaryDir> m_dir;
    QList<Step> m_steps;
    bool m_running = false;
    int m_timeoutMs = 10000;
    int m_signTimeoutMs = 60000; // signing talks to Apple's servers
};
