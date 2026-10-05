#ifndef CLASSTOPLAND_NEXT_CONFIG_H
#define CLASSTOPLAND_NEXT_CONFIG_H
#include<QString>
#include"CourseBarConfig.h"
#include "AppearanceConfig.h"
struct Config
{
    QString profileName = QStringLiteral("Default");
    CourseBarConfig courseBarConfig;
    AppearanceConfig appearanceConfig;
};
#endif //CLASSTOPLAND_NEXT_CONFIG_H
