// SPDX-License-Identifier: GPL-3.0-or-later
#include "SilenceController.h"

#include "NotificationInhibitor.h"
#include "PresetModel.h"
#include "TimerEngine.h"

#include "Localization.h"

namespace
{
constexpr int kShutdownWaitMs = 1000;
}

SilenceController::SilenceController(QObject *parent)
    : QObject(parent)
{
}

void SilenceController::attach(TimerEngine *engine, PresetModel *presets, NotificationInhibitor *inhibitor)
{
    Q_ASSERT_X(!m_engine, "SilenceController::attach", "attach() must be called once");
    m_engine = engine;
    m_presets = presets;
    m_inhibitor = inhibitor;

    connect(engine, &TimerEngine::stateChanged, this, &SilenceController::update);
    // `active` follows what the desktop actually granted (or lost).
    connect(inhibitor, &NotificationInhibitor::inhibitedChanged, this, &SilenceController::update);
    connect(presets, &PresetModel::currentChanged, this, &SilenceController::update);
    // Only the current timer's options matter; editing another timer changes nothing here.
    connect(presets, &PresetModel::presetChanged, this, [this](const QString &uuid) {
        if (uuid == m_presets->currentUuid()) {
            update();
        }
    });

    Q_EMIT availableChanged();
    update();
}

bool SilenceController::available() const
{
    return m_inhibitor && m_inhibitor->isAvailable();
}

void SilenceController::update()
{
    if (!m_engine) {
        return;
    }

    const bool wanted = m_engine->state() == TimerEngine::State::Working
        && m_presets->currentPreset().boolOption(PresetOption::SilenceWhileWorking);

    // Ask the desktop only when the decision changes (e.g. not on every edit of the timer).
    if (!m_decided || wanted != m_wanted) {
        m_decided = true;
        m_wanted = wanted;
        if (wanted) {
            m_inhibitor->inhibit(i18n("A focus session is in progress"));
        } else {
            m_inhibitor->release();
        }
    }

    const bool nowActive = wanted && available() && m_inhibitor->isInhibited();
    if (nowActive != m_active) {
        m_active = nowActive;
        Q_EMIT activeChanged();
    }
}

void SilenceController::shutdown()
{
    if (!m_inhibitor) {
        return;
    }
    m_engine = nullptr; // turns update() off: nothing may silence again while quitting
    m_wanted = false;
    m_inhibitor->release();
    m_inhibitor->waitUntilReleased(kShutdownWaitMs);
}
