// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QTimer>

class PresetModel;
class PresetRepository;

/**
 * Loads the presets into the model at construction and saves every later change.
 *
 * Saving is synchronous: a handful of rows in a WAL database costs well under a
 * millisecond, and it means a crash can lose at most the edit in progress. A save the database
 * refuses is tried again later (and at the next change); isSaveFailing() says so meanwhile.
 */
class PresetPersistence : public QObject
{
    Q_OBJECT

public:
    PresetPersistence(PresetModel *model, PresetRepository *repository, QObject *parent = nullptr);

    /// True when the stored timers were read (or none existed yet) and changes will be saved.
    /// False when loading failed: the model then only holds the built-in timer, unsaved.
    bool isActive() const { return m_active; }
    /// True while the latest changes could not be stored.
    bool isSaveFailing() const { return m_saveFailing; }
    void setRetryIntervalForTesting(int ms) { m_retryTimer.setInterval(ms); }

Q_SIGNALS:
    void saveFailingChanged();

private:
    void savePresets();
    void saveSelection();
    void setSaveFailing(bool failing);

    PresetModel *m_model;
    PresetRepository *m_repository;
    bool m_active = false;
    bool m_saveFailing = false;
    QTimer m_retryTimer;
};
