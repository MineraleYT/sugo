// SPDX-License-Identifier: GPL-3.0-or-later
#include "Localization.h"
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QTest>

#include "AppSettings.h"
#include "Autostart.h"
#include "DataManager.h"
#include "Database.h"
#include "Diagnostics.h"
#include "PresetModel.h"
#include "PresetRepository.h"
#include "SessionRepository.h"
#include "StatsModel.h"

class DiagnosticsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
    }

    void init()
    {
        Diagnostics::clearLogs();
    }

    void cleanup()
    {
        Diagnostics::setVerboseLogging(false);
    }

    void testRedaction()
    {
        const QString home = QDir::homePath();
        const QString rawPath = home + QStringLiteral("/.local/share/sugo/sugo.db");
        const QString redacted = Diagnostics::redact(rawPath);
        QVERIFY(!redacted.contains(home));
        QVERIFY(redacted.startsWith(QStringLiteral("~")));

        const QString genericHome = QStringLiteral("/home/john_doe/Documents/file.txt");
        QCOMPARE(Diagnostics::redact(genericHome), QStringLiteral("/home/<user>/Documents/file.txt"));

        const QString silverblueHome = QStringLiteral("/var/home/alice/test.log");
        QCOMPARE(Diagnostics::redact(silverblueHome), QStringLiteral("/var/home/<user>/test.log"));
    }

    void testLogCapturingAndRedaction()
    {
        Diagnostics::clearLogs();
        const QString home = QDir::homePath();

        Diagnostics::addLogEntry(QStringLiteral("INFO"), QStringLiteral("test"),
                                 QStringLiteral("Loaded database from %1/sugo.db").arg(home));

        const QString logs = Diagnostics().recentLogs();
        QVERIFY(logs.contains(QStringLiteral("[INFO]")));
        QVERIFY(logs.contains(QStringLiteral("[test]")));
        QVERIFY(logs.contains(QStringLiteral("~/sugo.db")));
        QVERIFY(!logs.contains(home));
    }

    void kdeFrameworksMessagesStayOutOfTheReport()
    {
        Diagnostics::clearLogs();
        Diagnostics::addLogEntry(QStringLiteral("WARN"), QStringLiteral("kf.kio.widgets.kdirmodel"),
                                 QStringLiteral("No node found for item that was just removed: QUrl(\"file:///home/someone/Downloads/medical-report.pdf\")"));
        Diagnostics::addLogEntry(QStringLiteral("WARN"), QStringLiteral("sugo.data"), QStringLiteral("Cannot detach the restored backup"));
        Diagnostics::addLogEntry(QStringLiteral("WARN"), QStringLiteral("qt.qpa.wayland"), QStringLiteral("Wayland message"));

        const QString logs = Diagnostics().recentLogs();
        QVERIFY(!logs.contains(QStringLiteral("kf.kio")));
        QVERIFY(!logs.contains(QStringLiteral("medical-report.pdf")));
        QVERIFY(logs.contains(QStringLiteral("[sugo.data]")));
        QVERIFY(logs.contains(QStringLiteral("[qt.qpa.wayland]")));

        // Also through the real message handler.
        Diagnostics::installLogHandler();
        Diagnostics::clearLogs();
        QTest::ignoreMessage(QtWarningMsg, "file:///home/someone/Downloads/secret.txt");
        QTest::ignoreMessage(QtWarningMsg, "kept");
        QMessageLogger(nullptr, 0, nullptr, "kf.kio.core").warning("file:///home/someone/Downloads/secret.txt");
        QMessageLogger(nullptr, 0, nullptr, "sugo.test").warning("kept");
        const QString handled = Diagnostics().recentLogs();
        QVERIFY(!handled.contains(QStringLiteral("secret.txt")));
        QVERIFY(handled.contains(QStringLiteral("kept")));
    }

    void kioDirModelWarningsAreSilencedInBothModes()
    {
        // The rule is installed by setVerboseLogging, which attach() calls at startup.
        const QLoggingCategory dirModel("kf.kio.widgets.kdirmodel");
        const QLoggingCategory otherKde("kf.kio.core");
        for (const bool verbose : {false, true}) {
            Diagnostics::setVerboseLogging(verbose);
            QVERIFY(!dirModel.isWarningEnabled());
            QVERIFY(otherKde.isWarningEnabled());
        }
        Diagnostics::setVerboseLogging(false);
    }

    void testCircularBufferCap()
    {
        Diagnostics::clearLogs();
        for (int i = 0; i < 150; ++i) {
            Diagnostics::addLogEntry(QStringLiteral("DEBUG"), QStringLiteral("cat"),
                                     QStringLiteral("Message number %1").arg(i));
        }

        const QString logs = Diagnostics().recentLogs();
        const QStringList lines = logs.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 120);
        QVERIFY(lines.first().contains(QStringLiteral("Message number 30")));
        QVERIFY(lines.last().contains(QStringLiteral("Message number 149")));
    }

    void verboseLoggingKeepsMoreLines()
    {
        QCOMPARE(Diagnostics::logCapacity(), 120);
        Diagnostics::setVerboseLogging(true);
        QVERIFY(Diagnostics::verboseLogging());
        QCOMPARE(Diagnostics::logCapacity(), 500);
        for (int i = 0; i < 600; ++i) {
            Diagnostics::addLogEntry(QStringLiteral("DEBUG"), QStringLiteral("cat"), QStringLiteral("Message number %1").arg(i));
        }
        QStringList lines = Diagnostics().recentLogs().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 500);
        QVERIFY(lines.first().contains(QStringLiteral("Message number 100")));

        // Back to normal: the buffer shrinks to the newest lines.
        Diagnostics::setVerboseLogging(false);
        lines = Diagnostics().recentLogs().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 120);
        QVERIFY(lines.first().contains(QStringLiteral("Message number 480")));
        QVERIFY(lines.last().contains(QStringLiteral("Message number 599")));
    }

    void verboseLoggingLetsSugoDebugMessagesThrough()
    {
        if (!qEnvironmentVariableIsEmpty("QT_LOGGING_RULES") || !qEnvironmentVariableIsEmpty("QT_LOGGING_CONF")) {
            QSKIP("The environment sets its own logging rules, which win over these.");
        }
        const QLoggingCategory category("sugo.test.verbose");
        Diagnostics::setVerboseLogging(true);
        QVERIFY(category.isDebugEnabled());
        Diagnostics::setVerboseLogging(false);
        QVERIFY(!category.isDebugEnabled());
    }

    void reportStatesVerboseLogging()
    {
        QTemporaryDir configDir;
        AppSettings settings(Settings::open(configDir.filePath(QStringLiteral("settings.conf"))), nullptr, false);
        Diagnostics diag;
        diag.attach(&settings, nullptr);
        QVERIFY(diag.generateReport().contains(QStringLiteral("- Verbose Logging: Off")));

        // The setting is followed while attached.
        settings.setVerboseLogging(true);
        QVERIFY(Diagnostics::verboseLogging());
        QVERIFY(diag.generateReport().contains(QStringLiteral("- Verbose Logging: On")));
        settings.setVerboseLogging(false);
        QVERIFY(!Diagnostics::verboseLogging());
        QVERIFY(diag.generateReport().contains(QStringLiteral("- Verbose Logging: Off")));

        // A saved choice applies as soon as Diagnostics is attached.
        settings.setVerboseLogging(true);
        Diagnostics::setVerboseLogging(false);
        Diagnostics other;
        other.attach(&settings, nullptr);
        QVERIFY(Diagnostics::verboseLogging());
    }

    void testGenerateReport()
    {
        Diagnostics diag;
        const QString report = diag.generateReport();

        QVERIFY(report.contains(QStringLiteral("# Sugo Diagnostics Report")));
        QVERIFY(report.contains(QStringLiteral("## Application")));
        QVERIFY(report.contains(QStringLiteral("## System & Desktop")));
        QVERIFY(report.contains(QStringLiteral("## Configuration")));
        QVERIFY(report.contains(QStringLiteral("## Storage")));
        QVERIFY(report.contains(QStringLiteral("## Recent Logs (Redacted)")));
        QVERIFY(report.contains(QStringLiteral("Version:")));
#ifdef Q_OS_MACOS
        // A test binary is not inside an .app bundle; there are no XDG variables to report.
        QVERIFY(report.contains(QStringLiteral("- App Bundle: No")));
        QVERIFY(report.contains(QStringLiteral("- Login Item (LaunchAgent): Unknown")));
        QVERIFY(!report.contains(QStringLiteral("XDG_")));
#else
        QVERIFY(report.contains(QStringLiteral("- Build Type: ")));
        QVERIFY(report.contains(QStringLiteral("XDG_CURRENT_DESKTOP")));
        QVERIFY(report.contains(QStringLiteral("- Autostart Supported: No")));
#endif
    }

    void testExportReport()
    {
        Diagnostics diag;
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());

        const QString targetPath = tempDir.filePath(QStringLiteral("diagnostics.txt"));
        const QUrl targetUrl = QUrl::fromLocalFile(targetPath);

        QVERIFY(diag.exportReport(targetUrl));

        QFile file(targetPath);
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString content = QString::fromUtf8(file.readAll());
        QVERIFY(content.startsWith(QStringLiteral("# Sugo Diagnostics Report")));
    }

    void testRedactsUserAtHostAndUserName()
    {
        QCOMPARE(Diagnostics::redact(QStringLiteral("ssh alice@workstation.local failed")),
                 QStringLiteral("ssh <user>@workstation.local failed"));
        QCOMPARE(Diagnostics::redact(QStringLiteral("owner /home/bob")), QStringLiteral("owner /home/<user>"));

        const QString user = qEnvironmentVariable("USER");
        if (user.length() > 2) {
            const QString redacted = Diagnostics::redact(QStringLiteral("started by %1 today").arg(user));
            QVERIFY(!redacted.contains(user));
        }
    }

    void testRedactsCalendarSecrets()
    {
        const QStringList inputs = {
            QStringLiteral("Authorization: Basic YWxpY2U6c2VjcmV0cGFzcw=="),
            QStringLiteral("PUT https://alice:hunter2pass@cloud.example.org/remote.php/dav failed"),
            QStringLiteral("AppPassword=hunter2pass saved"),
            QStringLiteral("{\"appPassword\":\"hunter2pass\",\"loginName\":\"x\"}"),
            QStringLiteral("body token=hunter2pass&x=1"),
        };
        for (const QString &input : inputs) {
            const QString redacted = Diagnostics::redact(input);
            QVERIFY2(!redacted.contains(QStringLiteral("hunter2pass")), qPrintable(redacted));
            QVERIFY2(!redacted.contains(QStringLiteral("YWxpY2U6")), qPrintable(redacted));
            QVERIFY2(!redacted.contains(QStringLiteral("alice:")), qPrintable(redacted));
        }
        // Ordinary text is left alone.
        QCOMPARE(Diagnostics::redact(QStringLiteral("Connected to https://cloud.example.org/x")),
                 QStringLiteral("Connected to https://cloud.example.org/x"));
    }

    void testRedactsPasswordsWithSpacesAndTheCalendarAccount()
    {
        // A password with spaces is redacted whole, quoted or not.
        for (const QString &input : {QStringLiteral("password=correct horse battery"),
                                     QStringLiteral("{\"password\": \"correct horse battery\"}"),
                                     QStringLiteral("password='correct horse battery' then")}) {
            const QString redacted = Diagnostics::redact(input);
            QVERIFY2(!redacted.contains(QStringLiteral("horse")) && !redacted.contains(QStringLiteral("battery")), qPrintable(redacted));
        }

        // The calendar account (its login can differ from $USER) and server are redacted too.
        Diagnostics::setAccountRedactions({QStringLiteral("nc-login-42"), QStringLiteral("cloud.example.org")});
        const QString redacted = Diagnostics::redact(
            QStringLiteral("PUT https://cloud.example.org/remote.php/dav/calendars/nc-login-42/personal/x.ics: 500"));
        QVERIFY2(!redacted.contains(QStringLiteral("nc-login-42")), qPrintable(redacted));
        QVERIFY2(!redacted.contains(QStringLiteral("cloud.example.org")), qPrintable(redacted));
        QVERIFY(redacted.contains(QStringLiteral("/remote.php/dav/calendars/")));
        Diagnostics::setAccountRedactions({});
        QVERIFY(Diagnostics::redact(QStringLiteral("cloud.example.org")).contains(QStringLiteral("cloud.example.org")));

        // An account word is redacted only where it stands on its own, in any case.
        Diagnostics::setAccountRedactions({QStringLiteral("tim")});
        QCOMPARE(Diagnostics::redact(QStringLiteral("Timer for tim started")), QStringLiteral("Timer for <account> started"));
        QCOMPARE(Diagnostics::redact(QStringLiteral("Tim: login failed")), QStringLiteral("<account>: login failed"));
        Diagnostics::setAccountRedactions({});
    }

    void testExportRejectsNonLocalUrls()
    {
        Diagnostics diag;
        QVERIFY(!diag.exportReport(QUrl(QStringLiteral("https://example.com/report.txt"))));
        QVERIFY(!diag.lastExportError().isEmpty());
    }

    void testExportReportsWriteFailures()
    {
        Diagnostics diag;
        QTemporaryDir tempDir;
        QVERIFY(tempDir.isValid());
        const QString missingDir = tempDir.filePath(QStringLiteral("no/such/dir/report.txt"));
        QVERIFY(!diag.exportReport(QUrl::fromLocalFile(missingDir)));
        QVERIFY(!diag.lastExportError().isEmpty());

        // A success clears the error; a plain absolute path works too.
        const QString target = tempDir.filePath(QStringLiteral("report.txt"));
        QVERIFY(diag.exportReport(QUrl(target)));
        QVERIFY(diag.lastExportError().isEmpty());
        QVERIFY(QFile::exists(target));
    }

    void testAttachedReport()
    {
        std::unique_ptr<Database> db = std::make_unique<Database>(QStringLiteral(":memory:"));
        QVERIFY(db->open());
        SessionRepository sessions(db->connectionName());
        PresetRepository presetsRepo(db->connectionName());
        PresetModel presets;
        StatsModel stats;
        DataManager dataManager(db.get(), &sessions, &presetsRepo, &presets, &stats);
        QTemporaryDir configDir;
        AppSettings settings(Settings::open(configDir.filePath(QStringLiteral("settings.conf"))), nullptr, false);

        Diagnostics diag;
        diag.attach(&settings, &dataManager);

        const QString report = diag.generateReport();
        QVERIFY(report.contains(QStringLiteral("Total Recorded Sessions: 0")));
        QVERIFY(report.contains(QStringLiteral("Sound Theme: ")));
    }

    void testReportRedactsSoundFilePaths()
    {
        QTemporaryDir configDir;
        AppSettings settings(Settings::open(configDir.filePath(QStringLiteral("settings.conf"))), nullptr, false);
        // Sounds may be files of the user's, given by their absolute path.
        settings.setSoundTheme(QStringLiteral("/home/alice/Music/bell.oga"));
        settings.setPreAlarmSound(QDir::homePath() + QStringLiteral("/Sounds/chime.ogg"));

        Diagnostics diag;
        diag.attach(&settings, nullptr);
        const QString report = diag.generateReport();
        QVERIFY(report.contains(QStringLiteral("Sound Theme: /home/<user>/Music/bell.oga")));
        QVERIFY(!report.contains(QStringLiteral("alice")));
        QVERIFY(report.contains(QStringLiteral("~/Sounds/chime.ogg")));
        QVERIFY(!report.contains(QDir::homePath() + QStringLiteral("/Sounds")));
    }
};

QTEST_MAIN(DiagnosticsTest)
#include "tst_diagnostics.moc"
