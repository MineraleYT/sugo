// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "NotificationInhibitor.h"

/// Records what the code under test asks for; release can be held back to simulate a slow desktop.
/// (No Q_OBJECT: it only reuses the base class' signal.)
class FakeInhibitor final : public NotificationInhibitor
{
public:
    bool available = true;
    bool deferReleases = false; ///< release() takes effect only after completeRelease()
    bool refuse = false;        ///< inhibit() fails: nothing is held
    QStringList calls;          ///< "inhibit" / "release", in order
    QString lastReason;

    bool isAvailable() const override { return available; }

    void inhibit(const QString &reason) override
    {
        calls << QStringLiteral("inhibit");
        lastReason = reason;
        m_releasePending = false;
        setHeld(!refuse);
    }

    void release() override
    {
        calls << QStringLiteral("release");
        if (deferReleases && m_held) {
            m_releasePending = true;
        } else {
            setHeld(false);
        }
    }

    bool isInhibited() const override { return m_held; }

    /// Finishes a deferred release.
    void completeRelease()
    {
        if (m_releasePending) {
            m_releasePending = false;
            setHeld(false);
        }
    }

private:
    void setHeld(bool held)
    {
        if (m_held != held) {
            m_held = held;
            Q_EMIT inhibitedChanged();
        }
    }

    bool m_held = false;
    bool m_releasePending = false;
};
