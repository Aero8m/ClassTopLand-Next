#include "ThemeManager.h"

#include "../ConfigManager/ConfigManager.h"
#include <ElaTheme.h>
#include <QApplication>
#include <QWidget>
#include <QStyleHints>

ThemeManager::ThemeManager() : themeColor_(AppearanceConfig{}.themeColor)
{
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this](Qt::ColorScheme scheme) {
        if (themeMode_ == AppearanceThemeMode::System) applyThemeMode(scheme);
    });
}

ThemeManager& ThemeManager::instance()
{
    static ThemeManager manager;
    return manager;
}

void ThemeManager::applyConfiguredTheme()
{
    const auto& appearance = ConfigManager::instance().config().appearanceConfig;
    themeMode_ = appearance.themeMode;
    applyTheme(QColor(appearance.themeColor));
    applyThemeMode(QGuiApplication::styleHints()->colorScheme());
    emit themeModeChanged(themeMode_);
}

bool ThemeManager::setThemeColor(const QColor& color, QString* error)
{
    if (error) error->clear();
    if (!color.isValid()) {
        if (error) *error = tr("无效的主题色");
        return false;
    }
    const QColor rgb = QColor::fromRgb(color.rgb());
    auto& configManager = ConfigManager::instance();
    Config candidate = configManager.config();
    candidate.appearanceConfig.themeColor = rgb.name(QColor::HexRgb);
    if (!configManager.commit(candidate, error)) return false;
    applyTheme(rgb);
    return true;
}

bool ThemeManager::setThemeMode(AppearanceThemeMode mode, QString* error)
{
    auto& configManager = ConfigManager::instance();
    Config candidate = configManager.config();
    candidate.appearanceConfig.themeMode = mode;
    if (!configManager.commit(candidate, error)) return false;
    themeMode_ = mode;
    applyThemeMode(QGuiApplication::styleHints()->colorScheme());
    emit themeModeChanged(themeMode_);
    return true;
}

void ThemeManager::applyThemeMode(Qt::ColorScheme systemScheme)
{
    const bool dark = themeMode_ == AppearanceThemeMode::Dark ||
        (themeMode_ == AppearanceThemeMode::System && systemScheme == Qt::ColorScheme::Dark);
    const auto mode = dark ? ElaThemeType::Dark : ElaThemeType::Light;
    if (eTheme->getThemeMode() != mode) eTheme->setThemeMode(mode);
    for (QWidget* widget : QApplication::allWidgets()) widget->update();
}

void ThemeManager::applyTheme(const QColor& color)
{
    themeColor_ = color;
    for (const auto mode : {ElaThemeType::Light, ElaThemeType::Dark}) {
        eTheme->setThemeColor(mode, ElaThemeType::PrimaryNormal, color);
        eTheme->setThemeColor(mode, ElaThemeType::PrimaryHover, color.lighter(110));
        eTheme->setThemeColor(mode, ElaThemeType::PrimaryPress, color.darker(110));
    }
    // Ela's color setter does not notify controls. Its mode setter emits even
    // for the current mode, refreshing palettes without toggling light/dark.
    eTheme->setThemeMode(eTheme->getThemeMode());
    emit themeColorChanged(themeColor_);
    for (QWidget* widget : QApplication::allWidgets()) widget->update();
}
