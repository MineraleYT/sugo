// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>

#include "LaunchAgentAutostart.h"

namespace
{
const QString kLabel = QStringLiteral("io.github.mineraleyt.sugo");

QStringList command(const QString &program = QStringLiteral("/Applications/Sugo.app/Contents/MacOS/Sugo"))
{
    return {program, QStringLiteral("--background")};
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

class LaunchAgentAutostartTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void enablingWritesTheAgent()
    {
        QTemporaryDir dir;
        const QString agents = dir.filePath(QStringLiteral("Library/LaunchAgents"));
        LaunchAgentAutostart autostart(agents, kLabel, command());
        QVERIFY(autostart.isSupported());
        QVERIFY(!autostart.isEnabled());

        QVERIFY(autostart.setEnabled(true)); // also creates the missing LaunchAgents folder
        QVERIFY(autostart.isEnabled());
        QCOMPARE(autostart.filePath(), agents + QStringLiteral("/io.github.mineraleyt.sugo.plist"));
        QCOMPARE(LaunchAgentAutostart::programArguments(autostart.filePath()), command());
    }

    void theAgentIsAWellFormedPropertyList()
    {
        const QByteArray plist = LaunchAgentAutostart::plist(kLabel, command());
        QVERIFY(plist.startsWith("<?xml version=\"1.0\""));
        QVERIFY(plist.contains("<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\""));

        // Walk the top-level dict: key, value, key, value...
        QXmlStreamReader xml(plist);
        QMap<QString, QString> values;
        QString key;
        int depth = 0;
        while (!xml.atEnd()) {
            const auto token = xml.readNext();
            if (token == QXmlStreamReader::StartElement) {
                ++depth;
                if (depth == 3) {
                    const QString name = xml.name().toString();
                    if (name == QLatin1String("key")) {
                        key = xml.readElementText();
                        --depth;
                    } else if (name == QLatin1String("string")) {
                        values.insert(key, xml.readElementText());
                        --depth;
                    } else {
                        values.insert(key, name);
                    }
                }
            } else if (token == QXmlStreamReader::EndElement) {
                --depth;
            }
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        QCOMPARE(values.value(QStringLiteral("Label")), kLabel);
        QCOMPARE(values.value(QStringLiteral("ProgramArguments")), QStringLiteral("array"));
        QCOMPARE(values.value(QStringLiteral("RunAtLoad")), QStringLiteral("true"));
        QCOMPARE(values.value(QStringLiteral("ProcessType")), QStringLiteral("Interactive"));
    }

    void specialCharactersInThePathAreEscaped()
    {
        QTemporaryDir dir;
        const QStringList odd = command(QStringLiteral("/Users/a & b/<Apps>/Sugo \"x\".app/Contents/MacOS/Sugo"));
        LaunchAgentAutostart autostart(dir.path(), kLabel, odd);
        QVERIFY(autostart.setEnabled(true));
        QVERIFY(readAll(autostart.filePath()).contains("a &amp; b/&lt;Apps&gt;"));
        QCOMPARE(LaunchAgentAutostart::programArguments(autostart.filePath()), odd);
    }

    void disablingRemovesTheAgent()
    {
        QTemporaryDir dir;
        LaunchAgentAutostart autostart(dir.path(), kLabel, command());
        QVERIFY(autostart.setEnabled(true));
        QVERIFY(autostart.setEnabled(false));
        QVERIFY(!autostart.isEnabled());
        QVERIFY(!QFile::exists(autostart.filePath()));
        QVERIFY(autostart.setEnabled(false)); // nothing left to remove is still a success
    }

    void repairUpdatesAMovedApplication()
    {
        QTemporaryDir dir;
        LaunchAgentAutostart old(dir.path(), kLabel, command(QStringLiteral("/Users/me/Downloads/Sugo.app/Contents/MacOS/Sugo")));
        QVERIFY(old.setEnabled(true));

        LaunchAgentAutostart current(dir.path(), kLabel, command());
        current.repair();
        QCOMPARE(LaunchAgentAutostart::programArguments(current.filePath()), command());
    }

    void repairLeavesACurrentAgentAlone()
    {
        QTemporaryDir dir;
        LaunchAgentAutostart autostart(dir.path(), kLabel, command());
        QVERIFY(autostart.setEnabled(true));
        const QDateTime before = QFileInfo(autostart.filePath()).lastModified();
        QTest::qWait(20);
        autostart.repair();
        QCOMPARE(QFileInfo(autostart.filePath()).lastModified(), before);
    }

    void repairNeverEnablesADisabledAgent()
    {
        QTemporaryDir dir;
        LaunchAgentAutostart autostart(dir.path(), kLabel, command());
        autostart.repair();
        QVERIFY(!autostart.isEnabled());
    }

    void anUnreadableAgentIsRewrittenByRepair()
    {
        QTemporaryDir dir;
        LaunchAgentAutostart autostart(dir.path(), kLabel, command());
        {
            QFile file(autostart.filePath());
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("not a property list");
        }
        QVERIFY(autostart.isEnabled());
        QVERIFY(LaunchAgentAutostart::programArguments(autostart.filePath()).isEmpty());
        autostart.repair();
        QCOMPARE(LaunchAgentAutostart::programArguments(autostart.filePath()), command());
    }

    void anUnwritableLocationFailsWithoutCrashing()
    {
        QTemporaryDir dir;
        const QString blocker = dir.filePath(QStringLiteral("file"));
        {
            QFile file(blocker);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        // The agents folder would have to be created inside a regular file.
        LaunchAgentAutostart autostart(blocker + QStringLiteral("/LaunchAgents"), kLabel, command());
        QVERIFY(!autostart.setEnabled(true));
        QVERIFY(!autostart.isEnabled());
    }
};

QTEST_GUILESS_MAIN(LaunchAgentAutostartTest)
#include "tst_launchagentautostart.moc"
