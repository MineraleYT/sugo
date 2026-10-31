// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDir>
#include <QIcon>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include "ThemeIcons.h"

/// The icons of the desktop's theme, as QML gets them through image://icon/.
class ThemeIconsTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // A one-icon theme: a 16 px PNG, transparent on the left half, green on the right.
        QVERIFY(m_dir.isValid());
        QDir root(m_dir.path());
        QVERIFY(root.mkpath(QStringLiteral("sugotest/16x16/apps")));
        QFile index(root.filePath(QStringLiteral("sugotest/index.theme")));
        QVERIFY(index.open(QIODevice::WriteOnly));
        index.write("[Icon Theme]\nName=sugotest\nDirectories=16x16/apps\n\n[16x16/apps]\nSize=16\nType=Fixed\n");
        index.close();
        QImage half(16, 16, QImage::Format_ARGB32);
        half.fill(Qt::transparent);
        for (int y = 0; y < 16; ++y) {
            for (int x = 8; x < 16; ++x) {
                half.setPixelColor(x, y, Qt::green);
            }
        }
        QVERIFY(half.save(root.filePath(QStringLiteral("sugotest/16x16/apps/sugo-half.png"))));
        QIcon::setThemeSearchPaths({m_dir.path()});
        QIcon::setThemeName(QStringLiteral("sugotest"));
        QIcon::setFallbackThemeName(QString());
    }

    void exists()
    {
        QVERIFY(ThemeIcons::exists(QStringLiteral("sugo-half")));
        QVERIFY(!ThemeIcons::exists(QStringLiteral("sugo-no-such-icon")));
        QVERIFY(!ThemeIcons::exists(QString()));
    }

    void ownColours()
    {
        ThemeIconProvider provider;
        QSize size;
        const QImage image = provider.requestImage(QStringLiteral("sugo-half"), &size, QSize(32, 32));
        QCOMPARE(size, QSize(32, 32));
        QCOMPARE(image.pixelColor(4, 16).alpha(), 0);
        QCOMPARE(image.pixelColor(24, 16), QColor(Qt::green));
    }

    void tinted()
    {
        // Symbolic use: every visible pixel in the colour asked for, the transparent ones left alone.
        ThemeIconProvider provider;
        QSize size;
        const QImage image = provider.requestImage(QStringLiteral("sugo-half?color=ff0000"), &size, QSize(16, 16));
        QCOMPARE(size, QSize(16, 16));
        QCOMPARE(image.pixelColor(2, 8).alpha(), 0);
        QCOMPARE(image.pixelColor(12, 8), QColor(Qt::red));

        // "aarrggbb", as QML writes a colour that is not opaque.
        const QImage faint = provider.requestImage(QStringLiteral("sugo-half?color=80ffffff"), &size, QSize(16, 16));
        QCOMPARE(faint.pixelColor(12, 8).alpha(), 0x80);
    }

    void defaultSizeAndUnknown()
    {
        ThemeIconProvider provider;
        QSize size;
        QCOMPARE(provider.requestImage(QStringLiteral("sugo-half"), &size, QSize()).size(), QSize(32, 32));
        QVERIFY(provider.requestImage(QStringLiteral("sugo-no-such-icon"), &size, QSize(16, 16)).isNull());
        QVERIFY(size.isEmpty());
    }

private:
    QTemporaryDir m_dir;
};

QTEST_MAIN(ThemeIconsTest)
#include "tst_themeicons.moc"
