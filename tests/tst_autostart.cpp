// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtGlobal>
#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusObjectPath>
#endif
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include "AppImage.h"
#include "Autostart.h"
#ifdef Q_OS_LINUX
#include "PortalAutostart.h"
#include "PrivateDBus.h"
#endif

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
    return XdgAutostart(dir, QStringLiteral("stream.sugo.app"), QStringLiteral("Sugo"), QStringLiteral("Pomodoro timer"), command);
}
#ifdef Q_OS_LINUX
const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");

/// One request of the fake portal: Close() ends its dialog unanswered, like the real portal.
class FakeRequest : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Request")

public:
    explicit FakeRequest(QStringList *closed, QObject *parent)
        : QObject(parent)
        , m_closed(closed)
    {
    }

    QString handle;
    bool isClosed = false;

public Q_SLOTS:
    void Close()
    {
        isClosed = true;
        *m_closed << handle;
    }

private:
    QStringList *m_closed;
};

/// Stand-in for xdg-desktop-portal's Background interface.
class FakeBackgroundPortal : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Background")

public:
    QList<QVariantMap> requests;
    QStringList handles; // of every request, in order
    QStringList closed; // the requests closed by the client
    uint answer = 0; // 0 granted, 1 cancelled
    int answerDelayMs = 50; // how long the user takes to answer the dialog

public Q_SLOTS:
    QDBusObjectPath RequestBackground(const QString &parentWindow, const QVariantMap &options)
    {
        Q_UNUSED(parentWindow)
        requests << options;
        QString sender = message().service().mid(1);
        sender.replace(QLatin1Char('.'), QLatin1Char('_'));
        const QString handle = kPortalPath + QStringLiteral("/request/") + sender + QLatin1Char('/')
            + options.value(QStringLiteral("handle_token")).toString();
        handles << handle;
        QDBusConnection bus = connection();
        auto *request = new FakeRequest(&closed, this);
        request->handle = handle;
        bus.registerObject(handle, request, QDBusConnection::ExportAllSlots);
        const uint code = answer;
        const bool autostart = options.value(QStringLiteral("autostart")).toBool();
        // Answer later, like the real portal after asking the user; a closed request never is.
        QTimer::singleShot(answerDelayMs, request, [bus, request, code, autostart]() mutable {
            bus.unregisterObject(request->handle);
            if (request->isClosed) {
                return;
            }
            QDBusMessage signal = QDBusMessage::createSignal(request->handle, QStringLiteral("org.freedesktop.portal.Request"),
                                                             QStringLiteral("Response"));
            signal << code << QVariantMap{{QStringLiteral("background"), true}, {QStringLiteral("autostart"), autostart}};
            bus.send(signal);
        });
        return QDBusObjectPath(handle);
    }
};
#endif
} // namespace

class AutostartTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void anAppImageStartsItsFileNotItsMount()
    {
        qunsetenv("APPIMAGE");
        qunsetenv("APPDIR");
        QCOMPARE(autostartProgram(), QCoreApplication::applicationFilePath());
        // As if this test ran from an AppImage mounted at its own folder's parent.
        const QString mount = QFileInfo(QCoreApplication::applicationDirPath()).absolutePath();
        qputenv("APPIMAGE", "/home/someone/Applications/Sugo-x86_64.AppImage");
        qputenv("APPDIR", mount.toUtf8());
        QCOMPARE(autostartProgram(), QStringLiteral("/home/someone/Applications/Sugo-x86_64.AppImage"));
        qunsetenv("APPIMAGE");
        qunsetenv("APPDIR");
    }

    void anotherAppImagesVariablesAreNotOurs()
    {
        // A build from source started in a terminal of another AppImage inherits its variables.
        const QString other = QStringLiteral("/home/someone/Applications/Editor.AppImage");
        QCOMPARE(AppImage::path(other, QStringLiteral("/tmp/.mount_Editor1"), QStringLiteral("/home/someone/sugo/build/bin/sugo")), QString());
        QCOMPARE(AppImage::path(other, QStringLiteral("/tmp/.mount_Sugo"), QStringLiteral("/tmp/.mount_Sugo2/usr/bin/sugo")), QString());
        QCOMPARE(AppImage::path(other, QString(), QStringLiteral("/tmp/.mount_Sugo/usr/bin/sugo")), QString());
        QCOMPARE(AppImage::path(QString(), QStringLiteral("/tmp/.mount_Sugo"), QStringLiteral("/tmp/.mount_Sugo/usr/bin/sugo")), QString());
        QCOMPARE(AppImage::path(other, QStringLiteral("/tmp/.mount_Sugo/"), QStringLiteral("/tmp/.mount_Sugo/usr/bin/sugo")), other);

        qputenv("APPIMAGE", other.toUtf8());
        qputenv("APPDIR", "/tmp/.mount_Editor1");
        QCOMPARE(autostartProgram(), QCoreApplication::applicationFilePath());
        QCOMPARE(AppImage::path(), QString());
        qunsetenv("APPIMAGE");
        qunsetenv("APPDIR");
    }

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
        QVERIFY(text.contains(QStringLiteral("\nIcon=stream.sugo.app\n")));
        QVERIFY(text.contains(QStringLiteral("\nExec=/opt/sugo/bin/sugo --background\n")));
        QVERIFY(text.contains(QStringLiteral("\nX-GNOME-Autostart-enabled=true\n")));
        QVERIFY(autostart.filePath().endsWith(QStringLiteral("/stream.sugo.app.desktop")));
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
        XdgAutostart autostart(tmp.path(), QStringLiteral("stream.sugo.app"), QStringLiteral("x\nExec=evil"),
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
#ifndef Q_OS_LINUX
        QSKIP("Flatpak and its portal exist on Linux only");
#else
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

        // No portal at all: a plain failure.
        server.unregisterService(kPortalService);
        QVERIFY(!autostart.setEnabled(true));
        QVERIFY(!autostart.isEnabled());

        server.unregisterObject(kPortalPath);
        QDBusConnection::disconnectFromBus(QStringLiteral("portal-client"));
        QDBusConnection::disconnectFromBus(QStringLiteral("portal-server"));
#endif
    }

    void anAnswerAfterTheTimeoutIsStillKept()
    {
#ifndef Q_OS_LINUX
        QSKIP("Flatpak and its portal exist on Linux only");
#else
        PrivateDBus bus;
        if (!bus.start()) {
            QSKIP("dbus-daemon is not available");
        }
        QDBusConnection server = QDBusConnection::connectToBus(bus.address(), QStringLiteral("late-server"));
        QDBusConnection client = QDBusConnection::connectToBus(bus.address(), QStringLiteral("late-client"));
        FakeBackgroundPortal portal;
        portal.answerDelayMs = 300; // the user answers the dialog after Sugo stopped waiting
        QVERIFY(server.registerObject(kPortalPath, &portal, QDBusConnection::ExportAllSlots));
        QVERIFY(server.registerService(kPortalService));
        QLoggingCategory::setFilterRules(QStringLiteral("sugo.platform.autostart.warning=false"));

        QTemporaryDir tmp;
        const QString state = tmp.filePath(QStringLiteral("config/portal-autostart"));
        {
            PortalAutostart impatient(client, state, QStringLiteral("reason"), {QStringLiteral("sugo")}, 10);
            int changes = 0;
            impatient.setChangedCallback([&changes]() { ++changes; });
            // No answer in time: a failure for now, nothing stored yet.
            QVERIFY(!impatient.setEnabled(true));
            QVERIFY(!impatient.isEnabled());
            QCOMPARE(changes, 0);
            // The portal wrote the login entry once the user allowed it: Sugo must know, and say so.
            QTRY_VERIFY(impatient.isEnabled());
            QCOMPARE(changes, 1);

            // The same for switching it off.
            QVERIFY(!impatient.setEnabled(false));
            QVERIFY(impatient.isEnabled());
            QTRY_VERIFY(!impatient.isEnabled());
            QCOMPARE(changes, 2);

            // A late refusal changes nothing.
            portal.answer = 1;
            QVERIFY(!impatient.setEnabled(true));
            QTest::qWait(portal.answerDelayMs + 200);
            QVERIFY(!impatient.isEnabled());
            QCOMPARE(changes, 2);
            portal.answer = 0;

            // Only the latest request counts: the one still open is closed, so its dialog cannot
            // switch on what the user switched off since.
            QVERIFY(portal.closed.isEmpty());
            QVERIFY(!impatient.setEnabled(true));
            portal.answerDelayMs = 600;
            QVERIFY(!impatient.setEnabled(false));
            QTRY_COMPARE(portal.closed, QStringList{portal.handles.at(portal.handles.size() - 2)});
            QTest::qWait(400); // the first request's answer would have come by now
            QVERIFY(!impatient.isEnabled());
            QCOMPARE(changes, 2);
            QTest::qWait(400); // the second one's
            QVERIFY(!impatient.isEnabled());
            QCOMPARE(changes, 2);

            // Asked again and not answered in time: closed when Sugo stops (quits).
            QVERIFY(!impatient.setEnabled(true));
        }
        QTRY_COMPARE(portal.closed.size(), 2);
        QCOMPARE(portal.closed.last(), portal.handles.last());

        server.unregisterObject(kPortalPath);
        QDBusConnection::disconnectFromBus(QStringLiteral("late-client"));
        QDBusConnection::disconnectFromBus(QStringLiteral("late-server"));
#endif
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
