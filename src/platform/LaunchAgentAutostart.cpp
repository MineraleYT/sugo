// SPDX-License-Identifier: GPL-3.0-or-later
#include "LaunchAgentAutostart.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <sys/statvfs.h>

namespace
{
Q_LOGGING_CATEGORY(lcLaunchAgent, "sugo.platform.autostart.launchagent")
} // namespace

LaunchAgentAutostart::LaunchAgentAutostart(const QString &directory, const QString &label, const QStringList &command,
                                           ReadOnlyCheck isOnReadOnlyVolume)
    : m_directory(directory)
    , m_label(label)
    , m_command(command)
    , m_isOnReadOnlyVolume(isOnReadOnlyVolume ? std::move(isOnReadOnlyVolume) : ReadOnlyCheck(&LaunchAgentAutostart::onReadOnlyVolume))
{
}

QString LaunchAgentAutostart::filePath() const
{
    return QDir(m_directory).filePath(m_label + QStringLiteral(".plist"));
}

QByteArray LaunchAgentAutostart::plist(const QString &label, const QStringList &command)
{
    QByteArray out;
    QXmlStreamWriter xml(&out);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(1);
    xml.writeStartDocument();
    xml.writeDTD(QStringLiteral("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                                "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">"));
    xml.writeStartElement(QStringLiteral("plist"));
    xml.writeAttribute(QStringLiteral("version"), QStringLiteral("1.0"));
    xml.writeStartElement(QStringLiteral("dict"));

    xml.writeTextElement(QStringLiteral("key"), QStringLiteral("Label"));
    xml.writeTextElement(QStringLiteral("string"), label);

    xml.writeTextElement(QStringLiteral("key"), QStringLiteral("ProgramArguments"));
    xml.writeStartElement(QStringLiteral("array"));
    for (const QString &argument : command) {
        xml.writeTextElement(QStringLiteral("string"), argument);
    }
    xml.writeEndElement(); // array

    xml.writeTextElement(QStringLiteral("key"), QStringLiteral("RunAtLoad"));
    xml.writeEmptyElement(QStringLiteral("true"));

    // A GUI application: launchd must not throttle it like a background daemon.
    xml.writeTextElement(QStringLiteral("key"), QStringLiteral("ProcessType"));
    xml.writeTextElement(QStringLiteral("string"), QStringLiteral("Interactive"));

    xml.writeEndElement(); // dict
    xml.writeEndElement(); // plist
    xml.writeEndDocument();
    return out;
}

QStringList LaunchAgentAutostart::programArguments(const QString &plistPath)
{
    QFile file(plistPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QXmlStreamReader xml(&file);
    QStringList arguments;
    int depth = 0;            // element depth; the top-level dict's keys are at depth 3 (plist, dict, key)
    bool nextIsArguments = false;
    bool inArguments = false;
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            ++depth;
            const QStringView name = xml.name();
            if (inArguments && depth == 4 && name == QLatin1String("string")) {
                arguments << xml.readElementText();
                --depth; // readElementText() consumed the end element
            } else if (depth == 3 && name == QLatin1String("key")) {
                nextIsArguments = xml.readElementText() == QLatin1String("ProgramArguments");
                --depth;
            } else if (depth == 3) {
                inArguments = nextIsArguments && name == QLatin1String("array");
                nextIsArguments = false;
            }
        } else if (token == QXmlStreamReader::EndElement) {
            if (depth == 3 && inArguments) {
                return arguments;
            }
            --depth;
        }
    }
    return xml.hasError() ? QStringList() : arguments;
}

bool LaunchAgentAutostart::onReadOnlyVolume(const QString &path)
{
    // statvfs() of the path itself, not QStorageInfo: on macOS that one asks about the volume's
    // root, and /Applications (a firmlink into the writable Data volume) has the sealed, read-only
    // system volume "/" as its root.
    struct statvfs info {};
    return ::statvfs(QFile::encodeName(path).constData(), &info) == 0 && (info.f_flag & ST_RDONLY) != 0;
}

bool LaunchAgentAutostart::runsFromTemporaryLocation() const
{
    if (m_command.isEmpty()) {
        return false;
    }
    // Not the /Volumes prefix: an app installed on an external disk or a second volume lives
    // there too, writable. A mounted disk image is read-only, and so is a translocated copy.
    const QString &program = m_command.constFirst();
    return program.contains(QLatin1String("/AppTranslocation/")) || m_isOnReadOnlyVolume(program);
}

bool LaunchAgentAutostart::isEnabled() const
{
    return QFile::exists(filePath());
}

bool LaunchAgentAutostart::write() const
{
    if (!QDir().mkpath(m_directory)) {
        qCWarning(lcLaunchAgent) << "Cannot create" << m_directory;
        return false;
    }
    // QSaveFile replaces the file atomically, so a crash cannot leave half an agent behind.
    QSaveFile file(filePath());
    if (!file.open(QIODevice::WriteOnly) || file.write(plist(m_label, m_command)) < 0 || !file.commit()) {
        qCWarning(lcLaunchAgent) << "Cannot write" << filePath() << file.errorString();
        return false;
    }
    return true;
}

bool LaunchAgentAutostart::setEnabled(bool enabled)
{
    if (enabled) {
        // launchd could not find the program at the next login: say it failed rather than
        // leave an agent that silently does nothing.
        if (runsFromTemporaryLocation()) {
            qCWarning(lcLaunchAgent) << "Not starting at login from a temporary location" << m_command.constFirst();
            return false;
        }
        return write();
    }
    if (!QFile::exists(filePath())) {
        return true;
    }
    if (!QFile::remove(filePath())) {
        qCWarning(lcLaunchAgent) << "Cannot remove" << filePath();
        return false;
    }
    return true;
}

void LaunchAgentAutostart::repair()
{
    if (!isEnabled()) {
        return;
    }
    // Run from the disk image or a translocated copy: the agent keeps the installed app.
    if (runsFromTemporaryLocation()) {
        return;
    }
    if (programArguments(filePath()) != m_command) {
        qCInfo(lcLaunchAgent) << "Updating the login agent to the current program path";
        write();
    }
}
