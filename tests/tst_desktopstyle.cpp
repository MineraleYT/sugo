// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDir>
#include <QFile>
#include <QFontInfo>
#include <QKeySequence>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "DesktopStyle.h"

/// Reading where the desktop puts its window buttons.
class DesktopStyleTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void kwinLetters()
    {
        // KWin's default: menu and "on all desktops" (not ours) left, minimize, maximize, close right.
        const auto layout = DesktopStyle::parseKWin(QStringLiteral("MS"), QStringLiteral("IAX"));
        QCOMPARE(layout.left, QStringList());
        QCOMPARE(layout.right, (QStringList{QStringLiteral("minimize"), QStringLiteral("maximize"), QStringLiteral("close")}));

        const auto mac = DesktopStyle::parseKWin(QStringLiteral("XIA"), QStringLiteral("H"));
        QCOMPARE(mac.left, (QStringList{QStringLiteral("close"), QStringLiteral("minimize"), QStringLiteral("maximize")}));
        QCOMPARE(mac.right, QStringList());
    }

    void gnomeLayout()
    {
        const auto plain = DesktopStyle::parseGnome(QStringLiteral("'appmenu:close'\n"));
        QCOMPARE(plain.left, QStringList());
        QCOMPARE(plain.right, QStringList{QStringLiteral("close")});

        const auto full = DesktopStyle::parseGnome(QStringLiteral("'icon:minimize,maximize,close'"));
        QCOMPARE(full.right, (QStringList{QStringLiteral("minimize"), QStringLiteral("maximize"), QStringLiteral("close")}));

        const auto left = DesktopStyle::parseGnome(QStringLiteral("close,minimize:appmenu"));
        QCOMPARE(left.left, (QStringList{QStringLiteral("close"), QStringLiteral("minimize")}));
        QCOMPARE(left.right, QStringList());
    }

    void defaults()
    {
        QCOMPARE(DesktopStyle::defaultLayout(QStringLiteral("macos")).left.first(), QStringLiteral("close"));
        QCOMPARE(DesktopStyle::defaultLayout(QStringLiteral("gnome")).right, QStringList{QStringLiteral("close")});
        QCOMPARE(DesktopStyle::defaultLayout(QStringLiteral("windows")).right.size(), 3);
    }

    void readsWhereTheDesktopPutsItsButtons()
    {
#ifndef Q_OS_LINUX
        QSKIP("Linux desktops only: elsewhere the style is the system's, whatever XDG_CURRENT_DESKTOP says");
#endif
        QStandardPaths::setTestModeEnabled(true);
        const QByteArray desktop = qgetenv("XDG_CURRENT_DESKTOP");
        const QByteArray path = qgetenv("PATH");
        qunsetenv("SUGO_WINDOW_STYLE");
        const QString configDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
        QVERIFY(QDir().mkpath(configDir));
        const QString kwinrc = configDir + QStringLiteral("/kwinrc");

        // KDE without a kwinrc: KWin's defaults.
        QFile::remove(kwinrc);
        qputenv("XDG_CURRENT_DESKTOP", "KDE");
        {
            const DesktopStyle style;
            QCOMPARE(style.style(), QStringLiteral("kde"));
            QCOMPARE(style.leftButtons(), QStringList());
            QCOMPARE(style.rightButtons(), (QStringList{QStringLiteral("minimize"), QStringLiteral("maximize"), QStringLiteral("close")}));
        }
        // KDE with close on the left and no minimize.
        {
            QFile file(kwinrc);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("[org.kde.kdecoration2]\nButtonsOnLeft=XM\nButtonsOnRight=A\n");
        }
        {
            const DesktopStyle style;
            QCOMPARE(style.leftButtons(), QStringList{QStringLiteral("close")});
            QCOMPARE(style.rightButtons(), QStringList{QStringLiteral("maximize")});
        }
        // A window rule forcing KWin's title bar on every window (Plasma 6.8): no buttons of ours.
        const QString rulesrc = configDir + QStringLiteral("/kwinrulesrc");
        {
            QFile file(rulesrc);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("[General]\nOrder=r1\n\n[r1]\ndecorationpolicy=server\ndecorationpolicyrule=2\n");
        }
        {
            const DesktopStyle style;
            QCOMPARE(style.leftButtons(), QStringList());
            QCOMPARE(style.rightButtons(), QStringList());
        }
        QFile::remove(rulesrc);
        QFile::remove(kwinrc);

        // GNOME where its setting cannot be read: GNOME's default, close alone.
        qputenv("XDG_CURRENT_DESKTOP", "ubuntu:GNOME");
        qputenv("PATH", "");
        {
            const DesktopStyle style;
            QCOMPARE(style.style(), QStringLiteral("gnome"));
            QCOMPARE(style.rightButtons(), QStringList{QStringLiteral("close")});
        }
        qputenv("PATH", path);

        // Any other desktop: all three on the right.
        qputenv("XDG_CURRENT_DESKTOP", "XFCE");
        {
            const DesktopStyle style;
            QCOMPARE(style.style(), QStringLiteral("generic"));
            QCOMPARE(style.rightButtons().size(), 3);
        }
        qputenv("XDG_CURRENT_DESKTOP", desktop);
        QStandardPaths::setTestModeEnabled(false);
    }

    void kwinRulesForTheTitleBar()
    {
        QTemporaryDir dir;
        QString path = dir.filePath(QStringLiteral("none"));
        int n = 0;
        // A new file each time: QSettings caches a file it read in the same second.
        const auto rules = [&](const QByteArray &rule) {
            path = dir.filePath(QString::number(++n));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("[General]\nOrder=a,b\n\n[a]\nwmclass=firefox\nwmclassmatch=1\ndecorationpolicy=server\ndecorationpolicyrule=2\n\n[b]\n" + rule);
        };
        QVERIFY(!DesktopStyle::kwinForcesTitleBar(path)); // no file
        rules("decorationpolicy=server\ndecorationpolicyrule=2\n");
        QVERIFY(DesktopStyle::kwinForcesTitleBar(path));
        rules("decorationpolicy=server\ndecorationpolicyrule=1\n"); // "do not affect"
        QVERIFY(!DesktopStyle::kwinForcesTitleBar(path));
        rules("decorationpolicy=client\ndecorationpolicyrule=2\n");
        QVERIFY(!DesktopStyle::kwinForcesTitleBar(path));
        rules("wmclass=sugo\nwmclassmatch=2\ndecorationpolicy=server\ndecorationpolicyrule=3\n");
        QVERIFY(DesktopStyle::kwinForcesTitleBar(path));
        rules("wmclass=^io\\\\.github\\\\..*$\nwmclassmatch=3\ndecorationpolicy=server\ndecorationpolicyrule=2\n");
        QVERIFY(DesktopStyle::kwinForcesTitleBar(path));
    }

    void forcedStyleAlwaysHasClose()
    {
        qputenv("SUGO_WINDOW_STYLE", "windows");
        const DesktopStyle style;
        QCOMPARE(style.style(), QStringLiteral("windows"));
#ifdef Q_OS_MACOS
        // The system's title bar has the buttons, whatever the style.
        QVERIFY(style.rightButtons().isEmpty());
#else
        QVERIFY(style.rightButtons().contains(QStringLiteral("close")));
#endif
        qunsetenv("SUGO_WINDOW_STYLE");
    }

    void macosStyleChoosesTheWording()
    {
        qputenv("SUGO_WINDOW_STYLE", "macos");
        const DesktopStyle style;
        QVERIFY(style.isMacos());
#ifdef Q_OS_MACOS
        // The system draws the title bar and its buttons.
        QVERIFY(style.nativeChrome());
        QVERIFY(style.leftButtons().isEmpty());
        QVERIFY(style.rightButtons().isEmpty());
#else
        // A preview on Linux: macOS's texts, but still our own title bar and its buttons.
        QVERIFY(!style.nativeChrome());
        QCOMPARE(style.leftButtons().first(), QStringLiteral("close"));
#endif
        qputenv("SUGO_WINDOW_STYLE", "kde");
        QVERIFY(!DesktopStyle().isMacos());
        qunsetenv("SUGO_WINDOW_STYLE");
    }

    void monoFamilyIsFixedWidth()
    {
        const DesktopStyle style;
        QVERIFY(!style.monoFamily().isEmpty());
        QFont font(style.monoFamily());
        QVERIFY(QFontInfo(font).fixedPitch());
    }

    void shortcutTextIsTheSystemsOwn()
    {
        const QString search = DesktopStyle::shortcutText(QStringLiteral("Ctrl+K"));
        QCOMPARE(search, QKeySequence(Qt::CTRL | Qt::Key_K).toString(QKeySequence::NativeText));
#ifdef Q_OS_MACOS
        QCOMPARE(search, QStringLiteral("⌘K"));
#else
        QCOMPARE(search, QStringLiteral("Ctrl+K"));
        QCOMPARE(DesktopStyle::shortcutText(QStringLiteral("Ctrl+,")), QStringLiteral("Ctrl+,"));
        QCOMPARE(DesktopStyle::shortcutText(QStringLiteral("Space")), QStringLiteral("Space"));
#endif
        QVERIFY(DesktopStyle::shortcutText(QString()).isEmpty());
    }
};

QTEST_MAIN(DesktopStyleTest)
#include "tst_desktopstyle.moc"
