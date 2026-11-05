// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QFile>
#include <QStringList>
#include <QTemporaryDir>

/**
 * A stand-in for macOS's /usr/bin/shortcuts, as a shell script in a temporary directory: it
 * records every call in calls.log (one line per call, arguments separated by '|'), prints
 * list.txt for `list` (with `--show-identifiers`, each name followed by its identifierOf(),
 * unless `no-identifiers` exists: then it fails as an older tool would), and for `run` waits
 * `delay` seconds, never answers if `hang` exists or while `hang-runs` counts down (one run
 * each), and fails if `fail-<name>` exists. Like the real tool, `run` takes an argument that
 * starts with "-" for an option and fails with exit code 64. Nothing real is ever run.
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
    if [ "$2" = "--show-identifiers" ]; then
        [ -f "$dir/no-identifiers" ] && { echo "Error: Unknown option '--show-identifiers'" >&2; exit 64; }
        [ -f "$dir/list.txt" ] && awk 'length($0) > 0 { printf "%s (00000000-0000-4000-8000-%012d)\n", $0, NR }' "$dir/list.txt"
        exit 0
    fi
    [ -f "$dir/list.txt" ] && cat "$dir/list.txt"
    exit 0 ;;
run)
    case "$2" in
    -*) echo "Error: Missing expected argument '<shortcut-name-or-identifier>'" >&2; exit 64 ;;
    esac
    [ -f "$dir/hang" ] && exec sleep 30
    if [ -f "$dir/hang-runs" ]; then
        left=$(cat "$dir/hang-runs")
        if [ "$left" -gt 0 ]; then echo $((left - 1)) > "$dir/hang-runs"; exec sleep 30; fi
    fi
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

    void setList(const QStringList &names)
    {
        m_list = names;
        write(QStringLiteral("list.txt"), names.join(QLatin1Char('\n')).toUtf8() + "\n");
    }
    /// The identifier `list --show-identifiers` prints for `name` (its place in the last setList()).
    QString identifierOf(const QString &name) const
    {
        return QStringLiteral("00000000-0000-4000-8000-%1").arg(m_list.indexOf(name) + 1, 12, 10, QLatin1Char('0'));
    }
    /// `list --show-identifiers` fails, as with a tool that does not know the option.
    void setNoIdentifiers(bool none) { flag(QStringLiteral("no-identifiers"), none); }
    void setDelay(const QString &seconds) { write(QStringLiteral("delay"), seconds.toUtf8()); }
    void setHang(bool hang) { flag(QStringLiteral("hang"), hang); }
    /// The next `count` runs never answer, the ones after them do.
    void setHangingRuns(int count) { write(QStringLiteral("hang-runs"), QByteArray::number(count)); }
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
    QStringList m_list;
};
