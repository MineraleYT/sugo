// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "Autostart.h"

#include <QString>
#include <QStringList>

/**
 * macOS login item as a per-user launchd agent: `<directory>/<label>.plist`, normally
 * ~/Library/LaunchAgents/io.github.mineraleyt.sugo.plist, which launchd loads at the next login
 * and runs once (RunAtLoad).
 *
 * "Enabled" is the file existing. Plain Qt and file I/O only, so it is compiled and tested on every
 * platform; createAutostart() picks it on macOS.
 */
class LaunchAgentAutostart final : public Autostart
{
public:
    /// `command` is the program followed by its arguments, e.g.
    /// {"/Applications/Sugo.app/Contents/MacOS/Sugo", "--background"}.
    LaunchAgentAutostart(const QString &directory, const QString &label, const QStringList &command);

    bool isSupported() const override { return true; }
    bool isEnabled() const override;
    bool setEnabled(bool enabled) override;
    /// Rewrites the agent when it starts another program or arguments (the app was moved).
    void repair() override;

    QString filePath() const;
    /// The property list written for `label` and `command`.
    static QByteArray plist(const QString &label, const QStringList &command);
    /// The ProgramArguments of an existing agent file; empty if it cannot be read.
    static QStringList programArguments(const QString &plistPath);

private:
    bool write() const;

    QString m_directory;
    QString m_label;
    QStringList m_command;
};
