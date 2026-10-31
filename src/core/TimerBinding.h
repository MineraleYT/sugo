// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>

class PresetModel;
class TimerEngine;

/**
 * Keeps the TimerEngine configured with the currently selected preset.
 *
 * Picking another preset resets the engine to a fresh Work phase; editing the
 * current preset only updates its configuration (a running phase is unaffected).
 */
class TimerBinding : public QObject
{
    Q_OBJECT

public:
    TimerBinding(TimerEngine *engine, PresetModel *presets, QObject *parent = nullptr);

private:
    void apply();

    TimerEngine *m_engine;
    PresetModel *m_presets;
    QString m_appliedUuid;
};
