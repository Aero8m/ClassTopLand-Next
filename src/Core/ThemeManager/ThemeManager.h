#ifndef CLASSTOPLAND_NEXT_THEMEMANAGER_H
#define CLASSTOPLAND_NEXT_THEMEMANAGER_H

#include <QColor>
#include <QObject>
#include "../Model/AppearanceConfig.h"

class QTimer;

// The application theme is global; call these methods on the GUI thread.
class ThemeManager final : public QObject
{
    Q_OBJECT
public:
    static ThemeManager& instance();
    const QColor& themeColor() const noexcept { return themeColor_; }
    AppearanceThemeMode themeMode() const noexcept { return themeMode_; }
    // The saved preference can differ from the effective mode on unsupported systems.
    AppearanceWindowEffect windowEffect() const noexcept { return windowEffect_; }
    AppearanceWindowEffect effectiveWindowEffect() const;
    QString windowEffectUnavailableReason(AppearanceWindowEffect effect) const;
    void applyConfiguredTheme();
    // Persist before publishing. Alpha is discarded; the config stores RGB only.
    bool setThemeColor(const QColor& color, QString* error = nullptr);
    bool setThemeMode(AppearanceThemeMode mode, QString* error = nullptr);
    bool setWindowEffect(AppearanceWindowEffect effect, QString* error = nullptr);

signals:
    void themeColorChanged(const QColor& color);
    void themeModeChanged(AppearanceThemeMode mode);
    void windowEffectChanged(AppearanceWindowEffect effect);

private:
    ThemeManager();
    void applyTheme(const QColor& color);
    void applyThemeMode(Qt::ColorScheme systemScheme);
    void applyWindowEffect();
    void refreshElaMicaWallpaper();
    QColor themeColor_;
    AppearanceThemeMode themeMode_ = AppearanceThemeMode::System;
    AppearanceWindowEffect windowEffect_ = AppearanceWindowEffect::Mica;
    QTimer* wallpaperRefreshTimer_;
    QString wallpaperFingerprint_;
};

#endif // CLASSTOPLAND_NEXT_THEMEMANAGER_H
