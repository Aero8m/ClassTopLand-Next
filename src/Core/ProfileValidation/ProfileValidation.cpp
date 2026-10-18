#include "ProfileValidation.h"
#include <QSet>
#include <QObject>
#include <algorithm>
namespace
{
bool validRange(const QTime &a, const QTime &b)
{
    return a.isValid() && b.isValid() && a < b;
}
} // namespace
ProfileValidation::ValidationError ProfileValidation::validate(const Profile &profile)
{
    QString reminderError;
    if (!Reminders::validate(profile.reminderRules, &reminderError)) return {reminderError, Page::Info};
    if (profile.name.trimmed().isEmpty())
        return {QObject::tr("档案名称不能为空"), Page::Info};
    QSet<QString> names;
    for (int i = 0; i < profile.subjects.size(); ++i)
    {
        const auto name = profile.subjects[i].name.trimmed();
        if (name.isEmpty() || names.contains(name))
            return {QObject::tr("科目名称为空或重复"), Page::Subjects, i};
        names.insert(name);
    }
    for (int i = 0; i < profile.timeLines.size(); ++i)
    {
        const auto &line = profile.timeLines[i];
        if (line.name.trimmed().isEmpty())
            return {QObject::tr("时间线名称不能为空"), Page::TimeLines, i};
        QList<int> order;
        for (int j = 0; j < line.timePoints.size(); ++j)
        {
            if (!validRange(line.timePoints[j].startTime, line.timePoints[j].endTime))
                return {QObject::tr("开始时间必须早于结束时间"), Page::TimeLines, i, j};
            order.append(j);
        }
        std::stable_sort(order.begin(), order.end(), [&line](int a, int b)
                         { return line.timePoints[a].startTime < line.timePoints[b].startTime; });
        for (int j = 1; j < order.size(); ++j)
            if (line.timePoints[order[j]].startTime < line.timePoints[order[j - 1]].endTime)
                return {QObject::tr("时间线时段重叠"), Page::TimeLines, i, order[j]};
    }
    for (int i = 0; i < profile.schedules.size(); ++i)
    {
        const auto &week = profile.schedules[i];
        if (week.name.trimmed().isEmpty())
            return {QObject::tr("课表名称不能为空"), Page::Schedules, i};
        if (week.mode != WeekScheduleMode::All && week.mode != WeekScheduleMode::Odd &&
            week.mode != WeekScheduleMode::Even)
            return {QObject::tr("无效的周模式"), Page::Schedules, i};
        QSet<int> days;
        for (const auto &day : week.daySchedules)
        {
            if (day.enableDay < 1 || day.enableDay > 7 || days.contains(day.enableDay))
                return {QObject::tr("星期无效或重复"), Page::Schedules, i, -1, day.enableDay};
            days.insert(day.enableDay);
            if (day.name.trimmed().isEmpty())
                return {QObject::tr("日课表名称不能为空"), Page::Schedules, i, -1, day.enableDay};
            QList<int> order;
            for (int j = 0; j < day.classes.size(); ++j)
            {
                const auto &course = day.classes[j];
                if (course.subject.trimmed().isEmpty())
                    return {QObject::tr("请选择课程科目"), Page::Schedules, i, j, day.enableDay};
                if (!validRange(course.startTime, course.endTime))
                    return {QObject::tr("开始时间必须早于结束时间"), Page::Schedules, i, j, day.enableDay};
                order.append(j);
            }
            std::stable_sort(order.begin(), order.end(), [&day](int a, int b)
                             { return day.classes[a].startTime < day.classes[b].startTime; });
            for (int j = 1; j < order.size(); ++j)
                if (day.classes[order[j]].startTime < day.classes[order[j - 1]].endTime)
                    return {QObject::tr("当天课程时间重叠"), Page::Schedules, i, order[j], day.enableDay};
        }
    }
    return {};
}
