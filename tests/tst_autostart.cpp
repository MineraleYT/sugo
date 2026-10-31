// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "Autostart.h"
#include "PrivateDBus.h"

namespace
{
QString readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

void writeFile(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text.toUtf8());
}

XdgAutostart make(const QString &dir, const QStringList &command = {QStringLiteral("/opt/sugo/bin/sugo"), QStringLiteral("--background")})
{
    return XdgAutostart(dir, QStringLiteral("io.github.mineraleyt.sugo"), QStringLiteral("Sugo"), QStringLiteral("Pomodoro timer"), command);
}
const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");

/// Stand-in for xdg-desktop-portal's Background interface.
class FakeBackgroundPortal : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Background")

public:
    QList<QVariantMap> requests;
    uint answer = 0; // 0 granted, 1 cancelled

public Q_SLOTS:
    QDBusObjectPath RequestBackground(const QString &parentWindow, const QVariantMap &options)
    {
        Q_UNUSED(parentWindow)
        requests << options;
        QString sender = message().service().mid(1);
        sender.replace(QLatin1Char('.'), QLatin1Char('_'));
        const QString handle = kPortalPath + QStringLiteral("/request/") + sender + QLatin1Char('/')
            + options.value(QStringLiteral("handle_token")).toString();
        QDBusConnection bus = connection();
        const uint code = answer;
        const bool autostart = options.value(QStringLiteral("autostart")).toBool();
        // Answer later, like the real portal after asking the user.
        QTimer::singleShot(50, this, [bus, handle, code, autostart]() mutable {
            QDBusMessage signal = QDBusMessage::createSignal(handle, QStringLiteral("org.freedesktop.portal.Request"),
                                                             QStringLiteral("Response"));
            signal << code << QVariantMap{{QStringLiteral("background"), true}, {QStringLiteral("autostart"), autostart}};
            bus.send(signal);
        });
        return QDBusObjectPath(handle);
    }
};
} // namespace

class AutostartTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void enablingWritesAValidEntry()
    {
        QTemporaryDir tmp;
        XdgAutostart autostart = make(tmp.filePath(QStringLiteral("config/autostart"))); // directory does not exist yet
        QVERIFY(autostart.isSupported());
        QVERIFY(!autostart.isEnabled());

        QVERIFY(autostart.setEnabled(true));
        QVERIFY(autostart.isEnabled());

        const QString text = readAll(autostart.filePath());
        QVERIFY(text.startsWith(QStringLiteral("[Desktop Entry]\n")));
        QVERIFY(text.contains(QStringLiteral("\nType=Application\n")));
        QVERIFY(text.contains(QStringLiteral("\nName=Sugo\n")));
        QVERIFY(text.contains(QStringLiteral("\nIcon=io.github.mineraleyt.sugo\n")));
        QVERIFY(text.contains(QStringLiteral("\nExec=/opt/sugo/bin/sugo --background\n")));
        QVERIFY(text.contains(QStringLiteral("\nX-GNOME-Autostart-enabled=true\n")));
        QVERIFY(autostart.filePath().endsWith(QStringLiteral("/io.github.mineraleyt.sugo.desktop")));
    }

    void disablingRemovesTheEntry()
    {
        QTemporaryDir tmp;
        XdgAutostart autostart = make(tmp.path());
        QVERIFY(autostart.setEnabled(true));
        QVERIFY(autostart.setEnabled(false));
        QVERIFY(!autostart.isEnabled());
        QVERIFY(!QFile::exists(autostart.filePath()));
    }

    void disablingWhenThereIsNothingToRemoveSucceeds()
    {
        QTemporaryDir tmp;
        XdgAutostart autostart = make(tmp.path());
        QVERIFY(autostart.setEnabled(false));
        QVERIFY(!autostart.isEnabled());
    }

    void enablingTwiceKeepsOneEntry()
    {
        QTemporaryDir tmp;
        XdgAutostart autostart = make(tmp.path());
        QVERIFY(autostart.setEnabled(true));
        QVERIFY(autostart.setEnabled(true));
        QCOMPARE(QDir(tmp.path()).entryList(QDir::Files).size(), 1);
    }

    void entryDisabledBySystemSettingsCountsAsDisabled()
    {
        // KDE System Settings turns an entry off by adding Hidden=true.
        QTemporaryDir tmp;
        XdgAutostart autostart = make(tmp.path());
        writeFile(autostart.filePath(), QStringLiteral("[Desktop Entry]\nType=Application\nName=Sugo\nExec=x\nHidden=true\n"));
        QVERIFY(!autostart.isEnabled());

        // Enabling from here must really enable it again.
        QVERIFY(autostart.setEnabled(true));
        QVERIFY(autostart.isEnabled());
        QVERIFY(!readAll(autostart.filePath()).contains(QStringLiteral("Hidden")));
    }

    void entryDisabledByGnomeCountsAsDisabled()
    {
        QTemporaryDir tmp;
        XdgAutostart autostart = make(tmp.path());
        writeFile(autostart.filePath(), QStringLiteral("[Desktop Entry]\nType=Application\nName=Sugo\nExec=x\nX-GNOME-Autostart-enabled=false\n"));
        QVERIFY(!autostart.isEnabled());
    }

    void keysOutsideTheDesktopEntryGroupAreIgnored()
    {
        QTemporaryDir tmp;
        XdgAutostart autostart = make(tmp.path());
        writeFile(autostart.filePath(),
                  QStringLiteral("[Desktop Entry]\nType=Application\nName=Sugo\nExec=x\n\n[Desktop Action other]\nHidden=true\n"));
        QVERIFY(autostart.isEnabled());
    }

    void execLineQuotesWhatTheSpecRequires()
    {
        QCOMPARE(XdgAutostart::execLine({QStringLiteral("/usr/bin/sugo"), QStringLiteral("--background")}),
                 QStringLiteral("/usr/bin/sugo --background"));
        // A space needs quotes.
        QCOMPARE(XdgAutostart::execLine({QStringLiteral("/home/a b/sugo"), QStringLiteral("--background")}),
                 QStringLiteral("\"/home/a b/sugo\" --background"));
        // $ is escaped inside quotes, and the backslash is then doubled by the string escaping.
        QCOMPARE(XdgAutostart::execLine({QStringLiteral("/tmp/$x/k")}), QStringLiteral("\"/tmp/\\\\$x/k\""));
        QCOMPARE(XdgAutostart::execLine({QStringLiteral("a\"b")}), QStringLiteral("\"a\\\\\"b\""));
        QCOMPARE(XdgAutostart::execLine({QStringLiteral("a\\b")}), QStringLiteral("\"a\\\\\\\\b\""));
        // An empty argument must stay an argument.
        QCOMPARE(XdgAutostart::execLine({QStringLiteral("x"), QString()}), QStringLiteral("x \"\""));
        // '%' starts a field code (%f, %u...): a literal one must be doubled, quoted or not.
        QCOMPARE(XdgAutostart::execLine({QStringLiteral("/home/u/100%/sugo"), QStringLiteral("--background")}),
                 QStringLiteral("/home/u/100%%/sugo --background"));
        QCOMPARE(XdgAutostart::execLine({QStringLiteral("/home/u/100% done/sugo")}), QStringLiteral("\"/home/u/100%% done/sugo\""));
    }

    void aSpaceInThePathStillProducesAWorkingEntry()
    {
        QTemporaryDir tmp;
        XdgAutostart autostart = make(tmp.path(), {QStringLiteral("/home/with space/sugo"), QStringLiteral("--background")});
        QVERIFY(autostart.setEnabled(true));
        QVERIFY(autostart.isEnabled());
        QVERIFY(readAll(autostart.filePath()).contains(QStringLiteral("\nExec=\"/home/with space/sugo\" --background\n")));
    }

    void namesCannotInjectOtherKeys()
    {
        QTemporaryDir tmp;
        XdgAutostart autostart(tmp.path(), QStringLiteral("io.github.mineraleyt.sugo"), QStringLiteral("x\nExec=evil"),
                               QStringLiteral("y\r\nHidden=true"), {QStringLiteral("/opt/sugo")});
        QVERIFY(autostart.setEnabled(true));
        const QString text = readAll(autostart.filePath());
        QCOMPARE(text.count(QStringLiteral("\nExec=")), 1);
        QVERIFY(!text.contains(QStringLiteral("\nHidden=")));
        QVERIFY(autostart.isEnabled());
    }

    void repairUpdatesAnOutdatedProgramPath()
    {
        QTemporaryDir tmp;
        XdgAutostart old = make(tmp.path(), {QStringLiteral("/old/prefix/sugo"), QStringLiteral("--background")});
        QVERIFY(old.setEnabled(true));

        XdgAutostart current = make(tmp.path(), {QStringLiteral("/new/prefix/sugo"), QStringLiteral("--background")});
        current.repair();

        const QString text = readAll(current.filePath());
        QVERIFY(text.contains(QStringLiteral("\nExec=/new/prefix/sugo --background\n")));
        QVERIFY(!text.contains(QStringLiteral("/old/prefix")));
        QVERIFY(current.isEnabled());
    }

    void repairLeavesACurrentEntryAlone()
    {
        QTemporaryDir tmp;
        XdgAutostart autostart = make(tmp.path());
        QVERIFY(autostart.setEnabled(true));
        const QString before = readAll(autostart.filePath());
        autostart.repair();
        QCOMPARE(readAll(autostart.filePath()), before);
    }

    void repairNeverReEnablesWhatTheUserTurnedOff()
    {
        QTemporaryDir tmp;
        XdgAutostart autostart = make(tmp.path());
        const QString disabled = QStringLiteral("[Desktop Entry]\nType=Application\nName=Sugo\nExec=/stale/path\nHidden=true\n");
        writeFile(autostart.filePath(), disabled);
        autostart.repair();
        QCOMPARE(readAll(autostart.filePath()), disabled);
        QVERIFY(!autostart.isEnabled());

        QTemporaryDir empty; // and nothing is created where there was no entry
        XdgAutostart none = make(empty.path());
        none.repair();
        QVERIFY(!QFile::exists(none.filePath()));
    }

    void anUnwritableLocationFailsWithoutCrashing()
    {
        QTemporaryDir tmp;
        QFile blocker(tmp.filePath(QStringLiteral("blocker")));
        QVERIFY(blocker.open(QIODevice::WriteOnly)); // a file where the directory should be
        blocker.close();

        XdgAutostart autostart = make(tmp.filePath(QStringLiteral("blocker/autostart")));
        QVERIFY(!autostart.setEnabled(true));
        QVERIFY(!autostart.isEnabled());
    }

    void thePortalIsAskedInsideFlatpakAndItsAnswerIsKept()
    {
        PrivateDBus bus;
        if (!bus.start()) {
            QSKIP("dbus-daemon is not available");
        }
        QDBusConnection server = QDBusConnection::connectToBus(bus.address(), QStringLiteral("portal-server"));
        QDBusConnection client = QDBusConnection::connectToBus(bus.address(), QStringLiteral("portal-client"));
        FakeBackgroundPortal portal;
        QVERIFY(server.registerObject(kPortalPath, &portal, QDBusConnection::ExportAllSlots));
        QVERIFY(server.registerService(kPortalService));
        QLoggingCategory::setFilterRules(QStringLiteral("sugo.platform.autostart.warning=false"));

        QTemporaryDir tmp;
        const QString state = tmp.filePath(QStringLiteral("config/portal-autostart"));
        PortalAutostart autostart(client, state, QStringLiteral("reason"), {QStringLiteral("sugo"), QStringLiteral("--background")}, 5000);
        QVERIFY(autostart.isSupported());
        QVERIFY(!autostart.isEnabled());

        QVERIFY(autostart.setEnabled(true));
        QVERIFY(autostart.isEnabled());
        QCOMPARE(portal.requests.size(), 1);
        QCOMPARE(portal.requests.at(0).value(QStringLiteral("autostart")).toBool(), true);
        QCOMPARE(portal.requests.at(0).value(QStringLiteral("commandline")).toStringList(),
                 (QStringList{QStringLiteral("sugo"), QStringLiteral("--background")}));

        // The user says no: failure, and the stored choice stays as it was.
        portal.answer = 1;
        QVERIFY(!autostart.setEnabled(false));
        QVERIFY(autostart.isEnabled());

        portal.answer = 0;
        QVERIFY(autostart.setEnabled(false));
        QVERIFY(!autostart.isEnabled());
        QCOMPARE(portal.requests.size(), 3);
        QCOMPARE(portal.requests.at(2).value(QStringLiteral("autostart")).toBool(), false);

        // No answer in time (the fake answers after 50 ms): a failure, nothing stored.
        PortalAutostart impatient(client, state, QStringLiteral("reason"), {QStringLiteral("sugo")}, 10);
        QVERIFY(!impatient.setEnabled(true));
        QVERIFY(!impatient.isEnabled());

        // No portal at all: a plain failure.
        server.unregisterService(kPortalService);
        QVERIFY(!autostart.setEnabled(true));
        QVERIFY(!autostart.isEnabled());

        server.unregisterObject(kPortalPath);
        QDBusConnection::disconnectFromBus(QStringLiteral("portal-client"));
        QDBusConnection::disconnectFromBus(QStringLiteral("portal-server"));
    }

    void theUnsupportedImplementationSaysSo()
    {
        UnsupportedAutostart unsupported;
        QVERIFY(!unsupported.isSupported());
        QVERIFY(!unsupported.isEnabled());
        QVERIFY(!unsupported.setEnabled(true));
        QVERIFY(!unsupported.isEnabled());
    }
};

QTEST_GUILESS_MAIN(AutostartTest)
#include "tst_autostart.moc"
