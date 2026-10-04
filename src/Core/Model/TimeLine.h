#ifndef CLASSTOPLAND_NEXT_TIMELINE_H
#define CLASSTOPLAND_NEXT_TIMELINE_H
#include<QString>
#include<QTime>
#include<QList>
#include"../../Utils/SupportFunctions.h"

struct TimeLinePoint
{
   TimeLinePoint(QTime startTime, QTime endTime)
   {
       this->startTime = startTime;
       this->endTime = endTime;
   }
   TimeLinePoint(QString startTimeString, QString endTimeString)
   {
       this->startTime = stringToTime(startTimeString);
       this->endTime = stringToTime(endTimeString);
   }
   QTime startTime;
   QTime endTime;
};
struct TimeLine
{
    TimeLine(QString name)
    {
        this->name = name;
    }
    QString name;
    QList<TimeLinePoint> timePoints;

};
#endif //CLASSTOPLAND_NEXT_TIMELINE_H