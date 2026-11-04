// SPDX-License-Identifier: GPL-3.0-or-later
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

#include <memory>

#include "FakeShortcutTools.h"
#include "ShortcutsFocusInstaller.h"

using State = ShortcutsFocusInstaller::State;

namespace
{
const QString kOn = QStringLiteral("Sugo Focus On");
const QString kOff = QStringLiteral("Sugo Focus Off");
} // namespace

/// Opening the shipped Focus shortcuts and waiting for the user to add them, against fake
/// `shortcuts` and `open` programs.
class ShortcutsFocusInstallerTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<FakeShortcutTools> tools;
    std::unique_ptr<ShortcutsFocusInstaller> installer;

    /// Runs an installation to its end; whether it went well, and the message.
    std::pair<bool, QString> installAndWait()
    {
        QSignalSpy finished(installer.get(), &ShortcutsFocusInstaller::finished);
        if (!installer->install() || !finished.wait(10000)) {
            return {false, QStringLiteral("<no answer>")};
        }
        return {finished.at(0).at(0).toBool(), finished.at(0).at(1).toString()};
    }

    static QList<State> states(const QSignalSpy &spy)
    {
        QList<State> found;
        for (const auto &arguments : spy) {
            found.append(arguments.at(0).value<State>());
        }
        return found;
    }

private Q_SLOTS:
    void init()
    {
        tools = std::make_unique<FakeShortcutTools>();
        QVERIFY(tools->isValid());
        installer = std::make_unique<ShortcutsFocusInstaller>(tools->programs(), tools->directory());
        installer->setTimingForTesting(20, 2000, 2000);
    }

    void cleanup()
    {
        installer.reset();
        tools.reset();
    }

    void theNamesAreFixed()
    {
        QCOMPARE(ShortcutsFocusInstaller::onName(), kOn);
        QCOMPARE(ShortcutsFocusInstaller::offName(), kOff);
        QVERIFY(ShortcutsFocusInstaller::bundledDirectory().endsWith(QStringLiteral("Resources/shortcuts")));
    }

    void opensOneAfterTheOtherOnceEachIsAdded()
    {
        QSignalSpy changed(installer.get(), &ShortcutsFocusInstaller::stateChanged);
        const auto [ok, message] = installAndWait();
        QVERIFY2(ok, qPrintable(message));
        QCOMPARE(installer->state(), State::Done);
        QCOMPARE(states(changed), (QList<State>{State::Installing, State::WaitingForOn, State::WaitingForOff, State::Done}));

        // list, open On, list (On is there) …, open Off, list (Off is there): never both sheets at once.
        const QStringList calls = tools->calls();
        const QStringList opens = tools->calls(QStringLiteral("open"));
        QCOMPARE(opens.size(), 2);
        QCOMPARE(opens.at(0), QStringLiteral("open|") + QDir(tools->directory()).filePath(kOn + QStringLiteral(".shortcut")) + QLatin1Char('|'));
        QCOMPARE(opens.at(1), QStringLiteral("open|") + QDir(tools->directory()).filePath(kOff + QStringLiteral(".shortcut")) + QLatin1Char('|'));
        const qsizetype firstOpen = calls.indexOf(opens.at(0));
        const qsizetype secondOpen = calls.indexOf(opens.at(1));
        QCOMPARE(calls.first(), QStringLiteral("shortcuts|list|"));
        QVERIFY(secondOpen > firstOpen + 1);
        QCOMPARE(calls.at(firstOpen + 1), QStringLiteral("shortcuts|list|"));
        QCOMPARE(calls.last(), QStringLiteral("shortcuts|list|"));
    }

    void shortcutsAlreadyThereGetNoSheet()
    {
        tools->setList({QStringLiteral("Something else"), kOn});
        QSignalSpy changed(installer.get(), &ShortcutsFocusInstaller::stateChanged);
        const auto [ok, message] = installAndWait();
        QVERIFY2(ok, qPrintable(message));
        QCOMPARE(states(changed), (QList<State>{State::Installing, State::WaitingForOff, State::Done}));
        const QStringList opens = tools->calls(QStringLiteral("open"));
        QCOMPARE(opens.size(), 1);
        QVERIFY(opens.at(0).contains(kOff + QStringLiteral(".shortcut")));

        // Both there: nothing opened at all.
        const auto [again, againMessage] = installAndWait();
        QVERIFY2(again, qPrintable(againMessage));
        QCOMPARE(tools->calls(QStringLiteral("open")).size(), 1);
    }

    void aMissingFileFailsWithoutRunningAnything()
    {
        tools->removeFile(kOff);
        const auto [ok, message] = installAndWait();
        QVERIFY(!ok);
        QCOMPARE(installer->state(), State::Failed);
        QVERIFY2(message.contains(QStringLiteral("missing file")), qPrintable(message));
        QVERIFY(tools->calls().isEmpty());
    }

    void toolErrorsFail_data()
    {
        QTest::addColumn<QString>("program");
        QTest::newRow("shortcuts") << QStringLiteral("shortcuts");
        QTest::newRow("open") << QStringLiteral("open");
    }

    void toolErrorsFail()
    {
        QFETCH(QString, program);
        tools->setFails(program, "Error: something broke");
        const auto [ok, message] = installAndWait();
        QVERIFY(!ok);
        QCOMPARE(installer->message(), message);
        QVERIFY2(message.contains(QStringLiteral("something broke")), qPrintable(message));
        QVERIFY2(message.startsWith(program), qPrintable(message));
        QCOMPARE(tools->calls(program).size(), 1); // nothing after the failure
        if (program == QStringLiteral("shortcuts")) {
            QVERIFY(tools->calls(QStringLiteral("open")).isEmpty());
        }
    }

    void aMissingToolFails()
    {
        ShortcutsFocusInstaller::Programs programs = tools->programs();
        programs.open = QDir(tools->directory()).filePath(QStringLiteral("no-such-program"));
        installer = std::make_unique<ShortcutsFocusInstaller>(programs, tools->directory());
        const auto [ok, message] = installAndWait();
        QVERIFY(!ok);
        QVERIFY2(message.startsWith(QStringLiteral("open")), qPrintable(message));
    }

    void aToolThatHangsTimesOut()
    {
        tools->setHangs(QStringLiteral("open"));
        installer->setTimingForTesting(20, 2000, 300);
        const auto [ok, message] = installAndWait();
        QVERIFY(!ok);
        QVERIFY2(message.contains(QStringLiteral("no answer")), qPrintable(message));
    }

    void givesUpWhenTheUserNeverAdds()
    {
        tools->setIgnored();
        installer->setTimingForTesting(20, 300, 2000);
        QSignalSpy changed(installer.get(), &ShortcutsFocusInstaller::stateChanged);
        const auto [ok, message] = installAndWait();
        QVERIFY(!ok);
        QVERIFY2(message.contains(kOn), qPrintable(message));
        QCOMPARE(states(changed), (QList<State>{State::Installing, State::WaitingForOn, State::Failed}));
        QCOMPARE(tools->calls(QStringLiteral("open")).size(), 1); // "Off" never opened
        QVERIFY(tools->calls(QStringLiteral("shortcuts")).size() >= 3); // it kept looking
    }

    void oneInstallationAtATime()
    {
        tools->setIgnored();
        installer->setTimingForTesting(20, 300, 2000);
        QSignalSpy finished(installer.get(), &ShortcutsFocusInstaller::finished);
        QVERIFY(installer->install());
        QVERIFY(installer->isRunning());
        QVERIFY(!installer->install());
        QVERIFY(finished.wait(5000));
        QVERIFY(!installer->isRunning());
        QCOMPARE(finished.count(), 1);
    }

    /// The files the app ships: Apple-signed shortcuts (Apple Encrypted Archive, "AEA1"), named
    /// as the installer expects.
    void theShippedFilesAreSignedShortcuts()
    {
        for (const QString &name : {kOn, kOff}) {
            QFile file(QStringLiteral(SUGO_SHORTCUTS_SOURCE_DIR "/") + name + QStringLiteral(".shortcut"));
            QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));
            QCOMPARE(file.read(4), QByteArray("AEA1"));
        }
    }
};

QTEST_GUILESS_MAIN(ShortcutsFocusInstallerTest)
#include "tst_shortcutsfocusinstaller.moc"
