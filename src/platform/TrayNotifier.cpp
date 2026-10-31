// SPDX-License-Identifier: GPL-3.0-or-later
#include "TrayNotifier.h"

#include <QGuiApplication>
#include <QSystemTrayIcon>

TrayNotifier::TrayNotifier(std::function<QSystemTrayIcon *()> trayIcon, QObject *parent)
    : Notifier(parent)
    , m_trayIcon(std::move(trayIcon))
{
}

void TrayNotifier::notify(QObject *context, const Notification &notification)
{
    QSystemTrayIcon *icon = m_trayIcon ? m_trayIcon() : nullptr;
    if (!icon || !icon->isVisible() || !QSystemTrayIcon::supportsMessages()) {
        return;
    }
    // The icon may have been created again since the last message.
    connect(icon, &QSystemTrayIcon::messageClicked, this, &TrayNotifier::onMessageClicked, Qt::UniqueConnection);
    m_context = context;
    m_clicked = notification.defaultAction;
    icon->showMessage(notification.title, notification.text, QGuiApplication::windowIcon());
}

void TrayNotifier::onMessageClicked()
{
    if (m_context && m_clicked) {
        const Handler handler = m_clicked;
        handler(QString());
    }
}
