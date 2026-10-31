// SPDX-License-Identifier: GPL-3.0-or-later
#include "NotificationInhibitor.h"

#include <QEventLoop>
#include <QTimer>

#include <memory>

void NotificationInhibitor::whenReleased(QObject *context, std::function<void()> callback, int timeoutMs)
{
    if (!isInhibited() || !isReleasing()) {
        callback();
        return;
    }

    // Both the state change and the timeout may fire; whichever comes first runs the callback once.
    auto done = std::make_shared<bool>(false);
    auto connection = std::make_shared<QMetaObject::Connection>();
    auto *timer = new QTimer(context);
    timer->setSingleShot(true);

    auto fire = [this, done, connection, timer, callback = std::move(callback)]() {
        if (*done) {
            return;
        }
        *done = true;
        QObject::disconnect(*connection);
        timer->deleteLater();
        callback();
    };

    *connection = connect(this, &NotificationInhibitor::inhibitedChanged, context, [this, fire]() {
        if (!isInhibited()) {
            fire();
        }
    });
    connect(timer, &QTimer::timeout, context, fire);
    timer->start(timeoutMs);
}

bool NotificationInhibitor::waitUntilReleased(int timeoutMs)
{
    if (!isInhibited()) {
        return true;
    }
    QEventLoop loop;
    connect(this, &NotificationInhibitor::inhibitedChanged, &loop, [&]() {
        if (!isInhibited()) {
            loop.quit();
        }
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return !isInhibited();
}
