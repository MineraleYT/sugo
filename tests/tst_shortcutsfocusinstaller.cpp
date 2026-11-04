// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>
#include <QVariant>
#include <QXmlStreamReader>

#include <memory>

#include "FakeShortcutTools.h"
#include "ShortcutsFocusInstaller.h"

using Result = ShortcutsFocusInstaller::Result;

namespace
{
const QString kOn = QStringLiteral("Sugo Focus On");
const QString kOff = QStringLiteral("Sugo Focus Off");

// A property list's value, read with QXmlStreamReader: dict → QVariantMap, array → QVariantList,
// string → QString, integer → qlonglong. The reader stands on the value's start element.
QVariant readValue(QXmlStreamReader &xml)
{
    const QStringView tag = xml.name();
    if (tag == QLatin1String("dict")) {
        QVariantMap map;
        QString key;
        while (xml.readNextStartElement()) {
            if (xml.name() == QLatin1String("key")) {
                key = xml.readElementText();
            } else {
                map.insert(key, readValue(xml));
            }
        }
        return map;
    }
    if (tag == QLatin1String("array")) {
        QVariantList list;
        while (xml.readNextStartElement()) {
            list.append(readValue(xml));
        }
        return list;
    }
    if (tag == QLatin1String("integer")) {
        return xml.readElementText().toLongLong();
    }
    if (tag == QLatin1String("string")) {
        return xml.readElementText();
    }
    xml.raiseError(QStringLiteral("unexpected element ") + tag.toString());
    return {};
}

QVariantMap readPlist(const QByteArray &data, QString *error)
{
    QXmlStreamReader xml(data);
    QVariantMap root;
    if (xml.readNextStartElement() && xml.name() == QLatin1String("plist") && xml.readNextStartElement()) {
        root = readValue(xml).toMap();
    }
    *error = xml.hasError() ? xml.errorString() : QString();
    return root;
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
} // namespace

/// Writing, signing and opening the Focus shortcuts, against fake plutil/shortcuts/open.
class ShortcutsFocusInstallerTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<FakeShortcutTools> tools;
    std::unique_ptr<ShortcutsFocusInstaller> installer;

    /// Runs an installation to its end; the result and message of finished().
    std::pair<Result, QString> installAndWait(const QString &on = kOn, const QString &off = kOff)
    {
        QSignalSpy finished(installer.get(), &ShortcutsFocusInstaller::finished);
        if (!installer->install(on, off) || !finished.wait(10000)) {
            return {Result::Failed, QStringLiteral("<no answer>")};
        }
        return {finished.at(0).at(0).value<Result>(), finished.at(0).at(1).toString()};
    }

private Q_SLOTS:
    void init()
    {
        tools = std::make_unique<FakeShortcutTools>();
        QVERIFY(tools->isValid());
        installer = std::make_unique<ShortcutsFocusInstaller>(tools->programs());
        installer->setTimeoutForTesting(2000);
    }

    void cleanup()
    {
        installer.reset();
        tools.reset();
    }

    void theShortcutsSetTheFocus_data()
    {
        QTest::addColumn<bool>("turnOn");
        QTest::newRow("on") << true;
        QTest::newRow("off") << false;
    }

    void theShortcutsSetTheFocus()
    {
        QFETCH(bool, turnOn);
        QString error;
        const QVariantMap shortcut = readPlist(ShortcutsFocusInstaller::shortcutPlist(turnOn), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));

        QVERIFY(shortcut.value(QStringLiteral("WFWorkflowClientVersion")).toString().size() > 0);
        QVERIFY(shortcut.contains(QStringLiteral("WFWorkflowMinimumClientVersion")));
        QVERIFY(shortcut.value(QStringLiteral("WFWorkflowIcon")).toMap().contains(QStringLiteral("WFWorkflowIconGlyphNumber")));
        QVERIFY(shortcut.contains(QStringLiteral("WFWorkflowTypes")));
        QVERIFY(shortcut.contains(QStringLiteral("WFWorkflowInputContentItemClasses")));

        const QVariantList actions = shortcut.value(QStringLiteral("WFWorkflowActions")).toList();
        QCOMPARE(actions.size(), 1);
        const QVariantMap action = actions.at(0).toMap();
        QCOMPARE(action.value(QStringLiteral("WFWorkflowActionIdentifier")).toString(), QStringLiteral("is.workflow.actions.dnd.set"));
        const QVariantMap parameters = action.value(QStringLiteral("WFWorkflowActionParameters")).toMap();
        QCOMPARE(parameters.value(QStringLiteral("Enabled")).toLongLong(), turnOn ? 1 : 0);
        const QVariantMap focus = parameters.value(QStringLiteral("FocusModes")).toMap();
        QCOMPARE(focus.value(QStringLiteral("Identifier")).toString(), QStringLiteral("com.apple.donotdisturb.mode.default"));
        QCOMPARE(focus.value(QStringLiteral("DisplayString")).toString(), QStringLiteral("Do Not Disturb"));
        // On until turned off; turning off needs no duration.
        QCOMPARE(parameters.value(QStringLiteral("AssertionType")).toString(), turnOn ? QStringLiteral("Turned Off") : QString());
        QCOMPARE(parameters.size(), turnOn ? 3 : 2);
    }

    void namesMustBeFileNames()
    {
        QVERIFY(ShortcutsFocusInstaller::isValidName(kOn));
        QVERIFY(ShortcutsFocusInstaller::isValidName(QStringLiteral("Fokus an (Sugo)")));
        QVERIFY(!ShortcutsFocusInstaller::isValidName(QString()));
        QVERIFY(!ShortcutsFocusInstaller::isValidName(QStringLiteral("  ")));
        QVERIFY(!ShortcutsFocusInstaller::isValidName(QStringLiteral(" padded")));
        QVERIFY(!ShortcutsFocusInstaller::isValidName(QStringLiteral("../escape")));
        QVERIFY(!ShortcutsFocusInstaller::isValidName(QStringLiteral("a/b")));
        QVERIFY(!ShortcutsFocusInstaller::isValidName(QStringLiteral("a:b")));
        QVERIFY(!ShortcutsFocusInstaller::isValidName(QStringLiteral(".hidden")));
    }

    void recognisesTheICloudError()
    {
        QVERIFY(ShortcutsFocusInstaller::isICloudError(QStringLiteral("Error: In order to do this, you must be signed into iCloud.")));
        QVERIFY(!ShortcutsFocusInstaller::isICloudError(QStringLiteral("Error: The file couldn't be opened.")));
    }

    void writesSignsAndOpensBothShortcuts()
    {
        const auto [result, message] = installAndWait();
        QCOMPARE(result, Result::Opened);
        QVERIFY(message.isEmpty());

        const QDir dir(installer->workDirectory());
        const QString unsignedOn = dir.filePath(QStringLiteral("unsigned/Sugo Focus On.shortcut"));
        const QString unsignedOff = dir.filePath(QStringLiteral("unsigned/Sugo Focus Off.shortcut"));
        const QString signedOn = dir.filePath(QStringLiteral("signed/Sugo Focus On.shortcut"));
        const QString signedOff = dir.filePath(QStringLiteral("signed/Sugo Focus Off.shortcut"));
        // Named after the shortcuts, with the right content (the fake tools copy it unchanged).
        QCOMPARE(readFile(signedOn), ShortcutsFocusInstaller::shortcutPlist(true));
        QCOMPARE(readFile(signedOff), ShortcutsFocusInstaller::shortcutPlist(false));

        const auto sign = [](const QString &in, const QString &out) {
            return QStringLiteral("shortcuts|sign|--mode|anyone|--input|%1|--output|%2|").arg(in, out);
        };
        QCOMPARE(tools->calls(),
                 (QStringList{
                     QStringLiteral("plutil|-convert|binary1|") + unsignedOn + QLatin1Char('|'),
                     sign(unsignedOn, signedOn),
                     QStringLiteral("plutil|-convert|binary1|") + unsignedOff + QLatin1Char('|'),
                     sign(unsignedOff, signedOff),
                     // Opened only once both are signed, "on" first.
                     QStringLiteral("open|") + signedOn + QLatin1Char('|'),
                     QStringLiteral("open|") + signedOff + QLatin1Char('|'),
                 }));
        QVERIFY(!installer->isRunning());
    }

    void usesTheGivenNames()
    {
        const auto [result, message] = installAndWait(QStringLiteral("Mine On"), QStringLiteral("Mine Off"));
        QCOMPARE(result, Result::Opened);
        const QDir dir(installer->workDirectory());
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("signed/Mine On.shortcut"))));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("signed/Mine Off.shortcut"))));
    }

    void withoutICloudNothingIsOpened()
    {
        tools->setNeedsICloud();
        const auto [result, message] = installAndWait();
        QCOMPARE(result, Result::NeedsICloud);
        QVERIFY(message.contains(QStringLiteral("iCloud")));
        QCOMPARE(tools->calls(QStringLiteral("shortcuts")).size(), 1); // stops at the first signature
        QVERIFY(tools->calls(QStringLiteral("open")).isEmpty());
    }

    void otherErrorsFail_data()
    {
        QTest::addColumn<QString>("program");
        QTest::newRow("plutil") << QStringLiteral("plutil");
        QTest::newRow("sign") << QStringLiteral("shortcuts");
        QTest::newRow("open") << QStringLiteral("open");
    }

    void otherErrorsFail()
    {
        QFETCH(QString, program);
        tools->setFails(program, "Error: something broke");
        const auto [result, message] = installAndWait();
        QCOMPARE(result, Result::Failed);
        QVERIFY2(message.contains(QStringLiteral("something broke")), qPrintable(message));
        QVERIFY2(message.startsWith(program), qPrintable(message));
        QCOMPARE(tools->calls(program).size(), 1); // nothing after the failure
        if (program != QStringLiteral("open")) {
            QVERIFY(tools->calls(QStringLiteral("open")).isEmpty());
        }
    }

    void aMissingToolFails()
    {
        ShortcutsFocusInstaller::Programs programs = tools->programs();
        programs.shortcuts = QStringLiteral("/nonexistent/sugo-test/shortcuts");
        installer = std::make_unique<ShortcutsFocusInstaller>(programs);
        const auto [result, message] = installAndWait();
        QCOMPARE(result, Result::Failed);
        QVERIFY(tools->calls(QStringLiteral("open")).isEmpty());
    }

    void aToolThatHangsTimesOut()
    {
        tools->setHangs(QStringLiteral("shortcuts"));
        installer->setTimeoutForTesting(300);
        const auto [result, message] = installAndWait();
        QCOMPARE(result, Result::Failed);
        QVERIFY2(message.contains(QStringLiteral("no answer")), qPrintable(message));
    }

    void invalidNamesFailWithoutRunningAnything()
    {
        QCOMPARE(installAndWait(QStringLiteral("a/b"), kOff).first, Result::Failed);
        QCOMPARE(installAndWait(kOn, QString()).first, Result::Failed);
        QCOMPARE(installAndWait(kOn, kOn).first, Result::Failed);
        QVERIFY(tools->calls().isEmpty());
    }

    void oneInstallationAtATime()
    {
        QSignalSpy finished(installer.get(), &ShortcutsFocusInstaller::finished);
        QVERIFY(installer->install(kOn, kOff));
        QVERIFY(installer->isRunning());
        QVERIFY(!installer->install(kOn, kOff));
        QVERIFY(finished.wait(10000));
        QTest::qWait(100);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(tools->calls(QStringLiteral("open")).size(), 2);
    }

    /// macOS, opt-in (SUGO_TEST_SHORTCUTS_SIGN=1): the real plutil and `shortcuts sign` accept
    /// the files. `open` is replaced by /usr/bin/true: nothing reaches the Shortcuts library.
    /// Without iCloud the result is NeedsICloud, with it Opened. The files go to $TMPDIR.
    void realToolsAcceptTheShortcuts()
    {
#ifndef Q_OS_MACOS
        QSKIP("macOS only");
#else
        if (qEnvironmentVariable("SUGO_TEST_SHORTCUTS_SIGN") != QStringLiteral("1")) {
            QSKIP("set SUGO_TEST_SHORTCUTS_SIGN=1 to run the real plutil and shortcuts sign");
        }
        ShortcutsFocusInstaller::Programs programs;
        programs.open = QStringLiteral("/usr/bin/true");
        installer = std::make_unique<ShortcutsFocusInstaller>(programs);
        QSignalSpy finished(installer.get(), &ShortcutsFocusInstaller::finished);
        QVERIFY(installer->install(kOn, kOff));
        QVERIFY(finished.wait(90000));
        const auto result = finished.at(0).at(0).value<Result>();
        qInfo() << "Result:" << result << finished.at(0).at(1).toString() << "in" << installer->workDirectory();
        QVERIFY2(result != Result::Failed, qPrintable(finished.at(0).at(1).toString()));
        const QString unsignedOn = QDir(installer->workDirectory()).filePath(QStringLiteral("unsigned/Sugo Focus On.shortcut"));
        QProcess lint;
        lint.start(QStringLiteral("/usr/bin/plutil"), {QStringLiteral("-lint"), unsignedOn});
        QVERIFY(lint.waitForFinished(10000));
        QCOMPARE(lint.exitCode(), 0);
#endif
    }
};

QTEST_GUILESS_MAIN(ShortcutsFocusInstallerTest)
#include "tst_shortcutsfocusinstaller.moc"
