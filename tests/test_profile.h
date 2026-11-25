#pragma once

#include "Core/Model/Profile.h"

namespace TestProfiles {
inline const QString math = QStringLiteral("数学");
inline const QString chinese = QStringLiteral("语文");
inline const QString science = QStringLiteral("科学");
inline const QString weekId = QStringLiteral("regular-week");

inline Profile regular()
{
    Profile profile;
    profile.name = QStringLiteral("测试档案");
    profile.subjects = {{math, QStringLiteral("数"), QStringLiteral("王老师")}, {chinese}, {science}};

    WeekSchedule week(QStringLiteral("常规课表"), WeekScheduleMode::All);
    week.id = weekId;
    week.initDaySchedules();
    week.getDaySchedule(0).classes = {{math, QTime(8, 0), QTime(8, 45)},
                                      {chinese, QTime(8, 55), QTime(9, 40)}};
    week.getDaySchedule(1).classes = {{science, QTime(10, 0), QTime(10, 45)}};
    profile.schedules.append(week);
    return profile;
}
}
