// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShortcutsFocusInstaller.h"

#include "ShortcutsFocusInhibitor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QProcess>
#include <QTimer>

#include <memory>

Q_LOGGING_CATEGORY(lcShortcutsInstaller, "sugo.platform.inhibitor.shortcuts.install")

ShortcutsFocusInstaller::ShortcutsFocusInstaller(QObject *parent)
    : ShortcutsFocusInstaller(Programs(), bundledDirectory(), parent)
{
}

ShortcutsFocusInstaller::ShortcutsFocusInstaller(const Programs &programs, const QString &directory, QObject *parent)
    : QObject(parent)
    , m_programs(programs)
    , m_directory(directory)
{
}

ShortcutsFocusInstaller::~ShortcutsFocusInstaller() = default;

QString ShortcutsFocusInstaller::onName()
{
    return QStringLiteral("Sugo Focus On");
}

QString ShortcutsFocusInstaller::offName()
{
    return QStringLiteral("Sugo Focus Off");
}

QString ShortcutsFocusInstaller::bundledDirectory()
{
    // Contents/MacOS/Sugo → Contents/Resources/shortcuts (src/CMakeLists.txt puts them there).
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/shortcuts"));
}

QString ShortcutsFocusInstaller::filePath(const QString &name) const
{
    return QDir(m_directory).filePath(name + QStringLiteral(".shortcut"));
}

bool ShortcutsFocusInstaller::install()
{
    if (isRunning()) {
        return false;
    }
    m_message.clear();
    m_waitingFor.clear();
    m_pending = {onName(), offName()};
    setState(State::Installing);

    for (const QString &name : std::as_const(m_pending)) {
        if (!QFileInfo(filePath(name)).isFile()) {
            // Asynchronous like every other outcome, so callers see one behaviour.
            const QString path = filePath(name);
            QTimer::singleShot(0, this, [this, path]() { fail(QStringLiteral("missing file ") + path); });
            return true;
        }
    }
    QTimer::singleShot(0, this, &ShortcutsFocusInstaller::installNext);
    return true;
}

void ShortcutsFocusInstaller::installNext()
{
    // Reads the list first: a shortcut the user already has gets no sheet.
    runProgram(m_programs.shortcuts, {QStringLiteral("list")}, [this](bool ok, const QByteArray &output, const QString &error) {
        if (!ok) {
            fail(QStringLiteral("shortcuts list: ") + error);
            return;
        }
        const QStringList present = ShortcutsFocusInhibitor::parseShortcutList(output);
        while (!m_pending.isEmpty() && present.contains(m_pending.constFirst())) {
            m_pending.removeFirst();
        }
        if (m_pending.isEmpty()) {
            setState(State::Done);
            Q_EMIT finished(true, {});
            return;
        }
        const QString name = m_pending.takeFirst();
        runProgram(m_programs.open, {filePath(name)}, [this, name](bool ok, const QByteArray &, const QString &error) {
            if (!ok) {
                fail(QStringLiteral("open: ") + error);
                return;
            }
            m_waitingFor = name;
            m_waited.start();
            setState(name == onName() ? State::WaitingForOn : State::WaitingForOff);
            QTimer::singleShot(m_pollMs, this, &ShortcutsFocusInstaller::poll);
        });
    });
}

void ShortcutsFocusInstaller::poll()
{
    runProgram(m_programs.shortcuts, {QStringLiteral("list")}, [this](bool ok, const QByteArray &output, const QString &error) {
        if (!ok) {
            fail(QStringLiteral("shortcuts list: ") + error);
            return;
        }
        if (ShortcutsFocusInhibitor::parseShortcutList(output).contains(m_waitingFor)) {
            m_waitingFor.clear();
            // Back to the list, which also opens the next file.
            installNext();
            return;
        }
        if (m_waited.elapsed() >= m_waitMs) {
            fail(QStringLiteral("\"%1\" was not added within %2 s").arg(m_waitingFor).arg(m_waitMs / 1000));
            return;
        }
        QTimer::singleShot(m_pollMs, this, &ShortcutsFocusInstaller::poll);
    });
}

void ShortcutsFocusInstaller::runProgram(const QString &program, const QStringList &arguments, Done done)
{
    auto *process = new QProcess(this);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    // finished, errorOccurred and the timeout may each fire; only the first one counts.
    auto handled = std::make_shared<bool>(false);
    const auto finishStep = [this, process, timer, handled, done = std::move(done)](bool ok, const QByteArray &output, const QString &error) {
        if (*handled) {
            return;
        }
        *handled = true;
        timer->stop();
        disconnect(process, nullptr, this, nullptr);
        if (process->state() == QProcess::NotRunning) {
            process->deleteLater();
        } else {
            connect(process, &QProcess::finished, process, &QObject::deleteLater);
        }
        done(ok, output, error);
    };

    connect(process, &QProcess::finished, this, [process, finishStep](int exitCode, QProcess::ExitStatus status) {
        const QByteArray output = process->readAllStandardOutput();
        if (status == QProcess::NormalExit && exitCode == 0) {
            finishStep(true, output, {});
            return;
        }
        // `shortcuts` prints its errors on either channel.
        QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
        const QString text = QString::fromUtf8(output).trimmed();
        if (!text.isEmpty()) {
            error = error.isEmpty() ? text : error + QLatin1Char('\n') + text;
        }
        if (error.isEmpty()) {
            error = status == QProcess::NormalExit ? QStringLiteral("exit code %1").arg(exitCode) : QStringLiteral("crashed");
        }
        finishStep(false, {}, error);
    });
    connect(process, &QProcess::errorOccurred, this, [process, finishStep](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finishStep(false, {}, process->errorString());
        }
    });
    const int timeoutMs = m_toolTimeoutMs;
    connect(timer, &QTimer::timeout, this, [process, finishStep, timeoutMs]() {
        process->kill();
        finishStep(false, {}, QStringLiteral("no answer within %1 ms").arg(timeoutMs));
    });

    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setStandardInputFile(QProcess::nullDevice());
    timer->start(timeoutMs);
    process->start(program, arguments);
}

void ShortcutsFocusInstaller::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    Q_EMIT stateChanged(state);
}

void ShortcutsFocusInstaller::fail(const QString &message)
{
    qCWarning(lcShortcutsInstaller) << "Cannot install the Focus shortcuts:" << message;
    m_pending.clear();
    m_waitingFor.clear();
    m_message = message;
    setState(State::Failed);
    Q_EMIT finished(false, message);
}
