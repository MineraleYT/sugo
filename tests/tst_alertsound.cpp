// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include "AppSettings.h"
#include "Autostart.h"
#include "Settings.h"

#include <memory>

namespace
{
/// A mono 8-bit WAV of `ms` milliseconds of silence.
bool writeSilence(const QString &path, int ms)
{
    const quint32 rate = 8000;
    const quint32 samples = rate * quint32(ms) / 1000;
    QByteArray wav;
    const auto u32 = [&wav](quint32 v) {
        for (int i = 0; i < 4; ++i) {
            wav.append(char((v >> (8 * i)) & 0xff));
        }
    };
    const auto u16 = [&wav](quint16 v) {
        wav.append(char(v & 0xff));
        wav.append(char(v >> 8));
    };
    wav.append("RIFF");
    u32(36 + samples);
    wav.append("WAVEfmt ");
    u32(16);
    u16(1); // PCM
    u16(1); // mono
    u32(rate);
    u32(rate); // bytes per second
    u16(1);    // block align
    u16(8);    // bits per sample
    wav.append("data");
    u32(samples);
    wav.append(QByteArray(int(samples), char(0x80)));
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(wav) == wav.size();
}
} // namespace

class AlertSoundTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::unique_ptr<AppSettings> m_settings;

    int players() const
    {
        return int(m_settings->findChildren<QMediaPlayer *>().size());
    }

    /// Plays a silent file of `ms` through the alert's Qt Multimedia player; false if Qt
    /// Multimedia has no backend here (a player program would be used instead).
    bool playSilence(int ms)
    {
        const QString file = m_dir.filePath(QStringLiteral("alert-%1.wav").arg(ms));
        if (!QFileInfo::exists(file) && !writeSilence(file, ms)) {
            return false;
        }
        QMediaPlayer probe;
        if (!probe.isAvailable()) {
            return false;
        }
        m_settings->playSoundPreview(file, 0);
        return players() == 1;
    }

private Q_SLOTS:
    void initTestCase()
    {
        // The settings file goes to a directory of its own, never the user's configuration.
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_dir.isValid());
        qputenv("SUGO_DATA_DIR", QFile::encodeName(m_dir.path()));
    }

    void init()
    {
        m_settings = std::make_unique<AppSettings>(Settings::openDefault(), nullptr, false);
    }

    void cleanup()
    {
        AppSettings::setAlertPlayerLifetimeForTesting(0);
        m_settings.reset();
    }

    void theLifetimeFollowsTheDuration()
    {
        QCOMPARE(AppSettings::alertPlayerLifetimeMs(500), 500 + AppSettings::kAlertPlayerGraceMs);
        QCOMPARE(AppSettings::alertPlayerLifetimeMs(0), AppSettings::kAlertPlayerUnknownDurationMs);
        QCOMPARE(AppSettings::alertPlayerLifetimeMs(-1), AppSettings::kAlertPlayerUnknownDurationMs);
    }

    void aSoundThatEndsReleasesItsPlayer()
    {
        if (!playSilence(300)) {
            QSKIP("Qt Multimedia has no backend here");
        }
        QTRY_COMPARE(players(), 0);
    }

    void aSoundThatDoesNotEndInTimeReleasesItsPlayer()
    {
        // As a backend that never reports the end: the sound outlives the player's lifetime.
        AppSettings::setAlertPlayerLifetimeForTesting(300);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("did not end in time; its player is released")));
        if (!playSilence(20 * 1000)) {
            QSKIP("Qt Multimedia has no backend here");
        }
        QTRY_COMPARE_WITH_TIMEOUT(players(), 0, 5000); // well before the sound's 20 s
    }
};

QTEST_GUILESS_MAIN(AlertSoundTest)
#include "tst_alertsound.moc"
