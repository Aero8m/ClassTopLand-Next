#include "../src/Core/ProfileManager/ProfileExchange.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using ProfileExchange::Format;
constexpr auto Tables = Format::ClassTopLandTablesJson;
int checks = 0;
void check(bool condition, const char* expression, int line)
{
    ++checks;
    if (!condition) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(expression) check(bool(expression), #expression, __LINE__)

QJsonObject course(const QString& name = QStringLiteral("语文"), const QString& start = QStringLiteral("08:00"),
                   const QString& end = QStringLiteral("08:45"))
{
    return {{QStringLiteral("name"), name}, {QStringLiteral("start"), start}, {QStringLiteral("end"), end}};
}
QByteArray bytes(const QJsonObject& root) { return QJsonDocument(root).toJson(); }
QByteArray native(const Profile& profile)
{
    QByteArray result;
    CHECK(ProfileExchange::serialize(profile, Format::NativeJson, result));
    return result;
}
Profile sample()
{
    Profile result;
    CHECK(ProfileExchange::decode(bytes({{QStringLiteral("Mon"), QJsonArray{course()}}}), Tables, result));
    result.name = QStringLiteral("测试档案");
    return result;
}
QJsonObject exported(const Profile& profile, const QString& id = {})
{
    QByteArray result;
    CHECK(ProfileExchange::serialize(profile, Tables, result, nullptr, nullptr, id));
    return QJsonDocument::fromJson(result).object();
}
QByteArray readBytes(const QString& path)
{
    QFile file(path);
    CHECK(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

void importCases()
{
    Profile result;
    QStringList warnings;
    QString error;
    const QJsonObject root{{QStringLiteral("Mon"), QJsonArray{course(QStringLiteral("数学"), QStringLiteral("09:00"), QStringLiteral("09:45")), course()}},
                          {QStringLiteral("Tue"), QJsonArray{course()}},
                          {QStringLiteral("appendixTables"), QJsonObject{}}};
    CHECK(ProfileExchange::decode(bytes(root), Tables, result, &warnings, &error));
    CHECK(error.isEmpty() && warnings.isEmpty());
    CHECK(result.schedules.size() == 1 && result.schedules[0].daySchedules.size() == 7);
    CHECK(result.schedules[0].name == QStringLiteral("常规课表"));
    CHECK(result.schedules[0].mode == WeekScheduleMode::All && !result.schedules[0].id.isEmpty());
    CHECK(result.activeWeekScheduleId.isEmpty() && result.timeLines.isEmpty());
    CHECK(result.subjects.size() == 2);
    CHECK(result.subjects[0].teacher.isEmpty() && result.subjects[0].simplifiedName == QStringLiteral("数"));
    const auto& days = result.schedules[0].daySchedules;
    CHECK(days[0].classes[0].subject == QStringLiteral("语文"));
    CHECK(days[0].classes[0].startTime == QTime(8, 0) && days[0].classes[1].startTime == QTime(9, 0));
    for (int i = 0; i < 7; ++i) CHECK(days[i].enableDay == i + 1 && !days[i].name.isEmpty());
    for (int i = 2; i < 7; ++i) CHECK(days[i].classes.isEmpty());

    const auto full = exported(result);
    CHECK(full.size() == 8 && full.value(QStringLiteral("appendixTables")).toObject().isEmpty());
    Profile roundTrip;
    CHECK(ProfileExchange::decode(bytes(full), Tables, roundTrip));
    CHECK(exported(roundTrip) == full);

    CHECK(ProfileExchange::decode(bytes({{QStringLiteral("Sun"), QJsonArray{}}}), Tables, result));
    CHECK(result.subjects.isEmpty() && result.schedules[0].daySchedules[6].classes.isEmpty());
    CHECK(ProfileExchange::decode(bytes({{QStringLiteral("Mon"), QJsonArray{}},
        {QStringLiteral("appendixTables"), QJsonObject{{QStringLiteral("考试"), QJsonArray{}}}}}), Tables, result, &warnings));
    CHECK(warnings.size() == 1 && warnings[0].contains(QStringLiteral("考试")));
    CHECK(ProfileExchange::decode(bytes({{QStringLiteral("Mon"), QJsonArray{course(QStringLiteral(" 语文 "))}}}), Tables, result));
    CHECK(result.subjects[0].name == QStringLiteral("语文"));
}

void importFailures()
{
    struct Failure { QByteArray data; QString location; };
    const QList<Failure> failures = {
        {"{", QStringLiteral("JSON")}, {"[]", QStringLiteral("JSON")},
        {"{}", QStringLiteral("星期")},
        {bytes({{QStringLiteral("Mon"), 3}}), QStringLiteral("Mon")},
        {bytes({{QStringLiteral("Mon"), QJsonArray{42}}}), QStringLiteral("Mon[0]")},
        {bytes({{QStringLiteral("Mon"), QJsonArray{QJsonObject{{QStringLiteral("start"), QStringLiteral("08:00")}}}}}), QStringLiteral("Mon[0].name")},
        {bytes({{QStringLiteral("Mon"), QJsonArray{course(QStringLiteral(" "))}}}), QStringLiteral("Mon[0].name")},
        {bytes({{QStringLiteral("Mon"), QJsonArray{course(QStringLiteral("语文"), QStringLiteral("8:00"))}}}), QStringLiteral("Mon[0].start")},
        {bytes({{QStringLiteral("Mon"), QJsonArray{course(QStringLiteral("语文"), QStringLiteral("24:00"))}}}), QStringLiteral("Mon[0].start")},
        {bytes({{QStringLiteral("Mon"), QJsonArray{course(QStringLiteral("语文"), QStringLiteral("08:00:01"))}}}), QStringLiteral("Mon[0].start")},
        {bytes({{QStringLiteral("Mon"), QJsonArray{course(QStringLiteral("语文"), QStringLiteral("08:00"), QStringLiteral("08:60"))}}}), QStringLiteral("Mon[0].end")},
        {bytes({{QStringLiteral("Mon"), QJsonArray{course(QStringLiteral("语文"), QStringLiteral("08:00"), QStringLiteral("08:00"))}}}), QStringLiteral("Mon[0]")},
        {bytes({{QStringLiteral("Mon"), QJsonArray{course(QStringLiteral("语文"), QStringLiteral("23:00"), QStringLiteral("01:00"))}}}), QStringLiteral("Mon[0]")},
        {bytes({{QStringLiteral("Mon"), QJsonArray{course(QStringLiteral("数学"), QStringLiteral("08:30"), QStringLiteral("09:00")), course()}}}), QStringLiteral("Mon[0]")},
        {bytes({{QStringLiteral("Mon"), QJsonArray{}}, {QStringLiteral("appendixTables"), QJsonArray{}}}), QStringLiteral("appendixTables")},
        {bytes({{QStringLiteral("Mon"), QJsonArray{}}, {QStringLiteral("appendixTables"), QJsonObject{{QStringLiteral("考试"), 3}}}}), QStringLiteral("考试")},
        {bytes({{QStringLiteral("Mon"), QJsonArray{}}, {QStringLiteral("appendixTables"), QJsonObject{{QStringLiteral("考试"), QJsonArray{course()}}}}}), QStringLiteral("考试")}
    };
    Profile sentinel = sample();
    const auto before = native(sentinel);
    for (const auto& failure : failures) {
        QString error;
        QStringList warnings{QStringLiteral("stale")};
        CHECK(!ProfileExchange::decode(failure.data, Tables, sentinel, &warnings, &error));
        CHECK(error.contains(failure.location));
        CHECK(native(sentinel) == before && warnings.isEmpty());
    }
    QJsonObject wrongType = course();
    wrongType.insert(QStringLiteral("end"), 845);
    QString error;
    CHECK(!ProfileExchange::decode(bytes({{QStringLiteral("Mon"), QJsonArray{wrongType}}}), Tables, sentinel, nullptr, &error));
    CHECK(error.contains(QStringLiteral("Mon[0].end")));
    QByteArray invalidUtf8 = "{\"Mon\":[{\"name\":\"";
    invalidUtf8 += char(0xff);
    invalidUtf8 += "\",\"start\":\"08:00\",\"end\":\"08:45\"}]}";
    CHECK(!ProfileExchange::decode(invalidUtf8, Tables, sentinel, nullptr, &error));
    CHECK(error.contains(QStringLiteral("UTF-8")) && native(sentinel) == before);
}

void detectionCases()
{
    QTemporaryDir directory(QDir::current().filePath(QStringLiteral("exchange-tests-XXXXXX")));
    CHECK(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("自定义名字.JSON"));
    Profile result;
    auto detected = Format::CsesYaml;
    QString error;
    if (!ProfileExchange::writeFile(path, bytes({{QStringLiteral("Mon"), QJsonArray{course()}}}), &error))
        throw std::runtime_error(error.toStdString());
    CHECK(ProfileExchange::readImportFile(path, result, detected));
    CHECK(detected == Tables && result.name == QStringLiteral("自定义名字"));
    const auto before = native(result);
    CHECK(ProfileExchange::writeFile(path, before));
    CHECK(ProfileExchange::readImportFile(path, result, detected));
    CHECK(detected == Format::NativeJson && native(result) == before);
    const QList<QByteArray> malformed = {
        bytes({{QStringLiteral("version"), 999}, {QStringLiteral("Mon"), QJsonArray{course()}}}),
        bytes({{QStringLiteral("profile"), QJsonObject{}}, {QStringLiteral("Mon"), QJsonArray{course()}}}),
        bytes({{QStringLiteral("unrelated"), QJsonArray{}}}), "[]", "{"
    };
    for (const auto& data : malformed) {
        CHECK(ProfileExchange::writeFile(path, data));
        CHECK(!ProfileExchange::readImportFile(path, result, detected, nullptr, &error));
        CHECK(detected == Format::NativeJson && native(result) == before && !error.isEmpty());
        CHECK(readBytes(path) == data);
    }
    CHECK(!ProfileExchange::readImportFile(directory.filePath(QStringLiteral("missing.json")), result, detected));
    CHECK(!ProfileExchange::readImportFile(directory.filePath(QStringLiteral("wrong.txt")), result, detected));
    CHECK(detected == Format::NativeJson && native(result) == before);

    // Native disk reads remain permissive; import rejects overlapping transfers.
    Profile overlapping = sample();
    overlapping.schedules[0].daySchedules[0].classes.append(Class(QStringLiteral("语文"), QTime(8, 30), QTime(9, 0)));
    CHECK(ProfileExchange::writeFile(path, native(overlapping)));
    Profile disk;
    CHECK(ProfileExchange::readFile(path, Format::NativeJson, disk));
    CHECK(!ProfileExchange::readImportFile(path, result, detected));
    CHECK(native(result) == before);
}

void exportCases()
{
    Profile profile = sample();
    auto& first = profile.schedules[0];
    first.daySchedules[0].classes.append(Class(QStringLiteral("数学"), QTime(9, 0), QTime(9, 45)));
    std::swap(first.daySchedules[0].classes[0], first.daySchedules[0].classes[1]);
    auto object = exported(profile);
    CHECK(object[QStringLiteral("Mon")].toArray()[0].toObject() == course());
    CHECK(object[QStringLiteral("Mon")].toArray()[0].toObject().size() == 3);
    CHECK(object[QStringLiteral("Sun")].toArray().isEmpty());

    WeekSchedule second(first.name, WeekScheduleMode::Odd);
    second.daySchedules.append(DaySchedule(QStringLiteral("特别安排"), 2));
    second.daySchedules[0].classes.append(Class(QStringLiteral("英语"), QTime(10, 0, 59), QTime(10, 45, 30)));
    profile.schedules.append(second);
    profile.activeWeekScheduleId = second.id;
    const auto original = native(profile);
    QByteArray output = "sentinel";
    QStringList warnings;
    QString error;
    CHECK(!ProfileExchange::serialize(profile, Tables, output, &warnings, &error));
    CHECK(error.contains(QStringLiteral("多个")) && output == "sentinel" && warnings.isEmpty());
    CHECK(!ProfileExchange::serialize(profile, Tables, output, &warnings, &error, QStringLiteral("missing")));
    CHECK(error.contains(QStringLiteral("ID")) && output == "sentinel");
    CHECK(ProfileExchange::serialize(profile, Tables, output, &warnings, &error, second.id));
    object = QJsonDocument::fromJson(output).object();
    CHECK(object[QStringLiteral("Mon")].toArray().isEmpty());
    CHECK(object[QStringLiteral("Tue")].toArray()[0].toObject() == course(QStringLiteral("英语"), QStringLiteral("10:00"), QStringLiteral("10:45")));
    CHECK(warnings.join('\n').contains(QStringLiteral("单双周")));
    CHECK(warnings.join('\n').contains(QStringLiteral("英语")) && warnings.join('\n').contains(QStringLiteral("截去秒")));
    CHECK(native(profile) == original);
    CHECK(!exported(profile, profile.schedules[0].id)[QStringLiteral("Mon")].toArray().isEmpty());

    profile.schedules[1].daySchedules[0].classes[0].endTime = QTime(10, 0, 59, 500);
    output = "sentinel";
    CHECK(!ProfileExchange::serialize(profile, Tables, output, &warnings, &error, second.id));
    CHECK(error.contains(QStringLiteral("英语")) && error.contains(QStringLiteral("同一分钟")));
    CHECK(output == "sentinel" && warnings.isEmpty());

    Profile empty;
    empty.name = QStringLiteral("空档案");
    CHECK(!ProfileExchange::serialize(empty, Tables, output, &warnings, &error));
    CHECK(error.contains(QStringLiteral("没有")) && output == "sentinel");
    empty.schedules.append(WeekSchedule(QStringLiteral("空课表"), WeekScheduleMode::All));
    object = exported(empty);
    CHECK(object.size() == 8);
    for (auto it = object.begin(); it != object.end(); ++it)
        CHECK(it.key() == QStringLiteral("appendixTables") ? it.value().toObject().isEmpty() : it.value().isArray() && it.value().toArray().isEmpty());
}

void persistenceAndRegression()
{
    Profile profile = sample();
    profile.subjects[0].simplifiedName = QStringLiteral("语");
    profile.subjects[0].teacher = QStringLiteral("张老师");
    profile.timeLines.append(TimeLine(QStringLiteral("上午")));
    profile.timeLines[0].timePoints.append(TimeLinePoint(QTime(8, 0), QTime(8, 45)));
    profile.activeWeekScheduleId = profile.schedules[0].id;
    const auto original = native(profile);
    Profile result;
    CHECK(ProfileExchange::decode(original, Format::NativeJson, result));
    CHECK(native(result) == original);
    auto legacyRoot = QJsonDocument::fromJson(original).object();
    auto legacyProfile = legacyRoot[QStringLiteral("profile")].toObject();
    auto schedules = legacyProfile[QStringLiteral("schedules")].toArray();
    auto week = schedules[0].toObject();
    week.remove(QStringLiteral("id"));
    schedules[0] = week;
    legacyProfile[QStringLiteral("schedules")] = schedules;
    legacyRoot[QStringLiteral("profile")] = legacyProfile;
    CHECK(ProfileExchange::decode(bytes(legacyRoot), Format::NativeJson, result));
    CHECK(!result.schedules[0].id.isEmpty() && result.activeWeekScheduleId.isEmpty());

    QByteArray yaml;
    CHECK(ProfileExchange::serialize(profile, Format::CsesYaml, yaml));
    CHECK(ProfileExchange::decode(yaml, Format::CsesYaml, result));
    CHECK(result.subjects[0].teacher == QStringLiteral("张老师"));
    CHECK(exported(result) == exported(profile));
    const auto sentinel = native(result);
    CHECK(!ProfileExchange::decode("version: 999\nsubjects: []\nschedules: []", Format::CsesYaml, result));
    CHECK(native(result) == sentinel);

    QTemporaryDir directory(QDir::current().filePath(QStringLiteral("exchange-tests-XXXXXX")));
    CHECK(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("tables.json"));
    CHECK(ProfileExchange::writeFile(path, "existing file"));
    profile.schedules[0].daySchedules[0].classes[0].endTime = QTime(8, 0, 30);
    QByteArray output = "sentinel";
    const bool written = ProfileExchange::serialize(profile, Tables, output) && ProfileExchange::writeFile(path, output);
    CHECK(!written && output == "sentinel" && readBytes(path) == "existing file");
    CHECK(ProfileExchange::writeFile(path, original));
    CHECK(readBytes(path) == original);
    CHECK(!ProfileExchange::writeFile(directory.path(), "cannot replace directory"));
    CHECK(readBytes(path) == original);
    const QString yamlPath = directory.filePath(QStringLiteral("课表.yml"));
    CHECK(ProfileExchange::writeFile(yamlPath, yaml));
    auto detected = Tables;
    CHECK(ProfileExchange::readImportFile(yamlPath, result, detected));
    CHECK(detected == Format::CsesYaml && result.name == QStringLiteral("课表"));
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        importCases();
        importFailures();
        detectionCases();
        exportCases();
        persistenceAndRegression();
        std::cout << "Profile exchange: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Profile exchange failed: " << error.what() << '\n';
        return 1;
    }
}
