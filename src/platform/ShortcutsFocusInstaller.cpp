// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShortcutsFocusInstaller.h"

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QLoggingCategory>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>

Q_LOGGING_CATEGORY(lcShortcutsInstaller, "sugo.platform.inhibitor.shortcuts.install")

namespace
{
// From a shortcut built in the Shortcuts app (macOS 26) and exported: the keys of the "Set Focus"
// action and the default Do Not Disturb Focus. AssertionType "Turned Off" is the action's
// "Until: Turned Off" option.
constexpr char kHeader[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>WFWorkflowClientVersion</key>
	<string>2607.0.2</string>
	<key>WFWorkflowMinimumClientVersion</key>
	<integer>900</integer>
	<key>WFWorkflowIcon</key>
	<dict>
		<key>WFWorkflowIconStartColor</key>
		<integer>4282601983</integer>
		<key>WFWorkflowIconGlyphNumber</key>
		<integer>59511</integer>
	</dict>
	<key>WFWorkflowTypes</key>
	<array/>
	<key>WFWorkflowInputContentItemClasses</key>
	<array>
		<string>WFStringContentItem</string>
	</array>
	<key>WFWorkflowActions</key>
	<array>
		<dict>
			<key>WFWorkflowActionIdentifier</key>
			<string>is.workflow.actions.dnd.set</string>
			<key>WFWorkflowActionParameters</key>
			<dict>
)";

constexpr char kFocusMode[] = R"(				<key>FocusModes</key>
				<dict>
					<key>Identifier</key>
					<string>com.apple.donotdisturb.mode.default</string>
					<key>DisplayString</key>
					<string>Do Not Disturb</string>
				</dict>
)";

constexpr char kFooter[] = R"(			</dict>
		</dict>
	</array>
</dict>
</plist>
)";
} // namespace

ShortcutsFocusInstaller::ShortcutsFocusInstaller(QObject *parent)
    : ShortcutsFocusInstaller(Programs(), parent)
{
}

ShortcutsFocusInstaller::ShortcutsFocusInstaller(const Programs &programs, QObject *parent)
    : QObject(parent)
    , m_programs(programs)
{
}

ShortcutsFocusInstaller::~ShortcutsFocusInstaller() = default;

QByteArray ShortcutsFocusInstaller::shortcutPlist(bool turnOn)
{
    QByteArray plist(kHeader);
    plist += "\t\t\t\t<key>Enabled</key>\n\t\t\t\t<integer>";
    plist += turnOn ? "1" : "0";
    plist += "</integer>\n";
    plist += kFocusMode;
    if (turnOn) {
        plist += "\t\t\t\t<key>AssertionType</key>\n\t\t\t\t<string>Turned Off</string>\n";
    }
    plist += kFooter;
    return plist;
}

bool ShortcutsFocusInstaller::isValidName(const QString &name)
{
    // Becomes a file name: nothing that leaves the directory or that the Finder would rewrite.
    return !name.trimmed().isEmpty() && name == name.trimmed() && !name.startsWith(QLatin1Char('.'))
        && !name.contains(QLatin1Char('/')) && !name.contains(QLatin1Char(':')) && !name.contains(QChar(0))
        && name.size() <= 200;
}

bool ShortcutsFocusInstaller::isICloudError(const QString &error)
{
    // "Error: In order to do this, you must be signed into iCloud." The product name is the same
    // in every language.
    return error.contains(QLatin1String("iCloud"), Qt::CaseInsensitive);
}

QString ShortcutsFocusInstaller::workDirectory() const
{
    return m_dir ? m_dir->path() : QString();
}

bool ShortcutsFocusInstaller::install(const QString &onName, const QString &offName)
{
    if (m_running) {
        return false;
    }
    m_running = true;
    m_steps.clear();

    if (!isValidName(onName) || !isValidName(offName) || onName == offName) {
        // Asynchronous like every other outcome, so callers see one behaviour.
        QTimer::singleShot(0, this, [this]() { finish(Result::Failed, QStringLiteral("invalid shortcut names")); });
        return true;
    }

    m_dir = std::make_unique<QTemporaryDir>(QDir::temp().filePath(QStringLiteral("sugo-shortcuts-XXXXXX")));
    const QDir root(m_dir->path());
    if (!m_dir->isValid() || !root.mkdir(QStringLiteral("unsigned")) || !root.mkdir(QStringLiteral("signed"))) {
        QTimer::singleShot(0, this, [this]() { finish(Result::Failed, QStringLiteral("cannot create a temporary directory")); });
        return true;
    }

    QStringList signedFiles;
    const QList<std::pair<QString, bool>> shortcuts{{onName, true}, {offName, false}};
    for (const auto &[name, turnOn] : shortcuts) {
        const QString file = name + QStringLiteral(".shortcut");
        const QString unsignedPath = root.filePath(QStringLiteral("unsigned/") + file);
        const QString signedPath = root.filePath(QStringLiteral("signed/") + file);
        QFile out(unsignedPath);
        if (!out.open(QIODevice::WriteOnly) || out.write(shortcutPlist(turnOn)) < 0 || !out.flush()) {
            const QString error = out.errorString();
            QTimer::singleShot(0, this, [this, error]() { finish(Result::Failed, QStringLiteral("cannot write a shortcut: ") + error); });
            return true;
        }
        out.close();
        m_steps.append({m_programs.plutil, {QStringLiteral("-convert"), QStringLiteral("binary1"), unsignedPath}, m_timeoutMs, false});
        m_steps.append({m_programs.shortcuts,
                        {QStringLiteral("sign"), QStringLiteral("--mode"), QStringLiteral("anyone"), QStringLiteral("--input"),
                         unsignedPath, QStringLiteral("--output"), signedPath},
                        m_signTimeoutMs,
                        true});
        signedFiles.append(signedPath);
    }
    // Opened only once both are signed: no half installation. Shortcuts shows one "Add Shortcut"
    // sheet per file; the second waits behind the first.
    for (const QString &path : std::as_const(signedFiles)) {
        m_steps.append({m_programs.open, {path}, m_timeoutMs, false});
    }
    QTimer::singleShot(0, this, &ShortcutsFocusInstaller::runNext);
    return true;
}

void ShortcutsFocusInstaller::runNext()
{
    if (m_steps.isEmpty()) {
        finish(Result::Opened, {});
        return;
    }
    const Step step = m_steps.takeFirst();
    runProgram(step, [this, step](bool ok, const QString &error) {
        if (ok) {
            runNext();
            return;
        }
        m_steps.clear();
        const QString detail = QFileInfo(step.program).fileName() + QStringLiteral(": ") + error;
        if (step.isSign && isICloudError(error)) {
            qCInfo(lcShortcutsInstaller) << "Cannot sign the Focus shortcuts: the Mac is not signed into iCloud";
            finish(Result::NeedsICloud, detail);
            return;
        }
        qCWarning(lcShortcutsInstaller) << "Cannot install the Focus shortcuts:" << detail;
        finish(Result::Failed, detail);
    });
}

void ShortcutsFocusInstaller::runProgram(const Step &step, Done done)
{
    auto *process = new QProcess(this);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    // finished, errorOccurred and the timeout may each fire; only the first one counts.
    auto handled = std::make_shared<bool>(false);
    const auto finishStep = [this, process, timer, handled, done = std::move(done)](bool ok, const QString &error) {
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
        done(ok, error);
    };

    connect(process, &QProcess::finished, this, [process, finishStep](int exitCode, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && exitCode == 0) {
            finishStep(true, {});
            return;
        }
        // `shortcuts` prints its errors on either channel.
        QString error = QString::fromUtf8(process->readAllStandardError()).trimmed();
        const QString output = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        if (!output.isEmpty()) {
            error = error.isEmpty() ? output : error + QLatin1Char('\n') + output;
        }
        if (error.isEmpty()) {
            error = status == QProcess::NormalExit ? QStringLiteral("exit code %1").arg(exitCode) : QStringLiteral("crashed");
        }
        finishStep(false, error);
    });
    connect(process, &QProcess::errorOccurred, this, [process, finishStep](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            finishStep(false, process->errorString());
        }
    });
    const int timeoutMs = step.timeoutMs;
    connect(timer, &QTimer::timeout, this, [process, finishStep, timeoutMs]() {
        process->kill();
        finishStep(false, QStringLiteral("no answer within %1 ms").arg(timeoutMs));
    });

    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->setStandardInputFile(QProcess::nullDevice());
    timer->start(step.timeoutMs);
    process->start(step.program, step.arguments);
}

void ShortcutsFocusInstaller::finish(Result result, const QString &message)
{
    m_running = false;
    Q_EMIT finished(result, message);
}
