// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QString>
#include <QUrl>

/**
 * Where Sugo may send credentials: https to any host, or plain http to this computer only
 * (tests and local servers). Used by the calendar (CalendarSync) and Sugo Sync (SyncApi).
 */
namespace UrlPolicy
{
/// localhost, 127.0.0.1 or ::1 (with or without brackets), lower case.
inline bool isLoopbackHost(const QString &host)
{
    return host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("::1")
        || host == QLatin1String("[::1]");
}

/// https with a host, or http to a loopback host.
inline bool isAllowed(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("https")) {
        return !url.host().isEmpty();
    }
    return scheme == QLatin1String("http") && isLoopbackHost(url.host().toLower());
}
} // namespace UrlPolicy
