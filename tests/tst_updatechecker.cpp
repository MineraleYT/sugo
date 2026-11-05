// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "AppSettings.h"
#include "Autostart.h"
#include "MacBundle.h"
#include "Settings.h"
#include "UpdateChecker.h"
#include "UpdateInstaller.h"

namespace
{
bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/// A folder with an "installed" AppImage, a newer build to download, and the feed announcing it,
/// all as local files.
struct LocalRelease {
    QTemporaryDir dir;
    QString appImage = dir.filePath(QStringLiteral("Sugo-x86_64.AppImage"));
    QString build = dir.filePath(QStringLiteral("download/Sugo-x86_64.AppImage"));
    QString feed = dir.filePath(QStringLiteral("feed.json"));
    QByteArray oldContent = QByteArrayLiteral("old AppImage");
    QByteArray newContent = QByteArray(200000, 'n');

    LocalRelease()
    {
        QDir(dir.path()).mkdir(QStringLiteral("download"));
        writeFile(appImage, oldContent);
        QFile::setPermissions(appImage, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                            | QFileDevice::ExeGroup);
        writeFile(build, newContent);
        announce(QStringLiteral("2.0.0"));
    }

    QString downloadPrefix() const { return QUrl::fromLocalFile(dir.filePath(QStringLiteral("download"))).toString() + QLatin1Char('/'); }

    void announce(const QString &version, QJsonObject changes = {})
    {
        QJsonObject feedObject{
            {QStringLiteral("version"), version},
            {QStringLiteral("url"), QUrl::fromLocalFile(build).toString()},
            {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(newContent, QCryptographicHash::Sha256).toHex())},
            {QStringLiteral("size"), qint64(newContent.size())},
            {QStringLiteral("page"), QString(QStringLiteral("https://github.com/MineraleYT/sugo/releases/tag/v") + version)},
        };
        for (auto it = changes.begin(); it != changes.end(); ++it) {
            feedObject.insert(it.key(), it.value());
        }
        writeFile(feed, QJsonDocument(feedObject).toJson());
    }

    void setUp(UpdateChecker &checker, bool installable = true) const
    {
        if (installable) {
            checker.setAppImagePathForTesting(appImage);
        }
        checker.setFeedUrlForTesting(QUrl::fromLocalFile(feed));
        checker.setDownloadPrefixForTesting(downloadPrefix());
    }

    /// Files in the folder other than the AppImage, the download folder and the feed: what a
    /// download may have left behind.
    QStringList leftovers() const
    {
        QStringList names = QDir(dir.path()).entryList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
        names.removeAll(QStringLiteral("Sugo-x86_64.AppImage"));
        names.removeAll(QStringLiteral("feed.json"));
        return names;
    }
};

/// An Info.plist like the disk image's (installsUpdates) or a build from source's.
QByteArray infoPlist(const QString &identifier, bool installsUpdates = true)
{
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
               "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
               "<plist version=\"1.0\">\n<dict>\n"
               "\t<key>CFBundleDocumentTypes</key>\n\t<array><dict><key>CFBundleIdentifier</key><string>not.this.one</string>"
               "<key>SugoInstallsUpdates</key><true/></dict></array>\n"
               "\t<key>CFBundleExecutable</key>\n\t<string>Sugo</string>\n"
               "\t<key>CFBundleIdentifier</key>\n\t<string>%1</string>\n"
               "\t<key>SugoInstallsUpdates</key>\n\t<%2/>\n"
               "</dict>\n</plist>\n")
        .arg(identifier, installsUpdates ? QStringLiteral("true") : QStringLiteral("false"))
        .toUtf8();
}

/// Makes a minimal Sugo.app in `folder` whose executable holds `content`.
bool makeBundle(const QString &folder, const QByteArray &content, const QString &identifier = QStringLiteral("io.github.mineraleyt.sugo"))
{
    const QString app = folder + QStringLiteral("/Sugo.app");
    return QDir().mkpath(app + QStringLiteral("/Contents/MacOS")) && writeFile(app + QStringLiteral("/Contents/Info.plist"), infoPlist(identifier))
        && writeFile(app + QStringLiteral("/Contents/MacOS/Sugo"), content);
}

/**
 * An "installed" Sugo.app in a folder of applications, a newer one in a "disk image" (a tar file
 * the stand-in hdiutil unpacks at the mount point), and stand-ins for hdiutil, codesign, ditto,
 * xattr and open that log their calls.
 */
struct LocalMacRelease {
    QTemporaryDir dir;
    QString applications = dir.filePath(QStringLiteral("Applications"));
    QString bundle = applications + QStringLiteral("/Sugo.app");
    QString tools = dir.filePath(QStringLiteral("tools"));
    QString dmg = dir.filePath(QStringLiteral("download/Sugo-macOS-universal.dmg"));
    QString feed = dir.filePath(QStringLiteral("feed.json"));
    QByteArray oldContent = QByteArrayLiteral("old Sugo");
    QByteArray newContent = QByteArrayLiteral("new Sugo");
    QByteArray dmgContent;

    explicit LocalMacRelease(const QString &identifier = QStringLiteral("io.github.mineraleyt.sugo"))
    {
        QDir root(dir.path());
        root.mkpath(QStringLiteral("download"));
        root.mkpath(QStringLiteral("tools"));
        root.mkpath(QStringLiteral("staging"));
        makeBundle(applications, oldContent);
        makeBundle(dir.filePath(QStringLiteral("staging")), newContent, identifier);
        QProcess tar;
        tar.start(QStringLiteral("tar"), {QStringLiteral("-cf"), dmg, QStringLiteral("-C"), dir.filePath(QStringLiteral("staging")), QStringLiteral("Sugo.app")});
        tar.waitForFinished();
        dmgContent = readFile(dmg);

        const QByteArray header = QByteArrayLiteral("#!/bin/sh\nd=$(dirname \"$0\")\necho \"$(basename \"$0\") $*\" >> \"$d/calls.log\"\n");
        tool(QStringLiteral("hdiutil"),
             header
                 + "if [ \"$1\" = attach ]; then\n"
                   "  [ -e \"$d/attach-fails\" ] && { echo 'hdiutil: attach failed - no mountable file systems'; exit 1; }\n"
                   "  tar -xf \"$7\" -C \"$6\" || exit 1\n"
                   "elif [ \"$1\" = detach ]; then\n"
                   "  rm -rf \"$2/Sugo.app\"\n"
                   "fi\n");
        tool(QStringLiteral("codesign"), header + "[ -e \"$d/codesign-fails\" ] && { echo 'a sealed resource is missing or invalid'; exit 1; }\nexit 0\n");
        tool(QStringLiteral("ditto"), header + "[ -e \"$d/ditto-fails\" ] && { echo 'ditto: No space left on device'; exit 1; }\ncp -R \"$1\" \"$2\"\n");
        tool(QStringLiteral("xattr"), header + "exit 0\n");
        tool(QStringLiteral("open"), header + "exit 0\n");
        announce(QStringLiteral("2.0.0"));
    }

    void tool(const QString &name, const QByteArray &script) const
    {
        const QString path = tools + QLatin1Char('/') + name;
        writeFile(path, script);
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    void failStep(const QString &step) const { writeFile(tools + QLatin1Char('/') + step + QStringLiteral("-fails"), QByteArray()); }

    QStringList calls() const { return QString::fromUtf8(readFile(tools + QStringLiteral("/calls.log"))).split(QLatin1Char('\n'), Qt::SkipEmptyParts); }

    /// The feed: top-level keys for the AppImage (a file the Mac must not take), "macos" for the disk image.
    void announce(const QString &version, const QJsonObject &macosChanges = {}, bool withMacos = true)
    {
        QJsonObject macos{
            {QStringLiteral("url"), QUrl::fromLocalFile(dmg).toString()},
            {QStringLiteral("sha256"), QString::fromLatin1(QCryptographicHash::hash(dmgContent, QCryptographicHash::Sha256).toHex())},
            {QStringLiteral("size"), qint64(dmgContent.size())},
        };
        for (auto it = macosChanges.begin(); it != macosChanges.end(); ++it) {
            macos.insert(it.key(), it.value());
        }
        QJsonObject feedObject{
            {QStringLiteral("version"), version},
            {QStringLiteral("url"), QUrl::fromLocalFile(dir.filePath(QStringLiteral("download/Sugo-x86_64.AppImage"))).toString()},
            {QStringLiteral("sha256"), QString(64, QLatin1Char('a'))},
            {QStringLiteral("size"), 1234},
            {QStringLiteral("page"), QString(QStringLiteral("https://github.com/MineraleYT/sugo/releases/tag/v") + version)},
        };
        if (withMacos) {
            feedObject.insert(QStringLiteral("macos"), macos);
        }
        writeFile(feed, QJsonDocument(feedObject).toJson());
    }

    MacBundleInstaller::Tools standIns(qint64 waitPid = 0) const
    {
        MacBundleInstaller::Tools t;
        t.hdiutil = tools + QStringLiteral("/hdiutil");
        t.codesign = tools + QStringLiteral("/codesign");
        t.ditto = tools + QStringLiteral("/ditto");
        t.xattr = tools + QStringLiteral("/xattr");
        t.open = tools + QStringLiteral("/open");
        t.waitPid = waitPid;
        return t;
    }

    void setUp(UpdateChecker &checker, qint64 waitPid = 0) const
    {
        checker.setInstallerForTesting(std::make_unique<MacBundleInstaller>(bundle, standIns(waitPid)));
        checker.setFeedUrlForTesting(QUrl::fromLocalFile(feed));
        checker.setDownloadPrefixForTesting(QUrl::fromLocalFile(dir.filePath(QStringLiteral("download"))).toString() + QLatin1Char('/'));
    }

    QString executable(const QString &app) const { return QString::fromUtf8(readFile(app + QStringLiteral("/Contents/MacOS/Sugo"))); }

    /// What the folder of applications holds besides Sugo.app.
    QStringList leftovers() const
    {
        QStringList names = QDir(applications).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        names.removeAll(QStringLiteral("Sugo.app"));
        return names;
    }

    /// The mount point the stand-in hdiutil was given, which must be gone again.
    QString mountPoint() const
    {
        for (const QString &call : calls()) {
            const QStringList words = call.split(QLatin1Char(' '));
            const int i = words.indexOf(QStringLiteral("-mountpoint"));
            if (i >= 0 && i + 1 < words.size()) {
                return words.at(i + 1);
            }
        }
        return QString();
    }
};

/// The id of a process that has ended: what the restart waits for in the tests.
qint64 endedProcessId()
{
    QProcess process;
    process.start(QStringLiteral("true"), {});
    process.waitForStarted();
    const qint64 pid = process.processId();
    process.waitForFinished();
    return pid;
}
} // namespace

class UpdateCheckerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void compareVersionsSame()
    {
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("v1.0.2"), QStringLiteral("1.0.2")), 0);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.0.2"), QStringLiteral("1.0.2")), 0);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.0"), QStringLiteral("1.0.0")), 0);
    }

    void compareVersionsNewer()
    {
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("v1.0.3"), QStringLiteral("v1.0.2")), 1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.1.0"), QStringLiteral("1.0.9")), 1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("2.0.0"), QStringLiteral("1.99.9")), 1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("v1.0.2.1"), QStringLiteral("v1.0.2")), 1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("v1.0.3-beta"), QStringLiteral("1.0.2")), 1);
    }

    void compareVersionsOlder()
    {
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("v1.0.1"), QStringLiteral("v1.0.2")), -1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("0.9.9"), QStringLiteral("1.0.0")), -1);
    }

    void initialPropertiesAndReset()
    {
        UpdateChecker checker;
        QCOMPARE(checker.status(), UpdateChecker::Status::Idle);
        QVERIFY(!checker.isChecking());
        QVERIFY(!checker.hasUpdate());
        QVERIFY(!checker.currentVersion().isEmpty());
        QVERIFY(checker.latestVersion().isEmpty());
        QVERIFY(checker.errorMessage().isEmpty());

        checker.reset();
        QCOMPARE(checker.status(), UpdateChecker::Status::Idle);
    }

    void overrideVersionEnvVar()
    {
        qputenv("SUGO_OVERRIDE_VERSION", "0.9.0");
        UpdateChecker checker;
        QCOMPARE(checker.currentVersion(), QStringLiteral("0.9.0"));
        qunsetenv("SUGO_OVERRIDE_VERSION");
    }

    void longNumbersCompareByValue()
    {
        // Numbers past 18 digits (or past int) are compared exactly, not by a cut-off prefix.
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.0.0-rc1234567890123456789"),
                                                QStringLiteral("1.0.0-rc1234567890123456780")), 1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.99999999999"), QStringLiteral("1.99999999998")), 1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.10"), QStringLiteral("1.9")), 1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.010"), QStringLiteral("1.10")), 0);
    }

    void onlyVersionLikeTagsAreAccepted()
    {
        QVERIFY(UpdateChecker::isPlausibleVersionTag(QStringLiteral("v1.0.2")));
        QVERIFY(UpdateChecker::isPlausibleVersionTag(QStringLiteral("1.1.0-rc.1+build5")));
        QVERIFY(!UpdateChecker::isPlausibleVersionTag(QStringLiteral("latest")));
        // Empty pre-release or build identifiers would compare as the release itself.
        QVERIFY(UpdateChecker::isPlausibleVersionTag(QStringLiteral("1.0.1-nightly.20261031.42")));
        QVERIFY(!UpdateChecker::isPlausibleVersionTag(QStringLiteral("1.0.0-.")));
        QVERIFY(!UpdateChecker::isPlausibleVersionTag(QStringLiteral("1.0.0-")));
        QVERIFY(!UpdateChecker::isPlausibleVersionTag(QStringLiteral("1.0.0-rc..1")));
        QVERIFY(!UpdateChecker::isPlausibleVersionTag(QStringLiteral("1.0.0-rc.")));
        QVERIFY(!UpdateChecker::isPlausibleVersionTag(QStringLiteral("1.0.0+.")));
        QVERIFY(!UpdateChecker::isPlausibleVersionTag(QStringLiteral("v1.0 <b>click here</b>")));
        QVERIFY(!UpdateChecker::isPlausibleVersionTag(QStringLiteral("v1.") + QString(200, QLatin1Char('1'))));
    }

    void releaseIsNewerThanItsPreReleases()
    {
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.1.0"), QStringLiteral("1.1.0-rc1")), 1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("v1.1.0-rc1"), QStringLiteral("1.1.0")), -1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.1.0-rc1"), QStringLiteral("1.0.9")), 1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.1.0-rc2"), QStringLiteral("1.1.0-rc10")), -1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.1.0-beta"), QStringLiteral("1.1.0-rc1")), -1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.1.0-rc.1"), QStringLiteral("1.1.0-rc.1.1")), -1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.1.0-rc1"), QStringLiteral("1.1.0-rc1")), 0);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.1.0+build5"), QStringLiteral("1.1.0")), 0);
    }

    void preReleasesDifferingInCaseCompareOneWay()
    {
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.0.0-rcA"), QStringLiteral("1.0.0-rca")), -1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.0.0-rca"), QStringLiteral("1.0.0-rcA")), 1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.0.0-RC.1"), QStringLiteral("1.0.0-rc.1")), -1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.0.0-rc.1"), QStringLiteral("1.0.0-RC.1")), 1);
        // Otherwise letters still compare regardless of case.
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.0.0-Beta"), QStringLiteral("1.0.0-rc1")), -1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.0.0-rc1"), QStringLiteral("1.0.0-Beta")), 1);
    }

    void releaseUrlMustPointIntoTheProjectOnGitHub()
    {
        const QString fallback = QStringLiteral("https://github.com/MineraleYT/sugo/releases");
        const QString good = QStringLiteral("https://github.com/MineraleYT/sugo/releases/tag/v1.1.0");
        QCOMPARE(UpdateChecker::safeReleaseUrl(good), good);
        QCOMPARE(UpdateChecker::safeReleaseUrl(QString()), fallback);
        QCOMPARE(UpdateChecker::safeReleaseUrl(QStringLiteral("http://github.com/MineraleYT/sugo/releases/tag/v1")), fallback);
        QCOMPARE(UpdateChecker::safeReleaseUrl(QStringLiteral("https://evil.example/MineraleYT/sugo/releases")), fallback);
        QCOMPARE(UpdateChecker::safeReleaseUrl(QStringLiteral("https://github.com.evil.example/MineraleYT/sugo/")), fallback);
        QCOMPARE(UpdateChecker::safeReleaseUrl(QStringLiteral("https://user@github.com/MineraleYT/sugo/releases")), fallback);
        QCOMPARE(UpdateChecker::safeReleaseUrl(QStringLiteral("javascript:alert(1)")), fallback);
        QCOMPARE(UpdateChecker::safeReleaseUrl(QStringLiteral("https://github.com/someone/else/releases")), fallback);
    }

    void feedsOfTheChannels()
    {
        QCOMPARE(UpdateChecker::feedUrl(QStringLiteral("stable")),
                 QUrl(QStringLiteral("https://github.com/MineraleYT/sugo/releases/latest/download/stable.json")));
        QCOMPARE(UpdateChecker::feedUrl(QStringLiteral("nightly")),
                 QUrl(QStringLiteral("https://github.com/MineraleYT/sugo/releases/download/nightly/nightly.json")));
        QCOMPARE(UpdateChecker::feedUrl(QStringLiteral("anything")), UpdateChecker::feedUrl(QStringLiteral("stable")));
    }

    void onlyAnAppImageInstallsOrFollowsNightly()
    {
        LocalRelease release;
        UpdateChecker other; // a Flatpak, a distribution's package, a build from source
        other.setAppImagePathForTesting(QString());
        other.setChannel(QStringLiteral("nightly"));
        QVERIFY(!other.canInstall());
        QCOMPARE(other.channel(), QStringLiteral("stable"));

        UpdateChecker appImage;
        appImage.setAppImagePathForTesting(release.appImage);
        QVERIFY(appImage.canInstall());
        appImage.setChannel(QStringLiteral("nightly"));
        QCOMPARE(appImage.channel(), QStringLiteral("nightly"));
        appImage.setChannel(QStringLiteral("beta"));
        QCOMPARE(appImage.channel(), QStringLiteral("stable"));

        UpdateChecker missing;
        missing.setAppImagePathForTesting(release.dir.filePath(QStringLiteral("gone.AppImage")));
        QVERIFY(!missing.canInstall());
    }

    void readOnlyAppImageIsNotInstalled()
    {
        // QSaveFile would refuse to replace it, in a folder it can write to or not: no "Install".
        LocalRelease release;
        QVERIFY(QFile::setPermissions(release.appImage, QFileDevice::ReadOwner | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                                                            | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther));
        if (QFileInfo(release.appImage).isWritable()) {
            QSKIP("Running as a user who can write to any file (root)");
        }
        QVERIFY(QFileInfo(release.dir.path()).isWritable());
        UpdateChecker checker;
        checker.setAppImagePathForTesting(release.appImage);
        QVERIFY(!checker.canInstall());
        checker.setChannel(QStringLiteral("nightly"));
        QCOMPARE(checker.channel(), QStringLiteral("stable"));
        QVERIFY(!AppImageInstaller(release.appImage).canInstall());
    }

    void sameOrOlderFeedIsUpToDate()
    {
        qputenv("SUGO_OVERRIDE_VERSION", "2.0.0");
        LocalRelease release;
        UpdateChecker checker;
        release.setUp(checker);
        checker.checkForUpdates();
        QTRY_COMPARE(checker.status(), UpdateChecker::Status::UpToDate);
        QCOMPARE(checker.latestVersion(), QStringLiteral("2.0.0"));
        QCOMPARE(readFile(release.appImage), release.oldContent);
        qunsetenv("SUGO_OVERRIDE_VERSION");
    }

    void nightlySitsBetweenTwoReleases()
    {
        // The nightly of the next version is newer than the release before it and older than
        // the release itself: switching back to stable waits for that release.
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.2.1-nightly.20261031.42"), QStringLiteral("1.2.0")), 1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.2.1-nightly.20261031.42"), QStringLiteral("1.2.1")), -1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.2.1-nightly.20261101.43"), QStringLiteral("1.2.1-nightly.20261031.42")), 1);
        QCOMPARE(UpdateChecker::compareVersions(QStringLiteral("1.2.1-nightly.20261031.100"), QStringLiteral("1.2.1-nightly.20261031.99")), 1);
    }

    void otherPackagesOnlyReportTheUpdate()
    {
        qputenv("SUGO_OVERRIDE_VERSION", "1.0.0");
        LocalRelease release;
        UpdateChecker checker;
        release.setUp(checker, false);
        checker.setAutoCheck(true);
        checker.checkForUpdates();
        QTRY_COMPARE(checker.status(), UpdateChecker::Status::UpdateAvailable);
        QCOMPARE(checker.latestVersion(), QStringLiteral("2.0.0"));
        QCOMPARE(checker.releaseUrl(), QStringLiteral("https://github.com/MineraleYT/sugo/releases/tag/v2.0.0"));
        checker.installUpdate(); // nothing to replace
        QCOMPARE(checker.status(), UpdateChecker::Status::UpdateAvailable);
        qunsetenv("SUGO_OVERRIDE_VERSION");
    }

    void installReplacesTheAppImage()
    {
        qputenv("SUGO_OVERRIDE_VERSION", "1.0.0");
        LocalRelease release;
        UpdateChecker checker;
        release.setUp(checker);
        checker.checkForUpdates();
        QTRY_COMPARE(checker.status(), UpdateChecker::Status::UpdateAvailable);
        QCOMPARE(readFile(release.appImage), release.oldContent); // not without being asked

        QSignalSpy progress(&checker, &UpdateChecker::progressChanged);
        checker.installUpdate();
        QTRY_COMPARE_WITH_TIMEOUT(checker.status(), UpdateChecker::Status::ReadyToRestart, 10000);
        QCOMPARE(readFile(release.appImage), release.newContent);
        QVERIFY(QFile::permissions(release.appImage) & QFileDevice::ExeOwner);
        QVERIFY(QFile::permissions(release.appImage) & QFileDevice::ExeGroup);
        QCOMPARE(checker.progress(), 1.0);
        QVERIFY(progress.count() >= 2);
        QVERIFY2(release.leftovers().isEmpty(), qPrintable(release.leftovers().join(QLatin1Char(' '))));

        // Waiting for the restart: no new check, no reset, and a restart only when asked.
        QVERIFY(!checker.restartOnQuit());
        checker.setRestartOnQuit(true);
        QVERIFY(checker.restartOnQuit());
        checker.checkForUpdates();
        checker.reset();
        QCOMPARE(checker.status(), UpdateChecker::Status::ReadyToRestart);
        checker.setRestartOnQuit(false); // the quit was cancelled
        QVERIFY(!checker.launchUpdated());
        qunsetenv("SUGO_OVERRIDE_VERSION");
    }

    void macosEntryInTheFeedIsIgnored()
    {
        // The feeds also describe the macOS disk image under "macos": the AppImage keeps reading
        // the top-level keys, and installs the file they announce.
        qputenv("SUGO_OVERRIDE_VERSION", "1.0.0");
        LocalRelease release;
        const QJsonObject macos{
            {QStringLiteral("url"), QStringLiteral("https://github.com/MineraleYT/sugo/releases/download/v2.0.0/Sugo-macOS-universal.dmg")},
            {QStringLiteral("sha256"), QString(64, QLatin1Char('0'))},
            {QStringLiteral("size"), 1000},
        };
        release.announce(QStringLiteral("2.0.0"), QJsonObject{{QStringLiteral("macos"), macos}});
        UpdateChecker checker;
        release.setUp(checker);
        checker.checkForUpdates();
        QTRY_COMPARE(checker.status(), UpdateChecker::Status::UpdateAvailable);
        QCOMPARE(checker.latestVersion(), QStringLiteral("2.0.0"));
        QCOMPARE(checker.releaseUrl(), QStringLiteral("https://github.com/MineraleYT/sugo/releases/tag/v2.0.0"));
        checker.installUpdate();
        QTRY_COMPARE_WITH_TIMEOUT(checker.status(), UpdateChecker::Status::ReadyToRestart, 10000);
        QCOMPARE(readFile(release.appImage), release.newContent);
        qunsetenv("SUGO_OVERRIDE_VERSION");
    }

    void automaticChecksNeverDownload()
    {
        qputenv("SUGO_OVERRIDE_VERSION", "1.0.0");
        LocalRelease release;
        UpdateChecker checker;
        release.setUp(checker);
        checker.setAutoCheck(true);
        checker.checkForUpdates();
        QTRY_COMPARE(checker.status(), UpdateChecker::Status::UpdateAvailable);
        QTest::qWait(300);
        QCOMPARE(checker.status(), UpdateChecker::Status::UpdateAvailable);
        QCOMPARE(readFile(release.appImage), release.oldContent);
        QVERIFY(release.leftovers().isEmpty());
        qunsetenv("SUGO_OVERRIDE_VERSION");
    }

    void periodicCheckRefreshesAFoundUpdate()
    {
        qputenv("SUGO_OVERRIDE_VERSION", "1.0.0");
        LocalRelease release;
        UpdateChecker checker;
        release.setUp(checker);
        checker.setAutoCheck(true);
        checker.checkForUpdates();
        QTRY_COMPARE(checker.status(), UpdateChecker::Status::UpdateAvailable);
        QCOMPARE(checker.latestVersion(), QStringLiteral("2.0.0"));

        // The update stays offered while the feed is read again, and a newer one replaces it.
        QList<UpdateChecker::Status> seen;
        connect(&checker, &UpdateChecker::statusChanged, &checker, [&]() {
            seen.append(checker.status());
        });
        release.announce(QStringLiteral("2.0.1"));
        checker.scheduleChecksForTesting(50, 50);
        QTRY_COMPARE(checker.latestVersion(), QStringLiteral("2.0.1"));
        QCOMPARE(checker.releaseUrl(), QStringLiteral("https://github.com/MineraleYT/sugo/releases/tag/v2.0.1"));
        QVERIFY(checker.isInstallable());

        // A feed that cannot be read keeps what was found.
        writeFile(release.feed, QByteArrayLiteral("not json"));
        QTest::qWait(300);
        QCOMPARE(checker.status(), UpdateChecker::Status::UpdateAvailable);
        QCOMPARE(checker.latestVersion(), QStringLiteral("2.0.1"));
        QVERIFY(checker.isInstallable());
        QVERIFY(checker.errorMessage().isEmpty());
        for (const UpdateChecker::Status status : std::as_const(seen)) {
            QCOMPARE(status, UpdateChecker::Status::UpdateAvailable);
        }
        QCOMPARE(readFile(release.appImage), release.oldContent); // never downloaded unasked

        checker.installUpdate();
        QTRY_COMPARE_WITH_TIMEOUT(checker.status(), UpdateChecker::Status::ReadyToRestart, 10000);
        QCOMPARE(readFile(release.appImage), release.newContent);
        qunsetenv("SUGO_OVERRIDE_VERSION");
    }

    void damagedDownloadIsNotInstalled_data()
    {
        QTest::addColumn<QJsonObject>("changes");
        QTest::addColumn<UpdateChecker::Status>("afterCheck");
        const QString zeros(64, QLatin1Char('0'));
        QTest::newRow("wrong checksum") << QJsonObject{{QStringLiteral("sha256"), zeros}} << UpdateChecker::Status::UpdateAvailable;
        QTest::newRow("larger than announced") << QJsonObject{{QStringLiteral("size"), 1000}} << UpdateChecker::Status::UpdateAvailable;
        QTest::newRow("smaller than announced") << QJsonObject{{QStringLiteral("size"), 300000}} << UpdateChecker::Status::UpdateAvailable;
        QTest::newRow("no checksum") << QJsonObject{{QStringLiteral("sha256"), QString()}} << UpdateChecker::Status::Error;
        QTest::newRow("absurd size") << QJsonObject{{QStringLiteral("size"), qint64(1) << 40}} << UpdateChecker::Status::Error;
        QTest::newRow("foreign address") << QJsonObject{{QStringLiteral("url"), QStringLiteral("file:///etc/passwd")}} << UpdateChecker::Status::Error;
    }

    void damagedDownloadIsNotInstalled()
    {
        QFETCH(QJsonObject, changes);
        QFETCH(UpdateChecker::Status, afterCheck);
        qputenv("SUGO_OVERRIDE_VERSION", "1.0.0");
        LocalRelease release;
        release.announce(QStringLiteral("2.0.0"), changes);
        UpdateChecker checker;
        release.setUp(checker);
        checker.checkForUpdates();
        QTRY_COMPARE(checker.status(), afterCheck);
        if (afterCheck == UpdateChecker::Status::UpdateAvailable) {
            checker.installUpdate();
            QTRY_COMPARE_WITH_TIMEOUT(checker.status(), UpdateChecker::Status::Error, 10000);
        }
        QVERIFY(!checker.errorMessage().isEmpty());
        QCOMPARE(readFile(release.appImage), release.oldContent);
        QVERIFY2(release.leftovers().isEmpty(), qPrintable(release.leftovers().join(QLatin1Char(' '))));
        qunsetenv("SUGO_OVERRIDE_VERSION");
    }

    void downloadsComeOnlyFromTheProjectsReleases()
    {
        qputenv("SUGO_OVERRIDE_VERSION", "1.0.0");
        LocalRelease release;
        const QString lookalike = QStringLiteral("https://github.com/MineraleYT/sugo-evil/releases/download/x/Sugo.AppImage");
        const QString genuine = QStringLiteral("https://github.com/MineraleYT/sugo/releases/download/v2.0.0/Sugo-x86_64.AppImage");
        for (const QString &url : {lookalike, genuine}) {
            release.announce(QStringLiteral("2.0.0"), {{QStringLiteral("url"), url}});
            UpdateChecker checker;
            checker.setAppImagePathForTesting(release.appImage);
            checker.setFeedUrlForTesting(QUrl::fromLocalFile(release.feed)); // the real download prefix
            checker.checkForUpdates();
            QTRY_VERIFY(checker.status() != UpdateChecker::Status::Checking);
            QCOMPARE(checker.status(), url == genuine ? UpdateChecker::Status::UpdateAvailable : UpdateChecker::Status::Error);
        }
        qunsetenv("SUGO_OVERRIDE_VERSION");
    }

    void unreadableFeedIsAnError()
    {
        LocalRelease release;
        writeFile(release.feed, QByteArrayLiteral("<html>not json</html>"));
        UpdateChecker checker;
        release.setUp(checker);
        checker.checkForUpdates();
        QTRY_COMPARE(checker.status(), UpdateChecker::Status::Error);

        QFile::remove(release.feed);
        checker.checkForUpdates();
        QTRY_COMPARE(checker.status(), UpdateChecker::Status::Error);
        QVERIFY(!checker.errorMessage().isEmpty());
    }

    void macBundleIsFoundFromTheExecutablesFolder()
    {
        QTemporaryDir dir;
        makeBundle(dir.path(), QByteArrayLiteral("x"));
        const QString bundle = QDir(dir.path()).filePath(QStringLiteral("Sugo.app"));
        QCOMPARE(MacBundle::path(bundle + QStringLiteral("/Contents/MacOS")), bundle);
        QCOMPARE(MacBundle::path(bundle + QStringLiteral("/Contents/MacOS/")), bundle);
        QVERIFY(MacBundle::path(dir.path()).isEmpty()); // a binary outside a bundle
        QVERIFY(MacBundle::path(QString()).isEmpty());
        QFile::remove(bundle + QStringLiteral("/Contents/Info.plist"));
        QVERIFY(MacBundle::path(bundle + QStringLiteral("/Contents/MacOS")).isEmpty());
#ifndef Q_OS_MACOS
        QVERIFY(MacBundle::path().isEmpty());
#endif
    }

    void bundleIdentifierIsReadFromTheTopLevelDict()
    {
        QTemporaryDir dir;
        const QString plist = dir.filePath(QStringLiteral("Info.plist"));
        writeFile(plist, infoPlist(QStringLiteral("io.github.mineraleyt.sugo")));
        QCOMPARE(MacBundleInstaller::bundleIdentifier(plist), QStringLiteral("io.github.mineraleyt.sugo"));
        writeFile(plist, QByteArrayLiteral("bplist00 binary"));
        QVERIFY(MacBundleInstaller::bundleIdentifier(plist).isEmpty());
        QVERIFY(MacBundleInstaller::bundleIdentifier(dir.filePath(QStringLiteral("missing.plist"))).isEmpty());
    }

    void installsUpdatesIsReadFromTheTopLevelDict()
    {
        QTemporaryDir dir;
        const QString plist = dir.filePath(QStringLiteral("Info.plist"));
        writeFile(plist, infoPlist(QStringLiteral("io.github.mineraleyt.sugo"), true));
        QVERIFY(MacBundleInstaller::installsUpdates(plist));
        // Only the top-level key counts (the nested one says true in both).
        writeFile(plist, infoPlist(QStringLiteral("io.github.mineraleyt.sugo"), false));
        QVERIFY(!MacBundleInstaller::installsUpdates(plist));
        writeFile(plist, QByteArrayLiteral("<plist><dict><key>SugoInstallsUpdates</key><string>true</string></dict></plist>"));
        QVERIFY(!MacBundleInstaller::installsUpdates(plist));
        writeFile(plist, QByteArrayLiteral("<plist><dict><key>CFBundleIdentifier</key><string>io.github.mineraleyt.sugo</string></dict></plist>"));
        QVERIFY(!MacBundleInstaller::installsUpdates(plist)); // an app from before the key
        QVERIFY(!MacBundleInstaller::installsUpdates(dir.filePath(QStringLiteral("missing.plist"))));
    }

    void macBundleBuiltFromSourceOnlyReports()
    {
        // build/bin/Sugo.app: "Install update" would replace the build being worked on with the release.
        LocalMacRelease release;
        writeFile(release.bundle + QStringLiteral("/Contents/Info.plist"), infoPlist(QStringLiteral("io.github.mineraleyt.sugo"), false));
        UpdateChecker checker;
        release.setUp(checker);
        QVERIFY(!checker.canInstall());
        checker.setChannel(QStringLiteral("nightly"));
        QCOMPARE(checker.channel(), QStringLiteral("stable"));
        QVERIFY(!MacBundleInstaller(release.bundle, release.standIns()).canInstall());
    }

    void macBundleInstallsAndFollowsNightly()
    {
        LocalMacRelease release;
        UpdateChecker checker;
        release.setUp(checker);
        QVERIFY(checker.canInstall());
        checker.setChannel(QStringLiteral("nightly"));
        QCOMPARE(checker.channel(), QStringLiteral("nightly"));

        UpdateChecker readOnly; // a bundle in a folder the user cannot write to
        QFile::setPermissions(release.applications, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        readOnly.setInstallerForTesting(std::make_unique<MacBundleInstaller>(release.bundle, release.standIns()));
        const bool writable = QFileInfo(release.applications).isWritable(); // root writes anywhere
        const bool installs = readOnly.canInstall();
        QFile::setPermissions(release.applications, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QCOMPARE(installs, writable);

        UpdateChecker missing;
        missing.setInstallerForTesting(std::make_unique<MacBundleInstaller>(release.applications + QStringLiteral("/Gone.app"), release.standIns()));
        QVERIFY(!missing.canInstall());
    }

    void macInstallReplacesTheBundleOnQuit()
    {
        qputenv("SUGO_OVERRIDE_VERSION", "1.0.0");
        LocalMacRelease release;
        UpdateChecker checker;
        release.setUp(checker, endedProcessId());
        checker.checkForUpdates();
        QTRY_COMPARE(checker.status(), UpdateChecker::Status::UpdateAvailable);
        QVERIFY(checker.isInstallable());
        QVERIFY(release.calls().isEmpty()); // not without being asked

        checker.installUpdate();
        QTRY_COMPARE_WITH_TIMEOUT(checker.status(), UpdateChecker::Status::ReadyToRestart, 20000);
        QCOMPARE(checker.progress(), 1.0);
        // The running bundle is untouched; the checked copy waits next to it.
        QCOMPARE(release.executable(release.bundle), QStringLiteral("old Sugo"));
        QCOMPARE(release.executable(release.bundle + QStringLiteral(".new")), QStringLiteral("new Sugo"));
        QCOMPARE(release.leftovers(), QStringList{QStringLiteral("Sugo.app.new")});

        const QStringList calls = release.calls();
        const QString mount = release.mountPoint();
        QVERIFY(!mount.isEmpty());
        QVERIFY2(!QFileInfo::exists(mount), qPrintable(mount));
        const QString app = mount + QStringLiteral("/Sugo.app");
        const QStringList expected{
            QStringLiteral("hdiutil attach -nobrowse -readonly -noautoopen -mountpoint %1 %2").arg(mount, release.applications + QStringLiteral("/.Sugo.app.update.dmg")),
            QStringLiteral("codesign --verify --deep --strict %1").arg(app),
            QStringLiteral("ditto %1 %2.new").arg(app, release.bundle),
            QStringLiteral("xattr -cr %1.new").arg(release.bundle),
            QStringLiteral("hdiutil detach %1").arg(mount),
        };
        QCOMPARE(calls, expected);

        // "Restart to update": the new bundle takes the old one's place and is opened.
        checker.setRestartOnQuit(true);
        QVERIFY(checker.launchUpdated());
        QCOMPARE(release.executable(release.bundle), QStringLiteral("new Sugo"));
        QTRY_COMPARE_WITH_TIMEOUT(release.calls().constLast(), QStringLiteral("open -n %1").arg(release.bundle), 10000);
        QTRY_VERIFY(release.leftovers().isEmpty()); // the backup removed
        qunsetenv("SUGO_OVERRIDE_VERSION");
    }

    void macUpdateIsInPlaceAfterAPlainQuit()
    {
        qputenv("SUGO_OVERRIDE_VERSION", "1.0.0");
        LocalMacRelease release;
        UpdateChecker checker;
        release.setUp(checker, endedProcessId());
        checker.checkForUpdates();
        QTRY_COMPARE(checker.status(), UpdateChecker::Status::UpdateAvailable);
        checker.installUpdate();
        QTRY_COMPARE_WITH_TIMEOUT(checker.status(), UpdateChecker::Status::ReadyToRestart, 20000);
        QVERIFY(!checker.launchUpdated()); // not started...
        QCOMPARE(release.executable(release.bundle), QStringLiteral("new Sugo")); // ...but installed
        QTRY_VERIFY(release.leftovers().isEmpty());
        QTest::qWait(300);
        QVERIFY(!release.calls().constLast().startsWith(QLatin1String("open")));
        qunsetenv("SUGO_OVERRIDE_VERSION");
    }

    void macFeedWithoutDiskImageIsOnlyReported()
    {
        qputenv("SUGO_OVERRIDE_VERSION", "1.0.0");
        LocalMacRelease release;
        release.announce(QStringLiteral("2.0.0"), {}, false);
        UpdateChecker checker;
        release.setUp(checker);
        checker.checkForUpdates();
        QTRY_COMPARE(checker.status(), UpdateChecker::Status::UpdateAvailable);
        QVERIFY(checker.canInstall());
        QVERIFY(!checker.isInstallable());
        QCOMPARE(checker.releaseUrl(), QStringLiteral("https://github.com/MineraleYT/sugo/releases/tag/v2.0.0"));
        checker.installUpdate();
        QCOMPARE(checker.status(), UpdateChecker::Status::UpdateAvailable);
        QVERIFY(release.leftovers().isEmpty());
        qunsetenv("SUGO_OVERRIDE_VERSION");
    }

    void macFailuresLeaveTheAppAsItWas_data()
    {
        QTest::addColumn<QString>("failingStep");
        QTest::addColumn<QString>("identifier");
        QTest::addColumn<QJsonObject>("changes");
        QTest::addColumn<bool>("attached");
        const QString sugo = QStringLiteral("io.github.mineraleyt.sugo");
        QTest::newRow("disk image does not open") << QStringLiteral("attach") << sugo << QJsonObject{} << false;
        QTest::newRow("another app inside") << QString() << QStringLiteral("com.example.other") << QJsonObject{} << true;
        QTest::newRow("bad signature") << QStringLiteral("codesign") << sugo << QJsonObject{} << true;
        QTest::newRow("copy fails") << QStringLiteral("ditto") << sugo << QJsonObject{} << true;
        QTest::newRow("wrong checksum") << QString() << sugo << QJsonObject{{QStringLiteral("sha256"), QString(64, QLatin1Char('0'))}} << false;
        QTest::newRow("incomplete entry") << QString() << sugo << QJsonObject{{QStringLiteral("size"), 0}} << false;
    }

    void macFailuresLeaveTheAppAsItWas()
    {
        QFETCH(QString, failingStep);
        QFETCH(QString, identifier);
        QFETCH(QJsonObject, changes);
        QFETCH(bool, attached);
        qputenv("SUGO_OVERRIDE_VERSION", "1.0.0");
        LocalMacRelease release(identifier);
        release.announce(QStringLiteral("2.0.0"), changes);
        if (!failingStep.isEmpty()) {
            release.failStep(failingStep);
        }
        UpdateChecker checker;
        release.setUp(checker, endedProcessId());
        checker.checkForUpdates();
        QTRY_VERIFY(checker.status() != UpdateChecker::Status::Checking);
        if (checker.status() == UpdateChecker::Status::UpdateAvailable) {
            checker.installUpdate();
            QTRY_COMPARE_WITH_TIMEOUT(checker.status(), UpdateChecker::Status::Error, 20000);
        }
        QCOMPARE(checker.status(), UpdateChecker::Status::Error);
        QVERIFY(!checker.errorMessage().isEmpty());
        QCOMPARE(release.executable(release.bundle), QStringLiteral("old Sugo"));
        QVERIFY2(release.leftovers().isEmpty(), qPrintable(release.leftovers().join(QLatin1Char(' '))));
        const QStringList calls = release.calls();
        QCOMPARE(calls.filter(QStringLiteral("hdiutil detach")).isEmpty(), !attached);
        if (!calls.isEmpty()) {
            QVERIFY(!QFileInfo::exists(release.mountPoint()));
        }
        QVERIFY(!checker.launchUpdated());
        QCOMPARE(release.executable(release.bundle), QStringLiteral("old Sugo"));
        qunsetenv("SUGO_OVERRIDE_VERSION");
    }

    void macRealToolsInstall()
    {
        // The real hdiutil, codesign and ditto on a disk image made here (never /Applications):
        // run by hand on a Mac with SUGO_TEST_MAC_TOOLS=1.
#ifndef Q_OS_MACOS
        QSKIP("macOS only");
#else
        if (qgetenv("SUGO_TEST_MAC_TOOLS") != "1") {
            QSKIP("set SUGO_TEST_MAC_TOOLS=1 to run the real disk image tools");
        }
        qputenv("SUGO_OVERRIDE_VERSION", "1.0.0");
        LocalMacRelease release;
        const QString staging = release.dir.filePath(QStringLiteral("signed"));
        QVERIFY(makeBundle(staging, QByteArray()));
        const QString executable = staging + QStringLiteral("/Sugo.app/Contents/MacOS/Sugo");
        QFile::remove(executable);
        QVERIFY(QFile::copy(QStringLiteral("/usr/bin/true"), executable));
        const auto run = [](const QString &program, const QStringList &arguments) {
            QProcess process;
            process.setProcessChannelMode(QProcess::MergedChannels);
            process.start(program, arguments);
            const bool ok = process.waitForFinished(120000) && process.exitCode() == 0;
            if (!ok) {
                qWarning().noquote() << program << arguments << process.readAll();
            }
            return ok;
        };
        QVERIFY(run(QStringLiteral("/usr/bin/codesign"), {QStringLiteral("--force"), QStringLiteral("--sign"), QStringLiteral("-"), staging + QStringLiteral("/Sugo.app")}));
        QFile::remove(release.dmg);
        QVERIFY(run(QStringLiteral("/usr/bin/hdiutil"), {QStringLiteral("create"), QStringLiteral("-quiet"), QStringLiteral("-srcfolder"), staging, QStringLiteral("-volname"),
                                                          QStringLiteral("Sugo"), QStringLiteral("-format"), QStringLiteral("UDZO"), release.dmg}));
        release.dmgContent = readFile(release.dmg);
        release.announce(QStringLiteral("2.0.0"));

        MacBundleInstaller::Tools tools; // the real programs, but nothing is opened
        tools.open = QStringLiteral("/usr/bin/true");
        tools.waitPid = endedProcessId();
        UpdateChecker checker;
        checker.setInstallerForTesting(std::make_unique<MacBundleInstaller>(release.bundle, tools));
        checker.setFeedUrlForTesting(QUrl::fromLocalFile(release.feed));
        checker.setDownloadPrefixForTesting(QUrl::fromLocalFile(release.dir.filePath(QStringLiteral("download"))).toString() + QLatin1Char('/'));
        checker.checkForUpdates();
        QTRY_COMPARE(checker.status(), UpdateChecker::Status::UpdateAvailable);
        checker.installUpdate();
        QTRY_VERIFY_WITH_TIMEOUT(checker.status() != UpdateChecker::Status::Downloading, 120000);
        QVERIFY2(checker.status() == UpdateChecker::Status::ReadyToRestart, qPrintable(checker.errorMessage()));
        QCOMPARE(release.leftovers(), QStringList{QStringLiteral("Sugo.app.new")});
        QVERIFY(run(QStringLiteral("/usr/bin/codesign"), {QStringLiteral("--verify"), QStringLiteral("--deep"), QStringLiteral("--strict"), release.bundle + QStringLiteral(".new")}));
        checker.setRestartOnQuit(true);
        QVERIFY(checker.launchUpdated());
        QCOMPARE(readFile(release.bundle + QStringLiteral("/Contents/MacOS/Sugo")), readFile(executable)); // the signed copy
        QTRY_VERIFY(release.leftovers().isEmpty());

        // A disk image that is not Sugo: refused, the app untouched.
        const QByteArray installed = readFile(release.bundle + QStringLiteral("/Contents/MacOS/Sugo"));
        LocalMacRelease other(QStringLiteral("com.example.other"));
        QFile::remove(other.dmg);
        QVERIFY(run(QStringLiteral("/usr/bin/hdiutil"), {QStringLiteral("create"), QStringLiteral("-quiet"), QStringLiteral("-srcfolder"), other.dir.filePath(QStringLiteral("staging")),
                                                          QStringLiteral("-format"), QStringLiteral("UDZO"), other.dmg}));
        other.dmgContent = readFile(other.dmg);
        other.announce(QStringLiteral("2.0.0"));
        UpdateChecker refused;
        refused.setInstallerForTesting(std::make_unique<MacBundleInstaller>(release.bundle, tools));
        refused.setFeedUrlForTesting(QUrl::fromLocalFile(other.feed));
        refused.setDownloadPrefixForTesting(QUrl::fromLocalFile(other.dir.filePath(QStringLiteral("download"))).toString() + QLatin1Char('/'));
        refused.checkForUpdates();
        QTRY_COMPARE(refused.status(), UpdateChecker::Status::UpdateAvailable);
        refused.installUpdate();
        QTRY_COMPARE_WITH_TIMEOUT(refused.status(), UpdateChecker::Status::Error, 120000);
        QCOMPARE(readFile(release.bundle + QStringLiteral("/Contents/MacOS/Sugo")), installed);
        QVERIFY(release.leftovers().isEmpty());
        qunsetenv("SUGO_OVERRIDE_VERSION");
#endif
    }

    void resetDuringACheckIgnoresTheReply()
    {
        UpdateChecker checker;
        checker.checkForUpdates();
        QCOMPARE(checker.status(), UpdateChecker::Status::Checking);
        checker.reset();
        QCOMPARE(checker.status(), UpdateChecker::Status::Idle);
        QTest::qWait(300); // the aborted reply finishes but must not change anything
        QCOMPARE(checker.status(), UpdateChecker::Status::Idle);
        QVERIFY(checker.errorMessage().isEmpty());
    }

    void switchingChannelDropsWhatTheOldOneFound()
    {
        qputenv("SUGO_OVERRIDE_VERSION", "1.0.0");
        LocalRelease release;
        release.announce(QStringLiteral("1.0.1-nightly.20261031.42"));
        QTemporaryDir configDir;
        AppSettings settings(Settings::open(configDir.filePath(QStringLiteral("settings.conf"))), nullptr, false);
        settings.setAutoCheckUpdates(false);
        settings.setUpdateChannel(QStringLiteral("nightly"));
        UpdateChecker checker;
        release.setUp(checker);
        checker.attach(&settings);
        QCOMPARE(checker.channel(), QStringLiteral("nightly"));

        // A check by hand finds a nightly; back on stable, it is no longer offered.
        checker.checkForUpdates();
        QTRY_COMPARE(checker.status(), UpdateChecker::Status::UpdateAvailable);
        QVERIFY(checker.isInstallable());
        settings.setUpdateChannel(QStringLiteral("stable"));
        QCOMPARE(checker.status(), UpdateChecker::Status::Idle);
        QVERIFY(!checker.isInstallable());
        checker.installUpdate();
        QCOMPARE(checker.status(), UpdateChecker::Status::Idle);
        QTest::qWait(300); // and no automatic check either
        QCOMPARE(checker.status(), UpdateChecker::Status::Idle);
        QCOMPARE(readFile(release.appImage), release.oldContent);

        // Its download is given up.
        settings.setUpdateChannel(QStringLiteral("nightly"));
        checker.checkForUpdates();
        QTRY_COMPARE(checker.status(), UpdateChecker::Status::UpdateAvailable);
        checker.installUpdate();
        QCOMPARE(checker.status(), UpdateChecker::Status::Downloading);
        settings.setUpdateChannel(QStringLiteral("stable"));
        QCOMPARE(checker.status(), UpdateChecker::Status::Idle);
        QTest::qWait(300);
        QCOMPARE(checker.status(), UpdateChecker::Status::Idle);
        QCOMPARE(readFile(release.appImage), release.oldContent);
        QVERIFY2(release.leftovers().isEmpty(), qPrintable(release.leftovers().join(QLatin1Char(' '))));

        // So is the answer of a check still running on the old channel.
        settings.setUpdateChannel(QStringLiteral("nightly"));
        checker.checkForUpdates();
        QCOMPARE(checker.status(), UpdateChecker::Status::Checking);
        settings.setUpdateChannel(QStringLiteral("stable"));
        QCOMPARE(checker.status(), UpdateChecker::Status::Idle);
        QTest::qWait(300);
        QCOMPARE(checker.status(), UpdateChecker::Status::Idle);
        QVERIFY(checker.latestVersion().isEmpty());
        qunsetenv("SUGO_OVERRIDE_VERSION");
    }
};

QTEST_GUILESS_MAIN(UpdateCheckerTest)
#include "tst_updatechecker.moc"
