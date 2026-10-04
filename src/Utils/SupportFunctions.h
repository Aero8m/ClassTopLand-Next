

#ifndef CLASSTOPLAND_NEXT_SUPPORTFUNCTIONS_H
#define CLASSTOPLAND_NEXT_SUPPORTFUNCTIONS_H
#include<QTime>
#include<QString>

inline QTime stringToTime(const QString& timeString)
{
    return QTime::fromString(timeString, QStringLiteral("hh:mm:ss"));
}

#endif //CLASSTOPLAND_NEXT_SUPPORTFUNCTIONS_H
