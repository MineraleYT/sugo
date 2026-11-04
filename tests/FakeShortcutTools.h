// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

#include "ShortcutsFocusInstaller.h"

/**
 * Stand-ins for macOS's /usr/bin/shortcuts and /usr/bin/open, as one shell script in a temporary
 * directory under two names, and a directory with the two shortcut files (placeholders).
 *
 * Each call is recorded in calls.log (the program's name, then its arguments, separated by '|').
 * `shortcuts list` prints list.txt. `open <file>` plays the user clicking Add Shortcut: it adds
 * the file's name (without ".shortcut") to list.txt, unless `ignore` exists (the user never
 * confirms). A file `fail-<program>` makes that program fail with its content as error output,
 * `hang-<program>` makes it never answer. Nothing real is ever run.
 */
class FakeShortcutTools
{
public:
    FakeShortcutTools()
    {
        const QString script = m_dir.filePath(QStringLiteral("tool.sh"));
        QFile file(script);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(R"SH(#!/bin/sh
dir=$(dirname "$0")
name=$(basename "$0")
line="$name|"
for arg in "$@"; do line="$line$arg|"; done
echo "$line" >> "$dir/calls.log"
[ -f "$dir/hang-$name" ] && exec sleep 30
[ -f "$dir/fail-$name" ] && { cat "$dir/fail-$name" >&2; exit 1; }
if [ "$name" = shortcuts ] && [ "$1" = list ]; then
    [ -f "$dir/list.txt" ] && cat "$dir/list.txt"
    exit 0
fi
if [ "$name" = open ] && [ ! -f "$dir/ignore" ]; then
    basename "$1" .shortcut >> "$dir/list.txt"
fi
exit 0
)SH");
            file.close();
            file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        }
        for (const char *name : {"shortcuts", "open"}) {
            QFile::link(script, m_dir.filePath(QLatin1String(name)));
        }
        QDir(m_dir.path()).mkdir(QStringLiteral("files"));
        for (const QString &name : {ShortcutsFocusInstaller::onName(), ShortcutsFocusInstaller::offName()}) {
            write(QStringLiteral("files/") + name + QStringLiteral(".shortcut"), "AEA1");
        }
    }

    bool isValid() const { return m_dir.isValid() && QFile::exists(m_dir.filePath(QStringLiteral("tool.sh"))); }

    ShortcutsFocusInstaller::Programs programs() const
    {
        ShortcutsFocusInstaller::Programs programs;
        programs.shortcuts = m_dir.filePath(QStringLiteral("shortcuts"));
        programs.open = m_dir.filePath(QStringLiteral("open"));
        return programs;
    }
    /// The directory of the two shortcut files.
    QString directory() const { return m_dir.filePath(QStringLiteral("files")); }

    void setList(const QStringList &names) { write(QStringLiteral("list.txt"), names.isEmpty() ? QByteArray() : names.join(QLatin1Char('\n')).toUtf8() + "\n"); }
    void setFails(const QString &program, const QByteArray &error) { write(QStringLiteral("fail-") + program, error); }
    void setHangs(const QString &program) { write(QStringLiteral("hang-") + program, "1"); }
    /// The user never clicks Add Shortcut.
    void setIgnored() { write(QStringLiteral("ignore"), "1"); }
    void removeFile(const QString &name) { QFile::remove(QDir(directory()).filePath(name + QStringLiteral(".shortcut"))); }

    /// The calls so far, e.g. "open|/tmp/…/Sugo Focus On.shortcut|".
    QStringList calls() const
    {
        QFile log(m_dir.filePath(QStringLiteral("calls.log")));
        if (!log.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    QStringList calls(const QString &program) const
    {
        QStringList found;
        for (const QString &call : calls()) {
            if (call.startsWith(program + QLatin1Char('|'))) {
                found.append(call);
            }
        }
        return found;
    }

private:
    void write(const QString &name, const QByteArray &data)
    {
        QFile file(m_dir.filePath(name));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
        }
    }

    QTemporaryDir m_dir;
};
