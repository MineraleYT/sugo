// SPDX-License-Identifier: GPL-3.0-or-later
#include "SingleInstance.h"

#include <QDBusMessage>
#include <QLoggingCategory>
#include <QMetaMethod>

Q_LOGGING_CATEGORY(lcSingleInstance, "sugo.platform.singleinstance")

namespace
{
// The primary instance answers only once it reaches its event loop, which can take a while
// on a cold start (database, QML). Waiting longer is harmless: this process exits anyway.
constexpr int kActivateTimeoutMs = 25000;
}

SingleInstance::SingleInstance(const QDBusConnection &bus, const QString &serviceName, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_service(serviceName)
{
}

SingleInstance::~SingleInstance()
{
    if (m_primary) {
        m_bus.unregisterService(m_service);
        m_bus.unregisterObject(objectPath());
    }
}

QString SingleInstance::objectPath() const
{
    QString path = QStringLiteral("/") + m_service;
    return path.replace(QLatin1Char('.'), QLatin1Char('/'));
}

bool SingleInstance::acquire(bool activateExisting)
{
    if (!m_bus.isConnected()) {
        return true;
    }

    // Export the object before taking the name, so a second launch that sees the name can
    // always reach Activate().
    const auto takeName = [this]() {
        m_bus.registerObject(objectPath(), this, QDBusConnection::ExportAllSlots);
        m_primary = m_bus.registerService(m_service);
        if (!m_primary) {
            m_bus.unregisterObject(objectPath());
        }
        return m_primary;
    };
    if (takeName()) {
        return true;
    }
    if (!activateExisting) {
        return false;
    }

    QVariantMap platformData;
    const QString token = qEnvironmentVariable("XDG_ACTIVATION_TOKEN");
    if (!token.isEmpty()) {
        platformData.insert(QStringLiteral("activation-token"), token);
    }
    QDBusMessage message = QDBusMessage::createMethodCall(m_service, objectPath(),
                                                          QStringLiteral("org.freedesktop.Application"),
                                                          QStringLiteral("Activate"));
    message.setArguments({QVariant::fromValue(platformData)});
    // BlockWithGui keeps this process' event loop running while it waits, so the call also works
    // when the answering instance lives in the same process (the unit tests).
    const QDBusMessage reply = m_bus.call(message, QDBus::BlockWithGui, kActivateTimeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        // The other instance may have quit since: then this one takes over, or none would run.
        if (takeName()) {
            return true;
        }
        qCWarning(lcSingleInstance) << "Sugo is already running but did not answer:" << reply.errorMessage();
    }
    return false;
}

void SingleInstance::Activate(const QVariantMap &platformData)
{
    const QString token = platformData.value(QStringLiteral("activation-token")).toString();
    if (!isSignalConnected(QMetaMethod::fromSignal(&SingleInstance::activateRequested))) {
        // Nobody can show a window yet (still starting up): keep the request for later.
        m_hasPendingActivation = true;
        m_pendingToken = token;
        return;
    }
    Q_EMIT activateRequested(token);
}

void SingleInstance::connectNotify(const QMetaMethod &signal)
{
    if (signal == QMetaMethod::fromSignal(&SingleInstance::activateRequested) && m_hasPendingActivation) {
        m_hasPendingActivation = false;
        // Queued, so the receiver's connect() has returned before it is called.
        QMetaObject::invokeMethod(
            this, [this, token = m_pendingToken]() { Q_EMIT activateRequested(token); }, Qt::QueuedConnection);
        m_pendingToken.clear();
    }
}
