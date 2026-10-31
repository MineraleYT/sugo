// SPDX-License-Identifier: GPL-3.0-or-later
#include "FocusSoundController.h"

#include "AppSettings.h"
#include "TimerEngine.h"

#include <QFile>
#include <QLoggingCategory>
#include <QStandardPaths>
#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace {
Q_LOGGING_CATEGORY(lcFocusSound, "sugo.audio.focussound")
constexpr int kSampleRate = 44100;
constexpr int kChunkSamples = 2205;          // 50 ms of audio
constexpr int kAheadSamples = kSampleRate / 2; // keep 500 ms queued ahead of playback
constexpr int kFeedIntervalMs = 50;
constexpr int kMaxRestarts = 3;
constexpr int kRestartDelayMs = 1000;
constexpr float kFadeInStep = 1.0f / (kSampleRate * 2);          // 2 s from silence to full
constexpr float kFadeOutStep = 1.0f / (kSampleRate * 3 / 2);     // 1.5 s from full to silence
constexpr int kAheadMs = kAheadSamples * 1000 / kSampleRate;
} // namespace

FocusSoundController::FocusSoundController(QObject *parent)
    : QObject(parent)
{
    m_feedTimer.setInterval(kFeedIntervalMs);
    m_feedTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_feedTimer, &QTimer::timeout, this, &FocusSoundController::feedAudio);

    m_previewTimer.setSingleShot(true);
    connect(&m_previewTimer, &QTimer::timeout, this, &FocusSoundController::stopPreview);

    m_stopTimer.setSingleShot(true);
    connect(&m_stopTimer, &QTimer::timeout, this, &FocusSoundController::discardProcess);
}

FocusSoundController::~FocusSoundController()
{
    discardProcess();
    // On quit no event loop deletes a killed player once it has ended: wait for it briefly here,
    // or it is destroyed while still running.
    const auto players = findChildren<QProcess *>();
    for (QProcess *player : players) {
        player->kill();
        player->waitForFinished(1000);
    }
}

void FocusSoundController::attach(TimerEngine *engine, AppSettings *settings)
{
    m_engine = engine;
    m_settings = settings;

    if (m_engine) {
        connect(m_engine, &TimerEngine::stateChanged, this, &FocusSoundController::updatePlayback);
    }
    if (m_settings) {
        m_mode = std::clamp(m_settings->focusSoundMode(), 0, LastMode);
        m_volume = m_settings->focusSoundVolume();
        connect(m_settings, &AppSettings::focusSoundModeChanged, this, [this]() {
            setMode(m_settings->focusSoundMode());
        });
        connect(m_settings, &AppSettings::focusSoundVolumeChanged, this, [this]() {
            setVolume(m_settings->focusSoundVolume());
        });
    }

    updatePlayback();
}

void FocusSoundController::setMode(int mode)
{
    mode = std::clamp(mode, 0, LastMode);
    if (m_mode == mode) {
        return;
    }
    m_mode = mode;
    updatePlayback();
}

void FocusSoundController::setVolume(int volume)
{
    volume = std::clamp(volume, 0, 100);
    if (m_volume == volume) {
        return;
    }
    m_volume = volume;
}

void FocusSoundController::preview(int mode, int volume)
{
    mode = std::clamp(mode, 0, LastMode);
    if (mode <= 0) {
        stopPreview();
        return;
    }
    m_previewing = true;
    m_previewMode = mode;
    m_previewVolume = std::clamp(volume, 0, 100);
    // generateChunk() follows the mode live, so a running player just changes sound.
    play();
    m_previewTimer.start(5000); // 5 seconds preview
}

void FocusSoundController::stopPreview()
{
    if (m_previewing) {
        m_previewTimer.stop();
        m_previewing = false;
        m_previewMode = 0;
        // Stops the player unless a work phase wants it to go on.
        const bool shouldPlay = m_engine && m_engine->state() == TimerEngine::State::Working && m_mode > 0;
        if (!shouldPlay) {
            stopPlayback();
        }
    }
}

void FocusSoundController::updatePlayback()
{
    if (m_previewing) {
        return;
    }

    // A mode change while playing needs no restart: generateChunk() reads the mode live.
    const bool shouldPlay = m_engine && m_engine->state() == TimerEngine::State::Working && m_mode > 0;
    if (!shouldPlay) {
        m_restarts = 0; // the next time the sound is wanted, a failing player gets new chances
    }
    if (shouldPlay) {
        play();
    } else {
        stopPlayback();
    }
}

void FocusSoundController::play()
{
    if (!m_process) {
        startPlayback();
    } else if (m_stopping) {
        // Wanted again while fading out: fade back in on the same player.
        m_stopping = false;
        m_stopTimer.stop();
    }
}

void FocusSoundController::startPlayback()
{
    discardProcess();

    QString program;
    QStringList args;
    if (!QStandardPaths::findExecutable(QStringLiteral("pw-play")).isEmpty()) {
        program = QStringLiteral("pw-play");
        args = {
            QStringLiteral("--rate"), QString::number(kSampleRate),
            QStringLiteral("--channels"), QStringLiteral("1"),
            QStringLiteral("--format"), QStringLiteral("s16"),
            QStringLiteral("--raw"),
            QStringLiteral("-")
        };
    } else if (!QStandardPaths::findExecutable(QStringLiteral("aplay")).isEmpty()) {
        program = QStringLiteral("aplay");
        args = {
            QStringLiteral("-r"), QString::number(kSampleRate),
            QStringLiteral("-c"), QStringLiteral("1"),
            QStringLiteral("-f"), QStringLiteral("S16_LE"),
            QStringLiteral("-")
        };
    } else {
        qCWarning(lcFocusSound) << "No suitable audio player (pw-play or aplay) found";
        return;
    }

    auto *process = new QProcess(this);
    m_process = process;
    connect(process, &QProcess::started, this, [this, process]() {
        if (process == m_process) {
            onPlayerStarted();
        }
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, program](QProcess::ProcessError error) {
        if (process == m_process && error == QProcess::FailedToStart) {
            qCWarning(lcFocusSound) << "Failed to start" << program;
            onPlayerFailed();
        }
    });
    connect(process, &QProcess::finished, this, [this, process]() {
        if (process == m_process) {
            // The player went away on its own (audio server gone, ...).
            qCWarning(lcFocusSound) << "Audio player exited unexpectedly";
            onPlayerFailed();
        }
    });
    process->start(program, args);
}

void FocusSoundController::onPlayerStarted()
{
    m_sampleIndex = 0;
    m_samplesWritten = 0;
    m_rainFilter = 0.0f;
    m_brown = 0.0f;
    std::fill(std::begin(m_pink), std::end(m_pink), 0.0f);
    m_gain = 0.0f;
    m_playClock.start();

    feedAudio(); // fills the initial lead
    m_feedTimer.start();
}

void FocusSoundController::onPlayerFailed()
{
    discardProcess();
    if (m_restarts >= kMaxRestarts) {
        return;
    }
    ++m_restarts;
    QTimer::singleShot(kRestartDelayMs, this, [this]() {
        if (m_process) {
            return;
        }
        // Only if the sound is still wanted by then.
        if (m_previewing) {
            startPlayback();
        } else {
            updatePlayback();
        }
    });
}

void FocusSoundController::stopPlayback()
{
    if (!m_process || m_stopping) {
        return;
    }
    if (m_process->state() != QProcess::Running || m_gain <= 0.0f) {
        discardProcess(); // nothing audible to fade
        return;
    }
    m_stopping = true;
}

void FocusSoundController::discardProcess()
{
    m_feedTimer.stop();
    m_stopTimer.stop();
    m_stopping = false;
    if (QProcess *process = m_process.data()) {
        m_process = nullptr; // the handlers above ignore this process from now on
        if (process->state() == QProcess::NotRunning) {
            process->deleteLater();
        } else {
            // Kill without waiting; the object goes once the process has really ended.
            connect(process, &QProcess::finished, process, &QObject::deleteLater);
            process->closeWriteChannel();
            process->kill();
        }
    }
}

void FocusSoundController::feedAudio()
{
    if (!m_process || m_process->state() != QProcess::Running) {
        return;
    }

    const int activeMode = m_previewing ? m_previewMode : m_mode;
    if (activeMode > 0) {
        m_soundMode = activeMode;
    }
    if (m_soundMode <= 0) {
        return;
    }
    if (m_stopping && m_gain <= 0.0f) {
        // Faded out: stop once what is queued (silence at its end) has been played.
        if (!m_stopTimer.isActive()) {
            m_stopTimer.start(kAheadMs + 100);
        }
        return;
    }

    // The pipe itself is not draining (player stuck): do not pile up more.
    if (m_process->bytesToWrite() > qint64(kSampleRate) * 2) {
        return;
    }

    // Keep the written audio about kAheadSamples ahead of what has been played by now. After a
    // stall longer than the lead the player has run dry already; resync instead of sending the
    // whole backlog, which would only add latency.
    const qint64 played = m_playClock.elapsed() * kSampleRate / 1000;
    if (m_samplesWritten < played) {
        m_samplesWritten = played;
    }
    const qint64 target = played + kAheadSamples;
    while (m_samplesWritten < target) {
        m_process->write(generateChunk(kChunkSamples));
        m_samplesWritten += kChunkSamples;
    }
}

QByteArray FocusSoundController::generateChunk(int numSamples)
{
    QByteArray data;
    data.resize(numSamples * sizeof(qint16));
    auto *samples = reinterpret_cast<qint16 *>(data.data());

    const float volFactor = (playbackVolume() / 100.0f);
    const int currentMode = m_soundMode;

    for (int i = 0; i < numSamples; ++i, ++m_sampleIndex) {
        float s = 0.0f;
        m_gain = m_stopping ? std::max(0.0f, m_gain - kFadeOutStep) : std::min(1.0f, m_gain + kFadeInStep);

        if (currentMode == static_cast<int>(Mode::ClockTick)) {
            // Crisp ticking clock: short impulse at the beginning of each second
            const int inSec = m_sampleIndex % kSampleRate;
            if (inSec < 882) { // 20ms duration
                const float t = static_cast<float>(inSec) / kSampleRate;
                const float freq = ((m_sampleIndex / kSampleRate) % 2 == 0) ? 1400.0f : 1050.0f;
                const float env = std::exp(-t * 260.0f);
                s = std::sin(2.0f * static_cast<float>(M_PI) * freq * t) * env;
            }
        } else if (currentMode == static_cast<int>(Mode::Rain)) {
            // Gentle rain: low-pass filtered noise with soothing texture
            const float white = ((static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f);
            m_rainFilter = (m_rainFilter * 0.94f) + (white * 0.06f);
            s = m_rainFilter * 3.5f;
        } else if (currentMode == static_cast<int>(Mode::WhiteNoise)) {
            // White noise
            s = ((static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f) * 0.5f;
        } else if (currentMode == static_cast<int>(Mode::BrownNoise) || currentMode == static_cast<int>(Mode::Waves)) {
            // Brown noise: leaky integration of white noise, a deep rumble
            const float white = ((static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f);
            m_brown = (m_brown + 0.02f * white) / 1.02f;
            s = m_brown * 3.5f;
            if (currentMode == static_cast<int>(Mode::Waves)) {
                // Waves: the rumble swells and ebbs every 8 seconds
                const float swell = std::sin(static_cast<float>(M_PI) * static_cast<float>(m_sampleIndex % (kSampleRate * 8)) / (kSampleRate * 8));
                s *= 0.25f + 0.75f * swell * swell;
            }
        } else if (currentMode == static_cast<int>(Mode::PinkNoise)) {
            // Pink noise (Paul Kellet's economy filter): softer than white, brighter than brown
            const float white = ((static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f);
            m_pink[0] = 0.99765f * m_pink[0] + white * 0.0990460f;
            m_pink[1] = 0.96300f * m_pink[1] + white * 0.2965164f;
            m_pink[2] = 0.57000f * m_pink[2] + white * 1.0526913f;
            s = (m_pink[0] + m_pink[1] + m_pink[2] + white * 0.1848f) * 0.12f;
        }

        s = std::clamp(s * volFactor * m_gain, -1.0f, 1.0f);
        samples[i] = static_cast<qint16>(s * 32767.0f);
    }

    return data;
}
