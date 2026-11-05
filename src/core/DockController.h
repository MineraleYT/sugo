// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

#include "TimerEngine.h"

class AppSettings;
class PresetModel;
class QMenu;
class QWindow;
class TimerMenuActions;

/**
 * The macOS Dock icon:
 * - its badge shows the minutes left while a phase runs or is paused (nothing while idle, or
 *   with the "showDockBadge" setting off); the badge is sent only when its text changes, so at
 *   most once a minute, never on each tick;
 * - its menu starts, pauses, skips and stops the timer and picks the timer in use (the same
 *   entries as the menu bar icon's menu, see TimerMenuActions);
 * - with "hideDockIcon" on and the menu bar icon shown, the icon goes away while the window is
 *   hidden (Sugo lives in the menu bar then) and comes back while it is shown, so the window
 *   still gets focus and the application menu.
 *
 * The decisions are plain C++; what they do to the Dock goes through `Native`, so the class is
 * tested on any platform. Created by main.cpp on macOS only.
 */
class DockController : public QObject
{
    Q_OBJECT

public:
    /// What the controller does to the real Dock.
    struct Native {
        /// Sets the badge text; an empty text removes the badge.
        std::function<void(const QString &)> setBadge;
        /// Hides the Dock icon (an accessory application) or shows it again (a regular one).
        std::function<void(bool hidden)> setIconHidden;
        /// Makes the menu the Dock icon's menu.
        std::function<void(QMenu *)> installMenu;
    };
    /// The real Dock on macOS with the native (cocoa) platform; does nothing anywhere else.
    static Native platformNative();

    DockController(TimerEngine *engine, PresetModel *presets, AppSettings *settings, Native native,
                   QObject *parent = nullptr);
    ~DockController() override;

    /// The window whose visibility decides whether the Dock icon may be hidden.
    void setWindow(QWindow *window);
    /// Call before showing the window: gives the Dock icon back first, since an accessory
    /// application's window would not come to the front or get a menu bar.
    void prepareToShowWindow();

    /// The text on the badge ("24"), or empty while there is none.
    QString badge() const { return m_badge; }
    bool iconHidden() const { return m_iconHidden; }
    /// The Dock icon's menu.
    QMenu *menu() const { return m_menu.get(); }

    /// The badge for a timer state: the minutes left, rounded up, while a phase runs or is paused.
    static QString badgeText(TimerEngine::State state, int remainingSeconds);
    /// The Dock icon is hidden only when asked to, while the menu bar icon is there to bring the
    /// window back, and while the window is not shown (a minimized window counts as shown: its
    /// tile is in the Dock).
    static bool wantsHiddenIcon(bool hideDockIcon, bool menuBarIconShown, bool windowShown);

Q_SIGNALS:
    /// "Stop" (`skipping` false) or "Skip" in the menu. The window decides: it asks first when
    /// work under way would be cut short (main.cpp).
    void endPhaseRequested(bool skipping);

private:
    void updateBadge();
    void updateIcon();

    TimerEngine *m_engine;
    AppSettings *m_settings;
    Native m_native;
    QPointer<QWindow> m_window;
    std::unique_ptr<QMenu> m_menu;
    std::unique_ptr<TimerMenuActions> m_timerActions;
    QString m_badge;
    bool m_iconHidden = false;
};
