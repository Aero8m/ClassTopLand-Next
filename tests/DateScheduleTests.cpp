#include "../src/Core/DateSchedule/DateSchedule.h"
#include "../src/Core/ProfileManager/ProfileManager.h"
#include "../src/Core/ProfileManager/ProfileExchange.h"
#include "../src/Core/CourseRefreshService/CourseRefreshService.h"
#include "../src/ProfileEditWindow/ProfileEditSession.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool value, const char* expression, int line)
{
    ++checks;
    if (!value) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(expression) check(bool(expression), #expression, __LINE__)
const QDate today = QDate::currentDate();

Profile sample()
{
    Profile profile;
    profile.name = QStringLiteral("测试");
    profile.subjects = {Subject(QStringLiteral("语文"), {}, {}), Subject(QStringLiteral("数学"), {}, {})};
    WeekSchedule week(QStringLiteral("每周"), WeekScheduleMode::All);
    week.initDaySchedules();
    for (auto& day : week.daySchedules)
        if (day.enableDay != 7)
            day.classes = {Class(QStringLiteral("语文"), QTime(8, 0), QTime(8, 45)),
                           Class(QStringLiteral("数学"), QTime(9, 0), QTime(9, 45)),
                           Class(QStringLiteral("语文"), QTime(10, 0), QTime(10, 45))};
    profile.activeWeekScheduleId = week.id;
    profile.schedules.append(week);
    return profile;
}

QByteArray native(const Profile& profile)
{
    QByteArray bytes;
    CHECK(ProfileExchange::serialize(profile, ProfileExchange::Format::NativeJson, bytes));
    return bytes;
}

void persistenceAndActions()
{
    auto& manager = ProfileManager::instance();
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/date-schedule-XXXXXX"));
    CHECK(dir.isValid());
    const auto path = dir.filePath(QStringLiteral("profile.json"));
    CHECK(manager.load(path));
    QString initialError;
    if (!manager.commit(sample(), &initialError)) throw std::runtime_error(initialError.toStdString());
    const auto week = manager.profile().schedules[0].id;
    const auto date = today.addDays(10);
    const auto base = native(manager.profile());
    QString error;
    CHECK(!manager.setDateReschedule(today.addDays(-1), week, 1, &error));
    CHECK(!error.isEmpty() && native(manager.profile()) == base);
    CHECK(!manager.setDateReschedule(date, QStringLiteral("missing"), 1));
    CHECK(manager.setDateReschedule(date, week, 1));
    CHECK(manager.profile().dateOverrides.size() == 1);
    const auto regular = manager.profile().schedules[0].daySchedules[0].classes;
    CHECK(manager.swapDateClasses(date, week, 0, 1));
    const auto* record = DateSchedule::find(manager.profile(), date);
    CHECK(record && record->classes[0].subject == QStringLiteral("数学"));
    CHECK(record->classes[0].startTime == QTime(8, 0));
    CHECK(DateSchedule::sameClasses(record->sourceClasses, regular));
    CHECK(DateSchedule::sameClasses(manager.profile().schedules[0].daySchedules[0].classes, regular));
    CHECK(DateSchedule::resolve(manager.profile(), date).special);
    CHECK(!DateSchedule::resolve(manager.profile(), date.addDays(1)).special);
    CHECK(!manager.swapDateClasses(date, week, 0, 0));
    CHECK(!manager.swapDateClasses(date, week, -1, 1));
    CHECK(!manager.swapDateClasses(date, week, 1, 2)); // identical subjects after the first swap
    CHECK(manager.swapDateClasses(date, week, 0, 2));
    const auto expected = native(manager.profile());
    CHECK(manager.load(path));
    CHECK(native(manager.profile()) == expected);
    CHECK(manager.setDateReschedule(date, week, 7));
    CHECK(DateSchedule::resolve(manager.profile(), date).classes.isEmpty());
    CHECK(manager.profile().dateOverrides.size() == 1);
    CHECK(manager.restoreDate(date));
    CHECK(!DateSchedule::find(manager.profile(), date));
    CHECK(manager.restoreDate(date));
    CHECK(manager.swapDateClasses(today.addDays(1 - today.dayOfWeek() + 7), week, 0, 1));
}

void codecAndInvalidation()
{
    auto& manager = ProfileManager::instance();
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/date-schedule-XXXXXX"));
    CHECK(manager.load(dir.filePath(QStringLiteral("profile.json"))));
    CHECK(manager.commit(sample()));
    const auto week = manager.profile().schedules[0].id;
    CHECK(manager.setDateReschedule(today, week, 1));
    CHECK(manager.setDateReschedule(today.addDays(1), week, 2));
    auto candidate = manager.profile();
    candidate.schedules[0].name = QStringLiteral("改名");
    std::reverse(candidate.schedules[0].daySchedules[0].classes.begin(), candidate.schedules[0].daySchedules[0].classes.end());
    candidate.schedules[0].daySchedules[2].classes.clear();
    QList<QDate> canceled;
    CHECK(manager.commit(candidate, nullptr, &canceled));
    CHECK(canceled.isEmpty() && manager.profile().dateOverrides.size() == 2);
    candidate = manager.profile();
    candidate.schedules[0].daySchedules[0].classes[0].subject = QStringLiteral("数学");
    CHECK(manager.commit(candidate, nullptr, &canceled));
    CHECK(canceled == QList<QDate>{today});
    CHECK(manager.profile().dateOverrides.size() == 1);
    candidate = manager.profile();
    candidate.schedules[0].daySchedules.removeAt(1);
    CHECK(manager.commit(candidate, nullptr, &canceled));
    CHECK(canceled == QList<QDate>{today.addDays(1)});

    CHECK(manager.setDateReschedule(today, week, 7));
    const auto bytes = native(manager.profile());
    Profile decoded;
    CHECK(ProfileExchange::decode(bytes, ProfileExchange::Format::NativeJson, decoded));
    CHECK(decoded.dateOverrides.size() == 1 && decoded.dateOverrides[0].classes.isEmpty());
    auto root = QJsonDocument::fromJson(bytes).object();
    auto data = root.value(QStringLiteral("profile")).toObject();
    const auto records = data.value(QStringLiteral("dateOverrides")).toArray();
    data.remove(QStringLiteral("dateOverrides")); root.insert(QStringLiteral("profile"), data);
    CHECK(ProfileExchange::decode(QJsonDocument(root).toJson(), ProfileExchange::Format::NativeJson, decoded));
    CHECK(decoded.dateOverrides.isEmpty());
    auto duplicate = records; duplicate.append(records[0]);
    data.insert(QStringLiteral("dateOverrides"), duplicate); root.insert(QStringLiteral("profile"), data);
    CHECK(!ProfileExchange::decode(QJsonDocument(root).toJson(), ProfileExchange::Format::NativeJson, decoded));
    CHECK(decoded.dateOverrides.isEmpty());
    auto invalid = records[0].toObject(); invalid.insert(QStringLiteral("date"), QStringLiteral("2026-02-30"));
    data.insert(QStringLiteral("dateOverrides"), QJsonArray{invalid}); root.insert(QStringLiteral("profile"), data);
    CHECK(!ProfileExchange::decode(QJsonDocument(root).toJson(), ProfileExchange::Format::NativeJson, decoded));
    invalid = records[0].toObject(); invalid.insert(QStringLiteral("sourceWeekday"), 8);
    data.insert(QStringLiteral("dateOverrides"), QJsonArray{invalid}); root.insert(QStringLiteral("profile"), data);
    CHECK(!ProfileExchange::decode(QJsonDocument(root).toJson(), ProfileExchange::Format::NativeJson, decoded));
    QStringList warnings;
    QByteArray output;
    CHECK(ProfileExchange::serialize(manager.profile(), ProfileExchange::Format::CsesYaml, output, &warnings));
    CHECK(warnings.join('\n').contains(QStringLiteral("调休和换课")));
    CHECK(ProfileExchange::serialize(manager.profile(), ProfileExchange::Format::ClassTopLandTablesJson, output, &warnings));
    CHECK(warnings.join('\n').contains(QStringLiteral("调休和换课")));
    candidate = manager.profile(); candidate.schedules.clear(); candidate.activeWeekScheduleId.clear();
    CHECK(manager.commit(candidate, nullptr, &canceled));
    CHECK(canceled == QList<QDate>{today} && manager.profile().dateOverrides.isEmpty());
}

void cleanupFailureAndDraft()
{
    auto& manager = ProfileManager::instance();
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/date-schedule-XXXXXX"));
    const auto path = dir.filePath(QStringLiteral("profile.json"));
    CHECK(manager.load(path)); CHECK(manager.commit(sample()));
    const auto week = manager.profile().schedules[0].id;
    for (int i = 0; i < 3; ++i) CHECK(manager.setDateReschedule(today.addDays(i), week, 1));
    ProfileEditSession session;
    session.reset(manager.profile()); session.edit().name = QStringLiteral("未保存修改"); session.notifyChanged();
    CHECK(session.isDirty());
    CHECK(manager.cleanPastDateOverrides(today.addDays(1)));
    CHECK(manager.profile().dateOverrides.size() == 2);
    CHECK(!DateSchedule::find(manager.profile(), today));
    CHECK(DateSchedule::find(manager.profile(), today.addDays(1)) && DateSchedule::find(manager.profile(), today.addDays(2)));
    session.syncDateOverrides(manager.profile().dateOverrides);
    CHECK(session.isDirty() && session.draft().name == QStringLiteral("未保存修改"));
    CHECK(session.draft().dateOverrides.size() == 2);
    CHECK(manager.commit(session.draft()));
    CHECK(!DateSchedule::find(manager.profile(), today));
    const auto before = native(manager.profile());
    // Lock the destination: QSaveFile's final replacement must fail atomically.
    QFile locked(path); CHECK(locked.open(QIODevice::ReadOnly));
    QString error;
    CHECK(!manager.cleanPastDateOverrides(today.addDays(2), &error));
    CHECK(!error.isEmpty() && native(manager.profile()) == before);
    auto candidate = manager.profile(); candidate.schedules[0].daySchedules[0].classes.clear();
    QList<QDate> canceled{today};
    CHECK(!manager.commit(candidate, &error, &canceled));
    CHECK(canceled.isEmpty() && native(manager.profile()) == before);
    CHECK(!manager.swapDateClasses(today.addDays(1), week, 0, 1, &error));
    CHECK(native(manager.profile()) == before);
    locked.close();
    CHECK(manager.cleanPastDateOverrides(today.addDays(2)));
    CHECK(manager.profile().dateOverrides.size() == 1);
    // No-op cleanup must succeed even when the file is locked.
    CHECK(locked.open(QIODevice::ReadOnly));
    CHECK(manager.cleanPastDateOverrides(today.addDays(2)));
    locked.close();
    CHECK(manager.load(path)); CHECK(manager.profile().dateOverrides.size() == 1);
    session.syncDateOverrides(manager.profile().dateOverrides);
    session.discard(); CHECK(!session.isDirty() && session.draft().dateOverrides.size() == 1);
}

void refreshAndMatching()
{
    auto& manager = ProfileManager::instance();
    QTemporaryDir dir(QDir::currentPath() + QStringLiteral("/date-schedule-XXXXXX"));
    CHECK(manager.load(dir.filePath(QStringLiteral("profile.json")))); CHECK(manager.commit(sample()));
    const auto week = manager.profile().schedules[0].id;
    CHECK(manager.setDateReschedule(today, week, 1));
    CHECK(manager.swapDateClasses(today, week, 0, 1));
    QDateTime now(today, QTime(8, 10));
    CourseRefreshService service([&now] { return now; });
    service.reloadTable();
    CHECK(service.table().classes[0].subject == QStringLiteral("数学"));
    CHECK(service.state().phase == CourseRefreshService::Phase::InClass);
    service.setTemporaryWeekday(7); CHECK(service.table().classes.isEmpty());
    service.clearTemporarySelection(); CHECK(service.table().classes[0].subject == QStringLiteral("数学"));
    service.setTemporarySchedule(DaySchedule(QStringLiteral("临时"), 1)); CHECK(service.table().classes.isEmpty());
    now = now.addDays(1); service.refresh();
    CHECK(service.table().date == today.addDays(1));
    CHECK(service.table().name == QStringLiteral("每周"));
    auto profile = sample(); profile.activeWeekScheduleId.clear();
    profile.schedules[0].mode = WeekScheduleMode::Odd;
    CHECK(!DateSchedule::resolve(profile, today).valid);
    CHECK(DateSchedule::resolve(profile, today, profile.schedules[0].id).valid);
    profile.schedules[0].mode = WeekScheduleMode::All;
    auto second = profile.schedules[0]; second.id = QStringLiteral("another"); profile.schedules.append(second);
    CHECK(!DateSchedule::resolve(profile, today.addDays(1 - today.dayOfWeek())).valid);
    CHECK(DateSchedule::resolve(profile, today, second.id).valid);
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try {
        persistenceAndActions(); codecAndInvalidation(); cleanupFailureAndDraft(); refreshAndMatching();
        std::cout << "Passed " << checks << " date schedule checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
