// SPDX-License-Identifier: GPL-3.0-or-later
#include "PortalAutostart.h"

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>

namespace
{
Q_LOGGING_CATEGORY(lcPortalAutostart, "sugo.platform.autostart")
}

namespace
{
const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kBackgroundInterface = QStringLiteral("org.freedesktop.portal.Background");
const QString kRequestInterface = QStringLiteral("org.freedesktop.portal.Request");
} // namespace

/// Receives the org.freedesktop.portal.Request.Response signal of one request.
class PortalResponse : public QObject
{
    Q_OBJECT

public:
    bool received = false;
    uint code = 2; // 0 success, 1 cancelled by the user, 2 other error
    QVariantMap results;

Q_SIGNALS:
    void done();

public Q_SLOTS:
    void onResponse(uint response, const QVariantMap &values)
    {
        received = true;
        code = response;
        results = values;
        Q_EMIT done();
    }
};

PortalAutostart::PortalAutostart(const QDBusConnection &bus, const QString &stateFile, const QString &reason,
                                 const QStringList &command, int responseTimeoutMs)
    : m_bus(bus)
    , m_stateFile(stateFile)
    , m_reason(reason)
    , m_command(command)
    , m_responseTimeoutMs(responseTimeoutMs)
{
}

bool PortalAutostart::isEnabled() const
{
    return QFile::exists(m_stateFile);
}

bool PortalAutostart::storeState(bool enabled) const
{
    if (!enabled) {
        return !QFile::exists(m_stateFile) || QFile::remove(m_stateFile);
    }
    QDir().mkpath(QFileInfo(m_stateFile).absolutePath());
    QSaveFile file(m_stateFile);
    return file.open(QIODevice::WriteOnly) && file.write("autostart=true\n") >= 0 && file.commit();
}

bool PortalAutostart::setEnabled(bool enabled)
{
    if (!m_bus.isConnected()) {
        return false;
    }

    // Subscribe to the request's Response before asking, at the path the portal will use
    // (…/request/<our unique name>/<token>), so a quick answer cannot be missed.
    const QString token = QStringLiteral("sugo_") + QUuid::createUuid().toString(QUuid::Id128);
    QString sender = m_bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    QString handle = kPortalPath + QStringLiteral("/request/") + sender + QLatin1Char('/') + token;

    PortalResponse response;
    m_bus.connect(kPortalService, handle, kRequestInterface, QStringLiteral("Response"), &response,
                  SLOT(onResponse(uint, QVariantMap)));

    QDBusMessage call = QDBusMessage::createMethodCall(kPortalService, kPortalPath, kBackgroundInterface,
                                                       QStringLiteral("RequestBackground"));
    const QVariantMap options{
        {QStringLiteral("handle_token"), token},
        {QStringLiteral("reason"), m_reason},
        {QStringLiteral("autostart"), enabled},
        {QStringLiteral("commandline"), m_command},
        {QStringLiteral("dbus-activatable"), false},
    };
    call.setArguments({QString(), options});
    const QDBusReply<QDBusObjectPath> reply = m_bus.call(call, QDBus::BlockWithGui, 10000);
    if (!reply.isValid()) {
        qCWarning(lcPortalAutostart) << "Background portal request failed:" << reply.error().message();
        m_bus.disconnect(kPortalService, handle, kRequestInterface, QStringLiteral("Response"), &response,
                         SLOT(onResponse(uint, QVariantMap)));
        return false;
    }
    if (reply.value().path() != handle && !response.received) {
        // Older portals choose their own path.
        m_bus.disconnect(kPortalService, handle, kRequestInterface, QStringLiteral("Response"), &response,
                         SLOT(onResponse(uint, QVariantMap)));
        handle = reply.value().path();
        m_bus.connect(kPortalService, handle, kRequestInterface, QStringLiteral("Response"), &response,
                      SLOT(onResponse(uint, QVariantMap)));
    }

    if (!response.received) {
        QEventLoop loop;
        QObject::connect(&response, &PortalResponse::done, &loop, &QEventLoop::quit);
        QTimer::singleShot(m_responseTimeoutMs, &loop, &QEventLoop::quit);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    m_bus.disconnect(kPortalService, handle, kRequestInterface, QStringLiteral("Response"), &response,
                     SLOT(onResponse(uint, QVariantMap)));

    if (!response.received) {
        // Only a confirmed answer is stored: the user may still refuse.
        qCWarning(lcPortalAutostart) << "No answer from the Background portal in time";
        return false;
    }
    if (response.code != 0) {
        qCWarning(lcPortalAutostart) << "The Background portal refused the request (response" << response.code << ")";
        return false;
    }
    if (response.results.value(QStringLiteral("autostart"), enabled).toBool() != enabled) {
        qCWarning(lcPortalAutostart) << "The Background portal did not change the login entry";
        return false;
    }
    return storeState(enabled);
}

#include "PortalAutostart.moc"
