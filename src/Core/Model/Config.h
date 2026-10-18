#ifndef CLASSTOPLAND_NEXT_CONFIG_H
#define CLASSTOPLAND_NEXT_CONFIG_H
#include<QString>
#include"CourseBarConfig.h"
#include "AppearanceConfig.h"
#include "WindowInterceptionConfig.h"
#include "DesktopCountdownConfig.h"
#include "PluginConfig.h"
#include "ReminderConfig.h"
struct Config
{
    QString profileName = QStringLiteral("Default");
    CourseBarConfig courseBarConfig;
    AppearanceConfig appearanceConfig;
    WindowInterceptionConfig windowInterceptionConfig;
    DesktopCountdownConfig desktopCountdownConfig;
    PluginConfig pluginConfig;
    ReminderConfig reminderConfig;
};
#endif //CLASSTOPLAND_NEXT_CONFIG_H
