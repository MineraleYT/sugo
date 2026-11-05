// SPDX-License-Identifier: GPL-3.0-or-later
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "PresetModel.h"
#include "PresetSyncFormat.h"

using PresetSyncFormat::Data;

namespace
{
const QString kMe = QStringLiteral("dev_me");
const QString kOther = QStringLiteral("dev_other");
constexpr qint64 kNow = 1762300000000; // November 2025
constexpr qint64 kDay = 24LL * 3600 * 1000;

TimerPreset preset(const QString &uuid, const QString &name, qint64 updatedAt, const QString &by = {}, qint64 createdAt = 1)
{
    TimerPreset p;
    p.uuid = uuid;
    p.name = name;
    p.builtin = uuid == PresetModel::DefaultUuid;
    p.createdAt = createdAt;
    p.updatedAt = updatedAt;
    p.updatedBy = by;
    return p;
}

PresetTombstone tombstone(const QString &uuid, qint64 deletedAt, const QString &by = {})
{
    return PresetTombstone{uuid, deletedAt, by};
}

QStringList names(const QList<TimerPreset> &list)
{
    QStringList result;
    for (const TimerPreset &p : list) {
        result.append(p.name);
    }
    return result;
}

QStringList uuids(const QList<PresetTombstone> &list)
{
    QStringList result;
    for (const PresetTombstone &t : list) {
        result.append(t.uuid);
    }
    return result;
}

QByteArray json(const QJsonObject &root)
{
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

/// The default preset as both computers start with it.
TimerPreset factoryDefault()
{
    return preset(PresetModel::DefaultUuid, QStringLiteral("Pomodoro"), 0, {}, 0);
}
} // namespace

class PresetSyncFormatTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // --- JSON ---

    void jsonRoundTripsAndNamesTheAuthors()
    {
        TimerPreset mine = preset(QStringLiteral("a"), QStringLiteral("Deep work"), kNow, {}, kNow - 5);
        mine.category = QStringLiteral("Focus");
        mine.workSeconds = 50 * 60;
        mine.shortBreakSeconds = 10 * 60;
        mine.longBreakSeconds = 0;
        mine.cyclesBeforeLong = 0;
        mine.options.insert(QStringLiteral("iconName"), QStringLiteral("sugo-focus"));
        mine.options.insert(PresetOption::SilenceWhileWorking, true);
        const TimerPreset theirs = preset(QStringLiteral("b"), QStringLiteral("Theirs"), kNow - 1, kOther);
        const Data data{{factoryDefault(), mine, theirs}, {tombstone(QStringLiteral("gone"), kNow - kDay)}};

        const QByteArray text = PresetSyncFormat::toJson(data, kMe);
        const QJsonObject root = QJsonDocument::fromJson(text).object();
        QCOMPARE(root.value(QLatin1String("format")).toInt(), PresetSyncFormat::Format);
        const QJsonArray presets = root.value(QLatin1String("presets")).toArray();
        QCOMPARE(presets.size(), 3);
        QCOMPARE(presets.at(1).toObject().value(QLatin1String("updatedBy")).toString(), kMe);
        QCOMPARE(presets.at(2).toObject().value(QLatin1String("updatedBy")).toString(), kOther);
        QCOMPARE(presets.at(1).toObject().value(QLatin1String("updatedAt")).toDouble(), double(kNow));
        QCOMPARE(root.value(QLatin1String("tombstones")).toArray().at(0).toObject().value(QLatin1String("deletedBy")).toString(), kMe);

        const PresetSyncFormat::ParseResult parsed = PresetSyncFormat::fromJson(text);
        QVERIFY(parsed.ok);
        QVERIFY(!parsed.newerFormat());
        QCOMPARE(parsed.data.presets.size(), 3);
        TimerPreset expected = mine;
        expected.updatedBy = kMe; // as written; merge() turns it back into "this computer"
        QVERIFY(parsed.data.presets.at(1).sameContent(expected));
        QCOMPARE(parsed.data.presets.at(1).createdAt, kNow - 5);
        QCOMPARE(parsed.data.presets.at(1).updatedAt, kNow);
        QCOMPARE(parsed.data.presets.at(1).updatedBy, kMe);
        QVERIFY(parsed.data.presets.at(0).builtin);
        QCOMPARE(parsed.data.tombstones, (QList<PresetTombstone>{tombstone(QStringLiteral("gone"), kNow - kDay, kMe)}));
    }

    void unknownOptionsAreKept()
    {
        const QJsonObject options{
            {QStringLiteral("futureColour"), QStringLiteral("#ff8800")},
            {QStringLiteral("futureNested"), QJsonObject{{QStringLiteral("x"), 1}}},
            {PresetOption::SilenceWhileWorking, false},
        };
        const QByteArray text = json({
            {QStringLiteral("format"), 1},
            {QStringLiteral("presets"), QJsonArray{QJsonObject{
                 {QStringLiteral("uuid"), QStringLiteral("a")},
                 {QStringLiteral("name"), QStringLiteral("A")},
                 {QStringLiteral("workSeconds"), 1500},
                 {QStringLiteral("options"), options},
             }}},
        });
        const PresetSyncFormat::ParseResult parsed = PresetSyncFormat::fromJson(text);
        QVERIFY(parsed.ok);
        const QVariantMap kept = parsed.data.presets.at(0).options;
        QCOMPARE(kept.value(QStringLiteral("futureColour")).toString(), QStringLiteral("#ff8800"));
        QCOMPARE(kept.value(QStringLiteral("futureNested")).toMap().value(QStringLiteral("x")).toInt(), 1);
        QCOMPARE(kept.value(PresetOption::SilenceWhileWorking).toBool(), false);

        // And they go back out as they came.
        const QJsonObject again = QJsonDocument::fromJson(PresetSyncFormat::toJson(parsed.data, kMe))
                                      .object().value(QLatin1String("presets")).toArray().at(0).toObject()
                                      .value(QLatin1String("options")).toObject();
        QCOMPARE(again, options);
    }

    void valuesOutsideTheLimitsAreBroughtIntoRange()
    {
        const QByteArray text = json({
            {QStringLiteral("format"), 1},
            {QStringLiteral("presets"), QJsonArray{
                 QJsonObject{
                     {QStringLiteral("uuid"), QStringLiteral("a")},
                     {QStringLiteral("name"), QString(300, QLatin1Char('n'))},
                     {QStringLiteral("workSeconds"), 99999999},
                     {QStringLiteral("shortBreakSeconds"), -5},
                     {QStringLiteral("longBreakSeconds"), QStringLiteral("long")},
                     {QStringLiteral("cyclesBeforeLong"), 1000},
                     {QStringLiteral("createdAt"), -1},
                     {QStringLiteral("options"), QJsonObject{{PresetOption::AutoStartMode, 999}}},
                 },
                 QJsonObject{{QStringLiteral("uuid"), QStringLiteral("b")}, {QStringLiteral("workSeconds"), 0}},
                 QJsonObject{{QStringLiteral("name"), QStringLiteral("no uuid")}},
                 QStringLiteral("not an object"),
                 QJsonObject{{QStringLiteral("uuid"), QStringLiteral("a")}, {QStringLiteral("name"), QStringLiteral("duplicate")}},
             }},
        });
        const PresetSyncFormat::ParseResult parsed = PresetSyncFormat::fromJson(text);
        QVERIFY(parsed.ok);
        QCOMPARE(parsed.data.presets.size(), 2);
        const TimerPreset &a = parsed.data.presets.at(0);
        QCOMPARE(a.name.size(), PresetLimits::MaxNameLength);
        QCOMPARE(a.workSeconds, PresetLimits::MaxWorkSeconds);
        QCOMPARE(a.shortBreakSeconds, 0);
        QCOMPARE(a.longBreakSeconds, TimerPreset().longBreakSeconds);
        QCOMPARE(a.cyclesBeforeLong, PresetLimits::MaxCycles);
        QCOMPARE(a.createdAt, 0);
        int maxMode = 0;
        for (const OptionDescriptor &d : PresetSettings::descriptors()) {
            if (d.key == PresetOption::AutoStartMode) {
                maxMode = d.maximum;
            }
        }
        QCOMPARE(a.options.value(PresetOption::AutoStartMode).toInt(), maxMode);
        const TimerPreset &b = parsed.data.presets.at(1);
        QCOMPARE(b.workSeconds, 1);
        QVERIFY(!b.name.isEmpty());
    }

    void defaultTombstoneIsIgnored()
    {
        const QByteArray text = json({
            {QStringLiteral("format"), 1},
            {QStringLiteral("tombstones"), QJsonArray{
                 QJsonObject{{QStringLiteral("uuid"), PresetModel::DefaultUuid}, {QStringLiteral("deletedAt"), double(kNow)}},
                 QJsonObject{{QStringLiteral("uuid"), QStringLiteral("x")}, {QStringLiteral("deletedAt"), double(kNow)}},
             }},
        });
        QCOMPARE(uuids(PresetSyncFormat::fromJson(text).data.tombstones), QStringList{QStringLiteral("x")});
    }

    void futureFormatIsReadOnly()
    {
        const QByteArray text = json({
            {QStringLiteral("format"), PresetSyncFormat::Format + 1},
            {QStringLiteral("presets"), QJsonArray{QJsonObject{
                 {QStringLiteral("uuid"), QStringLiteral("new")},
                 {QStringLiteral("name"), QStringLiteral("From the future")},
                 {QStringLiteral("updatedAt"), double(kNow)},
             }}},
        });
        const PresetSyncFormat::ParseResult parsed = PresetSyncFormat::fromJson(text);
        QVERIFY(parsed.ok);
        QVERIFY(parsed.newerFormat());

        // Its timers still arrive here, but nothing is sent back, even with local changes.
        const Data local{{factoryDefault(), preset(QStringLiteral("mine"), QStringLiteral("Mine"), kNow)}, {}};
        const PresetSyncFormat::MergeResult merged = PresetSyncFormat::merge(local, parsed.data, kMe, kNow, parsed.format);
        QCOMPARE(names(merged.result.presets), (QStringList{QStringLiteral("Pomodoro"), QStringLiteral("Mine"), QStringLiteral("From the future")}));
        QVERIFY(merged.changedLocal);
        QVERIFY(!merged.changedRemote);
    }

    void unreadableJsonIsRefused()
    {
        QVERIFY(!PresetSyncFormat::fromJson("not json").ok);
        QVERIFY(!PresetSyncFormat::fromJson("[1, 2]").ok);
        QVERIFY(!PresetSyncFormat::fromJson("{\"presets\": []}").ok);
        QVERIFY(!PresetSyncFormat::fromJson("{\"format\": 0}").ok);
        QVERIFY(!PresetSyncFormat::fromJson("{\"format\": 1.5}").ok);
        QVERIFY(PresetSyncFormat::fromJson("{\"format\": 1}").ok);
    }

    // --- Merge ---

    void identicalSidesChangeNothing()
    {
        const Data local{{factoryDefault(), preset(QStringLiteral("a"), QStringLiteral("A"), kNow)}, {tombstone(QStringLiteral("x"), kNow)}};
        // What this computer uploaded, read back: its own device id as author.
        const Data remote = PresetSyncFormat::fromJson(PresetSyncFormat::toJson(local, kMe)).data;
        const PresetSyncFormat::MergeResult merged = PresetSyncFormat::merge(local, remote, kMe, kNow);
        QVERIFY(!merged.changedLocal);
        QVERIFY(!merged.changedRemote);
        QCOMPARE(merged.result.presets.at(1).updatedBy, QString());
    }

    void firstUploadOnlyChangesTheRemote()
    {
        const Data local{{factoryDefault(), preset(QStringLiteral("a"), QStringLiteral("A"), kNow)}, {}};
        const PresetSyncFormat::MergeResult merged = PresetSyncFormat::merge(local, Data{}, kMe, kNow);
        QVERIFY(!merged.changedLocal);
        QVERIFY(merged.changedRemote);
        QCOMPARE(names(merged.result.presets), (QStringList{QStringLiteral("Pomodoro"), QStringLiteral("A")}));
    }

    void newerEditWinsEitherWay()
    {
        const Data local{{factoryDefault(), preset(QStringLiteral("a"), QStringLiteral("Local"), kNow - 10)}, {}};
        const Data remote{{factoryDefault(), preset(QStringLiteral("a"), QStringLiteral("Remote"), kNow, kOther)}, {}};

        PresetSyncFormat::MergeResult merged = PresetSyncFormat::merge(local, remote, kMe, kNow);
        QCOMPARE(names(merged.result.presets), (QStringList{QStringLiteral("Pomodoro"), QStringLiteral("Remote")}));
        QCOMPARE(merged.result.presets.at(1).updatedBy, kOther);
        QVERIFY(merged.changedLocal);
        QVERIFY(!merged.changedRemote);

        merged = PresetSyncFormat::merge(remote, local, kOther, kNow); // the same seen from the other side
        QCOMPARE(names(merged.result.presets), (QStringList{QStringLiteral("Pomodoro"), QStringLiteral("Remote")}));
        QVERIFY(!merged.changedLocal);
        QVERIFY(merged.changedRemote);
    }

    void equalTimesGoToTheHigherDeviceId()
    {
        // "dev_other" > "dev_me": the other device's edit wins on both computers.
        const Data mine{{preset(QStringLiteral("a"), QStringLiteral("Mine"), kNow)}, {}};
        const Data theirs{{preset(QStringLiteral("a"), QStringLiteral("Theirs"), kNow)}, {}};
        Data theirsAsSeenHere = theirs;
        theirsAsSeenHere.presets[0].updatedBy = kOther;
        Data mineAsSeenThere = mine;
        mineAsSeenThere.presets[0].updatedBy = kMe;

        QCOMPARE(PresetSyncFormat::merge(mine, theirsAsSeenHere, kMe, kNow).result.presets.at(0).name, QStringLiteral("Theirs"));
        QCOMPARE(PresetSyncFormat::merge(theirs, mineAsSeenThere, kOther, kNow).result.presets.at(0).name, QStringLiteral("Theirs"));
    }

    void timesOutOfRangeAreBroughtIntoIt()
    {
        QJsonObject p{{QStringLiteral("uuid"), QStringLiteral("a")}, {QStringLiteral("name"), QStringLiteral("A")},
                      {QStringLiteral("updatedAt"), 1e300}, {QStringLiteral("createdAt"), -5.0}};
        const QJsonObject root{{QStringLiteral("format"), 1}, {QStringLiteral("presets"), QJsonArray{p}}, {QStringLiteral("tombstones"), QJsonArray{}}};
        const PresetSyncFormat::ParseResult parsed = PresetSyncFormat::fromJson(json(root));
        QVERIFY(parsed.ok);
        QCOMPARE(parsed.data.presets.at(0).updatedAt, qint64(9007199254740992LL));
        QCOMPARE(parsed.data.presets.at(0).createdAt, qint64(0));
    }

    void editAgainstDeletion()
    {
        const TimerPreset edited = preset(QStringLiteral("a"), QStringLiteral("Edited"), kNow);

        // Deleted after the edit: stays deleted.
        PresetSyncFormat::MergeResult merged = PresetSyncFormat::merge(
            Data{{factoryDefault(), edited}, {}}, Data{{factoryDefault()}, {tombstone(QStringLiteral("a"), kNow + 1, kOther)}}, kMe, kNow);
        QCOMPARE(names(merged.result.presets), QStringList{QStringLiteral("Pomodoro")});
        QCOMPARE(merged.result.tombstones, (QList<PresetTombstone>{tombstone(QStringLiteral("a"), kNow + 1, kOther)}));
        QVERIFY(merged.changedLocal);
        QVERIFY(!merged.changedRemote);

        // Edited after the deletion: comes back, its tombstone goes.
        merged = PresetSyncFormat::merge(Data{{factoryDefault()}, {tombstone(QStringLiteral("a"), kNow - 1)}},
                                         Data{{factoryDefault(), preset(QStringLiteral("a"), QStringLiteral("Edited"), kNow, kOther)}, {}}, kMe, kNow);
        QCOMPARE(names(merged.result.presets), (QStringList{QStringLiteral("Pomodoro"), QStringLiteral("Edited")}));
        QVERIFY(merged.result.tombstones.isEmpty());
        QVERIFY(merged.changedLocal);
        QVERIFY(!merged.changedRemote); // the remote has it like this already
    }

    void deletionAtTheSameTimeBeatsTheEdit()
    {
        const PresetSyncFormat::MergeResult merged = PresetSyncFormat::merge(
            Data{{preset(QStringLiteral("a"), QStringLiteral("A"), kNow, kOther)}, {}},
            Data{{}, {tombstone(QStringLiteral("a"), kNow, kOther)}}, kMe, kNow);
        QVERIFY(merged.result.presets.isEmpty());
        QCOMPARE(uuids(merged.result.tombstones), QStringList{QStringLiteral("a")});
    }

    void defaultIsNeverDeleted()
    {
        TimerPreset edited = factoryDefault();
        edited.name = QStringLiteral("My Pomodoro");
        edited.updatedAt = kNow - 100;
        const PresetSyncFormat::MergeResult merged = PresetSyncFormat::merge(
            Data{{edited}, {}}, Data{{}, {tombstone(PresetModel::DefaultUuid, kNow, kOther)}}, kMe, kNow);
        QCOMPARE(names(merged.result.presets), QStringList{QStringLiteral("My Pomodoro")});
        QVERIFY(merged.result.presets.at(0).builtin);
        QVERIFY(merged.result.tombstones.isEmpty());
        QVERIFY(!merged.changedLocal);
        QVERIFY(merged.changedRemote);
    }

    void defaultEditedElsewhereArrives()
    {
        TimerPreset edited = factoryDefault();
        edited.workSeconds = 30 * 60;
        edited.updatedAt = kNow;
        edited.updatedBy = kOther;
        const PresetSyncFormat::MergeResult merged = PresetSyncFormat::merge(Data{{factoryDefault()}, {}}, Data{{edited}, {}}, kMe, kNow);
        QCOMPARE(merged.result.presets.size(), 1);
        QCOMPARE(merged.result.presets.at(0).workSeconds, 30 * 60);
        QVERIFY(merged.changedLocal);
    }

    void localOrderIsKeptAndNewTimersFollowByCreation()
    {
        const Data local{{preset(QStringLiteral("c"), QStringLiteral("C"), 5), factoryDefault(), preset(QStringLiteral("a"), QStringLiteral("A"), 5)}, {}};
        const Data remote{{
            preset(QStringLiteral("late"), QStringLiteral("Late"), 9, kOther, 300),
            factoryDefault(),
            preset(QStringLiteral("a"), QStringLiteral("A"), 5),
            preset(QStringLiteral("early"), QStringLiteral("Early"), 9, kOther, 100),
            preset(QStringLiteral("c"), QStringLiteral("C"), 5),
        }, {}};
        const PresetSyncFormat::MergeResult merged = PresetSyncFormat::merge(local, remote, kMe, kNow);
        QCOMPARE(names(merged.result.presets),
                 (QStringList{QStringLiteral("C"), QStringLiteral("Pomodoro"), QStringLiteral("A"), QStringLiteral("Early"), QStringLiteral("Late")}));
        QVERIFY(merged.changedLocal);
        QVERIFY(!merged.changedRemote); // same timers; the remote's order does not matter
    }

    void oldTombstonesAreDropped()
    {
        const qint64 old = kNow - PresetSyncFormat::TombstoneLifetimeMs - 1;
        const PresetSyncFormat::MergeResult merged = PresetSyncFormat::merge(
            Data{{}, {tombstone(QStringLiteral("old"), old), tombstone(QStringLiteral("recent"), kNow - kDay)}},
            Data{{}, {tombstone(QStringLiteral("old2"), old, kOther), tombstone(QStringLiteral("recent"), kNow - kDay, kMe)}}, kMe, kNow);
        QCOMPARE(uuids(merged.result.tombstones), QStringList{QStringLiteral("recent")});
        // Expiry alone is no reason to write: the next real change leaves them out.
        QVERIFY(!merged.changedLocal);
        QVERIFY(!merged.changedRemote);
    }

    void removedTimerOfAnExpiredTombstoneComesBack()
    {
        // A timer last seen before its tombstone expired is a timer again: accepted by the plan.
        const qint64 old = kNow - PresetSyncFormat::TombstoneLifetimeMs - 1;
        const PresetSyncFormat::MergeResult merged = PresetSyncFormat::merge(
            Data{{}, {tombstone(QStringLiteral("a"), old)}}, Data{{preset(QStringLiteral("a"), QStringLiteral("A"), 1, kOther)}, {}}, kMe, kNow);
        QCOMPARE(names(merged.result.presets), QStringList{QStringLiteral("A")});
    }
};

QTEST_GUILESS_MAIN(PresetSyncFormatTest)
#include "tst_presetsyncformat.moc"
