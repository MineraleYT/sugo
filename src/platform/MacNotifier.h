// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>

#include "Notifier.h"

class QSystemTrayIcon;

/**
 * Notifications in macOS' Notification Center (UserNotifications framework).
 *
 * Actions become buttons and the reply field a text field (UNTextInputNotificationAction), so
 * the task note can be written in the notification. Notifications also show while Sugo is in
 * front. The handlers are found again by request identifier (MacNotificationRegistry) and run on
 * the Qt main thread.
 *
 * The permission is asked for once, at construction. Before each notification the current
 * setting is read again (the user may change it in System Settings at any time): while
 * notifications are not allowed, they go to `fallback`, a message from the menu bar icon.
 *
 * Only for an application bundle: UNUserNotificationCenter needs a bundle identifier (see
 * isSupported()).
 */
class MacNotifier final : public Notifier
{
    Q_OBJECT

public:
    /// True when the process runs from a bundle with an identifier.
    static bool isSupported();

    explicit MacNotifier(std::unique_ptr<Notifier> fallback, QObject *parent = nullptr);
    ~MacNotifier() override;

    void notify(QObject *context, const Notification &notification) override;

    struct Private; ///< Declared here so the Objective-C side can reach it; defined in the .mm.

private:
    std::unique_ptr<Private> d;
};
