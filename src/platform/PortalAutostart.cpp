// SPDX-License-Identifier: GPL-3.0-or-later
#include "PortalAutostart.h"

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
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
    QString handle; // the request's object path
    bool enabled = false; // what was asked for
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

PortalAutostart::~PortalAutostart()
{
    dropPendingRequest();
}

void PortalAutostart::listen(const QString &handle, PortalResponse *response)
{
    m_bus.connect(kPortalService, handle, kRequestInterface, QStringLiteral("Response"), response,
                  SLOT(onResponse(uint, QVariantMap)));
}

void PortalAutostart::stopListening(const QString &handle, PortalResponse *response)
{
    m_bus.disconnect(kPortalService, handle, kRequestInterface, QStringLiteral("Response"), response,
                     SLOT(onResponse(uint, QVariantMap)));
}

void PortalAutostart::dropPendingRequest()
{
    if (!m_pending) {
        return;
    }
    stopListening(m_pending->handle, m_pending.get());
    // Its dialog may still be open: closed, it cannot change the login entry behind Sugo's back.
    // Not waited for (the portal may be gone, and quitting must not hang on it): a failure is logged.
    const QString handle = m_pending->handle;
    m_pending.reset();
    const QDBusPendingCall call = m_bus.asyncCall(
        QDBusMessage::createMethodCall(kPortalService, handle, kRequestInterface, QStringLiteral("Close")));
    auto *watcher = new QDBusPendingCallWatcher(call);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, watcher, [handle](QDBusPendingCallWatcher *finished) {
        if (finished->isError()) {
            qCWarning(lcPortalAutostart) << "Cannot close the Background portal request" << handle << ":"
                                         << finished->error().message();
        }
        finished->deleteLater();
    });
}

bool PortalAutostart::confirms(const PortalResponse &response, bool enabled)
{
    if (response.code != 0) {
        qCWarning(lcPortalAutostart) << "The Background portal refused the request (response" << response.code << ")";
        return false;
    }
    if (response.results.value(QStringLiteral("autostart"), enabled).toBool() != enabled) {
        qCWarning(lcPortalAutostart) << "The Background portal did not change the login entry";
        return false;
    }
    return true;
}

bool PortalAutostart::setEnabled(bool enabled)
{
    if (!m_bus.isConnected()) {
        return false;
    }
    // Only the latest choice counts: the answer to an older request is no longer awaited.
    dropPendingRequest();

    // Subscribe to the request's Response before asking, at the path the portal will use
    // (…/request/<our unique name>/<token>), so a quick answer cannot be missed.
    const QString token = QStringLiteral("sugo_") + QUuid::createUuid().toString(QUuid::Id128);
    QString sender = m_bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));

    auto response = std::make_unique<PortalResponse>();
    response->handle = kPortalPath + QStringLiteral("/request/") + sender + QLatin1Char('/') + token;
    response->enabled = enabled;
    listen(response->handle, response.get());

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
        stopListening(response->handle, response.get());
        return false;
    }
    if (reply.value().path() != response->handle && !response->received) {
        // Older portals choose their own path.
        stopListening(response->handle, response.get());
        response->handle = reply.value().path();
        listen(response->handle, response.get());
    }

    if (!response->received) {
        QEventLoop loop;
        QObject::connect(response.get(), &PortalResponse::done, &loop, &QEventLoop::quit);
        QTimer::singleShot(m_responseTimeoutMs, &loop, &QEventLoop::quit);
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }

    if (!response->received) {
        // The dialog may still be open: if the user allows the change later, the portal makes
        // it, so the answer is still listened to. Only a confirmed answer is stored.
        qCWarning(lcPortalAutostart) << "No answer from the Background portal in time; still waiting for it";
        QObject::connect(response.get(), &PortalResponse::done, response.get(), [this]() {
            onLateResponse();
        });
        m_pending = std::move(response);
        return false;
    }
    stopListening(response->handle, response.get());
    return confirms(*response, enabled) && storeState(enabled);
}

void PortalAutostart::onLateResponse()
{
    // Called from the response's own signal: deleted once that returns.
    PortalResponse *response = m_pending.release();
    stopListening(response->handle, response);
    response->deleteLater();

    if (!confirms(*response, response->enabled)) {
        return;
    }
    const bool before = isEnabled();
    if (!storeState(response->enabled)) {
        qCWarning(lcPortalAutostart) << "Cannot store the Background portal's late answer in" << m_stateFile;
        return;
    }
    qCInfo(lcPortalAutostart) << "The Background portal answered late: start at login" << (response->enabled ? "on" : "off");
    if (isEnabled() != before) {
        notifyChanged();
    }
}

#include "PortalAutostart.moc"
