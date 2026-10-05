#ifndef CLASSTOPLAND_NEXT_THEMEMANAGER_H
#define CLASSTOPLAND_NEXT_THEMEMANAGER_H

#include <QColor>
#include <QObject>
#include "../Model/AppearanceConfig.h"

// The application theme is global; call these methods on the GUI thread.
class ThemeManager final : public QObject
{
    Q_OBJECT
public:
    static ThemeManager& instance();
    const QColor& themeColor() const noexcept { return themeColor_; }
    AppearanceThemeMode themeMode() const noexcept { return themeMode_; }
    void applyConfiguredTheme();
    // Persist before publishing. Alpha is discarded; the config stores RGB only.
    bool setThemeColor(const QColor& color, QString* error = nullptr);
    bool setThemeMode(AppearanceThemeMode mode, QString* error = nullptr);

signals:
    void themeColorChanged(const QColor& color);
    void themeModeChanged(AppearanceThemeMode mode);

private:
    ThemeManager();
    void applyTheme(const QColor& color);
    void applyThemeMode(Qt::ColorScheme systemScheme);
    QColor themeColor_;
    AppearanceThemeMode themeMode_ = AppearanceThemeMode::System;
};

#endif // CLASSTOPLAND_NEXT_THEMEMANAGER_H
