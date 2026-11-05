// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QIcon>
#include <QObject>
#include <QPointer>

#include <memory>

class AppSettings;
class PresetModel;
class QAction;
class QMenu;
class QSystemTrayIcon;
class QWindow;
class TimerEngine;
class TimerMenuActions;

/**
 * The system tray icon: shows the phase and the remaining time (tooltip, and the minutes
 * as a badge on the icon), offers start/pause, stop and skip in its menu, and shows or
 * hides the window when clicked. Created and removed as the "Show tray icon" setting
 * changes.
 *
 * On macOS the icon sits in the menu bar: a click opens the menu (the window is shown from it
 * or from the Dock), and the idle icon is a monochrome template image.
 */
class TrayController : public QObject
{
    Q_OBJECT

public:
    TrayController(TimerEngine *engine, PresetModel *presets, AppSettings *settings, QObject *parent = nullptr);
    ~TrayController() override;

    /// The window the icon shows and hides when clicked.
    void setWindow(QWindow *window);

    bool isVisible() const { return m_item != nullptr; }
    /// The icon on display, or nullptr while there is none.
    QSystemTrayIcon *icon() const { return m_item.get(); }

Q_SIGNALS:
    /// A menu entry asks for the window on a page: "stats" or "settings".
    void openPageRequested(const QString &page);
    /// A click on the icon (or "Show") while the window is hidden or minimized.
    void showWindowRequested();
    /// "Quit" in the menu. The window decides: it asks first while a phase runs (main.cpp).
    void quitRequested();
    /// "Stop" (`skipping` false) or "Skip" in the menu. The window decides: it asks first when
    /// work under way would be cut short (main.cpp).
    void endPhaseRequested(bool skipping);

private:
    void sync();
    void createItem();
    void destroyItem();
    void refresh();
    void updateTexts();
    void updateStatusLine();
    /// Hides a window on screen, shows a hidden or minimized one.
    void toggleWindow();
    bool windowShown() const;

    TimerEngine *m_engine;
    PresetModel *m_presets;
    AppSettings *m_settings;
    QPointer<QWindow> m_window;

    std::unique_ptr<QSystemTrayIcon> m_item;
    std::unique_ptr<QMenu> m_menu; ///< The icon's menu; owns the actions below.
    std::unique_ptr<TimerMenuActions> m_timerActions; ///< Start/Pause, Stop, Skip and the Timer submenu.
    QPointer<QAction> m_statusAction;
    QPointer<QAction> m_statsAction;
    QPointer<QAction> m_settingsAction;
    QPointer<QAction> m_windowAction;
    QPointer<QAction> m_quitAction;
    QString m_menuKey;         ///< State the status line and the visibility were last set for.
    int m_badgeMinutes = -1;   ///< Minutes the badge shows (-1 = none), for describeTray() callers.
    QString m_iconKey;         ///< Everything the drawn icon depends on; empty forces the next draw.
    QString m_toolTip;         ///< The tooltip last sent, so unchanged text is not resent.
};
