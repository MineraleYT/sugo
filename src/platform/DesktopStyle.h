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
 * When a KWin window rule forces the system title bar on Sugo (Plasma 6.8+), both lists are
 * empty: the system bar already has the buttons.
 */
class DesktopStyle : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString style READ style CONSTANT FINAL)
    Q_PROPERTY(QStringList leftButtons READ leftButtons CONSTANT FINAL)
    Q_PROPERTY(QStringList rightButtons READ rightButtons CONSTANT FINAL)

public:
    explicit DesktopStyle(QObject *parent = nullptr);

    QString style() const { return m_style; }
    QStringList leftButtons() const { return m_left; }
    QStringList rightButtons() const { return m_right; }

    struct Layout {
        QStringList left;
        QStringList right;
    };
    /// KWin's letters: I minimize, A maximize, X close; the others are not ours to draw.
    static Layout parseKWin(const QString &left, const QString &right);
    /// GNOME's "appmenu:minimize,maximize,close" (left of the colon, right of it).
    static Layout parseGnome(const QString &layout);
    /// Whether a window rule in this kwinrulesrc forces KWin's own title bar on Sugo's window.
    static bool kwinForcesTitleBar(const QString &rulesPath);
    /// The defaults of each style.
    static Layout defaultLayout(const QString &style);

private:
    QString m_style;
    QStringList m_left;
    QStringList m_right;
};
