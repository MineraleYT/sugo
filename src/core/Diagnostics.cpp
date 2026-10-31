// SPDX-License-Identifier: GPL-3.0-or-later
#include "Diagnostics.h"

#include "AppSettings.h"
#include "DataManager.h"

#include "Localization.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDebug>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSysInfo>
#include <QTime>

#include <QLoggingCategory>

#include <atomic>
#include <deque>

namespace
{
// Allocated once and never destroyed: the message handler stays installed until the process ends,
// and a warning printed during static destruction must still find a live buffer and mutex.
QMutex &logMutex()
{
    static QMutex *mutex = new QMutex;
    return *mutex;
}
std::deque<QString> &logBuffer()
{
    static auto *buffer = new std::deque<QString>;
    return *buffer;
}
constexpr size_t kMaxLogEntries = 120;
// Reproducing a problem with verbose logging on produces many more lines.
constexpr size_t kMaxVerboseLogEntries = 500;
std::atomic<bool> s_verbose{false};

size_t maxLogEntries()
{
    return s_verbose.load() ? kMaxVerboseLogEntries : kMaxLogEntries;
}
QtMessageHandler s_originalHandler = nullptr;

void diagnosticsMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString level;
    switch (type) {
    case QtDebugMsg:    level = QStringLiteral("DEBUG"); break;
    case QtInfoMsg:     level = QStringLiteral("INFO");  break;
    case QtWarningMsg:  level = QStringLiteral("WARN");  break;
    case QtCriticalMsg: level = QStringLiteral("CRIT");  break;
    case QtFatalMsg:    level = QStringLiteral("FATAL"); break;
    }

    const QString category = context.category ? QString::fromUtf8(context.category) : QStringLiteral("default");
    Diagnostics::addLogEntry(level, category, msg);

    if (s_originalHandler) {
        s_originalHandler(type, context, msg);
    }
}
} // namespace

Diagnostics::Diagnostics(QObject *parent)
    : QObject(parent)
{
}

Diagnostics::~Diagnostics() = default;

void Diagnostics::attach(AppSettings *settings, DataManager *dataManager)
{
    disconnect(m_verboseConnection);
    m_settings = settings;
    m_dataManager = dataManager;
    if (m_settings) {
        // The saved choice applies from now on, and the setting is followed while the program runs.
        setVerboseLogging(m_settings->verboseLogging());
        m_verboseConnection = connect(m_settings, &AppSettings::verboseLoggingChanged, this, [this]() {
            setVerboseLogging(m_settings->verboseLogging());
        });
    }
}

void Diagnostics::setVerboseLogging(bool verbose)
{
    // An empty rule set is Qt's default. Rules from QT_LOGGING_RULES are evaluated after
    // these, so a developer's own rules still win.
    QLoggingCategory::setFilterRules(verbose ? QStringLiteral("sugo.*.debug=true") : QString());
    QMutexLocker locker(&logMutex());
    s_verbose = verbose;
    while (logBuffer().size() > maxLogEntries()) {
        logBuffer().pop_front();
    }
}

bool Diagnostics::verboseLogging()
{
    return s_verbose;
}

int Diagnostics::logCapacity()
{
    return int(maxLogEntries());
}

void Diagnostics::installLogHandler()
{
    if (!s_originalHandler) {
        s_originalHandler = qInstallMessageHandler(diagnosticsMessageHandler);
    }
}

bool Diagnostics::isRecordedCategory(const QString &category)
{
    // KDE Frameworks are loaded into Sugo by Plasma's platform theme (the file dialog, for one).
    // Their messages say nothing about Sugo and can name the user's files ("No node found for
    // item that was just removed: file:///home/.../Downloads/..."), so they stay out of a report
    // meant to be shared; they are still printed as usual.
    return !category.startsWith(QLatin1String("kf."));
}

void Diagnostics::addLogEntry(const QString &level, const QString &category, const QString &message)
{
    if (!isRecordedCategory(category)) {
        return;
    }
    const QString time = QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    const QString redactedMsg = redact(message);
    const QString line = QStringLiteral("[%1] [%2] [%3] %4").arg(time, level, category, redactedMsg);

    QMutexLocker locker(&logMutex());
    if (logBuffer().size() >= maxLogEntries()) {
        logBuffer().pop_front();
    }
    logBuffer().push_back(line);
}

void Diagnostics::clearLogs()
{
    QMutexLocker locker(&logMutex());
    logBuffer().clear();
}

QString Diagnostics::recentLogs() const
{
    QMutexLocker locker(&logMutex());
    QStringList lines;
    for (const QString &line : logBuffer()) {
        lines.append(line);
    }
    return lines.join(QLatin1Char('\n'));
}

namespace
{
QStringList &accountRedactions()
{
    static QStringList words;
    return words;
}
} // namespace

void Diagnostics::setAccountRedactions(const QStringList &words)
{
    QMutexLocker locker(&logMutex());
    accountRedactions().clear();
    for (const QString &word : words) {
        // Very short words would blank out ordinary text.
        if (word.trimmed().size() > 2) {
            accountRedactions().append(word.trimmed());
        }
    }
}

QString Diagnostics::redact(const QString &text)
{
    QString result = text;

    // 0. Secrets first: Authorization headers, user:password@host in URLs, password/token
    // assignments (the calendar sync keeps an app password; none of it may leave in a report).
    static const QRegularExpression authHeaderRegex(QStringLiteral(R"((authorization\s*[:=]\s*)[^\r\n]*)"),
                                                    QRegularExpression::CaseInsensitiveOption);
    result.replace(authHeaderRegex, QStringLiteral("\\1<redacted>"));
    static const QRegularExpression urlCredentialsRegex(QStringLiteral(R"(\b([A-Za-z][A-Za-z0-9+.-]*://)[^/\s@]+@)"));
    result.replace(urlCredentialsRegex, QStringLiteral("\\1<redacted>@"));
    static const QRegularExpression secretAssignmentRegex(
        // A quoted value up to its closing quote; an unquoted one up to the end of the line or a
        // query-string '&' (a password may contain spaces, so a space does not end it).
        QStringLiteral(R"re(("?\b(?:app[_-]?password|password|passwd|secret|token)"?\s*[:=]\s*)(?:"[^"\r\n]*"?|'[^'\r\n]*'?|[^\r\n&]+))re"),
        QRegularExpression::CaseInsensitiveOption);
    result.replace(secretAssignmentRegex, QStringLiteral("\\1<redacted>"));

    // The accounts the user set up (the calendar login can differ from the system user name).
    QStringList accounts;
    {
        QMutexLocker locker(&logMutex());
        accounts = accountRedactions();
    }
    for (const QString &word : std::as_const(accounts)) {
        result.replace(word, QStringLiteral("<account>"), Qt::CaseInsensitive);
    }

    // 1. Redact the home directory path (and its resolved form, if it is a symlink)
    const QString home = QDir::homePath();
    for (const QString &path : {home, QDir(home).canonicalPath()}) {
        if (!path.isEmpty() && path != QLatin1String("/")) {
            result.replace(path, QStringLiteral("~"));
        }
    }

    // 2. Redact /home/<username> and /var/home/<username> (Fedora Silverblue/Kinoite) of anyone
    static const QRegularExpression homeUserRegex(QStringLiteral(R"((/(?:var/)?home/)[^/\s'"]+)"));
    result.replace(homeUserRegex, QStringLiteral("\\1<user>"));

    // 3. Redact the user part of user@host (shell prompts, ssh, e-mail addresses)
    static const QRegularExpression userAtHostRegex(QStringLiteral(R"([A-Za-z0-9._%+-]+@([A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*))"));
    result.replace(userAtHostRegex, QStringLiteral("<user>@\\1"));

    // 4. Redact the current user name wherever it stands as a word of its own
    QString user = qEnvironmentVariable("USER");
    if (user.isEmpty()) {
        user = QFileInfo(home).fileName();
    }
    if (user.length() > 2) {
        const QRegularExpression userRegex(QStringLiteral(R"((?<![A-Za-z0-9_.-])%1(?![A-Za-z0-9_-]))")
                                               .arg(QRegularExpression::escape(user)));
        result.replace(userRegex, QStringLiteral("<user>"));
    }

    return result;
}

QString Diagnostics::generateReport() const
{
    QStringList lines;
    lines << QStringLiteral("# Sugo Diagnostics Report");
    lines << QStringLiteral("Generated: ") + QDateTime::currentDateTimeUtc().toString(Qt::ISODate) + QStringLiteral(" (UTC)");
    lines << QString();

    lines << QStringLiteral("## Application");
#ifdef PROJECT_VERSION
    lines << QStringLiteral("- Version: ") + QString::fromLatin1(PROJECT_VERSION);
#else
    lines << QStringLiteral("- Version: ") + QCoreApplication::applicationVersion();
#endif
    lines << QStringLiteral("- Build Type: ") + (QFile::exists(QStringLiteral("/.flatpak-info")) ? QStringLiteral("Flatpak (Sandboxed)") : QStringLiteral("Native"));
    lines << QStringLiteral("- Qt Runtime: ") + QString::fromUtf8(qVersion()) + QStringLiteral(" (compiled with ") + QStringLiteral(QT_VERSION_STR) + QStringLiteral(")");
    lines << QStringLiteral("- QPA Platform: ") + QGuiApplication::platformName();
    lines << QStringLiteral("- Verbose Logging: ") + (verboseLogging() ? QStringLiteral("On (debug messages are recorded, up to %1 lines)").arg(logCapacity()) : QStringLiteral("Off"));
    lines << QString();

    lines << QStringLiteral("## System & Desktop");
    lines << QStringLiteral("- OS: ") + QSysInfo::prettyProductName();
    lines << QStringLiteral("- Kernel: ") + QSysInfo::kernelType() + QStringLiteral(" ") + QSysInfo::kernelVersion();
    lines << QStringLiteral("- Architecture: ") + QSysInfo::currentCpuArchitecture();
    lines << QStringLiteral("- Desktop Environment (XDG_CURRENT_DESKTOP): ") + QString::fromLocal8Bit(qgetenv("XDG_CURRENT_DESKTOP"));
    lines << QStringLiteral("- Session Type (XDG_SESSION_TYPE): ") + QString::fromLocal8Bit(qgetenv("XDG_SESSION_TYPE"));
    lines << QStringLiteral("- System Tray Available: ") + ((m_settings && m_settings->trayAvailable()) ? QStringLiteral("Yes") : QStringLiteral("No"));
    lines << QStringLiteral("- Autostart Supported: ") + ((m_settings && m_settings->autostartSupported()) ? QStringLiteral("Yes") : QStringLiteral("No"));
    lines << QString();

    lines << QStringLiteral("## Configuration");
    if (m_settings) {
        lines << QStringLiteral("- Sound Theme: ") + m_settings->soundTheme();
        lines << QStringLiteral("- Sound Volume: ") + QString::number(m_settings->soundVolume()) + QStringLiteral("%");
        lines << QStringLiteral("- Pre-Alarm: ") + (m_settings->preAlarmEnabled() ? QStringLiteral("Enabled") : QStringLiteral("Disabled")) + QStringLiteral(" (") + m_settings->preAlarmSound() + QStringLiteral(")");
        lines << QStringLiteral("- Focus Sound Mode: ") + QString::number(m_settings->focusSoundMode()) + QStringLiteral(" (Volume: ") + QString::number(m_settings->focusSoundVolume()) + QStringLiteral("%)");
        lines << QStringLiteral("- Screen Lock Auto-Pause: ") + (m_settings->autoPauseOnScreenLock() ? QStringLiteral("Enabled") : QStringLiteral("Disabled"));
        lines << QStringLiteral("- Keep Screen Awake: ") + (m_settings->keepScreenAwake() ? QStringLiteral("Enabled") : QStringLiteral("Disabled"));
        lines << QStringLiteral("- Protect Weekend Streak: ") + (m_settings->protectWeekendStreak() ? QStringLiteral("Yes") : QStringLiteral("No"));
        lines << QStringLiteral("- Prompt Task Note: ") + (m_settings->promptTaskNote() ? QStringLiteral("Yes") : QStringLiteral("No"));
        lines << QStringLiteral("- Close To Tray: ") + (m_settings->closeToTray() ? QStringLiteral("Yes") : QStringLiteral("No"));
        lines << QStringLiteral("- Start Minimized: ") + (m_settings->startMinimized() ? QStringLiteral("Yes") : QStringLiteral("No"));
    } else {
        lines << QStringLiteral("- Settings: (Not attached)");
    }
    lines << QString();

    lines << QStringLiteral("## Storage");
    if (m_dataManager) {
        lines << QStringLiteral("- Database Path: ") + redact(m_dataManager->databasePath());
        lines << QStringLiteral("- Total Recorded Sessions: ") + QString::number(m_dataManager->sessionCount());
        lines << QStringLiteral("- Timers / Presets Count: ") + QString::number(m_dataManager->presetCount());
    } else {
        lines << QStringLiteral("- Storage: (Not attached)");
    }
    lines << QString();

    lines << QStringLiteral("## Recent Logs (Redacted)");
    lines << QStringLiteral("```");
    const QString logs = recentLogs();
    if (logs.trimmed().isEmpty()) {
        lines << QStringLiteral("(No recent logs recorded)");
    } else {
        lines << logs;
    }
    lines << QStringLiteral("```");
    lines << QString();

    return lines.join(QLatin1Char('\n'));
}

bool Diagnostics::exportReport(const QUrl &fileUrl) const
{
    m_lastExportError.clear();

    QString path;
    if (fileUrl.isLocalFile()) {
        path = fileUrl.toLocalFile();
    } else if (fileUrl.scheme().isEmpty() && QDir::isAbsolutePath(fileUrl.path())) {
        path = fileUrl.path();
    } else {
        m_lastExportError = i18n("The report can only be saved to a local file.");
        qWarning() << "Diagnostics export refused for non-local location" << fileUrl.toDisplayString();
        return false;
    }

    // Written in full or not at all: a failure never leaves a truncated report behind.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_lastExportError = i18n("Could not open %1 for writing: %2", path, file.errorString());
        return false;
    }
    const QByteArray data = generateReport().toUtf8();
    if (file.write(data) != data.size() || !file.commit()) {
        m_lastExportError = i18n("Could not write %1: %2", path, file.errorString());
        return false;
    }
    return true;
}

void Diagnostics::copyToClipboard() const
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        clipboard->setText(generateReport());
    }
}
