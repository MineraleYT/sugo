// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QMediaDevices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <cstring>
#include <memory>

#include "FakeClock.h"
#include "FocusSoundController.h"
#include "TimerEngine.h"

class FocusSoundControllerTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir dir;
    QByteArray oldPath;
    QByteArray oldCapture;
    FakeClock clock;
    std::unique_ptr<TimerEngine> engine;
    std::unique_ptr<FocusSoundController> sounds;

    QString capture() const { return dir.filePath(QStringLiteral("audio.raw")); }

    /// The process ids of the players started so far, oldest first.
    QList<qint64> startedPlayers() const
    {
        QFile file(dir.filePath(QStringLiteral("audio.raw.pids")));
        QList<qint64> pids;
        if (file.open(QIODevice::ReadOnly)) {
            for (const QByteArray &line : file.readAll().split('\n')) {
                if (!line.isEmpty()) pids.append(line.toLongLong());
            }
        }
        return pids;
    }

    /// The process exists and has not ended (a zombie not yet reaped counts as ended).
    static bool isRunning(qint64 pid)
    {
#ifdef Q_OS_LINUX
        QFile stat(QStringLiteral("/proc/%1/stat").arg(pid));
        if (!stat.open(QIODevice::ReadOnly)) return false;
        const QByteArray line = stat.readAll();
        const qsizetype end = line.lastIndexOf(')');
        return end >= 0 && line.size() > end + 2 && line.at(end + 2) != 'Z' && line.at(end + 2) != 'X';
#else
        // No /proc (macOS): ask ps for the process state; nothing printed if it is gone.
        QProcess ps;
        ps.start(QStringLiteral("ps"), {QStringLiteral("-o"), QStringLiteral("stat="), QStringLiteral("-p"), QString::number(pid)});
        if (!ps.waitForFinished(3000)) return false;
        const QByteArray state = ps.readAllStandardOutput().trimmed();
        return !state.isEmpty() && !state.startsWith('Z');
#endif
    }

    bool playerRunning() const
    {
        const QList<qint64> pids = startedPlayers();
        return !pids.isEmpty() && isRunning(pids.last());
    }

    /// A player is running and has been fed audio.
    bool soundPlaying() const { return playerRunning() && QFileInfo(capture()).size() > 0; }

    QByteArray capturedAudio() const
    {
        QFile file(capture());
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }

    /// The loudest of the samples [from, to) of mono s16 audio.
    static int loudest(const QByteArray &raw, qsizetype from, qsizetype to)
    {
        const auto *samples = reinterpret_cast<const qint16 *>(raw.constData());
        int peak = 0;
        for (qsizetype i = from; i < to; ++i) peak = std::max(peak, std::abs(int(samples[i])));
        return peak;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(dir.isValid());
        const QString bin = dir.filePath(QStringLiteral("bin"));
        QVERIFY(QDir().mkpath(bin));
        QFile player(bin + QStringLiteral("/pw-play"));
        QVERIFY(player.open(QIODevice::WriteOnly));
        player.write("#!/bin/sh\necho $$ >> \"$FOCUS_SOUND_CAPTURE.pids\"\nexec cat > \"$FOCUS_SOUND_CAPTURE\"\n");
        player.close();
        QVERIFY(player.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        oldPath = qgetenv("PATH");
        oldCapture = qgetenv("FOCUS_SOUND_CAPTURE");
        QByteArray path = bin.toLocal8Bit();
        path.append(':');
        path.append(oldPath);
        qputenv("PATH", path);
        qputenv("FOCUS_SOUND_CAPTURE", dir.filePath(QStringLiteral("audio.raw")).toLocal8Bit());
    }

    void cleanupTestCase()
    {
        qputenv("PATH", oldPath);
        if (oldCapture.isNull()) qunsetenv("FOCUS_SOUND_CAPTURE");
        else qputenv("FOCUS_SOUND_CAPTURE", oldCapture);
    }

    void init()
    {
        QFile::remove(dir.filePath(QStringLiteral("audio.raw")));
        QFile::remove(dir.filePath(QStringLiteral("audio.raw.pids")));
        clock = FakeClock();
        engine = std::make_unique<TimerEngine>(&clock, nullptr);
        sounds = std::make_unique<FocusSoundController>();
#ifndef Q_OS_LINUX
        // Elsewhere (macOS) the default is QAudioSink; these tests watch the fake pw-play.
        sounds->setBackendForTesting(FocusSoundController::Backend::PwPlay);
#endif
        sounds->attach(engine.get(), nullptr);
        sounds->setMode(static_cast<int>(FocusSoundController::Mode::WhiteNoise));
        sounds->setVolume(45);
    }

    void cleanup()
    {
        sounds.reset();
        engine.reset();
    }

    void audioStartsForWorkAndStopsWhenPaused()
    {
        engine->start();
        QTRY_VERIFY_WITH_TIMEOUT(soundPlaying(), 3000);

        engine->pause();
        QTRY_VERIFY_WITH_TIMEOUT(!playerRunning(), 5000); // faded out, then ended
        engine->resume();
        QTRY_VERIFY_WITH_TIMEOUT(startedPlayers().size() == 2 && soundPlaying(), 3000);
        engine->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!playerRunning(), 5000);
    }

    void quittingWhilePlayingEndsThePlayerFirst()
    {
        engine->start();
        QTRY_VERIFY_WITH_TIMEOUT(soundPlaying(), 3000);
        // The destructor runs after the event loop: nothing would delete the killed player later.
        QTest::failOnWarning(QRegularExpression(QStringLiteral("Destroyed while process")));
        sounds.reset();
    }

    void changingModeOffStopsPlayback()
    {
        engine->start();
        QTRY_VERIFY_WITH_TIMEOUT(soundPlaying(), 3000);
        sounds->setMode(static_cast<int>(FocusSoundController::Mode::Off));
        QTRY_VERIFY_WITH_TIMEOUT(!playerRunning(), 5000);
    }

    void changingModeWhilePlayingKeepsThePlayer()
    {
        engine->start();
        QTRY_VERIFY_WITH_TIMEOUT(soundPlaying(), 3000);
        sounds->setMode(static_cast<int>(FocusSoundController::Mode::Rain));
        engine->pause();
        engine->resume();
        sounds->setMode(static_cast<int>(FocusSoundController::Mode::ClockTick));
        QTest::qWait(2500); // longer than a whole fade-out: a player being stopped would be gone
        QVERIFY(playerRunning());
        QCOMPARE(startedPlayers().size(), 1); // the mode changes did not restart it
    }

    void aPlayerThatDiesWhileWorkingIsRestarted()
    {
        engine->start();
        QTRY_VERIFY_WITH_TIMEOUT(soundPlaying(), 3000);
        QCOMPARE(startedPlayers().size(), 1);
        const qint64 first = startedPlayers().first();
        QCOMPARE(QProcess::execute(QStringLiteral("kill"), {QStringLiteral("-KILL"), QString::number(first)}), 0);

        QTRY_VERIFY(!isRunning(first));
        QTRY_VERIFY_WITH_TIMEOUT(startedPlayers().size() == 2 && soundPlaying(), 5000);
    }

    void keepsHalfASecondBufferedAhead()
    {
        engine->start();
        const QString capture = dir.filePath(QStringLiteral("audio.raw"));
        // 500 ms of mono s16 at 44.1 kHz is 44100 bytes, written right at the start.
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(capture).size() >= 44100, 3000);
    }

    void previewNeverChangesTheWorkPhaseVolume()
    {
        engine->start(); // playing at the work volume (45)
        QTRY_VERIFY_WITH_TIMEOUT(soundPlaying(), 3000);
        sounds->preview(static_cast<int>(FocusSoundController::Mode::Rain), 90);
        QCOMPARE(sounds->playbackVolume(), 90);
        QCOMPARE(sounds->volume(), 45);

        // A change of the setting during the preview is kept, not replaced by the preview's.
        sounds->setVolume(60);
        QCOMPARE(sounds->playbackVolume(), 90);
        sounds->stopPreview();
        QCOMPARE(sounds->volume(), 60);
        QCOMPARE(sounds->playbackVolume(), 60);
        // The work phase goes on with its sound: the player is not stopped.
        QTest::qWait(2500); // longer than a whole fade-out
        QVERIFY(playerRunning());
        QCOMPARE(startedPlayers().size(), 1);
    }

    void soundFadesInFromSilence()
    {
        engine->start();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(capture()).size() >= 44100, 3000);
        const QByteArray raw = capturedAudio().left(44100);
        QVERIFY(loudest(raw, 0, 100) < 50);         // starts nearly silent
        QVERIFY(loudest(raw, 20000, 22050) > 1000); // and grows (white noise at 45 %, a quarter in)
    }

    void pausingFadesOutBeforeTheEnd()
    {
        engine->start();
        QTRY_VERIFY_WITH_TIMEOUT(soundPlaying(), 3000);
        QTest::qWait(2500); // fully faded in
        engine->pause();
        QTRY_VERIFY_WITH_TIMEOUT(!playerRunning(), 5000);

        // 1.5 s of fade-out (66150 samples) at the end of what the player got.
        const QByteArray raw = capturedAudio();
        const qsizetype n = raw.size() / 2;
        QVERIFY(n > 90000);
        QVERIFY(loudest(raw, n - 90000, n - 80000) > 3000); // full volume before the pause
        const int fading = loudest(raw, n - 12000, n - 10000);
        QVERIFY(fading > 50 && fading < 3000);              // getting quieter
        QVERIFY(loudest(raw, n - 100, n) < 50);             // silent at the end
    }

    void resumingDuringTheFadeOutKeepsThePlayer()
    {
        engine->start();
        QTRY_VERIFY_WITH_TIMEOUT(soundPlaying(), 3000);
        QTest::qWait(300); // let it fade in a little, so there is something to fade out
        engine->pause();
        engine->resume();
        QTest::qWait(2500); // longer than a whole fade-out
        QVERIFY(playerRunning());
        QCOMPARE(startedPlayers().size(), 1); // still the first player
        const QByteArray raw = capturedAudio();
        QVERIFY(loudest(raw, raw.size() / 2 - 2205, raw.size() / 2) > 1000); // faded back in, not out
    }

    void invalidModesAreClamped()
    {
        sounds->setMode(9);
        QCOMPARE(sounds->mode(), FocusSoundController::LastMode);
        sounds->setMode(-2);
        QCOMPARE(sounds->mode(), 0);
    }
    void theBackendFollowsThePlayersThereAre()
    {
        using Backend = FocusSoundController::Backend;
        const auto with = [](QStringList programs) {
            return FocusSoundController::chooseBackend([programs](const QString &p) { return programs.contains(p); });
        };
#ifdef Q_OS_MACOS
        QCOMPARE(with({QStringLiteral("pw-play"), QStringLiteral("aplay")}), Backend::AudioSink);
#else
        QCOMPARE(with({QStringLiteral("pw-play"), QStringLiteral("aplay")}), Backend::PwPlay);
        QCOMPARE(with({QStringLiteral("aplay")}), Backend::Aplay);
        QCOMPARE(with({}), Backend::AudioSink);
#endif
    }

    void monoAudioIsConvertedForTheDevice()
    {
        QByteArray mono(4, Qt::Uninitialized);
        const qint16 in[2] = {16384, -32768};
        std::memcpy(mono.data(), in, sizeof(in));

        QCOMPARE(FocusSoundController::convertMonoS16(mono, 1, false), mono);
        const QByteArray stereo = FocusSoundController::convertMonoS16(mono, 2, false);
        QCOMPARE(stereo.size(), 8);
        const auto *s16 = reinterpret_cast<const qint16 *>(stereo.constData());
        QCOMPARE(s16[0], qint16(16384));
        QCOMPARE(s16[1], qint16(16384));
        QCOMPARE(s16[2], qint16(-32768));
        QCOMPARE(s16[3], qint16(-32768));

        const QByteArray floats = FocusSoundController::convertMonoS16(mono, 2, true);
        QCOMPARE(floats.size(), 16);
        float f[4];
        std::memcpy(f, floats.constData(), sizeof(f));
        QCOMPARE(f[0], 0.5f);
        QCOMPARE(f[1], 0.5f);
        QCOMPARE(f[2], -1.0f);
        QCOMPARE(f[3], -1.0f);
    }

    void theAudioSinkBackendCopesWithoutADevice()
    {
        // With an audio device it plays (silently here) and stops; without one (CI) it only logs.
        const bool haveDevice = !QMediaDevices::defaultAudioOutput().isNull();
        sounds->setBackendForTesting(FocusSoundController::Backend::AudioSink);
        sounds->setVolume(0);
        engine->start();
        if (haveDevice) {
            QTRY_VERIFY_WITH_TIMEOUT(sounds->isPlaying(), 3000);
        } else {
            QTest::qWait(1500); // failed, retried
            QVERIFY(!sounds->isPlaying());
        }
        engine->pause();
        QTRY_VERIFY_WITH_TIMEOUT(!sounds->isPlaying(), 5000); // nothing audible to fade: stopped
        engine->resume();
        if (haveDevice) {
            QTRY_VERIFY_WITH_TIMEOUT(sounds->isPlaying(), 3000);
        }
        QVERIFY(startedPlayers().isEmpty()); // no player process
        sounds.reset();
    }
};

QTEST_GUILESS_MAIN(FocusSoundControllerTest)
#include "tst_focussoundcontroller.moc"
