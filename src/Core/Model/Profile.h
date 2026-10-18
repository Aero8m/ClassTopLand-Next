#ifndef CLASSTOPLAND_NEXT_PROFILE_H
#define CLASSTOPLAND_NEXT_PROFILE_H
#include<QString>
#include<QList>
#include<QDate>
#include"Schedule.h"
#include"Subject.h"
#include"TimeLine.h"

// The source snapshot detects edits without depending on mutable names or row order.
struct DateScheduleOverride
{
    QDate date;
    QString sourceWeekScheduleId;
    int sourceWeekday = 1;
    QList<Class> sourceClasses;
    QList<Class> classes;
};

struct Profile
{
    QString name;
    // Optional: the week containing this date is semester week 1 (Monday-based).
    QDate semesterStartDate;
    QList<Subject> subjects;
    QList<TimeLine> timeLines;
    QList<WeekSchedule> schedules;
    // Empty means automatic date/week matching.
    QString activeWeekScheduleId;
    QList<DateScheduleOverride> dateOverrides;
};
#endif //CLASSTOPLAND_NEXT_PROFILE_H
