// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QPointer>

#include "Notifier.h"

class QSystemTrayIcon;

/**
 * Notifications as a message from the tray icon (QSystemTrayIcon::showMessage), for systems
 * without org.freedesktop.Notifications.
 *
 * Such a message has no buttons and no text field: only the click on it, which runs the
 * default action of the latest message. Without a tray icon nothing is shown.
 */
class TrayNotifier final : public Notifier
{
    Q_OBJECT

public:
    /// `trayIcon` returns the icon on display, or nullptr while there is none.
    explicit TrayNotifier(std::function<QSystemTrayIcon *()> trayIcon, QObject *parent = nullptr);

    void notify(QObject *context, const Notification &notification) override;

private:
    void onMessageClicked();

    std::function<QSystemTrayIcon *()> m_trayIcon;
    QPointer<QObject> m_context;
    Handler m_clicked;
};
