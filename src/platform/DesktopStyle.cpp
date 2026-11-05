// SPDX-License-Identifier: GPL-3.0-or-later
#include "DesktopStyle.h"

#include <algorithm>

#include <QFontDatabase>
#include <QFontInfo>
#include <QKeySequence>
#include <QProcess>
#include <QRegularExpression>
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

bool DesktopStyle::kwinForcesTitleBar(const QString &rulesPath)
{
    QSettings rules(rulesPath, QSettings::IniFormat);
    // QSettings reads the [General] group as the top level.
    const QStringList order = rules.value(QStringLiteral("Order")).toStringList();
    const QStringList ourClasses = {QStringLiteral("stream.sugo.app"), QStringLiteral("sugo")};
    for (const QString &id : order) {
        rules.beginGroup(id);
        // KWin's rule kinds: 2 force, 3 apply, 4 remember (the others are temporary or unused).
        const int kind = rules.value(QStringLiteral("decorationpolicyrule")).toInt();
        bool applies = rules.value(QStringLiteral("decorationpolicy")).toString() == QLatin1String("server") && kind >= 2 && kind <= 4;
        if (applies) {
            // shortcut: only the window class is matched, not title, role or type; extend if such rules turn up.
            const QString wmclass = rules.value(QStringLiteral("wmclass")).toString();
            switch (rules.value(QStringLiteral("wmclassmatch")).toInt()) {
            case 0: // any window
                break;
            case 1:
                applies = ourClasses.contains(wmclass, Qt::CaseInsensitive);
                break;
            case 2:
                applies = std::any_of(ourClasses.cbegin(), ourClasses.cend(), [&](const QString &c) {
                    return c.contains(wmclass, Qt::CaseInsensitive);
                });
                break;
            default: {
                const QRegularExpression re(wmclass, QRegularExpression::CaseInsensitiveOption);
                applies = std::any_of(ourClasses.cbegin(), ourClasses.cend(), [&](const QString &c) {
                    return re.match(c).hasMatch();
                });
            }
            }
        }
        rules.endGroup();
        if (applies) {
            return true;
        }
    }
    return false;
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

QString DesktopStyle::monoFamily() const
{
    const QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    if (QFontInfo(font).fixedPitch()) {
        return font.family();
    }
    // A generic name the font system does not resolve ("Monospace" without fontconfig, e.g. the
    // offscreen platform on macOS): the first well-known fixed-pitch family that is installed.
    static const QStringList known = {QStringLiteral("SF Mono"), QStringLiteral("Menlo"), QStringLiteral("Monaco"),
                                      QStringLiteral("DejaVu Sans Mono"), QStringLiteral("Liberation Mono"),
                                      QStringLiteral("Noto Sans Mono"), QStringLiteral("Courier New")};
    for (const QString &family : known) {
        if (QFontDatabase::hasFamily(family) && QFontDatabase::isFixedPitch(family)) {
            return family;
        }
    }
    return font.family();
}

QString DesktopStyle::shortcutText(const QString &sequence)
{
    return QKeySequence(sequence, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

void DesktopStyle::showRestored(QWindow *window)
{
    if (!window) {
        return;
    }
    // show() keeps the window's states (it does not call showNormal()).
    window->show();
    const Qt::WindowStates states = window->windowStates();
    if (states & Qt::WindowMinimized) {
        window->setWindowStates(states & ~Qt::WindowMinimized);
    }
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
    if (nativeChrome()) {
        return; // the system's title bar has the buttons
    }
    if (m_style == QLatin1String("kde")
        && kwinForcesTitleBar(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kwinrulesrc"))) {
        return;
    }
    // Close must be reachable even with an odd setting.
    if (!layout.left.contains(QStringLiteral("close")) && !layout.right.contains(QStringLiteral("close"))) {
        layout.right.append(QStringLiteral("close"));
    }
    m_left = layout.left;
    m_right = layout.right;
}
