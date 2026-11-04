// SPDX-License-Identifier: GPL-3.0-or-later
#include "UpdateInstaller.h"

#include "Localization.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QXmlStreamReader>

namespace
{
constexpr int kAttachTimeoutMs = 2 * 60 * 1000;
constexpr int kVerifyTimeoutMs = 3 * 60 * 1000;
constexpr int kCopyTimeoutMs = 5 * 60 * 1000;
constexpr int kSmallStepTimeoutMs = 60 * 1000;
constexpr int kCancelWaitMs = 10 * 1000;

/// The first line of a program's output, for an error message.
QString firstLine(const QString &output)
{
    const QString line = output.section(QLatin1Char('\n'), 0, 0).trimmed();
    return line.left(300);
}
} // namespace

// --- AppImage ---------------------------------------------------------------------------------

AppImageInstaller::AppImageInstaller(const QString &appImage)
    : m_path(appImage)
{
}

bool AppImageInstaller::canInstall() const
{
    if (m_path.isEmpty()) {
        return false;
    }
    // QSaveFile writes next to the file and renames over it, and refuses a file it could not
    // write to itself (read-only, or owned by someone else) even in a folder it can write to.
    const QFileInfo file(m_path);
    return file.isFile() && file.isWritable() && QFileInfo(file.absolutePath()).isWritable();
}

void AppImageInstaller::install(QSaveFile &download, Done done)
{
    // The download was written next to the AppImage; committing it moves it over the AppImage.
    const QFileDevice::Permissions permissions = QFile::permissions(m_path);
    if (!download.commit()) {
        done(i18n("Cannot install the update: %1", download.errorString()));
        return;
    }
    QFile::setPermissions(m_path, permissions | QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    done(QString());
}

bool AppImageInstaller::finish(bool launch, qint64 pid)
{
    if (!launch) {
        return false; // already in place: the next start is the new version
    }
    // The new copy starts once this process is gone: it would otherwise find this one still
    // holding the D-Bus name and hand itself over to it.
    return QProcess::startDetached(QStringLiteral("/bin/sh"),
                                   {QStringLiteral("-c"),
                                    QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; exec \"$0\""),
                                    m_path, QString::number(pid)});
}

// --- macOS app bundle -------------------------------------------------------------------------

MacBundleInstaller::MacBundleInstaller(const QString &bundle, const Tools &tools)
    : m_bundle(bundle.isEmpty() ? QString() : QDir::cleanPath(bundle))
    , m_tools(tools)
{
}

MacBundleInstaller::~MacBundleInstaller()
{
    cancel();
}

bool MacBundleInstaller::canInstall() const
{
    if (m_bundle.isEmpty()) {
        return false;
    }
    const QFileInfo bundle(m_bundle);
    return bundle.isDir() && QFileInfo(bundle.absolutePath()).isWritable();
}

QString MacBundleInstaller::downloadPath() const
{
    // In the bundle's folder: the same volume as the copy that will replace the bundle.
    const QFileInfo bundle(m_bundle);
    return bundle.absolutePath() + QStringLiteral("/.") + bundle.fileName() + QStringLiteral(".update.dmg");
}

QString MacBundleInstaller::bundleIdentifier(const QString &infoPlist)
{
    QFile file(infoPlist);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QXmlStreamReader xml(&file);
    int dictDepth = 0;
    bool identifierNext = false;
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const auto name = xml.name();
            if (name == QLatin1String("dict")) {
                ++dictDepth;
                identifierNext = false;
            } else if (dictDepth == 1 && name == QLatin1String("key")) {
                identifierNext = xml.readElementText() == QLatin1String("CFBundleIdentifier");
            } else if (identifierNext) {
                return name == QLatin1String("string") ? xml.readElementText().trimmed() : QString();
            }
        } else if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("dict")) {
            --dictDepth;
        }
    }
    return QString();
}

void MacBundleInstaller::run(const QString &program, const QStringList &arguments, int timeoutMs, Then then)
{
    auto *process = new QProcess(this);
    m_process = process;
    process->setProcessChannelMode(QProcess::MergedChannels);
    auto *timer = new QTimer(process);
    timer->setSingleShot(true);
    auto timedOut = std::make_shared<bool>(false);
    connect(timer, &QTimer::timeout, process, [process, timedOut]() {
        *timedOut = true;
        process->kill();
    });

    auto handled = std::make_shared<bool>(false);
    const auto end = [this, process, then, handled, timedOut](bool ok) {
        if (*handled) {
            return;
        }
        *handled = true;
        const QString output = *timedOut ? i18n("it did not finish in time") : QString::fromLocal8Bit(process->readAll()).trimmed();
        process->deleteLater();
        if (m_process != process) {
            return; // cancelled
        }
        m_process = nullptr;
        then(ok && !*timedOut, output);
    };
    connect(process, &QProcess::finished, this, [end](int code, QProcess::ExitStatus status) {
        end(status == QProcess::NormalExit && code == 0);
    });
    connect(process, &QProcess::errorOccurred, this, [end](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            end(false); // no finished() follows
        }
    });
    process->start(program, arguments);
    timer->start(timeoutMs);
}

void MacBundleInstaller::install(QSaveFile &download, Done done)
{
    cancel();
    m_done = std::move(done);
    if (!download.commit()) {
        conclude(i18n("Cannot write the update: %1", download.errorString()));
        return;
    }

    QTemporaryDir mount(QDir::tempPath() + QStringLiteral("/sugo-update-XXXXXX"));
    if (!mount.isValid()) {
        conclude(i18n("Cannot open the update: %1", mount.errorString()));
        return;
    }
    mount.setAutoRemove(false); // removed with rmdir() only, so never with a disk image still in it
    m_mountPoint = mount.path();

    run(m_tools.hdiutil,
        {QStringLiteral("attach"), QStringLiteral("-nobrowse"), QStringLiteral("-readonly"), QStringLiteral("-noautoopen"),
         QStringLiteral("-mountpoint"), m_mountPoint, downloadPath()},
        kAttachTimeoutMs, [this](bool ok, const QString &output) {
            if (!ok) {
                fail(i18n("Cannot open the downloaded disk image: %1", firstLine(output)));
                return;
            }
            m_mounted = true;
            attached();
        });
}

void MacBundleInstaller::attached()
{
    const QString app = m_mountPoint + QStringLiteral("/Sugo.app");
    if (bundleIdentifier(app + QStringLiteral("/Contents/Info.plist")) != QLatin1String(kBundleIdentifier)) {
        fail(i18n("The downloaded disk image does not contain Sugo."));
        return;
    }
    run(m_tools.codesign, {QStringLiteral("--verify"), QStringLiteral("--deep"), QStringLiteral("--strict"), app}, kVerifyTimeoutMs,
        [this](bool ok, const QString &output) {
            if (!ok) {
                fail(i18n("The signature of the downloaded Sugo is not valid: %1", firstLine(output)));
                return;
            }
            verified();
        });
}

void MacBundleInstaller::verified()
{
    const QString copy = newBundlePath();
    if (QFileInfo::exists(copy) && !QDir(copy).removeRecursively()) {
        fail(i18n("Cannot remove the earlier update at %1.", copy));
        return;
    }
    run(m_tools.ditto, {m_mountPoint + QStringLiteral("/Sugo.app"), copy}, kCopyTimeoutMs, [this](bool ok, const QString &output) {
        if (!ok) {
            fail(i18n("Cannot copy the update next to %1: %2", m_bundle, firstLine(output)));
            return;
        }
        // Nothing we download is quarantined, but a stray attribute must not stop the new version.
        run(m_tools.xattr, {QStringLiteral("-cr"), newBundlePath()}, kSmallStepTimeoutMs, [this](bool, const QString &) {
            copied();
        });
    });
}

void MacBundleInstaller::copied()
{
    if (!QFileInfo(newBundlePath() + QStringLiteral("/Contents/Info.plist")).isFile()) {
        fail(i18n("Cannot copy the update next to %1: %2", m_bundle, i18n("the copy is incomplete")));
        return;
    }
    detach([this]() {
        conclude(QString());
    });
}

void MacBundleInstaller::detach(const std::function<void()> &then)
{
    if (!m_mounted) {
        then();
        return;
    }
    run(m_tools.hdiutil, {QStringLiteral("detach"), m_mountPoint}, kSmallStepTimeoutMs, [this, then](bool ok, const QString &) {
        if (ok) {
            m_mounted = false;
            then();
            return;
        }
        run(m_tools.hdiutil, {QStringLiteral("detach"), m_mountPoint, QStringLiteral("-force")}, kSmallStepTimeoutMs,
            [this, then](bool forced, const QString &) {
                m_mounted = !forced;
                then();
            });
    });
}

void MacBundleInstaller::fail(const QString &message)
{
    detach([this, message]() {
        QDir(newBundlePath()).removeRecursively();
        conclude(message);
    });
}

void MacBundleInstaller::conclude(const QString &error)
{
    removeLeftovers();
    Done done = std::move(m_done);
    m_done = nullptr;
    if (done) {
        done(error);
    }
}

void MacBundleInstaller::removeLeftovers()
{
    QFile::remove(downloadPath());
    if (!m_mounted && !m_mountPoint.isEmpty()) {
        QDir().rmdir(m_mountPoint);
        m_mountPoint.clear();
    }
}

void MacBundleInstaller::cancel()
{
    if (!m_done && !m_process) {
        return;
    }
    m_done = nullptr;
    if (QProcess *process = m_process.data()) {
        m_process = nullptr;
        process->kill();
        process->waitForFinished(kCancelWaitMs);
    }
    if (m_mounted) {
        QProcess detach;
        detach.start(m_tools.hdiutil, {QStringLiteral("detach"), m_mountPoint, QStringLiteral("-force")});
        m_mounted = !(detach.waitForFinished(kCancelWaitMs) && detach.exitStatus() == QProcess::NormalExit && detach.exitCode() == 0);
    }
    QDir(newBundlePath()).removeRecursively();
    removeLeftovers();
}

bool MacBundleInstaller::finish(bool launch, qint64 pid)
{
    const QString fresh = newBundlePath();
    if (m_bundle.isEmpty() || !QFileInfo(fresh + QStringLiteral("/Contents/Info.plist")).isFile()) {
        return false;
    }
    // The running bundle steps aside, the new one takes its place; if that fails, the old one
    // comes back. A running program keeps its files open whatever their bundle is called.
    const QString backup = m_bundle + QStringLiteral(".old");
    if (QFileInfo::exists(backup) && !QDir(backup).removeRecursively()) {
        return false;
    }
    QDir folder;
    if (!folder.rename(m_bundle, backup)) {
        return false;
    }
    if (!folder.rename(fresh, m_bundle)) {
        folder.rename(backup, m_bundle);
        return false;
    }
    // The old bundle goes, and the new one starts, once this process has ended (a second Sugo
    // would hand itself over to this one).
    const qint64 waitFor = m_tools.waitPid > 0 ? m_tools.waitPid : pid;
    const QString script = launch ? QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; rm -rf \"$2\"; exec \"$3\" -n \"$0\"")
                                  : QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; rm -rf \"$2\"");
    const bool started = QProcess::startDetached(QStringLiteral("/bin/sh"),
                                                 {QStringLiteral("-c"), script, m_bundle, QString::number(waitFor), backup, m_tools.open});
    return launch && started;
}
