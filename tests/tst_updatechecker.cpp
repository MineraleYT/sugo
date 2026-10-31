// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include "UpdateChecker.h"

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
        checker.setAutoInstall(true);
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

    void autoInstallGoesStraightToTheRestart()
    {
        qputenv("SUGO_OVERRIDE_VERSION", "1.0.0");
        LocalRelease release;
        UpdateChecker checker;
        release.setUp(checker);
        checker.setAutoInstall(true);
        checker.checkForUpdates();
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
};

QTEST_GUILESS_MAIN(UpdateCheckerTest)
#include "tst_updatechecker.moc"
