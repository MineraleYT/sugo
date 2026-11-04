// SPDX-License-Identifier: GPL-3.0-or-later
#include "Autostart.h"
#include "AppImage.h"
#include "LaunchAgentAutostart.h"

#ifdef Q_OS_LINUX
#include "PortalAutostart.h"

#include <QDBusConnection>
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

Q_LOGGING_CATEGORY(lcAutostart, "sugo.platform.autostart")

namespace
{
QString quoteArgument(const QString &argument)
{
    static const QString reserved = QStringLiteral(" \t\n\"'\\><~|&;$*?#()`");
    const bool needsQuotes = argument.isEmpty() || std::any_of(argument.cbegin(), argument.cend(), [](QChar c) {
                                 return reserved.contains(c);
                             });
    if (!needsQuotes) {
        return argument;
    }
    QString quoted = QStringLiteral("\"");
    for (const QChar c : argument) {
        if (c == QLatin1Char('"') || c == QLatin1Char('`') || c == QLatin1Char('$') || c == QLatin1Char('\\')) {
            quoted += QLatin1Char('\\');
        }
        quoted += c;
    }
    quoted += QLatin1Char('"');
    return quoted;
}

QString oneLine(QString text)
{
    return text.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\r'), QLatin1Char(' '));
}
} // namespace

XdgAutostart::XdgAutostart(const QString &directory, const QString &appId, const QString &displayName, const QString &comment, const QStringList &command)
    : m_directory(directory)
    , m_appId(appId)
    , m_displayName(oneLine(displayName))
    , m_comment(oneLine(comment))
    , m_command(command)
{
}

QString XdgAutostart::filePath() const
{
    return QDir(m_directory).filePath(m_appId + QStringLiteral(".desktop"));
}

QString XdgAutostart::execLine(const QStringList &command)
{
    QStringList quoted;
    for (const QString &argument : command) {
        // A literal '%' would start a field code (%f, %u...) and must be doubled, quoted or not.
        quoted << quoteArgument(argument).replace(QLatin1Char('%'), QStringLiteral("%%"));
    }
    // The Exec quoting rules apply first, then the general string escaping, which doubles
    // every backslash (so a literal backslash inside quotes ends up as four).
    return quoted.join(QLatin1Char(' ')).replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
}

QMap<QString, QString> XdgAutostart::readEntries() const
{
    QMap<QString, QString> entries;
    QFile file(filePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return entries;
    }
    bool inDesktopEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inDesktopEntry = line == QLatin1String("[Desktop Entry]");
        } else if (inDesktopEntry && !line.startsWith(QLatin1Char('#'))) {
            const int equals = line.indexOf(QLatin1Char('='));
            if (equals > 0 && !entries.contains(line.left(equals).trimmed())) {
                entries.insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
            }
        }
    }
    return entries;
}

bool XdgAutostart::isEnabled() const
{
    if (!QFile::exists(filePath())) {
        return false;
    }
    const QMap<QString, QString> entries = readEntries();
    if (entries.value(QStringLiteral("Hidden")).compare(QLatin1String("true"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    if (entries.value(QStringLiteral("X-GNOME-Autostart-enabled")).compare(QLatin1String("false"), Qt::CaseInsensitive) == 0) {
        return false;
    }
    return true;
}

QByteArray XdgAutostart::contents() const
{
    QString text;
    text += QStringLiteral("[Desktop Entry]\n");
    text += QStringLiteral("Type=Application\n");
    text += QStringLiteral("Name=") + m_displayName + QLatin1Char('\n');
    text += QStringLiteral("Comment=") + m_comment + QLatin1Char('\n');
    text += QStringLiteral("Icon=") + m_appId + QLatin1Char('\n');
    text += QStringLiteral("Exec=") + execLine(m_command) + QLatin1Char('\n');
    text += QStringLiteral("Terminal=false\n");
    text += QStringLiteral("X-GNOME-Autostart-enabled=true\n");
    text += QStringLiteral("X-KDE-autostart-after=panel\n");
    return text.toUtf8();
}

bool XdgAutostart::write() const
{
    if (!QDir().mkpath(m_directory)) {
        qCWarning(lcAutostart) << "Cannot create" << m_directory;
        return false;
    }
    // QSaveFile replaces the file atomically, so a crash cannot leave half an entry behind.
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly) || file.write(contents()) < 0 || !file.commit()) {
        qCWarning(lcAutostart) << "Cannot write" << filePath() << file.errorString();
        return false;
    }
    return true;
}

bool XdgAutostart::setEnabled(bool enabled)
{
    if (enabled) {
        return write(); // also clears Hidden= / X-GNOME-Autostart-enabled=false left by other tools
    }
    if (!QFile::exists(filePath())) {
        return true;
    }
    if (!QFile::remove(filePath())) {
        qCWarning(lcAutostart) << "Cannot remove" << filePath();
        return false;
    }
    return true;
}

void XdgAutostart::repair()
{
    if (!isEnabled()) {
        return;
    }
    const QString expected = execLine(m_command);
    if (readEntries().value(QStringLiteral("Exec")) != expected) {
        qCInfo(lcAutostart) << "Updating the login entry to the current program path";
        write();
    }
}

QString autostartProgram()
{
    // An AppImage runs from a mount that is gone once it quits: the login entry starts the file.
    const QString appImage = AppImage::path();
    return appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
}

std::unique_ptr<Autostart> createAutostart()
{
#ifdef Q_OS_LINUX
    if (QFile::exists(QStringLiteral("/.flatpak-info"))) {
        return std::make_unique<PortalAutostart>(
            QDBusConnection::sessionBus(),
            QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + QStringLiteral("/portal-autostart"),
            QStringLiteral("Start Sugo at login, hidden in the system tray"),
            QStringList{QStringLiteral("sugo"), QStringLiteral("--background")});
    }
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/autostart");
    return std::make_unique<XdgAutostart>(directory,
                                          QGuiApplication::desktopFileName(),
                                          QStringLiteral("Sugo"),
                                          QStringLiteral("Pomodoro timer"),
                                          QStringList{autostartProgram(), QStringLiteral("--background")});
#elif defined(Q_OS_MACOS)
    // The bundle's executable (Sugo.app/Contents/MacOS/Sugo); repair() follows the bundle if it moves.
    return std::make_unique<LaunchAgentAutostart>(
        QDir::homePath() + QStringLiteral("/Library/LaunchAgents"),
        QStringLiteral("io.github.mineraleyt.sugo"),
        QStringList{QCoreApplication::applicationFilePath(), QStringLiteral("--background")});
#else
    return std::make_unique<UnsupportedAutostart>();
#endif
}
