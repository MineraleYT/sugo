// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

/**
 * A stand-in for macOS's /usr/bin/shortcuts, as a shell script in a temporary directory: it
 * records every call in calls.log (one line per call, arguments separated by '|'), prints
 * list.txt for `list`, and for `run` waits `delay` seconds, never answers if `hang` exists, and
 * fails if `fail-<name>` exists. Nothing real is ever run.
 */
class FakeShortcuts
{
public:
    FakeShortcuts()
    {
        QFile script(program());
        if (script.open(QIODevice::WriteOnly)) {
            script.write(R"SH(#!/bin/sh
dir=$(dirname "$0")
line=""
for arg in "$@"; do line="$line$arg|"; done
echo "$line" >> "$dir/calls.log"
case "$1" in
list)
    [ -f "$dir/fail-list" ] && { echo "list failed" >&2; exit 1; }
    [ -f "$dir/list.txt" ] && cat "$dir/list.txt"
    exit 0 ;;
run)
    [ -f "$dir/hang" ] && exec sleep 30
    [ -f "$dir/delay" ] && sleep "$(cat "$dir/delay")"
    [ -f "$dir/fail-$2" ] && { echo "The shortcut failed" >&2; exit 1; }
    exit 0 ;;
esac
exit 2
)SH");
            script.close();
            script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        }
    }

    bool isValid() const { return m_dir.isValid() && QFile::exists(program()); }
    QString program() const { return m_dir.filePath(QStringLiteral("shortcuts")); }

    void setList(const QStringList &names) { write(QStringLiteral("list.txt"), names.join(QLatin1Char('\n')).toUtf8() + "\n"); }
    void setDelay(const QString &seconds) { write(QStringLiteral("delay"), seconds.toUtf8()); }
    void setHang(bool hang) { flag(QStringLiteral("hang"), hang); }
    void setFails(const QString &shortcut, bool fails) { flag(QStringLiteral("fail-") + shortcut, fails); }

    /// The calls so far, e.g. "list|", "run|Sugo Focus On|".
    QStringList calls() const
    {
        QFile log(m_dir.filePath(QStringLiteral("calls.log")));
        if (!log.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(log.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    QStringList runs() const { return calls().filter(QStringLiteral("run|")); }

private:
    void write(const QString &name, const QByteArray &data)
    {
        QFile file(m_dir.filePath(name));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
        }
    }
    void flag(const QString &name, bool on)
    {
        if (on) {
            write(name, "1");
        } else {
            QFile::remove(m_dir.filePath(name));
        }
    }

    QTemporaryDir m_dir;
};
