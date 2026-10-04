#ifndef CLASSTOPLAND_NEXT_SCHEDULE_H
#define CLASSTOPLAND_NEXT_SCHEDULE_H
#include<QString>
#include<QList>
#include"Class.h"
enum class WeekScheduleMode
{
    All,
    Odd,
    Even
};

struct DaySchedule
{
    DaySchedule(QString name, int enableDay)
    {
        this->name = name;

        if (enableDay < 1 || enableDay > 7)
        {
            this->enableDay = 1;
        }
        else
        {
            this->enableDay = enableDay;
        }
    }
    QString name;
    int enableDay;
    QList<Class> classes;
};

struct WeekSchedule
{
    WeekSchedule(QString name, WeekScheduleMode mode)
    {
        this->name = name;
        this->mode = mode;
    }
    void initDaySchedules()
    {
        for (int i = 0; i < 7; i++)
        {
            daySchedules.append(DaySchedule("星期 " + QString::number(i + 1), i + 1));
        }
    }
    DaySchedule& getDaySchedule(int index)
    {
        return daySchedules[index];
    }
    QString name;
    WeekScheduleMode mode;
    QList<DaySchedule> daySchedules;
};
#endif //CLASSTOPLAND_NEXT_SCHEDULE_H