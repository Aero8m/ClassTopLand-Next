#ifndef CLASSTOPLAND_NEXT_CONFIG_H
#define CLASSTOPLAND_NEXT_CONFIG_H
#include<QString>
#include"CourseBarConfig.h"
struct Config
{
    QString profileName = QStringLiteral("Default");
    CourseBarConfig courseBarConfig;
};
#endif //CLASSTOPLAND_NEXT_CONFIG_H
