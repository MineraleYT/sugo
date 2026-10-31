// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPointer>

#include "Notifier.h"

/// Keeps what the code under test shows, so a test can read it and "click" its actions.
/// (No Q_OBJECT: nothing to add to the base class.)
class FakeNotifier final : public Notifier
{
public:
    QList<Notification> shown;
    QPointer<QObject> lastContext;

    void notify(QObject *context, const Notification &notification) override
    {
        shown.append(notification);
        lastContext = context;
    }

    const Notification &last() const { return shown.constLast(); }

    QStringList actionLabels() const
    {
        QStringList labels;
        for (const Action &action : last().actions) {
            labels << action.label;
        }
        return labels;
    }

    /// Runs the action labelled `label` of the last notification; false if there is none.
    bool invoke(const QString &label, const QString &activationToken = QString()) const
    {
        for (const Action &action : last().actions) {
            if (action.label == label) {
                action.handler(activationToken);
                return true;
            }
        }
        return false;
    }
};
