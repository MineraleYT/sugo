// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "NotificationInhibitor.h"

/// Fallback for platforms or desktops without a way to silence notifications.
class NoopInhibitor final : public NotificationInhibitor
{
    Q_OBJECT

public:
    using NotificationInhibitor::NotificationInhibitor;

    bool isAvailable() const override { return false; }
    void inhibit(const QString &) override {}
    void release() override {}
    bool isInhibited() const override { return false; }
};
