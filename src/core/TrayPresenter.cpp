// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrayPresenter.h"

#include "Localization.h"

QString formatClock(int totalSeconds)
{
    totalSeconds = qMax(0, totalSeconds);
    const int hours = totalSeconds / 3600;
    const int minutes = (totalSeconds % 3600) / 60;
    const int seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

namespace
{
QString phaseName(TimerEngine::Phase phase)
{
    switch (phase) {
    case TimerEngine::Phase::Work:
        return i18n("Work");
    case TimerEngine::Phase::ShortBreak:
        return i18n("Short break");
    case TimerEngine::Phase::LongBreak:
        return i18n("Long break");
    }
    Q_UNREACHABLE_RETURN(QString());
}
} // namespace

TrayStatus describeTray(TimerEngine::State state, TimerEngine::Phase phase, int remainingSeconds, const QString &presetName)
{
    TrayStatus status;
    const QString clock = formatClock(remainingSeconds);
    const QString phaseLabel = phaseName(phase);

    switch (state) {
    case TimerEngine::State::Idle:
        status.toolTipTitle = i18n("Sugo: ready");
        status.toolTipText = i18n("Next: %1, %2\n%3", phaseLabel, clock, presetName);
        break;
    case TimerEngine::State::Paused:
        status.toolTipTitle = i18n("%1 (paused)", phaseLabel);
        status.toolTipText = i18n("%1 left\n%2", clock, presetName);
        status.badgeMinutes = (remainingSeconds + 59) / 60;
        break;
    default:
        status.toolTipTitle = phaseLabel;
        status.toolTipText = i18n("%1 left\n%2", clock, presetName);
        status.badgeMinutes = (remainingSeconds + 59) / 60;
        break;
    }
    return status;
}

QString menuStatusLine(TimerEngine::State state, TimerEngine::Phase phase, int remainingSeconds, const QString &presetName)
{
    QString phaseLabel = phaseName(phase);
    if (state == TimerEngine::State::Paused) {
        phaseLabel = i18n("%1 (paused)", phaseLabel);
    }
    const QString name = presetName.isEmpty() ? i18n("Sugo") : presetName;
    return i18nc("@item:inmenu Tray menu status line: %1 is the phase (e.g. Work), %2 the time left (e.g. 12:30), "
                 "%3 the name of the timer",
                 "%1 · %2 · %3", phaseLabel, formatClock(remainingSeconds), name);
}
