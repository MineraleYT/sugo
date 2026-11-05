// SPDX-License-Identifier: GPL-3.0-or-later
#include "DockController.h"

#include "AppSettings.h"
#include "PresetModel.h"
#include "TimerMenuActions.h"

#include <QGuiApplication>
#include <QMenu>
#include <QWindow>

#ifdef Q_OS_MACOS
#include "MacApp.h"
#endif

DockController::Native DockController::platformNative()
{
    Native native{[](const QString &) {}, [](bool) {}, [](QMenu *) {}};
#ifdef Q_OS_MACOS
    // Offscreen runs (tests, smoke runs) must leave the user's Dock alone.
    if (QGuiApplication::platformName() == QLatin1String("cocoa")) {
        native.setBadge = [](const QString &label) { macSetDockBadge(label); };
        native.setIconHidden = [](bool hidden) { macSetDockIconHidden(hidden); };
        native.installMenu = [](QMenu *menu) { menu->setAsDockMenu(); };
    }
#endif
    return native;
}

DockController::DockController(TimerEngine *engine, PresetModel *presets, AppSettings *settings, Native native,
                               QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_settings(settings)
    , m_native(std::move(native))
{
    connect(engine, &TimerEngine::stateChanged, this, &DockController::updateBadge);
    connect(engine, &TimerEngine::remainingChanged, this, &DockController::updateBadge);
    connect(settings, &AppSettings::showDockBadgeChanged, this, &DockController::updateBadge);
    for (auto signal : {&AppSettings::hideDockIconChanged, &AppSettings::showTrayIconChanged, &AppSettings::trayAvailableChanged}) {
        connect(settings, signal, this, &DockController::updateIcon);
    }

    // Start/Pause, Stop, Skip and the timers, as in the menu bar icon's menu. macOS adds the
    // window list and Quit by itself.
    m_menu = std::make_unique<QMenu>();
    m_timerActions = std::make_unique<TimerMenuActions>(engine, presets);
    m_timerActions->addControls(m_menu.get());
    connect(m_timerActions.get(), &TimerMenuActions::endPhaseRequested, this, &DockController::endPhaseRequested);
    m_menu->addSeparator();
    m_timerActions->addTimerMenu(m_menu.get());
    m_timerActions->updateTexts();
    connect(m_menu.get(), &QMenu::aboutToShow, m_timerActions.get(), &TimerMenuActions::rebuildTimerMenu);
    if (m_native.installMenu) {
        m_native.installMenu(m_menu.get());
    }

    updateBadge();
}

DockController::~DockController()
{
    m_timerActions.reset();
    m_menu.reset();
}

void DockController::setWindow(QWindow *window)
{
    if (m_window) {
        disconnect(m_window, nullptr, this, nullptr);
    }
    m_window = window;
    if (window) {
        connect(window, &QWindow::visibleChanged, this, &DockController::updateIcon);
    }
    updateIcon();
}

void DockController::prepareToShowWindow()
{
    if (m_iconHidden) {
        m_iconHidden = false;
        if (m_native.setIconHidden) {
            m_native.setIconHidden(false);
        }
    }
}

QString DockController::badgeText(TimerEngine::State state, int remainingSeconds)
{
    if (state == TimerEngine::State::Idle) {
        return QString();
    }
    return QString::number((qMax(0, remainingSeconds) + 59) / 60);
}

bool DockController::wantsHiddenIcon(bool hideDockIcon, bool menuBarIconShown, bool windowShown)
{
    return hideDockIcon && menuBarIconShown && !windowShown;
}

void DockController::updateBadge()
{
    const QString badge = m_settings->showDockBadge() ? badgeText(m_engine->state(), m_engine->remainingSeconds()) : QString();
    // Ticks come every second; the Dock hears only about a new number.
    if (badge == m_badge) {
        return;
    }
    m_badge = badge;
    if (m_native.setBadge) {
        m_native.setBadge(badge);
    }
}

void DockController::updateIcon()
{
    const bool hidden = wantsHiddenIcon(m_settings->hideDockIcon(), m_settings->showTrayIcon() && m_settings->trayAvailable(),
                                        !m_window || m_window->isVisible()); // no window yet: keep the icon
    if (hidden == m_iconHidden) {
        return;
    }
    m_iconHidden = hidden;
    if (m_native.setIconHidden) {
        m_native.setIconHidden(hidden);
    }
}
