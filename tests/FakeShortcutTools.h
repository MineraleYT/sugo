// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

#include "ShortcutsFocusInstaller.h"

/**
 * Stand-ins for macOS's /usr/bin/plutil, /usr/bin/shortcuts and /usr/bin/open, as one shell
 * script in a temporary directory under three names. Each call is recorded in calls.log (the
 * program's name, then its arguments, separated by '|'). `shortcuts sign` copies --input to
 * --output; a file `fail-<program>` makes that program fail with its content as error output,
 * `icloud` makes `shortcuts sign` print Apple's iCloud error, `hang-<program>` makes it never
 * answer. Nothing real is ever run.
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
if [ "$name" = shortcuts ] && [ "$1" = sign ]; then
    if [ -f "$dir/icloud" ]; then
        echo "Error: In order to do this, you must be signed into iCloud."
        exit 1
    fi
    cp "$5" "$7" || exit 1
fi
exit 0
)SH");
            file.close();
            file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        }
        for (const char *name : {"plutil", "shortcuts", "open"}) {
            QFile::link(script, m_dir.filePath(QLatin1String(name)));
        }
    }

    bool isValid() const { return m_dir.isValid() && QFile::exists(m_dir.filePath(QStringLiteral("tool.sh"))); }

    ShortcutsFocusInstaller::Programs programs() const
    {
        ShortcutsFocusInstaller::Programs programs;
        programs.plutil = m_dir.filePath(QStringLiteral("plutil"));
        programs.shortcuts = m_dir.filePath(QStringLiteral("shortcuts"));
        programs.open = m_dir.filePath(QStringLiteral("open"));
        return programs;
    }

    void setFails(const QString &program, const QByteArray &error) { write(QStringLiteral("fail-") + program, error); }
    void setHangs(const QString &program) { write(QStringLiteral("hang-") + program, "1"); }
    void setNeedsICloud() { write(QStringLiteral("icloud"), "1"); }

    /// The calls so far, e.g. "plutil|-convert|binary1|/tmp/…/Sugo Focus On.shortcut|".
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
