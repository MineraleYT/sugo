// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusArgument>
#include <QDBusMetaType>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QList>

#include <functional>

class TimerEngine;
class PresetModel;

struct RemoteMatch {
    QString id;
    QString text;
    QString iconName;
    // KRunner::QueryMatch::CategoryRelevance (Plasma 6): Lowest=0, Low=30, Moderate=50, High=70, Highest=100.
    int categoryRelevance = 50;
    double relevance = 0.5;
    // Recognised keys: "subtext", and "actions" (QStringList of Actions() ids shown as buttons;
    // an empty list shows none, a missing key would show every action).
    QVariantMap properties;
};
Q_DECLARE_METATYPE(RemoteMatch)

inline QDBusArgument &operator<<(QDBusArgument &argument, const RemoteMatch &match)
{
    argument.beginStructure();
    argument << match.id << match.text << match.iconName << match.categoryRelevance << match.relevance << match.properties;
    argument.endStructure();
    return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument, RemoteMatch &match)
{
    argument.beginStructure();
    argument >> match.id >> match.text >> match.iconName >> match.categoryRelevance >> match.relevance >> match.properties;
    argument.endStructure();
    return argument;
}

struct RemoteAction {
    QString id;
    QString text;
    QString iconName;
};
Q_DECLARE_METATYPE(RemoteAction)

inline QDBusArgument &operator<<(QDBusArgument &argument, const RemoteAction &action)
{
    argument.beginStructure();
    argument << action.id << action.text << action.iconName;
    argument.endStructure();
    return argument;
}

inline const QDBusArgument &operator>>(const QDBusArgument &argument, RemoteAction &action)
{
    argument.beginStructure();
    argument >> action.id >> action.text >> action.iconName;
    argument.endStructure();
    return argument;
}

/**
 * Implements the standard org.kde.krunner1 D-Bus interface for Plasma 6 KRunner search.
 *
 * Queries start with a keyword (pomodoro, sugo, tomato, timer) as a whole word, optionally
 * followed by a command (start, pause, resume, stop, skip), a number of minutes for a one-off
 * work phase, or (part of) a preset name.
 */
class KRunnerService : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.krunner1")

public:
    explicit KRunnerService(TimerEngine *timer, PresetModel *presets, QObject *parent = nullptr);
    ~KRunnerService() override = default;

    static void registerMetaTypes();

    /// Asked before matching or running anything; without one, everything is allowed. When it
    /// says no, KRunner only offers to open Sugo (where the setting is).
    void setControlAllowed(std::function<bool()> allowed) { m_allowed = std::move(allowed); }
    /// For a runner created (and registered) before the timer exists, with null pointers: until
    /// then it matches nothing and runs nothing.
    void attach(TimerEngine *timer, PresetModel *presets)
    {
        m_timer = timer;
        m_presets = presets;
    }

public Q_SLOTS:
    QList<RemoteAction> Actions();
    QList<RemoteMatch> Match(const QString &searchTerm);
    void Run(const QString &id, const QString &actionId);
    void Teardown();

Q_SIGNALS:
    void openRequested();

private:
    void runCommand(const QString &command);

    bool controlAllowed() const { return !m_allowed || m_allowed(); }

    TimerEngine *m_timer;
    PresetModel *m_presets;
    std::function<bool()> m_allowed;
};
