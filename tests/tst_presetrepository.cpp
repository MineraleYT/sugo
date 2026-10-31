// SPDX-License-Identifier: GPL-3.0-or-later
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>

#include "Localization.h"

#include <memory>

#include "Database.h"
#include "PresetModel.h"
#include "PresetPersistence.h"
#include "PresetRepository.h"

namespace
{
TimerPreset makePreset(const QString &uuid, const QString &name, int workSeconds = 25 * 60)
{
    TimerPreset p;
    p.uuid = uuid;
    p.name = name;
    p.workSeconds = workSeconds;
    return p;
}

TimerPreset builtinDefault()
{
    TimerPreset p = makePreset(PresetModel::DefaultUuid, QStringLiteral("Default"));
    p.builtin = true;
    return p;
}

QStringList uuids(const QList<TimerPreset> &list)
{
    QStringList result;
    for (const TimerPreset &p : list) {
        result.append(p.uuid);
    }
    return result;
}
} // namespace

class PresetRepositoryTest : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<Database> db;
    std::unique_ptr<PresetRepository> repo;

private Q_SLOTS:
    void initTestCase()
    {
    }

    void init()
    {
        db = std::make_unique<Database>(QStringLiteral(":memory:"));
        QVERIFY(db->open());
        repo = std::make_unique<PresetRepository>(db->connectionName());
    }

    void cleanup()
    {
        repo.reset();
        db.reset();
    }

    void emptyDatabaseLoadsNothing()
    {
        bool ok = false;
        QVERIFY(repo->loadAll(&ok).isEmpty());
        QVERIFY(ok);
    }

    void saveAndLoadRoundTrip()
    {
        TimerPreset custom = makePreset(QStringLiteral("u1"), QStringLiteral("Study"), 50 * 60);
        custom.category = QStringLiteral("Study");
        custom.shortBreakSeconds = 10 * 60;
        custom.longBreakSeconds = 30 * 60;
        custom.cyclesBeforeLong = 2;
        custom.options = {
            {PresetOption::SilenceWhileWorking, true},
            {PresetOption::NotifyOnEnd, false},
            {QStringLiteral("optionFromNewerVersion"), QStringLiteral("kept")}, // unknown keys survive
        };

        QVERIFY(repo->saveAll({builtinDefault(), custom}));

        bool ok = false;
        const QList<TimerPreset> loaded = repo->loadAll(&ok);
        QVERIFY(ok);
        QCOMPARE(loaded.size(), 2);

        QVERIFY(loaded.at(0).builtin);
        QCOMPARE(loaded.at(0).uuid, PresetModel::DefaultUuid);

        const TimerPreset &p = loaded.at(1);
        QVERIFY(!p.builtin);
        QCOMPARE(p.uuid, custom.uuid);
        QCOMPARE(p.name, custom.name);
        QCOMPARE(p.category, custom.category);
        QCOMPARE(p.workSeconds, 50 * 60);
        QCOMPARE(p.shortBreakSeconds, 10 * 60);
        QCOMPARE(p.longBreakSeconds, 30 * 60);
        QCOMPARE(p.cyclesBeforeLong, 2);
        QCOMPARE(p.options, custom.options);
        QCOMPARE(p.options.value(PresetOption::SilenceWhileWorking).typeId(), QMetaType::Bool);
    }

    void savingAgainUpdatesInPlaceAndReorders()
    {
        QVERIFY(repo->saveAll({builtinDefault(), makePreset(QStringLiteral("a"), QStringLiteral("A")),
                               makePreset(QStringLiteral("b"), QStringLiteral("B")),
                               makePreset(QStringLiteral("c"), QStringLiteral("C"))}));

        QVERIFY(repo->saveAll({builtinDefault(), makePreset(QStringLiteral("c"), QStringLiteral("C renamed")),
                               makePreset(QStringLiteral("a"), QStringLiteral("A"), 10 * 60)}));

        const QList<TimerPreset> loaded = repo->loadAll();
        QCOMPARE(uuids(loaded), (QStringList{QStringLiteral("default"), QStringLiteral("c"), QStringLiteral("a")}));
        QCOMPARE(loaded.at(1).name, QStringLiteral("C renamed"));
        QCOMPARE(loaded.at(2).workSeconds, 10 * 60);
    }

    void removedOptionsDisappear()
    {
        TimerPreset p = makePreset(QStringLiteral("a"), QStringLiteral("A"));
        p.options = {{PresetOption::NotifyOnEnd, true}, {PresetOption::SoundOnEnd, false}};
        QVERIFY(repo->saveAll({builtinDefault(), p}));

        p.options = {{PresetOption::NotifyOnEnd, true}};
        QVERIFY(repo->saveAll({builtinDefault(), p}));

        QCOMPARE(repo->loadAll().at(1).options, p.options);
    }

    void unreadableOptionsSurviveASave()
    {
        TimerPreset p = makePreset(QStringLiteral("a"), QStringLiteral("A"));
        p.options = {{PresetOption::NotifyOnEnd, true}};
        QVERIFY(repo->saveAll({builtinDefault(), p}));

        // A value this version cannot read (damaged, or written by a newer version).
        QSqlQuery query(QSqlDatabase::database(db->connectionName()));
        QVERIFY(query.exec(QStringLiteral("INSERT INTO preset_option (preset_id, key, value) "
                                          "SELECT id, 'futureOption', '{not json' FROM preset WHERE uuid = 'a'")));
        const QList<TimerPreset> loaded = repo->loadAll();
        QCOMPARE(loaded.at(1).options, p.options); // skipped when loading

        // Saving the loaded timers keeps it; a readable option the model dropped still goes.
        TimerPreset edited = loaded.at(1);
        edited.options.remove(PresetOption::NotifyOnEnd);
        QVERIFY(repo->saveAll({builtinDefault(), edited}));
        QVERIFY(query.exec(QStringLiteral("SELECT key, value FROM preset_option o JOIN preset p ON p.id = o.preset_id "
                                          "WHERE p.uuid = 'a' ORDER BY key")));
        QVERIFY(query.next());
        QCOMPARE(query.value(0).toString(), QStringLiteral("futureOption"));
        QCOMPARE(query.value(1).toString(), QStringLiteral("{not json"));
        QVERIFY(!query.next());

        // Setting that key again replaces it.
        edited.options.insert(QStringLiteral("futureOption"), 3);
        QVERIFY(repo->saveAll({builtinDefault(), edited}));
        QCOMPARE(repo->loadAll().at(1).options.value(QStringLiteral("futureOption")).toInt(), 3);
    }

        void builtinPresetIsNeverDeleted()
    {
        QVERIFY(repo->saveAll({builtinDefault(), makePreset(QStringLiteral("a"), QStringLiteral("A"))}));
        QVERIFY(repo->saveAll({makePreset(QStringLiteral("a"), QStringLiteral("A"))})); // default missing from list
        QVERIFY(uuids(repo->loadAll()).contains(PresetModel::DefaultUuid));
    }

    void failedSaveRollsBackCompletely()
    {
        QVERIFY(repo->saveAll({builtinDefault(), makePreset(QStringLiteral("a"), QStringLiteral("A"))}));

        // work_sec = 0 violates a CHECK constraint, so the whole transaction must be undone.
        QVERIFY(!repo->saveAll({builtinDefault(), makePreset(QStringLiteral("a"), QStringLiteral("A changed")),
                                makePreset(QStringLiteral("bad"), QStringLiteral("Bad"), 0)}));

        const QList<TimerPreset> loaded = repo->loadAll();
        QCOMPARE(uuids(loaded), (QStringList{QStringLiteral("default"), QStringLiteral("a")}));
        QCOMPARE(loaded.at(1).name, QStringLiteral("A"));
    }

    void currentUuidRoundTrips()
    {
        QVERIFY(repo->currentUuid().isEmpty());
        QVERIFY(repo->setCurrentUuid(QStringLiteral("abc")));
        QCOMPARE(repo->currentUuid(), QStringLiteral("abc"));
        QVERIFY(repo->setCurrentUuid(QStringLiteral("def")));
        QCOMPARE(repo->currentUuid(), QStringLiteral("def"));
    }

    // --- PresetPersistence: model <-> repository -----------------------------

    void firstRunStoresTheBuiltinPreset()
    {
        PresetModel model;
        PresetPersistence persistence(&model, repo.get());

        const QList<TimerPreset> stored = repo->loadAll();
        QCOMPARE(stored.size(), 1);
        QVERIFY(stored.at(0).builtin);
        QCOMPARE(repo->currentUuid(), PresetModel::DefaultUuid);
    }

    void editsSurviveARestart()
    {
        QString studyUuid;
        {
            PresetModel model;
            PresetPersistence persistence(&model, repo.get());

            studyUuid = model.create({{QStringLiteral("name"), QStringLiteral("Study")},
                                      {QStringLiteral("category"), QStringLiteral("Uni")},
                                      {QStringLiteral("workSeconds"), 45 * 60}});
            model.setOption(studyUuid, PresetOption::AutoStartMode, 2);
            model.duplicate(studyUuid);
            model.update(PresetModel::DefaultUuid, {{QStringLiteral("workSeconds"), 30 * 60}});
            model.setCurrentUuid(studyUuid);
        }

        PresetModel reloaded; // "restart"
        PresetPersistence persistence(&reloaded, repo.get());

        QCOMPARE(reloaded.count(), 3);
        QCOMPARE(reloaded.currentUuid(), studyUuid);
        const QVariantMap study = reloaded.get(studyUuid);
        QCOMPARE(study.value(QStringLiteral("name")).toString(), QStringLiteral("Study"));
        QCOMPARE(study.value(QStringLiteral("category")).toString(), QStringLiteral("Uni"));
        QCOMPARE(study.value(QStringLiteral("workSeconds")).toInt(), 45 * 60);
        QCOMPARE(reloaded.option(studyUuid, PresetOption::AutoStartMode).toInt(), 2);
        QCOMPARE(reloaded.get(PresetModel::DefaultUuid).value(QStringLiteral("workSeconds")).toInt(), 30 * 60);
        // The duplicate keeps its position right after its source.
        QCOMPARE(reloaded.indexOf(studyUuid), 1);
        QVERIFY(reloaded.data(reloaded.index(2), PresetModel::NameRole).toString().contains(QStringLiteral("Study")));
    }

    void refusedSavesAreReportedAndRetried()
    {
        PresetModel model;
        PresetPersistence persistence(&model, repo.get());
        persistence.setRetryIntervalForTesting(30);
        QSignalSpy failing(&persistence, &PresetPersistence::saveFailingChanged);

        QSqlQuery query(QSqlDatabase::database(db->connectionName()));
        QVERIFY(query.exec(QStringLiteral("CREATE TEMP TRIGGER refuse BEFORE INSERT ON preset "
                                          "BEGIN SELECT RAISE(ABORT, 'disk full'); END")));
        const QString uuid = model.create({{QStringLiteral("name"), QStringLiteral("Study")}});
        QVERIFY(persistence.isSaveFailing());
        QCOMPARE(failing.count(), 1);
        QVERIFY(!uuids(repo->loadAll()).contains(uuid));

        QVERIFY(query.exec(QStringLiteral("DROP TRIGGER refuse")));
        QTRY_VERIFY(!persistence.isSaveFailing());
        QCOMPARE(failing.count(), 2);
        QVERIFY(uuids(repo->loadAll()).contains(uuid));
    }

    void deletedPresetsStayDeletedAndSelectionFallsBack()
    {
        QString uuid;
        {
            PresetModel model;
            PresetPersistence persistence(&model, repo.get());
            uuid = model.create({{QStringLiteral("name"), QStringLiteral("Temp")}});
            model.setCurrentUuid(uuid);
            model.remove(uuid);
        }
        PresetModel reloaded;
        PresetPersistence persistence(&reloaded, repo.get());
        QCOMPARE(reloaded.count(), 1);
        QCOMPARE(reloaded.currentUuid(), PresetModel::DefaultUuid);
    }

    void resetOfBuiltinIsPersisted()
    {
        {
            PresetModel model;
            PresetPersistence persistence(&model, repo.get());
            model.update(PresetModel::DefaultUuid, {{QStringLiteral("workSeconds"), 99 * 60}});
            model.setOption(PresetModel::DefaultUuid, PresetOption::AutoStartMode, 2);
            model.reset(PresetModel::DefaultUuid);
        }
        PresetModel reloaded;
        PresetPersistence persistence(&reloaded, repo.get());
        QCOMPARE(reloaded.get(PresetModel::DefaultUuid).value(QStringLiteral("workSeconds")).toInt(), 25 * 60);
        QCOMPARE(reloaded.option(PresetModel::DefaultUuid, PresetOption::AutoStartMode).toInt(), 0);
    }

    void unknownStoredSelectionFallsBackToDefault()
    {
        QVERIFY(repo->saveAll({builtinDefault()}));
        QVERIFY(repo->setCurrentUuid(QStringLiteral("does-not-exist")));

        PresetModel model;
        PresetPersistence persistence(&model, repo.get());
        QCOMPARE(model.currentUuid(), PresetModel::DefaultUuid);
    }

    void storedBuiltinFlagOnOtherPresetsIsIgnored()
    {
        TimerPreset sneaky = makePreset(QStringLiteral("x"), QStringLiteral("Sneaky"));
        sneaky.builtin = true; // would make it undeletable
        QVERIFY(repo->saveAll({builtinDefault(), sneaky}));

        PresetModel model;
        PresetPersistence persistence(&model, repo.get());
        QVERIFY(model.remove(QStringLiteral("x")));
    }

    void loadingNeverWritesBack()
    {
        QVERIFY(repo->saveAll({builtinDefault(), makePreset(QStringLiteral("a"), QStringLiteral("A"))}));
        // Make a later write visible: if loading re-saved the data, this marker would be rewritten.
        QVERIFY(repo->setCurrentUuid(QStringLiteral("a")));

        PresetModel model;
        PresetPersistence persistence(&model, repo.get());
        QCOMPARE(model.currentUuid(), QStringLiteral("a"));
        QCOMPARE(repo->currentUuid(), QStringLiteral("a"));
        QCOMPARE(repo->loadAll().size(), 2);
    }

    void failedLoadNeverOverwritesTheStoredPresets()
    {
        QVERIFY(repo->saveAll({builtinDefault(), makePreset(QStringLiteral("mine"), QStringLiteral("Mine"))}));

        // Make loading fail, then repair the table so that a save, if attempted, would succeed.
        QSqlQuery query(QSqlDatabase::database(db->connectionName(), false));
        QVERIFY(query.exec(QStringLiteral("ALTER TABLE preset_option RENAME TO preset_option_hidden")));
        PresetModel model;
        PresetPersistence persistence(&model, repo.get());
        QVERIFY(query.exec(QStringLiteral("ALTER TABLE preset_option_hidden RENAME TO preset_option")));

        model.create({{QStringLiteral("name"), QStringLiteral("Edit after failed load")}});
        QCOMPARE(uuids(repo->loadAll()), (QStringList{PresetModel::DefaultUuid, QStringLiteral("mine")}));
    }
};

QTEST_GUILESS_MAIN(PresetRepositoryTest)
#include "tst_presetrepository.moc"
