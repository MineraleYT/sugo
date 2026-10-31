// SPDX-License-Identifier: GPL-3.0-or-later
#include "DesktopStyle.h"

#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace
{
QString buttonForKWin(QChar letter)
{
    switch (letter.unicode()) {
    case 'I':
        return QStringLiteral("minimize");
    case 'A':
        return QStringLiteral("maximize");
    case 'X':
        return QStringLiteral("close");
    default:
        return QString();
    }
}

QStringList gnomeButtons(const QString &side)
{
    QStringList list;
    const QStringList names = side.split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (const QString &name : names) {
        const QString trimmed = name.trimmed();
        if (trimmed == QLatin1String("minimize") || trimmed == QLatin1String("maximize") || trimmed == QLatin1String("close")) {
            list.append(trimmed);
        }
    }
    return list;
}

QString detectStyle()
{
    const QString forced = qEnvironmentVariable("SUGO_WINDOW_STYLE");
    if (!forced.isEmpty()) {
        return forced;
    }
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP").toLower();
    if (desktop.contains(QLatin1String("kde"))) {
        return QStringLiteral("kde");
    }
    static const QStringList gnomeLike = {QStringLiteral("gnome"), QStringLiteral("unity"), QStringLiteral("budgie"),
                                          QStringLiteral("pantheon"), QStringLiteral("cinnamon"), QStringLiteral("cosmic")};
    for (const QString &name : gnomeLike) {
        if (desktop.contains(name)) {
            return QStringLiteral("gnome");
        }
    }
    return QStringLiteral("generic");
#endif
}
} // namespace

DesktopStyle::Layout DesktopStyle::parseKWin(const QString &left, const QString &right)
{
    Layout layout;
    for (const QChar c : left) {
        const QString button = buttonForKWin(c);
        if (!button.isEmpty()) {
            layout.left.append(button);
        }
    }
    for (const QChar c : right) {
        const QString button = buttonForKWin(c);
        if (!button.isEmpty()) {
            layout.right.append(button);
        }
    }
    return layout;
}

DesktopStyle::Layout DesktopStyle::parseGnome(const QString &value)
{
    QString text = value.trimmed();
    if (text.size() >= 2 && text.startsWith(QLatin1Char('\'')) && text.endsWith(QLatin1Char('\''))) {
        text = text.mid(1, text.size() - 2);
    }
    const qsizetype colon = text.indexOf(QLatin1Char(':'));
    Layout layout;
    layout.left = gnomeButtons(colon >= 0 ? text.left(colon) : QString());
    layout.right = gnomeButtons(colon >= 0 ? text.mid(colon + 1) : text);
    return layout;
}

DesktopStyle::Layout DesktopStyle::defaultLayout(const QString &style)
{
    const QStringList all = {QStringLiteral("minimize"), QStringLiteral("maximize"), QStringLiteral("close")};
    if (style == QLatin1String("macos")) {
        return {{QStringLiteral("close"), QStringLiteral("minimize"), QStringLiteral("maximize")}, {}};
    }
    if (style == QLatin1String("gnome")) {
        return {{}, {QStringLiteral("close")}};
    }
    return {{}, all};
}

DesktopStyle::DesktopStyle(QObject *parent)
    : QObject(parent)
    , m_style(detectStyle())
{
    Layout layout = defaultLayout(m_style);
    if (m_style == QLatin1String("kde")) {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kwinrc");
        QSettings kwin(path, QSettings::IniFormat);
        kwin.beginGroup(QStringLiteral("org.kde.kdecoration2"));
        // KWin's defaults: menu and "on all desktops" left, minimize, maximize and close right.
        layout = parseKWin(kwin.value(QStringLiteral("ButtonsOnLeft"), QStringLiteral("MS")).toString(),
                           kwin.value(QStringLiteral("ButtonsOnRight"), QStringLiteral("IAX")).toString());
    } else if (m_style == QLatin1String("gnome")) {
        QProcess gsettings;
        gsettings.start(QStringLiteral("gsettings"),
                        {QStringLiteral("get"), QStringLiteral("org.gnome.desktop.wm.preferences"), QStringLiteral("button-layout")});
        if (gsettings.waitForFinished(1000) && gsettings.exitStatus() == QProcess::NormalExit && gsettings.exitCode() == 0) {
            const Layout read = parseGnome(QString::fromUtf8(gsettings.readAllStandardOutput()));
            if (!read.left.isEmpty() || !read.right.isEmpty()) {
                layout = read;
            }
        }
    }
    // Close must be reachable even with an odd setting.
    if (!layout.left.contains(QStringLiteral("close")) && !layout.right.contains(QStringLiteral("close"))) {
        layout.right.append(QStringLiteral("close"));
    }
    m_left = layout.left;
    m_right = layout.right;
}
