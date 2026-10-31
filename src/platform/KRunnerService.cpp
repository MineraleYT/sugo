// SPDX-License-Identifier: GPL-3.0-or-later
#include "KRunnerService.h"

#include "PresetModel.h"
#include "TimerEngine.h"

#include "Localization.h"
#include <QRegularExpression>

KRunnerService::KRunnerService(TimerEngine *timer, PresetModel *presets, QObject *parent)
    : QObject(parent)
    , m_timer(timer)
    , m_presets(presets)
{
    registerMetaTypes();
}

void KRunnerService::registerMetaTypes()
{
    static bool registered = false;
    if (!registered) {
        qDBusRegisterMetaType<RemoteMatch>();
        qDBusRegisterMetaType<QList<RemoteMatch>>();
        qDBusRegisterMetaType<RemoteAction>();
        qDBusRegisterMetaType<QList<RemoteAction>>();
        registered = true;
    }
}

namespace {
// KRunner::QueryMatch::CategoryRelevance values (Plasma 6).
constexpr int CategoryModerate = 50;
constexpr int CategoryHigh = 70;
constexpr int CategoryHighest = 100;

constexpr int kMaxCustomMinutes = 180;
const QString kInvalidMinutesId = QStringLiteral("invalid-minutes");
const QString kControlDisabledId = QStringLiteral("control-disabled");

const QString ActionsKey = QStringLiteral("actions");
const QString SubtextKey = QStringLiteral("subtext");

RemoteMatch commandMatch(const QString &id, const QString &text, const QString &subtext, const QString &icon)
{
    RemoteMatch m;
    m.id = id;
    m.text = text;
    m.iconName = icon;
    if (!subtext.isEmpty()) {
        m.properties[SubtextKey] = subtext;
    }
    // The match itself is the command: no extra buttons.
    m.properties[ActionsKey] = QStringList();
    m.categoryRelevance = CategoryHighest;
    m.relevance = 1.0;
    return m;
}
} // namespace

QList<RemoteAction> KRunnerService::Actions()
{
    return {
        {QStringLiteral("start"), i18n("Start"), QStringLiteral("media-playback-start")},
        {QStringLiteral("pause"), i18n("Pause"), QStringLiteral("media-playback-pause")},
        {QStringLiteral("resume"), i18n("Resume"), QStringLiteral("media-playback-start")},
        {QStringLiteral("skip"), i18n("Skip"), QStringLiteral("media-skip-forward")},
        {QStringLiteral("stop"), i18n("Stop"), QStringLiteral("media-playback-stop")},
    };
}

QList<RemoteMatch> KRunnerService::Match(const QString &searchTerm)
{
    QList<RemoteMatch> matches;

    // The keyword must be a whole word: "tomatoes" or "timers" are not ours.
    static const QRegularExpression keywordRegex(QStringLiteral("^(?:pomodoro|sugo|tomato|timer)(?:\\s+(.*))?$"),
                                                 QRegularExpression::CaseInsensitiveOption);
    const auto keywordMatch = keywordRegex.match(searchTerm.trimmed());
    if (!keywordMatch.hasMatch()) {
        return matches;
    }
    const QString subCommand = keywordMatch.captured(1).trimmed().toLower();

    if (!controlAllowed()) {
        RemoteMatch info = commandMatch(kControlDisabledId, i18n("Sugo: control from other programs is turned off"),
                                        i18n("Open Sugo to change this in its settings"), QStringLiteral("io.github.mineraleyt.sugo"));
        info.properties.insert(ActionsKey, QStringList());
        matches.append(info);
        return matches;
    }

    const int remSec = m_timer->remainingSeconds();
    const QString timeStr = QString::asprintf("%02d:%02d", remSec / 60, remSec % 60);

    QString phaseStr = i18n("Work");
    if (m_timer->phase() == TimerEngine::Phase::ShortBreak) {
        phaseStr = i18n("Short break");
    } else if (m_timer->phase() == TimerEngine::Phase::LongBreak) {
        phaseStr = i18n("Long break");
    }
    const QString presetName = m_presets ? m_presets->currentName() : QString();

    // 1. Explicit commands: "pomodoro start" and so on.
    if (subCommand == QStringLiteral("start")) {
        matches.append(commandMatch(QStringLiteral("start"), i18n("Start Pomodoro Timer"), i18n("Preset: %1", presetName),
                                    QStringLiteral("media-playback-start")));
        return matches;
    }
    if (subCommand == QStringLiteral("pause")) {
        matches.append(commandMatch(QStringLiteral("pause"), i18n("Pause Pomodoro Timer"), i18n("%1 remaining", timeStr),
                                    QStringLiteral("media-playback-pause")));
        return matches;
    }
    if (subCommand == QStringLiteral("resume")) {
        matches.append(commandMatch(QStringLiteral("resume"), i18n("Resume Pomodoro Timer"), i18n("%1 remaining", timeStr),
                                    QStringLiteral("media-playback-start")));
        return matches;
    }
    if (subCommand == QStringLiteral("stop")) {
        matches.append(commandMatch(QStringLiteral("stop"), i18n("Stop Pomodoro Timer"), i18n("Reset session"),
                                    QStringLiteral("media-playback-stop")));
        return matches;
    }
    if (subCommand == QStringLiteral("skip")) {
        matches.append(commandMatch(QStringLiteral("skip"), i18n("Skip to Next Phase"),
                                    i18n("Advance to the next work or break phase"), QStringLiteral("media-skip-forward")));
        return matches;
    }

    // 2. One-off work phase: e.g. "pomodoro 15". The preset's own duration is not changed.
    // ASCII digits only (\\d would also take other scripts' digits, which toInt() cannot read).
    static const QRegularExpression numRegex(QStringLiteral("^([0-9]+)$"));
    const auto matchNum = numRegex.match(subCommand);
    if (matchNum.hasMatch()) {
        bool ok = false;
        const int mins = matchNum.captured(1).toInt(&ok); // ok is false on overflow
        if (ok && mins > 0 && mins <= kMaxCustomMinutes) {
            matches.append(commandMatch(QStringLiteral("custom:") + QString::number(mins), i18n("Start %1-minute timer", mins),
                                        i18n("Start custom focus session"), QStringLiteral("chronometer")));
        } else {
            // Say why nothing can start, instead of showing nothing at all.
            matches.append(commandMatch(kInvalidMinutesId, i18n("Choose between 1 and %1 minutes", kMaxCustomMinutes),
                                        i18n("A custom focus session lasts 1 to %1 minutes.", kMaxCustomMinutes),
                                        QStringLiteral("dialog-information")));
        }
        return matches;
    }

    // 3. Presets by name: "pomodoro deep" starts "Deep work".
    if (!subCommand.isEmpty()) {
        for (int row = 0; m_presets && row < m_presets->count(); ++row) {
            const QModelIndex index = m_presets->index(row, 0);
            const QString name = index.data(PresetModel::NameRole).toString();
            const QString lowerName = name.toLower();
            if (!lowerName.contains(subCommand)) {
                continue;
            }
            RemoteMatch m = commandMatch(QStringLiteral("preset:") + index.data(PresetModel::UuidRole).toString(),
                                         i18n("Start preset %1", name),
                                         i18np("%1 minute of work", "%1 minutes of work",
                                               index.data(PresetModel::WorkMinutesRole).toInt()),
                                         index.data(PresetModel::IconNameRole).toString());
            if (m.iconName.isEmpty()) {
                m.iconName = QStringLiteral("io.github.mineraleyt.sugo");
            }
            m.categoryRelevance = CategoryHigh;
            m.relevance = lowerName == subCommand ? 1.0 : (lowerName.startsWith(subCommand) ? 0.9 : 0.7);
            matches.append(m);
        }
        return matches;
    }

    // 4. Bare keyword: status and quick controls.
    if (m_timer->isActive()) {
        RemoteMatch status;
        status.categoryRelevance = CategoryHigh;
        status.relevance = 1.0;
        if (m_timer->isPaused()) {
            status.id = QStringLiteral("resume");
            status.text = i18n("Sugo: Paused (%1 left)", timeStr);
            status.properties[SubtextKey] = i18n("Click to resume %1", phaseStr);
            status.iconName = QStringLiteral("media-playback-start");
            status.properties[ActionsKey] = QStringList{QStringLiteral("resume"), QStringLiteral("skip"), QStringLiteral("stop")};
        } else {
            status.id = QStringLiteral("pause");
            status.text = i18n("Sugo: %1 (%2 left)", phaseStr, timeStr);
            status.properties[SubtextKey] = i18n("Click to pause timer");
            status.iconName = QStringLiteral("media-playback-pause");
            status.properties[ActionsKey] = QStringList{QStringLiteral("pause"), QStringLiteral("skip"), QStringLiteral("stop")};
        }
        matches.append(status);

        RemoteMatch skipMatch = commandMatch(QStringLiteral("skip"), i18n("Skip to next phase"), QString(),
                                             QStringLiteral("media-skip-forward"));
        skipMatch.categoryRelevance = CategoryModerate;
        skipMatch.relevance = 0.8;
        matches.append(skipMatch);

        RemoteMatch stopMatch = commandMatch(QStringLiteral("stop"), i18n("Stop timer"), QString(),
                                             QStringLiteral("media-playback-stop"));
        stopMatch.categoryRelevance = CategoryModerate;
        stopMatch.relevance = 0.7;
        matches.append(stopMatch);
    } else {
        RemoteMatch m = commandMatch(QStringLiteral("start"), i18n("Start Pomodoro (%1 min)", m_timer->workSeconds() / 60),
                                     i18n("Preset: %1", presetName), QStringLiteral("io.github.mineraleyt.sugo"));
        m.categoryRelevance = CategoryHigh;
        matches.append(m);
    }

    RemoteMatch openMatch = commandMatch(QStringLiteral("open"), i18n("Open Sugo Window"), QString(),
                                         QStringLiteral("io.github.mineraleyt.sugo"));
    openMatch.categoryRelevance = CategoryModerate;
    openMatch.relevance = 0.6;
    matches.append(openMatch);

    return matches;
}

void KRunnerService::Run(const QString &id, const QString &actionId)
{
    if (id == kControlDisabledId || (id == QStringLiteral("open") && actionId.isEmpty())) {
        Q_EMIT openRequested();
        return;
    }
    if (!controlAllowed()) {
        return; // a match, or a button, from before the setting changed
    }
    // A button on a match runs that button's command, not the match's own one.
    if (!actionId.isEmpty()) {
        runCommand(actionId);
        return;
    }

    if (id == kInvalidMinutesId) {
        return; // only an explanation
    }

    if (id.startsWith(QStringLiteral("custom:"))) {
        const int mins = id.mid(7).toInt();
        if (mins > 0 && mins <= kMaxCustomMinutes) {
            m_timer->startCustomWork(mins * 60);
        }
    } else if (id.startsWith(QStringLiteral("preset:"))) {
        const QString uuid = id.mid(7);
        if (m_presets && m_presets->indexOf(uuid) >= 0) {
            m_presets->setCurrentUuid(uuid);
            m_timer->start();
        }
    } else {
        runCommand(id);
    }
}

void KRunnerService::runCommand(const QString &command)
{
    if (command == QStringLiteral("start")) {
        m_timer->start();
    } else if (command == QStringLiteral("pause")) {
        m_timer->pause();
    } else if (command == QStringLiteral("resume")) {
        m_timer->resume();
    } else if (command == QStringLiteral("toggle")) {
        m_timer->toggle();
    } else if (command == QStringLiteral("stop")) {
        m_timer->stop();
    } else if (command == QStringLiteral("skip")) {
        m_timer->skip();
    }
}

void KRunnerService::Teardown()
{
}
