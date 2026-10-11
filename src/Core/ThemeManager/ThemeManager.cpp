#include "ThemeManager.h"

#include "../ConfigManager/ConfigManager.h"
#include <ElaApplication.h>
#include <ElaTheme.h>
#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QOperatingSystemVersion>
#include <QTimer>
#include <QWidget>
#include <QStyleHints>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

ThemeManager::ThemeManager() : themeColor_(AppearanceConfig{}.themeColor)
{
    wallpaperRefreshTimer_ = new QTimer(this);
    wallpaperRefreshTimer_->setInterval(5000);
    connect(wallpaperRefreshTimer_, &QTimer::timeout, this, &ThemeManager::refreshElaMicaWallpaper);
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
    windowEffect_ = appearance.windowEffect;
    applyTheme(QColor(appearance.themeColor));
    applyThemeMode(QGuiApplication::styleHints()->colorScheme());
    applyWindowEffect();
    emit themeModeChanged(themeMode_);
    emit windowEffectChanged(windowEffect_);
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

QString ThemeManager::windowEffectUnavailableReason(AppearanceWindowEffect effect) const
{
    switch (effect) {
    case AppearanceWindowEffect::Normal:
    case AppearanceWindowEffect::ElaMica:
        return {};
    case AppearanceWindowEffect::Mica:
    case AppearanceWindowEffect::MicaAlt:
    case AppearanceWindowEffect::Acrylic:
    case AppearanceWindowEffect::DwmBlur:
#ifdef Q_OS_WIN
        {
            const auto version = QOperatingSystemVersion::current();
            if (effect == AppearanceWindowEffect::Mica &&
                version < QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22000))
                return tr("Mica 需要 Windows 11（build 22000）或更高版本。");
            if ((effect == AppearanceWindowEffect::MicaAlt || effect == AppearanceWindowEffect::Acrylic) &&
                version < QOperatingSystemVersion(QOperatingSystemVersion::Windows, 10, 0, 22621))
                return tr("Mica-Alt 和 Acrylic 需要 Windows 11 22H2（build 22621）或更高版本。");
            return {};
        }
#else
        return tr("该窗口效果仅支持 Windows。");
#endif
    }
    return tr("无效的窗口效果。");
}

AppearanceWindowEffect ThemeManager::effectiveWindowEffect() const
{
    return windowEffectUnavailableReason(windowEffect_).isEmpty()
        ? windowEffect_ : AppearanceWindowEffect::Normal;
}

bool ThemeManager::setWindowEffect(AppearanceWindowEffect effect, QString* error)
{
    if (error) error->clear();
    const QString reason = windowEffectUnavailableReason(effect);
    if (!reason.isEmpty()) {
        if (error) *error = reason;
        return false;
    }
    auto& configManager = ConfigManager::instance();
    Config candidate = configManager.config();
    candidate.appearanceConfig.windowEffect = effect;
    if (!configManager.commit(candidate, error)) return false;
    windowEffect_ = effect;
    applyWindowEffect();
    emit windowEffectChanged(windowEffect_);
    return true;
}

void ThemeManager::applyWindowEffect()
{
    ElaApplicationType::WindowDisplayMode mode = ElaApplicationType::Normal;
    switch (effectiveWindowEffect()) {
    case AppearanceWindowEffect::Normal: break;
    case AppearanceWindowEffect::ElaMica:
        refreshElaMicaWallpaper();
        mode = ElaApplicationType::ElaMica;
        break;
#ifdef Q_OS_WIN
    case AppearanceWindowEffect::Mica: mode = ElaApplicationType::Mica; break;
    case AppearanceWindowEffect::MicaAlt: mode = ElaApplicationType::MicaAlt; break;
    case AppearanceWindowEffect::Acrylic: mode = ElaApplicationType::Acrylic; break;
    case AppearanceWindowEffect::DwmBlur: mode = ElaApplicationType::DWMBlur; break;
#else
    case AppearanceWindowEffect::Mica:
    case AppearanceWindowEffect::MicaAlt:
    case AppearanceWindowEffect::Acrylic:
    case AppearanceWindowEffect::DwmBlur: break;
#endif
    }
    eApp->setWindowDisplayMode(mode);
#ifdef Q_OS_WIN
    if (mode == ElaApplicationType::ElaMica) wallpaperRefreshTimer_->start();
    else wallpaperRefreshTimer_->stop();
#endif
}

void ThemeManager::refreshElaMicaWallpaper()
{
    QString path = QStringLiteral(":/include/Image/MicaBase.png");
    QString fingerprint = path;
#ifdef Q_OS_WIN
    wchar_t wallpaper[MAX_PATH]{};
    if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, wallpaper, 0) && wallpaper[0]) {
        const QString desktopPath = QString::fromWCharArray(wallpaper);
        const QFileInfo file(desktopPath);
        const QString desktopFingerprint = desktopPath + QLatin1Char('|') + QString::number(file.size())
            + QLatin1Char('|') + QString::number(file.lastModified().toMSecsSinceEpoch());
        // Only decode again when the wallpaper path or file contents have changed.
        if (desktopFingerprint == wallpaperFingerprint_ && eApp->getElaMicaImagePath() == desktopPath)
            return;
        if (!QImage(desktopPath).isNull()) {
            path = desktopPath;
            fingerprint = desktopFingerprint;
        }
    }
#endif
    if (fingerprint == wallpaperFingerprint_ && eApp->getElaMicaImagePath() == path) return;
    wallpaperFingerprint_ = fingerprint;
    if (eApp->getElaMicaImagePath() != path || eApp->getWindowDisplayMode() == ElaApplicationType::ElaMica)
        eApp->setElaMicaImagePath(path);
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
