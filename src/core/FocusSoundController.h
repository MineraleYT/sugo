// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QTimer>

class TimerEngine;
class AppSettings;

/// Generates and streams soothing ambient focus sounds (clock tick, rain, white, brown and pink
/// noise, waves) during work phases, automatically starting on work and stopping on break or
/// pause. The sound fades in when it starts and fades out before the player is stopped.
///
/// The player process is fed raw PCM a little ahead of real time (about half a second), so a
/// busy GUI thread does not make it run dry. Nothing here blocks: the player is started and
/// stopped asynchronously.
class FocusSoundController : public QObject
{
    Q_OBJECT

public:
    enum class Mode {
        Off = 0,
        ClockTick = 1,
        Rain = 2,
        WhiteNoise = 3,
        BrownNoise = 4,
        PinkNoise = 5,
        Waves = 6
    };
    Q_ENUM(Mode)
    static constexpr int LastMode = static_cast<int>(Mode::Waves);

    explicit FocusSoundController(QObject *parent = nullptr);
    ~FocusSoundController() override;

    void attach(TimerEngine *engine, AppSettings *settings);

    int mode() const { return m_mode; }
    void setMode(int mode);

    int volume() const { return m_volume; }
    void setVolume(int volume);
    /// The volume the player uses now: the preview's while one plays, else volume().
    int playbackVolume() const { return m_previewing ? m_previewVolume : m_volume; }

    /// True while the sound plays or fades in; false as soon as it starts fading out.
    bool isPlaying() const { return m_isPlaying; }

    Q_INVOKABLE void preview(int mode, int volume);
    Q_INVOKABLE void stopPreview();

public Q_SLOTS:
    void updatePlayback();

Q_SIGNALS:
    void isPlayingChanged();

private Q_SLOTS:
    void feedAudio();

private:
    void play();
    void startPlayback();
    void stopPlayback();
    void setPlaying(bool playing);
    void onPlayerStarted();
    void onPlayerFailed();
    void discardProcess();
    QByteArray generateChunk(int numSamples);

    TimerEngine *m_engine = nullptr;
    AppSettings *m_settings = nullptr;

    int m_mode = 0;
    int m_previewMode = 0;
    int m_volume = 30;
    int m_previewVolume = 30; ///< Kept apart, so a preview never changes the work-phase volume.
    bool m_isPlaying = false;
    bool m_previewing = false;

    QPointer<QProcess> m_process; ///< The current player, starting or running; null when stopped.
    int m_restarts = 0;           ///< Players restarted after a failure since the sound was last off.
    QTimer m_feedTimer;
    QTimer m_previewTimer;
    QTimer m_stopTimer;          ///< Kills the player once the faded-out audio has been played.
    bool m_stopping = false;     ///< Fading out, to be stopped.
    int m_soundMode = 0;         ///< The sound being generated; kept through a fade-out to Off.
    float m_gain = 0.0f;         ///< Fade level, 0..1.

    QElapsedTimer m_playClock;   ///< Started when the player started.
    qint64 m_samplesWritten = 0; ///< Since the player started (or since the last resync).
    qint64 m_sampleIndex = 0;
    float m_rainFilter = 0.0f;
    float m_brown = 0.0f;
    float m_pink[3] = {};
};
