// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

#include <functional>

#include "TimerEngine.h"

class NotificationInhibitor;
class Notifier;
class PresetModel;
class AppSettings;

/**
 * Tells the user that a phase ended, honouring the current preset's
 * "notification" and "sound" options.
 *
 * The sound is played by Sugo itself (AppSettings::playSoundPreview), so the chosen sound and
 * volume apply on every desktop; the popup goes through a Notifier, and is silent.
 *
 * When a work phase ends while notifications are silenced, the notification is held
 * back until the silencing has really ended; otherwise the desktop would swallow the
 * very message announcing the break.
 *
 * The actions of a notification only act while the timer is still where the notification
 * left it: once another phase has started, they do nothing (a late "Skip Break" never skips
 * the following work phase).
 */
class PhaseNotifier : public QObject
{
    Q_OBJECT

public:
    /// Without a `notifier` there are no popups; without `settings`, no sounds and no task notes.
    PhaseNotifier(TimerEngine *engine, PresetModel *presets, NotificationInhibitor *inhibitor, Notifier *notifier,
                  AppSettings *settings = nullptr, QObject *parent = nullptr);

    /// Returns the id of the work session recorded last (<= 0 if unknown). Asked when a work
    /// phase ends; without it, notifications do not offer to log a task note.
    void setLastWorkSessionIdProvider(std::function<qint64()> provider);

    /// How long an announcement waits for the silencing to end before going out anyway.
    void setReleaseTimeoutMs(int ms) { m_releaseTimeoutMs = ms; }

Q_SIGNALS:
    /// The user wants to write a note for the work session `sessionId`. `activationToken`, if
    /// not empty, lets the window take the focus on Wayland.
    void taskNotePromptRequested(const QString &presetName, qint64 sessionId, const QString &activationToken);
    /// The user wrote `note` for the work session `sessionId` straight in the notification.
    void taskNoteSubmitted(qint64 sessionId, const QString &note);
    /// The notification was clicked and there is nothing more specific to do: show the window.
    void showWindowRequested(const QString &activationToken);
    /// The end of `finished` was announced (sound and/or popup), now that notifications are back.
    void phaseEndAnnounced(TimerEngine::Phase finished);

private Q_SLOTS:
    void onPhaseFinished(TimerEngine::Phase finished, TimerEngine::Phase next);
    void onOneMinuteRemaining(TimerEngine::Phase phase);

private:
    TimerEngine *m_engine;
    PresetModel *m_presets;
    NotificationInhibitor *m_inhibitor;
    Notifier *m_notifier;
    AppSettings *m_settings;
    std::function<qint64()> m_lastWorkSessionId;
    int m_releaseTimeoutMs = 2000;
    quint64 m_phaseSerial = 0; ///< Counts phaseStarted(), so stale notification actions can be ignored.
};
