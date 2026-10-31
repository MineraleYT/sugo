// SPDX-License-Identifier: GPL-3.0-or-later
#include "TimerBinding.h"

#include "PresetModel.h"
#include "TimerEngine.h"

TimerBinding::TimerBinding(TimerEngine *engine, PresetModel *presets, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
    , m_presets(presets)
{
    connect(presets, &PresetModel::currentChanged, this, &TimerBinding::apply);
    connect(presets, &PresetModel::presetChanged, this, [this](const QString &uuid) {
        if (uuid == m_presets->currentUuid()) {
            apply();
        }
    });
    apply();
}

void TimerBinding::apply()
{
    const TimerPreset preset = m_presets->currentPreset();

    if (preset.uuid != m_appliedUuid) {
        if (!m_appliedUuid.isEmpty()) {
            m_engine->reset();
        }
        m_appliedUuid = preset.uuid;
    }

    m_engine->setWorkSeconds(preset.workSeconds);
    m_engine->setShortBreakSeconds(preset.shortBreakSeconds);
    m_engine->setLongBreakSeconds(preset.longBreakSeconds);
    m_engine->setCyclesBeforeLong(preset.cyclesBeforeLong);
    const int autoMode = preset.option(PresetOption::AutoStartMode).toInt();
    m_engine->setAutoStartModeInt(autoMode);
}
