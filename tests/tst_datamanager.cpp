// SPDX-License-Identifier: GPL-3.0-or-later
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QDateTime>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include "Localization.h"

#include "DataManager.h"
#include "Database.h"
#include "FakeClock.h"
#include "PresetModel.h"
#include "PresetPersistence.h"
#include "PresetRepository.h"
#include "SessionRecorder.h"
#include "SessionRepository.h"
#include "TimerEngine.h"

#include <limits>
#include <memory>

namespace
{
/// Runs statements on a database file through a throw-away connection.
bool execOn(const QString &path, const QStringList &statements)
{
    const QString name = QStringLiteral("edit-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    bool ok = true;
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        ok = db.open();
        QSqlQuery query(db);
        for (const QString &statement : statements) {
            ok = ok && query.exec(statement);
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(name);
    return ok;
}

} // namespace

class DataManagerTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir dir;
    std::unique_ptr<Database> database;
    std::unique_ptr<SessionRepository> sessions;
    std::unique_ptr<PresetRepository> presetRepository;
    std::unique_ptr<PresetModel> presetModel;
    std::unique_ptr<DataManager> manager;

private Q_SLOTS:
    void initTestCase()
    {
    }

    void init()
    {
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("active.db"));
        QFile::remove(path);
        for (const QString &suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm")}) {
            QFile::remove(dir.filePath(QStringLiteral("backup.db")) + suffix);
        }
        database = std::make_unique<Database>(path);
        QVERIFY(database->open());
        sessions = std::make_unique<SessionRepository>(database->connectionName());
        presetRepository = std::make_unique<PresetRepository>(database->connectionName());
        presetModel = std::make_unique<PresetModel>();
        TimerPreset preset;
        preset.uuid = QStringLiteral("persisted-preset");
        preset.name = QStringLiteral("Persisted preset");
        preset.category = QStringLiteral("Test");
        QVERIFY(presetRepository->saveAll({preset}));
        QVERIFY(presetRepository->setCurrentUuid(preset.uuid));
        presetModel->setPresets({preset}, preset.uuid);
        manager = std::make_unique<DataManager>(database.get(), sessions.get(), presetRepository.get(), presetModel.get(), nullptr);
    }

    void cleanup()
    {
        manager.reset();
        presetModel.reset();
        presetRepository.reset();
        sessions.reset();
        database.reset();
    }

    void backupCanBeRestoredAfterHistoryIsCleared()
    {
        SessionRecord record;
        record.kind = SessionKind::Work;
        record.startedAtMs = 1000;
        record.endedAtMs = 1600;
        record.durationSec = 600;
        record.plannedSec = 1500;
        record.completed = true;
        record.presetName = QStringLiteral("Pomodoro");
        QVERIFY(sessions->insert(record));
        QCOMPARE(manager->sessionCount(), 1);

        QSignalSpy succeeded(manager.get(), &DataManager::operationSucceeded);
        const QUrl backup = QUrl::fromLocalFile(dir.filePath(QStringLiteral("backup.db")));
        QVERIFY(manager->backupDatabase(backup));
        QVERIFY(QFile::exists(backup.toLocalFile()));
        QCOMPARE(succeeded.count(), 1);

        QVERIFY(manager->clearHistory());
        QCOMPARE(manager->sessionCount(), 0);
        QCOMPARE(succeeded.count(), 2);

        QVERIFY(presetRepository->saveAll({}));
        presetModel->setPresets({}, QString());

        QSignalSpy modified(presetModel.get(), &PresetModel::modified);
        const qint64 before = QDateTime::currentMSecsSinceEpoch();
        QVERIFY(manager->restoreDatabase(backup));
        // Changed now by this computer, so Sugo Sync publishes the restored timers.
        QCOMPARE(modified.count(), 1);
        for (const TimerPreset &preset : presetModel->presets()) {
            QVERIFY(preset.updatedAt >= before);
            QVERIFY(preset.updatedBy.isEmpty());
        }
        QCOMPARE(manager->sessionCount(), 1);
        const auto restored = sessions->between(0, 2000);
        QCOMPARE(restored.size(), 1);
        QCOMPARE(restored.first().presetName, QStringLiteral("Pomodoro"));
        QCOMPARE(restored.first().plannedSec, 1500);
        QCOMPARE(presetModel->count(), 2); // built-in default plus the restored custom preset
        QCOMPARE(presetModel->get(QStringLiteral("persisted-preset")).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Persisted preset"));
    }

    void invalidBackupAndActiveDatabaseAreRejectedWithoutDataLoss()
    {
        SessionRecord record;
        record.startedAtMs = 100;
        record.endedAtMs = 200;
        record.presetName = QStringLiteral("Existing");
        QVERIFY(sessions->insert(record));
        QSignalSpy failed(manager.get(), &DataManager::operationFailed);

        const QString invalidPath = dir.filePath(QStringLiteral("invalid.db"));
        QFile invalid(invalidPath);
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("not a database");
        invalid.close();
        QVERIFY(!manager->restoreDatabase(QUrl::fromLocalFile(invalidPath)));
        QVERIFY(!manager->restoreDatabase(QUrl::fromLocalFile(database->path())));
        QVERIFY(!manager->backupDatabase(QUrl::fromLocalFile(database->path())));

        QCOMPARE(manager->sessionCount(), 1);
        QCOMPARE(sessions->between(0, 1000).first().presetName, QStringLiteral("Existing"));
        QCOMPARE(failed.count(), 3);
    }

    SessionRecord sampleRecord(const QString &name = QStringLiteral("Pomodoro"))
    {
        SessionRecord record;
        record.kind = SessionKind::Work;
        record.startedAtMs = 1000;
        record.endedAtMs = 1600;
        record.durationSec = 600;
        record.plannedSec = 1500;
        record.completed = true;
        record.presetName = name;
        record.note = QStringLiteral("Backed-up note");
        return record;
    }

    void backupFromNewerOrForeignDatabaseIsRejected()
    {
        QVERIFY(sessions->insert(sampleRecord(QStringLiteral("Existing"))));
        const QString backupPath = dir.filePath(QStringLiteral("backup.db"));
        QVERIFY(manager->backupDatabase(QUrl::fromLocalFile(backupPath)));
        QSignalSpy failed(manager.get(), &DataManager::operationFailed);

        QVERIFY(execOn(backupPath, {QStringLiteral("PRAGMA user_version = %1").arg(Database::latestSchemaVersion() + 1)}));
        QVERIFY(!manager->restoreDatabase(QUrl::fromLocalFile(backupPath)));

        QVERIFY(execOn(backupPath, {QStringLiteral("PRAGMA user_version = %1").arg(Database::latestSchemaVersion()),
                                    QStringLiteral("PRAGMA application_id = 42")}));
        QVERIFY(!manager->restoreDatabase(QUrl::fromLocalFile(backupPath)));

        QCOMPARE(failed.count(), 2);
        QCOMPARE(manager->sessionCount(), 1);
        QCOMPARE(sessions->between(0, 2000).first().presetName, QStringLiteral("Existing"));
    }

    void backupIsReadableOnlyByItsOwner()
    {
        // The whole history (timer names, categories, notes) goes into it, wherever it is saved.
        const QString path = dir.filePath(QStringLiteral("backup.db"));
        QVERIFY(manager->backupDatabase(QUrl::fromLocalFile(path)));
        const QFile::Permissions permissions = QFileInfo(path).permissions();
        QVERIFY(permissions & QFile::ReadOwner);
        QVERIFY2(!(permissions & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)),
                 "the backup must not be readable by other users");

        // Also when an existing (public) file is replaced.
        QVERIFY(QFile::setPermissions(path, permissions | QFile::ReadOther | QFile::ReadGroup));
        QVERIFY(manager->backupDatabase(QUrl::fromLocalFile(path)));
        QVERIFY(!(QFileInfo(path).permissions() & (QFile::ReadGroup | QFile::ReadOther)));
    }

        void missingBackupFileIsNotCreated()
    {
        const QString missing = dir.filePath(QStringLiteral("does-not-exist.db"));
        QVERIFY(!manager->restoreDatabase(QUrl::fromLocalFile(missing)));
        QVERIFY(!QFile::exists(missing));
    }

    void recorderKeepsSessionCountAndLastWorkSessionIdCurrent()
    {
        QCOMPARE(manager->lastWorkSessionId(), -1); // no recorder yet

        FakeClock clock;
        TimerEngine engine(&clock, nullptr);
        SessionRecorder recorder(&engine, presetModel.get(), sessions.get());
        manager->setSessionRecorder(&recorder);
        QSignalSpy changed(manager.get(), &DataManager::dataChanged);

        engine.start();
        clock.advanceSeconds(60);
        engine.refresh();
        engine.stop();

        QCOMPARE(changed.count(), 1);
        QCOMPARE(manager->sessionCount(), 1);
        const qint64 id = manager->lastWorkSessionId();
        QCOMPARE(id, sessions->between(0, std::numeric_limits<qint64>::max()).first().id);

        QVERIFY(manager->updateSessionNote(id, QStringLiteral("Wrote tests")));
        QCOMPARE(manager->recentTaskNotes(), QStringList{QStringLiteral("Wrote tests")});

        manager->setSessionRecorder(nullptr);
        QCOMPARE(manager->lastWorkSessionId(), -1);
    }

    void saveFailuresAreReportedUntilTheyAreStored()
    {
        FakeClock clock;
        TimerEngine engine(&clock, nullptr);
        SessionRecorder recorder(&engine, presetModel.get(), sessions.get());
        recorder.setRetryIntervalForTesting(30);
        PresetPersistence persistence(presetModel.get(), presetRepository.get());
        persistence.setRetryIntervalForTesting(30);
        manager->setSessionRecorder(&recorder);
        manager->setPresetPersistence(&persistence);
        QSignalSpy failing(manager.get(), &DataManager::saveFailingChanged);
        QVERIFY(!manager->saveFailing());

        QSqlQuery query(QSqlDatabase::database(database->connectionName()));
        QVERIFY(query.exec(QStringLiteral("CREATE TEMP TRIGGER refuse BEFORE INSERT ON session "
                                          "BEGIN SELECT RAISE(ABORT, 'disk full'); END")));
        engine.start();
        clock.advanceSeconds(25 * 60);
        engine.refresh();
        QVERIFY(manager->saveFailing());
        QVERIFY(failing.count() >= 1);

        QVERIFY(query.exec(QStringLiteral("DROP TRIGGER refuse")));
        QTRY_VERIFY(!manager->saveFailing());
        QCOMPARE(manager->sessionCount(), 1);

        manager->setSessionRecorder(nullptr);
        manager->setPresetPersistence(nullptr);
    }

    void sessionsWaitingToBeStoredDoNotReappearAfterClearing()
    {
        FakeClock clock;
        TimerEngine engine(&clock, nullptr);
        SessionRecorder recorder(&engine, presetModel.get(), sessions.get());
        recorder.setRetryIntervalForTesting(30);
        manager->setSessionRecorder(&recorder);

        QSqlQuery query(QSqlDatabase::database(database->connectionName()));
        QVERIFY(query.exec(QStringLiteral("CREATE TEMP TRIGGER refuse BEFORE INSERT ON session "
                                          "BEGIN SELECT RAISE(ABORT, 'disk full'); END")));
        engine.start();
        clock.advanceSeconds(25 * 60);
        engine.refresh();
        QVERIFY(manager->saveFailing());

        QVERIFY(manager->clearHistory());
        QVERIFY(!manager->saveFailing());
        QVERIFY(query.exec(QStringLiteral("DROP TRIGGER refuse")));
        QTest::qWait(150); // several retry intervals
        QCOMPARE(manager->sessionCount(), 0);

        manager->setSessionRecorder(nullptr);
    }

    void restoreWhoseTimersCannotBeReadChangesNothing()
    {
        QVERIFY(sessions->insert(sampleRecord(QStringLiteral("In the backup"))));
        const QUrl backup = QUrl::fromLocalFile(dir.filePath(QStringLiteral("backup.db")));
        QVERIFY(manager->backupDatabase(backup));
        QVERIFY(manager->clearHistory());
        QVERIFY(sessions->insert(sampleRecord(QStringLiteral("Current"))));

        // Shadows the timer options for this connection only: the restore itself (which names
        // main.preset_option) works, reading the timers back does not.
        QSqlQuery query(QSqlDatabase::database(database->connectionName()));
        QVERIFY(query.exec(QStringLiteral("CREATE TEMP TABLE preset_option (unrelated INTEGER)")));
        QSignalSpy failed(manager.get(), &DataManager::operationFailed);
        QVERIFY(!manager->restoreDatabase(backup));
        QCOMPARE(failed.count(), 1);
        QVERIFY(query.exec(QStringLiteral("DROP TABLE temp.preset_option")));

        const auto stored = sessions->between(0, 2000);
        QCOMPARE(stored.size(), 1);
        QCOMPARE(stored.first().presetName, QStringLiteral("Current"));
    }

        void temporaryDatabaseIsReported()
    {
        QVERIFY(!manager->usingTemporaryDatabase());
        Database memory(QStringLiteral(":memory:"));
        QVERIFY(memory.open());
        DataManager inMemory(&memory, nullptr, nullptr, nullptr, nullptr);
        QVERIFY(inMemory.usingTemporaryDatabase());
    }

    void clearHistoryOlderThanKeepsRecentSessions()
    {
        const QDateTime now(QDate(2026, 10, 31), QTime(12, 0));
        manager->setNowProvider([now]() { return now; });
        SessionRecord record;
        record.kind = SessionKind::Work;
        record.startedAtMs = now.addMonths(-7).toMSecsSinceEpoch();
        QVERIFY(sessions->insert(record));
        record.startedAtMs = now.addMonths(-4).toMSecsSinceEpoch();
        QVERIFY(sessions->insert(record));
        record.startedAtMs = now.addDays(-2).toMSecsSinceEpoch();
        QVERIFY(sessions->insert(record));

        QSignalSpy succeeded(manager.get(), &DataManager::operationSucceeded);
        QSignalSpy failed(manager.get(), &DataManager::operationFailed);
        QSignalSpy changed(manager.get(), &DataManager::dataChanged);
        QVERIFY(manager->clearHistoryOlderThan(6));
        QCOMPARE(manager->sessionCount(), 2);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(succeeded.count(), 1);
        QCOMPARE(succeeded.first().first().toString(), QStringLiteral("1 session removed."));

        QVERIFY(manager->clearHistoryOlderThan(3));
        QCOMPARE(manager->sessionCount(), 1);
        QCOMPARE(succeeded.last().first().toString(), QStringLiteral("1 session removed."));

        // Nothing left to remove is not an error.
        QVERIFY(manager->clearHistoryOlderThan(3));
        QCOMPARE(manager->sessionCount(), 1);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(failed.count(), 0);

        QVERIFY(!manager->clearHistoryOlderThan(0));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(manager->sessionCount(), 1);
    }

    void exportToCsvQuotesFieldsAndNeutralisesFormulas()
    {
        SessionRecord record;
        record.kind = SessionKind::Work;
        record.startedAtMs = 1'700'000'000'000;
        record.endedAtMs = 1'700'000'600'000;
        record.durationSec = 600;
        record.plannedSec = 1500;
        record.completed = true;
        record.presetName = QStringLiteral("Deep, \"focus\"");
        record.category = QStringLiteral("=SUM(A1)");
        record.note = QStringLiteral("line one\nline two");
        QVERIFY(sessions->insert(record));
        record.kind = SessionKind::ShortBreak;
        record.completed = false;
        record.presetName = QStringLiteral("@cmd");
        record.category = QStringLiteral("-1+1");
        record.note = QStringLiteral("+x");
        QVERIFY(sessions->insert(record));

        QSignalSpy succeeded(manager.get(), &DataManager::operationSucceeded);
        const QString path = dir.filePath(QStringLiteral("history.csv"));
        QVERIFY(manager->exportHistory(QUrl::fromLocalFile(path)));
        QCOMPARE(succeeded.count(), 1);
        QCOMPARE(succeeded.first().first().toString(), QStringLiteral("Exported 2 sessions."));
        QCOMPARE(QFile::permissions(path) & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther),
                 QFileDevice::Permissions());

        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString csv = QString::fromUtf8(file.readAll());
        const QStringList lines = csv.split(QStringLiteral("\r\n"));
        QCOMPARE(lines.first(), QStringLiteral("id,kind,started_at,ended_at,duration_sec,planned_sec,completed,preset_name,category,note"));
        // The first row's note spans two physical lines: it is one quoted field.
        QVERIFY(csv.contains(QStringLiteral(",600,1500,1,\"Deep, \"\"focus\"\"\",'=SUM(A1),\"line one\nline two\"\r\n")));
        QVERIFY(csv.contains(QStringLiteral(",short_break,")));
        QVERIFY(csv.contains(QStringLiteral(",600,1500,0,'@cmd,'-1+1,'+x\r\n")));
        QVERIFY(csv.endsWith(QStringLiteral("\r\n")));
        // The times carry an offset from UTC and name the same instants.
        const QString started = csv.split(QStringLiteral("\r\n")).at(1).split(QLatin1Char(',')).at(2);
        QVERIFY(started.contains(QRegularExpression(QStringLiteral("[+-]\\d\\d:\\d\\d$|Z$"))));
        QCOMPARE(QDateTime::fromString(started, Qt::ISODate).toMSecsSinceEpoch(), qint64(1'700'000'000'000));
    }

    void exportToJsonKeepsTextAsIs()
    {
        SessionRecord record;
        record.kind = SessionKind::LongBreak;
        record.startedAtMs = 1'700'000'000'000;
        record.endedAtMs = 1'700'000'900'000;
        record.durationSec = 900;
        record.plannedSec = 900;
        record.completed = true;
        record.presetName = QStringLiteral("Pomodoro");
        record.category = QStringLiteral("Study");
        record.note = QStringLiteral("=keep");
        QVERIFY(sessions->insert(record));

        const QString path = dir.filePath(QStringLiteral("history.JSON"));
        QVERIFY(manager->exportHistory(QUrl::fromLocalFile(path)));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QVERIFY(doc.isArray());
        QCOMPARE(doc.array().size(), 1);
        const QJsonObject row = doc.array().first().toObject();
        QCOMPARE(row.value(QStringLiteral("kind")).toString(), QStringLiteral("long_break"));
        QCOMPARE(row.value(QStringLiteral("duration_sec")).toInt(), 900);
        QCOMPARE(row.value(QStringLiteral("planned_sec")).toInt(), 900);
        QCOMPARE(row.value(QStringLiteral("completed")).toBool(), true);
        QCOMPARE(row.value(QStringLiteral("preset_name")).toString(), QStringLiteral("Pomodoro"));
        QCOMPARE(row.value(QStringLiteral("category")).toString(), QStringLiteral("Study"));
        QCOMPARE(row.value(QStringLiteral("note")).toString(), QStringLiteral("=keep"));
        QCOMPARE(QDateTime::fromString(row.value(QStringLiteral("ended_at")).toString(), Qt::ISODate).toMSecsSinceEpoch(),
                 qint64(1'700'000'900'000));
        QVERIFY(row.contains(QStringLiteral("id")));
    }

    void exportFailsForUnusableTargets()
    {
        QSignalSpy failed(manager.get(), &DataManager::operationFailed);
        QVERIFY(!manager->exportHistory(QUrl(QStringLiteral("https://example.com/history.csv"))));
        QCOMPARE(failed.count(), 1);
        QVERIFY(!manager->exportHistory(QUrl::fromLocalFile(dir.filePath(QStringLiteral("history.txt")))));
        QCOMPARE(failed.count(), 2);
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("history.txt"))));
        QVERIFY(!manager->exportHistory(QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing/dir/history.csv")))));
        QCOMPARE(failed.count(), 3);
    }

    void presetCountReflectsModelCount()
    {
        QCOMPARE(manager->presetCount(), presetModel->count());
        QCOMPARE(manager->presetCount(), 2);

        const QString newUuid = presetModel->create({{QStringLiteral("name"), QStringLiteral("Second")}});
        QCOMPARE(manager->presetCount(), 3);

        presetModel->remove(newUuid);
        QCOMPARE(manager->presetCount(), 2);
    }
};

QTEST_GUILESS_MAIN(DataManagerTest)
#include "tst_datamanager.moc"
