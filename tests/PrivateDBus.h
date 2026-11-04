// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <memory>

/// A throw-away message bus, so D-Bus tests never touch the user's real session.
///
/// It has no service directories of the machine: nothing installed there (a keyring, a
/// notification server...) can be started on it by activation, so a test only ever talks to its
/// own fakes.
class PrivateDBus
{
public:
    ~PrivateDBus() { stop(); }

    /// False if dbus-daemon is not installed (tests then skip).
    ///
    /// `startedOnDemand`: names the bus treats as activatable, as when a real service is started by
    /// the first call made to it. What the bus starts is only a placeholder that sleeps a few
    /// seconds: the call waits on the bus until the test registers the name itself, then reaches it.
    bool start(const QStringList &startedOnDemand = {})
    {
        if (!startedOnDemand.isEmpty()) {
            m_services = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/sugo-test-services-XXXXXX"));
            if (!m_services->isValid()) {
                return false;
            }
            for (const QString &name : startedOnDemand) {
                QFile service(m_services->filePath(name + QStringLiteral(".service")));
                if (!service.open(QIODevice::WriteOnly)) {
                    return false;
                }
                // The placeholder outlives the bus: it keeps none of the test's pipes open.
                const QByteArray definition = QByteArray("[D-BUS Service]\nName=") + name.toUtf8()
                    + QByteArray("\nExec=/bin/sh -c \"exec sleep 10 </dev/null >/dev/null 2>&1\"\n");
                service.write(definition);
            }
        }

        m_config.setFileTemplate(QDir::tempPath() + QStringLiteral("/sugo-test-bus-XXXXXX.conf"));
        if (!m_config.open()) {
            return false;
        }
        m_config.write(R"(<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <auth>EXTERNAL</auth>
)");
        if (m_services) {
            const QByteArray serviceDir = QByteArray("  <servicedir>") + m_services->path().toUtf8() + QByteArray("</servicedir>\n");
            m_config.write(serviceDir);
        }
        m_config.write(R"(  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
)");
        m_config.flush();
        m_daemon.start(QStringLiteral("dbus-daemon"),
                       {QStringLiteral("--config-file=") + m_config.fileName(), QStringLiteral("--nofork"), QStringLiteral("--print-address=1")});
        if (!m_daemon.waitForStarted(3000) || !m_daemon.waitForReadyRead(5000)) {
            return false;
        }
        m_address = QString::fromLocal8Bit(m_daemon.readLine().trimmed());
        return !m_address.isEmpty();
    }

    void stop()
    {
        if (m_daemon.state() != QProcess::NotRunning) {
            m_daemon.terminate();
            m_daemon.waitForFinished(3000);
        }
    }

    QString address() const { return m_address; }

private:
    std::unique_ptr<QTemporaryDir> m_services;
    QTemporaryFile m_config;
    QProcess m_daemon;
    QString m_address;
};
