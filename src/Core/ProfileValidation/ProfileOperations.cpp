#include "ProfileOperations.h"
#include "../DateSchedule/DateSchedule.h"
#include <utility>
namespace
{
bool fail(QString *e, const QString &m)
{
    if (e)
        *e = m;
    return false;
}
} // namespace
bool ProfileOperations::reschedule(Profile &profile, const QDate &date, const QString &weekId, int weekday,
                                   QString *error)
{
    if (!date.isValid() || date < QDate::currentDate())
        return fail(error, QStringLiteral("请选择今天或未来日期"));
    const auto *day = DateSchedule::sourceDay(profile, weekId, weekday);
    if (!day)
        return fail(error, QStringLiteral("请选择有效的来源课表和星期"));
    if (!DateSchedule::validateClasses(day->classes, error))
        return false;
    Profile candidate = profile;
    for (qsizetype i = candidate.dateOverrides.size(); i-- > 0;)
        if (candidate.dateOverrides[i].date == date)
            candidate.dateOverrides.removeAt(i);
    const auto courses = DateSchedule::sorted(day->classes);
    candidate.dateOverrides.append({date, weekId, weekday, courses, courses});
    profile = std::move(candidate);
    return true;
}

bool ProfileOperations::swap(Profile &profile, const QDate &firstDate, const QString &firstWeekId, int first,
                             const QDate &secondDate, const QString &secondWeekId, int second, QString *error)
{
    if (error)
        error->clear();
    const auto today = QDate::currentDate();
    if (!firstDate.isValid() || !secondDate.isValid() || firstDate < today || secondDate < today)
        return fail(error, QStringLiteral("请选择今天或未来日期"));
    auto left = DateSchedule::resolve(profile, firstDate, firstWeekId);
    auto right = DateSchedule::resolve(profile, secondDate, secondWeekId);
    if (!left.valid)
        return fail(error, left.error);
    if (!right.valid)
        return fail(error, right.error);
    const bool sameDate = firstDate == secondDate;
    if (sameDate && (left.sourceWeekScheduleId != right.sourceWeekScheduleId ||
                     left.sourceWeekday != right.sourceWeekday))
        return fail(error, QStringLiteral("同一日期必须使用相同的来源课表"));
    if (first < 0 || second < 0 || first >= left.classes.size() || second >= right.classes.size() ||
        (sameDate && first == second))
        return fail(error, QStringLiteral("请选择两节不同的课程"));
    if (left.classes[first].subject == right.classes[second].subject)
        return fail(error, QStringLiteral("两节课的科目相同，无需交换"));
    const auto *leftDay = DateSchedule::sourceDay(profile, left.sourceWeekScheduleId, left.sourceWeekday);
    const auto *rightDay = DateSchedule::sourceDay(profile, right.sourceWeekScheduleId, right.sourceWeekday);
    if (!leftDay || !rightDay)
        return fail(error, QStringLiteral("来源日课表不存在"));
    if (sameDate)
        std::swap(left.classes[first].subject, left.classes[second].subject);
    else
        std::swap(left.classes[first].subject, right.classes[second].subject);

    Profile candidate = profile;
    auto replace = [&](const QDate &date, const DateSchedule::Result &table, const DaySchedule &day)
    {
        // Keep an existing source snapshot; only the effective subjects change.
        const auto *existing = DateSchedule::find(profile, date);
        DateScheduleOverride record{date, table.sourceWeekScheduleId, table.sourceWeekday,
                                    existing && table.special ? existing->sourceClasses
                                                              : DateSchedule::sorted(day.classes),
                                    table.classes};
        for (qsizetype i = candidate.dateOverrides.size(); i-- > 0;)
            if (candidate.dateOverrides[i].date == date)
                candidate.dateOverrides.removeAt(i);
        candidate.dateOverrides.append(record);
    };
    replace(firstDate, left, *leftDay);
    if (!sameDate)
        replace(secondDate, right, *rightDay);
    // Both endpoints are published only after the single atomic write succeeds.
    profile = std::move(candidate);
    return true;
}

bool ProfileOperations::restore(Profile &profile, const QDate &date, QString *error)
{
    if (error)
        error->clear();
    if (!date.isValid())
        return fail(error, QStringLiteral("恢复日期无效"));
    if (!DateSchedule::find(profile, date))
        return true;
    Profile candidate = profile;
    for (qsizetype i = candidate.dateOverrides.size(); i-- > 0;)
        if (candidate.dateOverrides[i].date == date)
            candidate.dateOverrides.removeAt(i);
    profile = std::move(candidate);
    return true;
}
