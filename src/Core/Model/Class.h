#ifndef CLASSTOPLAND_NEXT_CLASS_H
#define CLASSTOPLAND_NEXT_CLASS_H
#include<QString>
#include<QTime>
#include"../../Utils/SupportFunctions.h"

struct Class
{
    Class(QString subject, QTime startTime, QTime endTime)
    {
        this->subject = subject;
        this->startTime = startTime;
        this->endTime = endTime;
    }
    Class(QString subject, QString startTimeString, QString endTimeString)
    {
        this->subject = subject;
        this->startTime = stringToTime(startTimeString);
        this->endTime = stringToTime(endTimeString);
    }
    QString subject;
    QTime startTime;
    QTime endTime;
};
#endif //CLASSTOPLAND_NEXT_CLASS_H