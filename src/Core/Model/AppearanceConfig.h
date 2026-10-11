#ifndef CLASSTOPLAND_NEXT_APPEARANCECONFIG_H
#define CLASSTOPLAND_NEXT_APPEARANCECONFIG_H

#include <QString>
#include <QMetaType>

enum class AppearanceThemeMode
{
    System,
    Light,
    Dark
};
Q_DECLARE_METATYPE(AppearanceThemeMode)

enum class AppearanceWindowEffect
{
    Normal,
    ElaMica,
    Mica,
    MicaAlt,
    Acrylic,
    DwmBlur
};
Q_DECLARE_METATYPE(AppearanceWindowEffect)

struct AppearanceConfig
{
    QString themeColor = QStringLiteral("#1191d3");
    AppearanceThemeMode themeMode = AppearanceThemeMode::System;
    AppearanceWindowEffect windowEffect = AppearanceWindowEffect::Mica;
};

#endif // CLASSTOPLAND_NEXT_APPEARANCECONFIG_H
