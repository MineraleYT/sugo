// SPDX-License-Identifier: GPL-3.0-or-later
#include "LaunchAgentAutostart.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace
{
Q_LOGGING_CATEGORY(lcLaunchAgent, "sugo.platform.autostart.launchagent")
} // namespace

LaunchAgentAutostart::LaunchAgentAutostart(const QString &directory, const QString &label, const QStringList &command)
    : m_directory(directory)
    , m_label(label)
    , m_command(command)
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
    if (programArguments(filePath()) != m_command) {
        qCInfo(lcLaunchAgent) << "Updating the login agent to the current program path";
        write();
    }
}
