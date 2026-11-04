// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

/**
 * How the desktop draws window buttons, so the window (which has no system title bar) can draw
 * its own the same way: the style ("kde", "gnome", "windows", "macos" or "generic") and which
 * buttons sit on each side, in order ("minimize", "maximize", "close").
 *
 * KDE: [org.kde.kdecoration2] ButtonsOnLeft/ButtonsOnRight in kwinrc. GNOME and the desktops
 * that follow it: the org.gnome.desktop.wm.preferences button-layout setting.
 * SUGO_WINDOW_STYLE forces a style (development, screenshots).
 *
 * On macOS the window keeps the system's title bar, made transparent over the content
 * (`nativeChrome`): the system draws the window buttons, so both lists are empty. `macos` is true
 * for the macOS style, also when SUGO_WINDOW_STYLE=macos previews it on Linux: QML uses it to
 * choose the wording of macOS (menu bar, Finder, System Settings), never to change behaviour.
 */
class DesktopStyle : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString style READ style CONSTANT FINAL)
    Q_PROPERTY(QStringList leftButtons READ leftButtons CONSTANT FINAL)
    Q_PROPERTY(QStringList rightButtons READ rightButtons CONSTANT FINAL)
    /// The system draws the title bar and its buttons (true only when built for macOS).
    Q_PROPERTY(bool nativeChrome READ nativeChrome CONSTANT FINAL)
    /// The macOS style: the texts of macOS.
    Q_PROPERTY(bool macos READ isMacos CONSTANT FINAL)
    /// The system's fixed-width font ("monospace" is a fontconfig alias that macOS does not know).
    Q_PROPERTY(QString monoFamily READ monoFamily CONSTANT FINAL)

public:
    explicit DesktopStyle(QObject *parent = nullptr);

    QString style() const { return m_style; }
    QStringList leftButtons() const { return m_left; }
    QStringList rightButtons() const { return m_right; }
    static constexpr bool nativeChrome()
    {
#ifdef Q_OS_MACOS
        return true;
#else
        return false;
#endif
    }
    bool isMacos() const { return m_style == QLatin1String("macos"); }
    QString monoFamily() const;

    /// A shortcut as the system writes it ("Ctrl+K", "⌘K" on macOS), from a portable sequence
    /// such as "Ctrl+K" (Ctrl is Command on macOS).
    Q_INVOKABLE static QString shortcutText(const QString &sequence);

    struct Layout {
        QStringList left;
        QStringList right;
    };
    /// KWin's letters: I minimize, A maximize, X close; the others are not ours to draw.
    static Layout parseKWin(const QString &left, const QString &right);
    /// GNOME's "appmenu:minimize,maximize,close" (left of the colon, right of it).
    static Layout parseGnome(const QString &layout);
    /// The defaults of each style.
    static Layout defaultLayout(const QString &style);

private:
    QString m_style;
    QStringList m_left;
    QStringList m_right;
};
