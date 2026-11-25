#include "Core/ProfileManager/ProfileExchange.h"
#include "test_profile.h"

#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using ProfileExchange::Format;

class ProfileExchangeTest : public QObject
{
    Q_OBJECT

private slots:
    void nativeJsonRoundTrip();
    void legacyNativeJsonWithoutOptionalFields();
    void tablesJsonImportAndExport();
    void csesYamlImportAndExport();
    void failedImportPreservesOutput_data();
    void failedImportPreservesOutput();
};

void ProfileExchangeTest::nativeJsonRoundTrip()
{
    Profile original = TestProfiles::regular();
    original.semesterStartDate = QDate(2026, 9, 2);
    original.activeWeekScheduleId = TestProfiles::weekId;
    TimeLine line(QStringLiteral("上午"));
    line.timePoints = {{QTime(8, 0), QTime(8, 45)}};
    original.timeLines.append(line);
    const auto classes = original.schedules.first().daySchedules.first().classes;
    original.dateOverrides.append({QDate(2026, 9, 7), TestProfiles::weekId, 1, classes, {}});
    ReminderRule rule;
    rule.id = QStringLiteral("morning");
    rule.name = QStringLiteral("晨间提醒");
    rule.text = QStringLiteral("请到教室");
    original.reminderRules.append(rule);

    QByteArray contents;
    QString error;
    QVERIFY2(ProfileExchange::serialize(original, Format::NativeJson, contents, nullptr, &error), qPrintable(error));

    QTemporaryDir directory;
    QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
    const QString path = directory.filePath(QStringLiteral("backup.json"));
    QVERIFY2(ProfileExchange::writeFile(path, contents, &error), qPrintable(error));
    Profile restored;
    Format detected = Format::CsesYaml;
    QVERIFY2(ProfileExchange::readImportFile(path, restored, detected, nullptr, &error), qPrintable(error));
    QCOMPARE(detected, Format::NativeJson);
    QCOMPARE(restored.name, original.name);
    QCOMPARE(restored.semesterStartDate, original.semesterStartDate);
    QCOMPARE(restored.activeWeekScheduleId, TestProfiles::weekId);
    QCOMPARE(restored.timeLines.size(), 1);
    QCOMPARE(restored.dateOverrides.size(), 1);
    QCOMPARE(restored.dateOverrides.first().sourceClasses.size(), 2);
    QVERIFY(restored.dateOverrides.first().classes.isEmpty());
    QCOMPARE(restored.reminderRules.size(), 1);
    QCOMPARE(restored.reminderRules.first().id, rule.id);

    QByteArray restoredContents;
    QVERIFY2(ProfileExchange::serialize(restored, Format::NativeJson, restoredContents, nullptr, &error), qPrintable(error));
    QCOMPARE(restoredContents, contents);
}

void ProfileExchangeTest::legacyNativeJsonWithoutOptionalFields()
{
    const QByteArray contents = R"json({
        "version": 1,
        "profile": {
            "name": "Legacy",
            "subjects": [],
            "timeLines": [],
            "schedules": [{"name": "Old week", "mode": "all", "daySchedules": []}]
        }
    })json";
    Profile profile;
    QString error;
    QVERIFY2(ProfileExchange::decode(contents, Format::NativeJson, profile, nullptr, &error), qPrintable(error));
    QCOMPARE(profile.name, QStringLiteral("Legacy"));
    QCOMPARE(profile.schedules.size(), 1);
    QVERIFY(!profile.schedules.first().id.isEmpty());
    QVERIFY(profile.activeWeekScheduleId.isEmpty());
    QVERIFY(!profile.semesterStartDate.isValid());
    QVERIFY(profile.dateOverrides.isEmpty());
    QVERIFY(profile.reminderRules.isEmpty());
}

void ProfileExchangeTest::tablesJsonImportAndExport()
{
    const auto contents = QStringLiteral(R"json({
        "Mon": [
            {"name": "语文", "start": "08:55", "end": "09:40"},
            {"name": "数学", "start": "08:00", "end": "08:45"}
        ],
        "appendixTables": {"Empty": []}
    })json").toUtf8();
    QTemporaryDir directory;
    QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
    const QString path = directory.filePath(QStringLiteral("renamed.json"));
    QString error;
    QVERIFY2(ProfileExchange::writeFile(path, contents, &error), qPrintable(error));

    Profile imported;
    Format detected = Format::NativeJson;
    QStringList warnings;
    QVERIFY2(ProfileExchange::readImportFile(path, imported, detected, &warnings, &error), qPrintable(error));
    QCOMPARE(detected, Format::ClassTopLandTablesJson);
    QCOMPARE(imported.name, QStringLiteral("renamed"));
    QCOMPARE(imported.subjects.size(), 2);
    QCOMPARE(imported.schedules.size(), 1);
    const auto& week = imported.schedules.first();
    QCOMPARE(week.mode, WeekScheduleMode::All);
    QCOMPARE(week.daySchedules.size(), 7);
    QVERIFY(week.daySchedules.at(1).classes.isEmpty());
    const auto& monday = week.daySchedules.first().classes;
    QCOMPARE(monday.size(), 2);
    QCOMPARE(monday.first().subject, TestProfiles::math);
    QCOMPARE(monday.first().startTime, QTime(8, 0));
    QCOMPARE(monday.last().subject, TestProfiles::chinese);
    QVERIFY(!warnings.isEmpty());

    imported.schedules[0].getDaySchedule(0).classes[0].startTime = QTime(8, 0, 15);
    QByteArray exported;
    QVERIFY2(ProfileExchange::serialize(imported, detected, exported, &warnings, &error), qPrintable(error));
    const auto json = QJsonDocument::fromJson(exported).object();
    QCOMPARE(json.size(), 8);
    const auto classes = json.value(QStringLiteral("Mon")).toArray();
    QCOMPARE(classes.size(), 2);
    const auto first = classes.first().toObject();
    QCOMPARE(first.value(QStringLiteral("name")).toString(), TestProfiles::math);
    QCOMPARE(first.value(QStringLiteral("start")).toString(), QStringLiteral("08:00"));
    QCOMPARE(first.value(QStringLiteral("end")).toString(), QStringLiteral("08:45"));
    QVERIFY(json.value(QStringLiteral("appendixTables")).isObject());
    QVERIFY(json.value(QStringLiteral("appendixTables")).toObject().isEmpty());
    QCOMPARE(warnings.size(), 2);
}

void ProfileExchangeTest::csesYamlImportAndExport()
{
    const auto contents = QStringLiteral(R"yaml(version: 1
subjects:
  - name: 数学
    simplified_name: 数
    teacher: 王老师
schedules:
  - name: Odd Monday
    enable_day: 1
    weeks: odd
    classes:
      - subject: 数学
        start_time: "08:00:15"
        end_time: "08:45:00"
        room: A101
)yaml").toUtf8();
    QTemporaryDir directory;
    QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
    const QString path = directory.filePath(QStringLiteral("week.yml"));
    QString error;
    QVERIFY2(ProfileExchange::writeFile(path, contents, &error), qPrintable(error));
    Profile imported;
    Format detected = Format::NativeJson;
    QVERIFY2(ProfileExchange::readImportFile(path, imported, detected, nullptr, &error), qPrintable(error));
    QCOMPARE(detected, Format::CsesYaml);
    QCOMPARE(imported.name, QStringLiteral("week"));
    QCOMPARE(imported.subjects.size(), 1);
    QCOMPARE(imported.subjects.first().name, TestProfiles::math);
    QCOMPARE(imported.subjects.first().simplifiedName, QStringLiteral("数"));
    QCOMPARE(imported.subjects.first().teacher, QStringLiteral("王老师"));
    QCOMPARE(imported.schedules.size(), 1);
    QCOMPARE(imported.schedules.first().mode, WeekScheduleMode::Odd);

    QByteArray exported;
    QVERIFY2(ProfileExchange::serialize(imported, detected, exported, nullptr, &error), qPrintable(error));
    Profile restored;
    QVERIFY2(ProfileExchange::decode(exported, detected, restored, nullptr, &error), qPrintable(error));
    QCOMPARE(restored.subjects.size(), 1);
    QCOMPARE(restored.subjects.first().name, TestProfiles::math);
    QCOMPARE(restored.subjects.first().simplifiedName, QStringLiteral("数"));
    QCOMPARE(restored.subjects.first().teacher, QStringLiteral("王老师"));
    QCOMPARE(restored.schedules.size(), 1);
    const auto& week = restored.schedules.first();
    QCOMPARE(week.mode, WeekScheduleMode::Odd);
    QCOMPARE(week.daySchedules.size(), 1);
    const auto& day = week.daySchedules.first();
    QCOMPARE(day.name, QStringLiteral("Odd Monday"));
    QCOMPARE(day.enableDay, 1);
    QCOMPARE(day.classes.size(), 1);
    QCOMPARE(day.classes.first().subject, TestProfiles::math);
    QCOMPARE(day.classes.first().startTime, QTime(8, 0, 15));
    QCOMPARE(day.classes.first().endTime, QTime(8, 45));
}

void ProfileExchangeTest::failedImportPreservesOutput_data()
{
    QTest::addColumn<QByteArray>("contents");
    QTest::newRow("invalid-time") << QByteArray(R"json({"Mon":[{"name":"A","start":"25:00","end":"08:45"}]})json");
    QTest::newRow("overlap") << QByteArray(R"json({"Mon":[{"name":"A","start":"08:00","end":"08:45"},
        {"name":"B","start":"08:30","end":"09:00"}]})json");
    QTest::newRow("corrupt-native-marker") << QByteArray(R"json({"version":1,"profile":false,"Mon":[]})json");
}

void ProfileExchangeTest::failedImportPreservesOutput()
{
    QFETCH(QByteArray, contents);
    QTemporaryDir directory;
    QVERIFY2(directory.isValid(), qPrintable(directory.errorString()));
    const QString path = directory.filePath(QStringLiteral("invalid.json"));
    QString error;
    QVERIFY2(ProfileExchange::writeFile(path, contents, &error), qPrintable(error));
    Profile profile = TestProfiles::regular();
    QByteArray before;
    QVERIFY2(ProfileExchange::serialize(profile, Format::NativeJson, before, nullptr, &error), qPrintable(error));
    Format detected = Format::CsesYaml;

    QVERIFY(!ProfileExchange::readImportFile(path, profile, detected, nullptr, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(detected, Format::CsesYaml);
    QByteArray after;
    QVERIFY2(ProfileExchange::serialize(profile, Format::NativeJson, after, nullptr, &error), qPrintable(error));
    QCOMPARE(after, before);
}

QTEST_GUILESS_MAIN(ProfileExchangeTest)
#include "profile_exchange.moc"
