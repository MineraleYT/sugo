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
const QString kTranslocated =
    QStringLiteral("/private/var/folders/xy/abc/T/AppTranslocation/1234-5678/d/Sugo.app/Contents/MacOS/Sugo");

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

    void temporaryLocations()
    {
        const QString diskImage = QStringLiteral("/Volumes/Sugo/Sugo.app/Contents/MacOS/Sugo");
        const QString otherVolume = QStringLiteral("/Volumes/Data/Applications/Sugo.app/Contents/MacOS/Sugo");
        // The volume of the disk image is read-only; the second disk the app is installed on is not.
        const auto readOnly = [diskImage](const QString &path) { return path == diskImage; };
        const auto runsFromTemporaryLocation = [&](const QString &program) {
            return LaunchAgentAutostart(QString(), kLabel, command(program), readOnly).runsFromTemporaryLocation();
        };
        QVERIFY(runsFromTemporaryLocation(diskImage));
        QVERIFY(runsFromTemporaryLocation(kTranslocated));
        QVERIFY(!runsFromTemporaryLocation(otherVolume));
        QVERIFY(!runsFromTemporaryLocation(QStringLiteral("/Applications/Sugo.app/Contents/MacOS/Sugo")));
        QVERIFY(!runsFromTemporaryLocation(QStringLiteral("/Users/me/Applications/Sugo.app/Contents/MacOS/Sugo")));
        QVERIFY(!LaunchAgentAutostart(QString(), kLabel, {}, readOnly).runsFromTemporaryLocation());
    }

    void theRealCheckSeesAWritableFolderAsWritable()
    {
        QTemporaryDir dir;
        QVERIFY(!LaunchAgentAutostart::onReadOnlyVolume(dir.path()));
        QVERIFY(!LaunchAgentAutostart::onReadOnlyVolume(dir.filePath(QStringLiteral("missing/Sugo"))));
        LaunchAgentAutostart autostart(dir.path(), kLabel, command(dir.filePath(QStringLiteral("Sugo"))));
        QVERIFY(!autostart.runsFromTemporaryLocation());
#ifdef Q_OS_MACOS
        // A firmlink into the writable Data volume, though "/" (the system volume) is read-only.
        QVERIFY(!LaunchAgentAutostart::onReadOnlyVolume(QStringLiteral("/Applications")));
#endif
    }

    void enablingFromATemporaryLocationFails()
    {
        QTemporaryDir dir;
        LaunchAgentAutostart autostart(dir.path(), kLabel, command(QStringLiteral("/Volumes/Sugo/Sugo.app/Contents/MacOS/Sugo")),
                                       [](const QString &) { return true; });
        QVERIFY(!autostart.setEnabled(true));
        QVERIFY(!autostart.isEnabled());
    }

    // An app installed on a second, writable volume can start at login and is followed when it moves.
    void anotherWritableVolumeIsARealInstallation()
    {
        QTemporaryDir dir;
        const auto writable = [](const QString &) { return false; };
        const QString otherVolume = QStringLiteral("/Volumes/Data/Applications/Sugo.app/Contents/MacOS/Sugo");
        LaunchAgentAutostart installed(dir.path(), kLabel, command(), writable);
        QVERIFY(installed.setEnabled(true));

        LaunchAgentAutostart moved(dir.path(), kLabel, command(otherVolume), writable);
        moved.repair();
        QCOMPARE(LaunchAgentAutostart::programArguments(installed.filePath()), command(otherVolume));

        QVERIFY(moved.setEnabled(false));
        QVERIFY(moved.setEnabled(true));
        QCOMPARE(LaunchAgentAutostart::programArguments(installed.filePath()), command(otherVolume));
    }

    void repairKeepsTheAgentWhenRunFromATemporaryLocation()
    {
        QTemporaryDir dir;
        const QString diskImage = QStringLiteral("/Volumes/Sugo/Sugo.app/Contents/MacOS/Sugo");
        const auto readOnly = [diskImage](const QString &path) { return path == diskImage; };
        LaunchAgentAutostart installed(dir.path(), kLabel, command(), readOnly);
        QVERIFY(installed.setEnabled(true));

        LaunchAgentAutostart fromDiskImage(dir.path(), kLabel, command(diskImage), readOnly);
        fromDiskImage.repair();
        QCOMPARE(LaunchAgentAutostart::programArguments(installed.filePath()), command());
        LaunchAgentAutostart fromTranslocation(dir.path(), kLabel, command(kTranslocated), readOnly);
        fromTranslocation.repair();
        QCOMPARE(LaunchAgentAutostart::programArguments(installed.filePath()), command());
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
