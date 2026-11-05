// SPDX-License-Identifier: GPL-3.0-or-later
#include "FocusSoundController.h"

#include "AppSettings.h"
#include "TimerEngine.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSink>
#include <QFile>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <QProcess>
#include <QStandardPaths>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cmath>
#include <cstring>

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

/// A uniformly random sample in -1..1. RAND_MAX (INT_MAX on some systems) is converted to float
/// explicitly: it has no exact float value.
float whiteSample()
{
    return static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 2.0f - 1.0f;
}
} // namespace

/// A player the controller feeds mono s16 PCM at kSampleRate. start() returns at once; the
/// player reports later, through onStarted or onFailed (which also tells that a running player
/// went away).
class FocusSoundOutput : public QObject
{
public:
    using QObject::QObject;

    std::function<void()> onStarted;
    std::function<void()> onFailed;

    virtual void start() = 0;
    virtual bool isRunning() const = 0;
    /// Bytes written but not yet taken by the player.
    virtual qint64 backlogBytes() const = 0;
    virtual void write(const QByteArray &pcm) = 0;
    /// Hands queued audio to the player, where it does not take it by itself.
    virtual void pump() {}
    /// Stops the player without reporting it; the object deletes itself once it has stopped.
    virtual void discard() = 0;
};

namespace {
/// pw-play or aplay, reading raw PCM from its standard input.
class ProcessOutput final : public FocusSoundOutput
{
public:
    ProcessOutput(const QString &program, const QStringList &arguments, QObject *parent)
        : FocusSoundOutput(parent)
        , m_program(program)
        , m_arguments(arguments)
    {
    }

    void start() override
    {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::started, this, [this]() {
            if (!m_discarded && onStarted) {
                onStarted();
            }
        });
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (!m_discarded && error == QProcess::FailedToStart) {
                qCWarning(lcFocusSound) << "Failed to start" << m_program;
                if (onFailed) {
                    onFailed();
                }
            }
        });
        connect(m_process, &QProcess::finished, this, [this]() {
            if (!m_discarded) {
                // The player went away on its own (audio server gone, ...).
                qCWarning(lcFocusSound) << "Audio player exited unexpectedly";
                if (onFailed) {
                    onFailed();
                }
            }
        });
        m_process->start(m_program, m_arguments);
    }

    bool isRunning() const override { return m_process && m_process->state() == QProcess::Running; }
    qint64 backlogBytes() const override { return m_process ? m_process->bytesToWrite() : 0; }
    void write(const QByteArray &pcm) override
    {
        if (m_process) {
            m_process->write(pcm);
        }
    }

    void discard() override
    {
        m_discarded = true; // the handlers above ignore this process from now on
        if (!m_process || m_process->state() == QProcess::NotRunning) {
            deleteLater();
        } else {
            // Kill without waiting; the object goes once the process has really ended.
            connect(m_process, &QProcess::finished, this, &QObject::deleteLater);
            m_process->closeWriteChannel();
            m_process->kill();
        }
    }

private:
    QString m_program;
    QStringList m_arguments;
    QProcess *m_process = nullptr;
    bool m_discarded = false;
};

/// Qt Multimedia's audio output in push mode. Audio the device has no room for yet waits here
/// and goes on the next pump().
class AudioSinkOutput final : public FocusSoundOutput
{
public:
    using FocusSoundOutput::FocusSoundOutput;

    void start() override
    {
        // Asynchronous like a process, so that the controller sees the same order of events.
        QMetaObject::invokeMethod(this, [this]() { open(); }, Qt::QueuedConnection);
    }

    bool isRunning() const override { return m_running; }
    qint64 backlogBytes() const override { return m_pending.size(); }

    void write(const QByteArray &pcm) override
    {
        if (m_running) {
            m_pending.append(pcm);
            pump();
        }
    }

    void pump() override
    {
        if (!m_running || !m_device || m_pending.isEmpty()) {
            return;
        }
        const int frameBytes = m_format.bytesPerFrame();
        const qint64 frames = std::min<qint64>(m_sink->bytesFree() / frameBytes, m_pending.size() / qint64(sizeof(qint16)));
        if (frames <= 0) {
            return;
        }
        const QByteArray mono = m_pending.left(frames * qint64(sizeof(qint16)));
        const QByteArray out = m_float || m_format.channelCount() != 1
            ? FocusSoundController::convertMonoS16(mono, m_format.channelCount(), m_float)
            : mono;
        const qint64 written = m_device->write(out);
        if (written > 0) {
            m_pending.remove(0, (written / frameBytes) * qint64(sizeof(qint16)));
        }
    }

    void discard() override
    {
        m_discarded = true;
        m_running = false;
        if (m_sink) {
            m_sink->stop(); // what is left is the silence at the end of the fade-out
        }
        deleteLater();
    }

private:
    void open()
    {
        if (m_discarded) {
            return;
        }
        const QAudioDevice device = QMediaDevices::defaultAudioOutput();
        if (device.isNull()) {
            qCWarning(lcFocusSound) << "No audio output device: the focus sound cannot play";
            fail();
            return;
        }
        // The generated format first; else one the device takes, converted in pump().
        bool found = false;
        for (const auto &[channels, sampleFormat] : std::array<std::pair<int, QAudioFormat::SampleFormat>, 4>{{
                 {1, QAudioFormat::Int16}, {2, QAudioFormat::Int16}, {1, QAudioFormat::Float}, {2, QAudioFormat::Float}}}) {
            m_format.setSampleRate(kSampleRate);
            m_format.setChannelCount(channels);
            m_format.setSampleFormat(sampleFormat);
            if (device.isFormatSupported(m_format)) {
                found = true;
                break;
            }
        }
        if (!found) {
            qCWarning(lcFocusSound) << "The audio output" << device.description() << "takes no 44.1 kHz format: the focus sound cannot play";
            fail();
            return;
        }
        m_float = m_format.sampleFormat() == QAudioFormat::Float;

        m_sink = new QAudioSink(device, m_format, this);
        m_sink->setBufferSize(m_format.bytesForDuration(1000000)); // room for the whole lead
        connect(m_sink, &QAudioSink::stateChanged, this, [this](QAudio::State state) {
            // Running dry (IdleState, UnderrunError) is not a failure: more audio resumes it.
            if (m_running && state == QAudio::StoppedState && m_sink->error() != QAudio::NoError) {
                qCWarning(lcFocusSound) << "The audio output stopped with error" << m_sink->error();
                fail();
            }
        });
        m_device = m_sink->start();
        if (!m_device || m_sink->error() != QAudio::NoError) {
            qCWarning(lcFocusSound) << "Cannot open the audio output" << device.description() << "error" << m_sink->error();
            fail();
            return;
        }
        m_running = true;
        if (onStarted) {
            onStarted();
        }
    }

    void fail()
    {
        m_running = false;
        if (!m_discarded && onFailed) {
            onFailed();
        }
    }

    QAudioSink *m_sink = nullptr;
    QIODevice *m_device = nullptr; ///< Owned by m_sink.
    QAudioFormat m_format;
    bool m_float = false;
    QByteArray m_pending; ///< Mono s16, not yet taken by the device.
    bool m_running = false;
    bool m_discarded = false;
};
} // namespace

FocusSoundController::Backend FocusSoundController::chooseBackend(const std::function<bool(const QString &)> &programExists)
{
#ifdef Q_OS_MACOS
    Q_UNUSED(programExists)
    return Backend::AudioSink;
#else
    if (programExists(QStringLiteral("pw-play"))) {
        return Backend::PwPlay;
    }
    if (programExists(QStringLiteral("aplay"))) {
        return Backend::Aplay;
    }
    return Backend::AudioSink;
#endif
}

QByteArray FocusSoundController::convertMonoS16(const QByteArray &pcm, int channelCount, bool floatSamples)
{
    channelCount = std::max(channelCount, 1);
    const qsizetype frames = pcm.size() / qsizetype(sizeof(qint16));
    const qsizetype sampleBytes = floatSamples ? qsizetype(sizeof(float)) : qsizetype(sizeof(qint16));
    QByteArray out(frames * channelCount * sampleBytes, Qt::Uninitialized);
    const char *in = pcm.constData();
    char *dst = out.data();
    for (qsizetype i = 0; i < frames; ++i) {
        const qint16 sample = qFromLittleEndian<qint16>(in + i * qsizetype(sizeof(qint16)));
        for (int c = 0; c < channelCount; ++c) {
            if (floatSamples) {
                const float value = float(sample) / 32768.0f;
                std::memcpy(dst, &value, sizeof(value));
            } else {
                std::memcpy(dst, &sample, sizeof(sample));
            }
            dst += sampleBytes;
        }
    }
    return out;
}

FocusSoundController::FocusSoundController(QObject *parent)
    : QObject(parent)
{
    m_feedTimer.setInterval(kFeedIntervalMs);
    m_feedTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_feedTimer, &QTimer::timeout, this, &FocusSoundController::feedAudio);

    m_previewTimer.setSingleShot(true);
    connect(&m_previewTimer, &QTimer::timeout, this, &FocusSoundController::stopPreview);

    m_stopTimer.setSingleShot(true);
    connect(&m_stopTimer, &QTimer::timeout, this, &FocusSoundController::discardOutput);
}

FocusSoundController::~FocusSoundController()
{
    discardOutput();
    // On quit no event loop deletes a killed player once it has ended: wait for it briefly here,
    // or it is destroyed while still running.
    const auto players = findChildren<QProcess *>();
    for (QProcess *player : players) {
        player->kill();
        player->waitForFinished(1000);
    }
}

bool FocusSoundController::isPlaying() const
{
    return m_output && m_output->isRunning();
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
    if (!m_output) {
        startPlayback();
    } else if (m_stopping) {
        // Wanted again while fading out: fade back in on the same player.
        m_stopping = false;
        m_stopTimer.stop();
    }
}

void FocusSoundController::startPlayback()
{
    discardOutput();

    const Backend backend = m_forcedBackend.value_or(chooseBackend([](const QString &program) {
        return !QStandardPaths::findExecutable(program).isEmpty();
    }));
    FocusSoundOutput *output = nullptr;
    switch (backend) {
    case Backend::PwPlay:
        output = new ProcessOutput(QStringLiteral("pw-play"), {
            QStringLiteral("--rate"), QString::number(kSampleRate),
            QStringLiteral("--channels"), QStringLiteral("1"),
            QStringLiteral("--format"), QStringLiteral("s16"),
            QStringLiteral("--raw"),
            QStringLiteral("-")
        }, this);
        break;
    case Backend::Aplay:
        output = new ProcessOutput(QStringLiteral("aplay"), {
            QStringLiteral("-r"), QString::number(kSampleRate),
            QStringLiteral("-c"), QStringLiteral("1"),
            QStringLiteral("-f"), QStringLiteral("S16_LE"),
            QStringLiteral("-")
        }, this);
        break;
    case Backend::AudioSink:
#ifndef Q_OS_MACOS
        if (!m_forcedBackend) {
            qCInfo(lcFocusSound) << "Neither pw-play nor aplay found: playing through Qt Multimedia";
        }
#endif
        output = new AudioSinkOutput(this);
        break;
    }

    m_output = output;
    output->onStarted = [this, output]() {
        if (output == m_output) {
            onPlayerStarted();
        }
    };
    output->onFailed = [this, output]() {
        if (output == m_output) {
            onPlayerFailed();
        }
    };
    output->start();
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
    discardOutput();
    if (m_restarts >= kMaxRestarts) {
        return;
    }
    ++m_restarts;
    QTimer::singleShot(kRestartDelayMs, this, [this]() {
        if (m_output) {
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
    if (!m_output || m_stopping) {
        return;
    }
    if (!m_output->isRunning() || m_gain <= 0.0f) {
        discardOutput(); // nothing audible to fade
        return;
    }
    m_stopping = true;
}

void FocusSoundController::discardOutput()
{
    m_feedTimer.stop();
    m_stopTimer.stop();
    m_stopping = false;
    if (FocusSoundOutput *output = m_output.data()) {
        m_output = nullptr; // the callbacks above ignore this player from now on
        output->discard();
    }
}

void FocusSoundController::feedAudio()
{
    if (!m_output || !m_output->isRunning()) {
        return;
    }
    m_output->pump();

    // A fade-out ends the sound that was playing, as loud as it was: the end of a preview fades
    // out the preview, not the sound and volume set meanwhile.
    if (!m_stopping) {
        const int activeMode = m_previewing ? m_previewMode : m_mode;
        if (activeMode > 0) {
            m_soundMode = activeMode;
        }
        m_soundVolume = playbackVolume();
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

    // The pipe or queue itself is not draining (player stuck): do not pile up more.
    if (m_output->backlogBytes() > qint64(kSampleRate) * 2) {
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
        m_output->write(generateChunk(kChunkSamples));
        m_samplesWritten += kChunkSamples;
    }
}

QByteArray FocusSoundController::generateChunk(int numSamples)
{
    QByteArray data;
    data.resize(numSamples * sizeof(qint16));
    auto *samples = reinterpret_cast<qint16 *>(data.data());

    const float volFactor = (m_soundVolume / 100.0f);
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
            const float white = whiteSample();
            m_rainFilter = (m_rainFilter * 0.94f) + (white * 0.06f);
            s = m_rainFilter * 3.5f;
        } else if (currentMode == static_cast<int>(Mode::WhiteNoise)) {
            // White noise
            s = whiteSample() * 0.5f;
        } else if (currentMode == static_cast<int>(Mode::BrownNoise) || currentMode == static_cast<int>(Mode::Waves)) {
            // Brown noise: leaky integration of white noise, a deep rumble
            const float white = whiteSample();
            m_brown = (m_brown + 0.02f * white) / 1.02f;
            s = m_brown * 3.5f;
            if (currentMode == static_cast<int>(Mode::Waves)) {
                // Waves: the rumble swells and ebbs every 8 seconds
                const float swell = std::sin(static_cast<float>(M_PI) * static_cast<float>(m_sampleIndex % (kSampleRate * 8)) / (kSampleRate * 8));
                s *= 0.25f + 0.75f * swell * swell;
            }
        } else if (currentMode == static_cast<int>(Mode::PinkNoise)) {
            // Pink noise (Paul Kellet's economy filter): softer than white, brighter than brown
            const float white = whiteSample();
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
