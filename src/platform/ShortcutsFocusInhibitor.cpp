// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShortcutsFocusInhibitor.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QTimer>

#include <memory>

Q_LOGGING_CATEGORY(lcShortcutsInhibitor, "sugo.platform.inhibitor.shortcuts")

ShortcutsFocusInhibitor::ShortcutsFocusInhibitor(const QString &program, QObject *parent)
    : NotificationInhibitor(parent)
    , m_program(program)
{
}

bool ShortcutsFocusInhibitor::toolFound() const
{
    const QFileInfo info(m_program);
    return info.isFile() && info.isExecutable();
}

bool ShortcutsFocusInhibitor::isAvailable() const
{
    return !m_onShortcut.isEmpty() && !m_offShortcut.isEmpty() && m_list.contains(m_onShortcut)
        && m_list.contains(m_offShortcut) && toolFound();
}

void ShortcutsFocusInhibitor::setShortcutNames(const QString &onShortcut, const QString &offShortcut)
{
    const QString on = onShortcut.trimmed();
    const QString off = offShortcut.trimmed();
    if (on == m_onShortcut && off == m_offShortcut) {
        return;
    }
    m_onShortcut = on;
    m_offShortcut = off;
    report();
    refreshShortcutList();
}

QStringList ShortcutsFocusInhibitor::parseShortcutList(const QByteArray &output)
{
    QStringList names;
    for (const QString &line : QString::fromUtf8(output).split(QLatin1Char('\n'))) {
        const QString name = line.trimmed();
        if (!name.isEmpty()) {
            names.append(name);
        }
    }
    names.sort(Qt::CaseInsensitive);
    names.removeDuplicates();
    return names;
}

void ShortcutsFocusInhibitor::refreshShortcutList()
{
    if (m_listing) {
        m_listAgain = true;
        return;
    }
    if (!toolFound()) {
        m_list.clear();
        report();
        Q_EMIT shortcutListChanged();
        return;
    }
    m_listing = true;
    runProgram({QStringLiteral("list")}, m_listTimeoutMs, [this](bool ok, bool, const QByteArray &output, const QString &error) {
        m_listing = false;
        if (ok) {
            m_list = parseShortcutList(output);
        } else {
            qCWarning(lcShortcutsInhibitor) << "Cannot list the shortcuts:" << error;
            m_list.clear();
        }
        m_onFailed = false; // the shortcuts may have been fixed: worth another try
        report();
        Q_EMIT shortcutListChanged();
        if (m_listAgain) {
            m_listAgain = false;
            refreshShortcutList();
        } else {
            sync(); // silencing may have been wanted while the shortcuts were not known yet
        }
    });
}

void ShortcutsFocusInhibitor::inhibit(const QString &reason)
{
    Q_UNUSED(reason) // a Focus has no reason to show
    m_wanted = true;
    m_onFailed = false;
    sync();
}

void ShortcutsFocusInhibitor::release()
{
    m_wanted = false;
    sync();
}

// With no run in flight, make the Focus match what is wanted. Every finished run ends by
// calling this again, so a change of mind during a run is picked up as soon as it returns.
void ShortcutsFocusInhibitor::sync()
{
    if (m_busy) {
        return;
    }
    if (m_wanted && !m_on && !m_onFailed) {
        if (!isAvailable()) {
            if (!m_warnedUnavailable) {
                m_warnedUnavailable = true;
                qCInfo(lcShortcutsInhibitor) << "Cannot turn the Focus on: the shortcuts" << m_onShortcut << "and"
                                             << m_offShortcut << "are not both available";
            }
            return;
        }
        m_warnedUnavailable = false;
        m_busy = true;
        report();
        runProgram({QStringLiteral("run"), m_onShortcut}, m_runTimeoutMs,
                   [this](bool ok, bool timedOut, const QByteArray &, const QString &error) {
                       m_busy = false;
                       if (!ok) {
                           qCWarning(lcShortcutsInhibitor) << "Cannot turn the Focus on:" << error;
                           // No retry loop: the next inhibit() or a new list of shortcuts tries again.
                           m_onFailed = !timedOut;
                       }
                       // A run that timed out may still have turned it on.
                       m_on = ok || timedOut;
                       report();
                       sync(); // release() may have been called meanwhile
                   });
    } else if (!m_wanted && m_on) {
        m_busy = true;
        report();
        runProgram({QStringLiteral("run"), m_offShortcut}, m_runTimeoutMs,
                   [this](bool ok, bool, const QByteArray &, const QString &error) {
                       m_busy = false;
                       if (!ok) {
                           // No retry: a shortcut that fails now will fail again, and quitting
                           // must not wait for it. The user can still turn the Focus off.
                           qCWarning(lcShortcutsInhibitor) << "Cannot turn the Focus off:" << error;
                       }
                       m_on = false;
                       report();
                       sync(); // inhibit() may have been called meanwhile
                   });
    }
}

void ShortcutsFocusInhibitor::runProgram(const QStringList &arguments, int timeoutMs, Done done)
{
    auto *process = new QProcess(this);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    // finished, errorOccurred and the timeout may each fire; only the first one counts.
    auto handled = std::make_shared<bool>(false);
    const auto finish = [this, process, timer, handled, done = std::move(done)](bool ok, bool timedOut, const QString &error) {
        if (*handled) {
            return;
        }
        *handled = true;
        timer->stop();
        disconnect(process, nullptr, this, nullptr);
        const QByteArray output = process->readAllStandardOutput();
        if (process->state() == QProcess::NotRunning) {
            process->deleteLater();
        } else {
            // Killed after a timeout: gone once the system has reaped it.
            connect(process, &QProcess::finished, process, &QObject::deleteLater);
        }
        done(ok, timedOut, output, error);
    };

    connect(process, &QProcess::finished, this, [process, finish](int exitCode, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && exitCode == 0) {
            finish(true, false, {});
            return;
        }
        QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
        if (error.isEmpty()) {
            error = status == QProcess::NormalExit ? QStringLiteral("exit code %1").arg(exitCode) : QStringLiteral("crashed");
        }
        finish(false, false, error);
    });
    connect(process, &QProcess::errorOccurred, this, [process, finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finish(false, false, process->errorString());
        }
    });
    connect(timer, &QTimer::timeout, this, [process, finish, timeoutMs]() {
        process->kill();
        finish(false, true, QStringLiteral("no answer within %1 ms").arg(timeoutMs));
    });

    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setStandardInputFile(QProcess::nullDevice());
    timer->start(timeoutMs);
    process->start(m_program, arguments);
}

void ShortcutsFocusInhibitor::report()
{
    const bool available = isAvailable();
    if (available != m_wasAvailable) {
        m_wasAvailable = available;
        Q_EMIT availableChanged();
    }
    const bool now = isInhibited();
    if (now != m_reported) {
        m_reported = now;
        Q_EMIT inhibitedChanged();
    }
}
