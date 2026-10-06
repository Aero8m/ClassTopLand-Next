#include "DateSchedule.h"
#include <QSet>
#include <algorithm>

namespace DateSchedule {
QList<Class> sorted(QList<Class> classes)
{
    std::stable_sort(classes.begin(), classes.end(), [](const Class& a, const Class& b) {
        return a.startTime < b.startTime;
    });
    return classes;
}

bool sameClasses(const QList<Class>& left, const QList<Class>& right)
{
    if (left.size() != right.size()) return false;
    const auto a = sorted(left), b = sorted(right);
    for (qsizetype i = 0; i < a.size(); ++i)
        if (a[i].subject != b[i].subject || a[i].startTime != b[i].startTime || a[i].endTime != b[i].endTime)
            return false;
    return true;
}

bool validateClasses(const QList<Class>& classes, QString* error)
{
    if (error) error->clear();
    auto fail = [error](const QString& message) { if (error) *error = message; return false; };
    const auto courses = sorted(classes);
    for (const auto& course : courses)
        if (course.subject.trimmed().isEmpty() || !course.startTime.isValid() ||
            !course.endTime.isValid() || course.startTime >= course.endTime)
            return fail(QStringLiteral("Course has an empty subject or invalid time range"));
    for (qsizetype i = 1; i < courses.size(); ++i)
        if (courses[i].startTime < courses[i - 1].endTime)
            return fail(QStringLiteral("Course time ranges overlap"));
    return true;
}

bool validateOverrides(const QList<DateScheduleOverride>& records, QString* error)
{
    if (error) error->clear();
    auto fail = [error](const QString& message) { if (error) *error = message; return false; };
    QSet<QDate> dates;
    for (const auto& record : records) {
        if (!record.date.isValid() || dates.contains(record.date))
            return fail(QStringLiteral("特殊安排日期无效或重复"));
        dates.insert(record.date);
        if (record.sourceWeekScheduleId.trimmed().isEmpty() || record.sourceWeekday < 1 || record.sourceWeekday > 7)
            return fail(QStringLiteral("特殊安排来源无效"));
        if (!validateClasses(record.sourceClasses, error) || !validateClasses(record.classes, error)) return false;
    }
    return true;
}

const DaySchedule* sourceDay(const Profile& profile, const QString& weekId, int weekday)
{
    for (const auto& week : profile.schedules)
        if (week.id == weekId)
            for (const auto& day : week.daySchedules)
                if (day.enableDay == weekday) return &day;
    return nullptr;
}

const DateScheduleOverride* find(const Profile& profile, const QDate& date)
{
    for (const auto& record : profile.dateOverrides) if (record.date == date) return &record;
    return nullptr;
}

bool sourceMatches(const Profile& profile, const DateScheduleOverride& record)
{
    const auto* day = sourceDay(profile, record.sourceWeekScheduleId, record.sourceWeekday);
    return day && sameClasses(day->classes, record.sourceClasses);
}

Result resolve(const Profile& profile, const QDate& date, const QString& weekId,
               int weekday, int weekIndex, const QDate& weekOneMonday)
{
    Result result;
    auto fail = [&result](const QString& message) {
        result.valid = false; result.error = message; result.classes.clear(); return result;
    };
    if (!date.isValid()) return fail(QStringLiteral("日期无效"));
    if (!weekday) {
        if (const auto* record = find(profile, date); record && sourceMatches(profile, *record)) {
            result.classes = sorted(record->classes);
            result.sourceWeekScheduleId = record->sourceWeekScheduleId;
            result.sourceWeekday = record->sourceWeekday;
            result.special = true;
            for (qsizetype i = 0; i < profile.schedules.size(); ++i)
                if (profile.schedules[i].id == record->sourceWeekScheduleId) {
                    result.weekScheduleIndex = int(i);
                    result.name = profile.schedules[i].name + QStringLiteral(" · 特殊安排");
                }
            if (!validateClasses(result.classes, &result.error)) result.valid = false;
            return result;
        }
    }
    weekday = weekday ? weekday : date.dayOfWeek();
    result.sourceWeekday = weekday;
    int selected = weekIndex;
    if (selected < -1 || selected >= profile.schedules.size())
        return fail(QStringLiteral("Week schedule index is out of range"));
    const QString selection = weekId.isEmpty() ? profile.activeWeekScheduleId : weekId;
    if (selected == -1 && !selection.isEmpty()) {
        for (qsizetype i = 0; i < profile.schedules.size(); ++i)
            if (profile.schedules[i].id == selection) { selected = int(i); break; }
        if (selected == -1) return fail(QStringLiteral("选择的来源课表不存在"));
    }
    if (selected == -1) {
        for (qsizetype i = 0; i < profile.schedules.size(); ++i) {
            const auto& week = profile.schedules[i];
            if (!sourceDay(profile, week.id, weekday)) continue;
            if (week.mode != WeekScheduleMode::All) {
                if (week.mode != WeekScheduleMode::Odd && week.mode != WeekScheduleMode::Even)
                    return fail(QStringLiteral("Week schedule has an invalid mode"));
                if (!weekOneMonday.isValid())
                    return fail(QStringLiteral("Odd/even schedules require a week reference date"));
                const auto monday = date.addDays(1 - date.dayOfWeek());
                const qint64 weekNumber = weekOneMonday.daysTo(monday) / 7 + 1;
                if ((week.mode == WeekScheduleMode::Odd) != (weekNumber % 2 != 0)) continue;
            }
            if (selected != -1)
                return fail(QStringLiteral("Multiple week schedules match; select an explicit index"));
            selected = int(i);
        }
    }
    if (selected != -1) {
        const auto& week = profile.schedules[selected];
        result.name = week.name;
        result.sourceWeekScheduleId = week.id;
        result.weekScheduleIndex = selected;
        QSet<int> days;
        for (const auto& day : week.daySchedules) {
            if (day.enableDay < 1 || day.enableDay > 7 || days.contains(day.enableDay))
                return fail(QStringLiteral("Week schedule has an invalid or duplicate weekday"));
            days.insert(day.enableDay);
            if (day.enableDay == weekday) result.classes = sorted(day.classes);
        }
    }
    if (!validateClasses(result.classes, &result.error)) result.valid = false;
    return result;
}
}
