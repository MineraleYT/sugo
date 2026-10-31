// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDir>
#include <QProcess>
#include <QString>
#include <QTemporaryFile>

/// A throw-away message bus, so D-Bus tests never touch the user's real session.
///
/// It has no service directories: nothing installed on the machine (a keyring, a notification
/// server...) can be started on it by activation, so a test only ever talks to its own fakes.
class PrivateDBus
{
public:
    ~PrivateDBus() { stop(); }

    /// False if dbus-daemon is not installed (tests then skip).
    bool start()
    {
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
  <policy context="default">
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
    QTemporaryFile m_config;
    QProcess m_daemon;
    QString m_address;
};
