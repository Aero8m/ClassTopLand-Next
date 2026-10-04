#ifndef CLASSTOPLAND_NEXT_PROFILE_H
#define CLASSTOPLAND_NEXT_PROFILE_H
#include<QString>
#include<QList>
#include"Schedule.h"
#include"Subject.h"
#include"TimeLine.h"

struct Profile
{
    QString name;
    QList<Subject> subjects;
    QList<TimeLine> timeLines;
    QList<WeekSchedule> schedules;
};
#endif //CLASSTOPLAND_NEXT_PROFILE_H