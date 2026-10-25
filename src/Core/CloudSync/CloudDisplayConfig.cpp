#include "CloudDisplayConfig.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>

namespace {
bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

bool readBool(const QJsonObject& object, const QString& key, bool fallback, bool& value,
              QString* error, const QString& path)
{
    const QJsonValue item = object.value(key);
    if (item.isUndefined()) {
        value = fallback;
        return true;
    }
    if (!item.isBool()) return fail(error, path + QStringLiteral(" 必须为布尔值"));
    value = item.toBool();
    return true;
}

bool readInt(const QJsonObject& object, const QString& key, int fallback, int minimum, int maximum,
             int& value, QString* error, const QString& path)
{
    const QJsonValue item = object.value(key);
    if (item.isUndefined()) {
        value = fallback;
        return true;
    }
    if (!item.isDouble() || item.toDouble() != item.toInt())
        return fail(error, path + QStringLiteral(" 必须为整数"));
    const int candidate = item.toInt();
    if (candidate < minimum || candidate > maximum)
        return fail(error, path + QStringLiteral(" 必须在 %1～%2 之间").arg(minimum).arg(maximum));
    value = candidate;
    return true;
}

bool readString(const QJsonObject& object, const QString& key, const QString& fallback, int maximum,
                QString& value, QString* error, const QString& path)
{
    const QJsonValue item = object.value(key);
    if (item.isUndefined()) {
        value = fallback;
        return true;
    }
    if (!item.isString()) return fail(error, path + QStringLiteral(" 必须为文本"));
    const QString text = item.toString();
    if (text.size() > maximum) return fail(error, path + QStringLiteral(" 超过长度上限"));
    value = text;
    return true;
}

bool parseTarget(const QString& text, QDateTime& value, QString* error, const QString& path)
{
    if (text.isEmpty()) {
        value = QDateTime();
        return true;
    }
    // The payload is a naive local time, independent of any time zone offset.
    const QDateTime parsed = QDateTime::fromString(text, QStringLiteral("yyyy-MM-dd'T'HH:mm:ss"));
    if (!parsed.isValid() ||
        parsed.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss")) != text)
        return fail(error, path + QStringLiteral(" 必须为 YYYY-MM-DDTHH:mm:ss"));
    value = parsed;
    return true;
}

bool parseComponents(const QJsonArray& components, QList<CourseBarComponentConfig>& result,
                     QString* error)
{
    if (components.size() > 32) return fail(error, QStringLiteral("课程条组件最多 32 项"));
    QSet<QString> instanceIds;
    result.clear();
    for (qsizetype i = 0; i < components.size(); ++i) {
        const QString path = QStringLiteral("courseBarConfig.components[%1]").arg(i);
        if (!components.at(i).isObject()) return fail(error, path + QStringLiteral(" 必须为对象"));
        const QJsonObject item = components.at(i).toObject();
        const QJsonValue typeId = item.value(QStringLiteral("typeId"));
        if (!typeId.isString()) return fail(error, path + QStringLiteral(".typeId 必须为文本"));
        CourseBarComponentType type = CourseBarComponentType::Date;
        const QString typeText = typeId.toString();
        if (typeText == QStringLiteral("builtin/date")) type = CourseBarComponentType::Date;
        else if (typeText == QStringLiteral("builtin/courseView")) type = CourseBarComponentType::CourseView;
        else if (typeText == QStringLiteral("builtin/countdown")) type = CourseBarComponentType::Countdown;
        else if (typeText == QStringLiteral("builtin/textTip")) type = CourseBarComponentType::TextTip;
        else return fail(error, path + QStringLiteral(".typeId 不受支持：%1").arg(typeText));

        const QJsonValue instanceId = item.value(QStringLiteral("instanceId"));
        if (!instanceId.isString() || instanceId.toString().isEmpty() ||
            instanceId.toString().size() > 128)
            return fail(error, path + QStringLiteral(".instanceId 必须为非空文本"));
        if (instanceIds.contains(instanceId.toString()))
            return fail(error, path + QStringLiteral(".instanceId 重复"));
        instanceIds.insert(instanceId.toString());

        CourseBarComponentConfig component{type};
        component.typeId = typeText;
        component.instanceId = instanceId.toString();
        if (!readBool(item, QStringLiteral("enabled"), true, component.enabled, error, path)) return false;
        const QJsonValue settings = item.value(QStringLiteral("settings"));
        if (!settings.isUndefined()) {
            if (!settings.isObject()) return fail(error, path + QStringLiteral(".settings 必须为对象"));
            component.settings = settings.toObject();
        }
        const QJsonValue text = item.value(QStringLiteral("text"));
        if (!text.isUndefined() && !text.isNull()) {
            if (!text.isString()) return fail(error, path + QStringLiteral(".text 必须为文本"));
            if (text.toString().size() > 1000)
                return fail(error, path + QStringLiteral(".text 超过长度上限"));
            component.text = text.toString();
        }
        const QJsonValue spacing = item.value(QStringLiteral("courseSpacing"));
        if (!spacing.isUndefined() && !spacing.isNull()) {
            if (!spacing.isDouble() || spacing.toDouble() != spacing.toInt())
                return fail(error, path + QStringLiteral(".courseSpacing 必须为整数"));
            if (spacing.toInt() < 0 || spacing.toInt() > CourseBarConfig::MaximumSpacing)
                return fail(error, path + QStringLiteral(".courseSpacing 必须在 0～%1 之间")
                                        .arg(CourseBarConfig::MaximumSpacing));
            component.courseSpacing = spacing.toInt();
        }
        result.append(component);
    }
    return true;
}

bool parseAppearance(const QJsonValue& value, AppearanceConfig& appearance, QString* error)
{
    appearance = AppearanceConfig{};
    if (value.isUndefined()) return true;
    if (!value.isObject()) return fail(error, QStringLiteral("appearanceConfig 必须为对象"));
    const QJsonObject object = value.toObject();
    const QString path = QStringLiteral("appearanceConfig");
    if (!readString(object, QStringLiteral("themeColor"), appearance.themeColor, 7,
                    appearance.themeColor, error, path + QStringLiteral(".themeColor")))
        return false;
    static const QRegularExpression hexColor(QStringLiteral("\\A#[0-9a-fA-F]{6}\\z"));
    if (!hexColor.match(appearance.themeColor).hasMatch())
        return fail(error, QStringLiteral("appearanceConfig.themeColor 必须为 #rrggbb"));
    QString mode;
    if (!readString(object, QStringLiteral("themeMode"), QStringLiteral("system"), 16, mode, error,
                    path + QStringLiteral(".themeMode")))
        return false;
    if (mode == QStringLiteral("system")) appearance.themeMode = AppearanceThemeMode::System;
    else if (mode == QStringLiteral("light")) appearance.themeMode = AppearanceThemeMode::Light;
    else if (mode == QStringLiteral("dark")) appearance.themeMode = AppearanceThemeMode::Dark;
    else return fail(error, QStringLiteral("appearanceConfig.themeMode 不受支持"));
    QString effect;
    if (!readString(object, QStringLiteral("windowEffect"), QStringLiteral("mica"), 16, effect, error,
                    path + QStringLiteral(".windowEffect")))
        return false;
    if (effect == QStringLiteral("normal")) appearance.windowEffect = AppearanceWindowEffect::Normal;
    else if (effect == QStringLiteral("elaMica")) appearance.windowEffect = AppearanceWindowEffect::ElaMica;
    else if (effect == QStringLiteral("mica")) appearance.windowEffect = AppearanceWindowEffect::Mica;
    else if (effect == QStringLiteral("micaAlt")) appearance.windowEffect = AppearanceWindowEffect::MicaAlt;
    else if (effect == QStringLiteral("acrylic")) appearance.windowEffect = AppearanceWindowEffect::Acrylic;
    else if (effect == QStringLiteral("dwmBlur")) appearance.windowEffect = AppearanceWindowEffect::DwmBlur;
    else return fail(error, QStringLiteral("appearanceConfig.windowEffect 不受支持"));
    return true;
}

bool parseDesktopCountdown(const QJsonValue& value, DesktopCountdownConfig& desktop, QString* error)
{
    desktop = DesktopCountdownConfig{};
    if (value.isUndefined()) return true;
    if (!value.isObject()) return fail(error, QStringLiteral("desktopCountdownConfig 必须为对象"));
    const QJsonObject object = value.toObject();
    const QString path = QStringLiteral("desktopCountdownConfig");
    if (!readBool(object, QStringLiteral("enable"), false, desktop.enable, error, path)) return false;
    if (!readBool(object, QStringLiteral("showMilliseconds"), true, desktop.showMilliseconds, error,
                  path + QStringLiteral(".showMilliseconds")))
        return false;
    if (!readBool(object, QStringLiteral("useLegacyPlacement"), true, desktop.useLegacyPlacement,
                  error, path + QStringLiteral(".useLegacyPlacement")))
        return false;
    QString target;
    if (!readString(object, QStringLiteral("targetDateTime"), QString(), 19, target, error,
                    path + QStringLiteral(".targetDateTime")))
        return false;
    if (!parseTarget(target, desktop.targetDateTime, error, path + QStringLiteral(".targetDateTime")))
        return false;
    if (desktop.enable && !desktop.targetDateTime.isValid())
        return fail(error, QStringLiteral("开启桌面倒计时时必须提供 targetDateTime"));
    if (!readString(object, QStringLiteral("labelTag"), QString(), 100, desktop.labelTag, error,
                    path + QStringLiteral(".labelTag")))
        return false;
    if (!readString(object, QStringLiteral("englishTag"), QString(), 100, desktop.englishTag, error,
                    path + QStringLiteral(".englishTag")))
        return false;
    if (!readInt(object, QStringLiteral("rightMargin"), 24, 0, DesktopCountdownConfig::MaximumMargin,
                 desktop.rightMargin, error, path + QStringLiteral(".rightMargin")))
        return false;
    if (!readInt(object, QStringLiteral("bottomMargin"), 24, 0,
                 DesktopCountdownConfig::MaximumMargin, desktop.bottomMargin, error,
                 path + QStringLiteral(".bottomMargin")))
        return false;
    if (!readInt(object, QStringLiteral("scalePercent"), 100, 50, 200, desktop.scalePercent, error,
                 path + QStringLiteral(".scalePercent")))
        return false;
    return true;
}
} // namespace

bool CloudDisplay::hasPluginComponent(const CourseBarConfig& config)
{
    for (const auto& component : config.components)
        if (component.type == CourseBarComponentType::Plugin) return true;
    return false;
}

bool CloudDisplay::buildConfig(const QJsonObject& payload, const Config& current, Config& result,
                               QString* error)
{
    if (error) error->clear();
    const QJsonValue version = payload.value(QStringLiteral("schema_version"));
    if (!version.isUndefined() && (!version.isDouble() || version.toInt() != 1))
        return fail(error, QStringLiteral("仅支持展示配置 schema_version 1"));
    const QJsonValue courseBarValue = payload.value(QStringLiteral("courseBarConfig"));
    if (!courseBarValue.isObject())
        return fail(error, QStringLiteral("展示配置缺少 courseBarConfig"));
    const QJsonObject courseBarObject = courseBarValue.toObject();
    CourseBarConfig bar;
    if (!readBool(courseBarObject, QStringLiteral("enable"), true, bar.enable, error,
                  QStringLiteral("courseBarConfig")))
        return false;
    if (!readInt(courseBarObject, QStringLiteral("height"), 49, 1, 1000, bar.height, error,
                 QStringLiteral("courseBarConfig.height")))
        return false;
    if (!readInt(courseBarObject, QStringLiteral("componentSpacing"), 14, 0,
                 CourseBarConfig::MaximumSpacing, bar.componentSpacing, error,
                 QStringLiteral("courseBarConfig.componentSpacing")))
        return false;
    const QJsonValue components = courseBarObject.value(QStringLiteral("components"));
    if (!components.isArray())
        return fail(error, QStringLiteral("courseBarConfig.components 必须为数组"));
    if (!parseComponents(components.toArray(), bar.components, error)) return false;

    AppearanceConfig appearance;
    if (!parseAppearance(payload.value(QStringLiteral("appearanceConfig")), appearance, error))
        return false;
    DesktopCountdownConfig desktop;
    if (!parseDesktopCountdown(payload.value(QStringLiteral("desktopCountdownConfig")), desktop,
                               error))
        return false;

    Config candidate = current;
    candidate.courseBarConfig.enable = bar.enable;
    candidate.courseBarConfig.height = bar.height;
    candidate.courseBarConfig.componentSpacing = bar.componentSpacing;
    candidate.courseBarConfig.components = bar.components;
    // courseBarConfig.uiAccessEnabled stays local.
    candidate.appearanceConfig = appearance;
    candidate.desktopCountdownConfig.enable = desktop.enable;
    candidate.desktopCountdownConfig.targetDateTime = desktop.targetDateTime;
    candidate.desktopCountdownConfig.labelTag = desktop.labelTag;
    candidate.desktopCountdownConfig.englishTag = desktop.englishTag;
    candidate.desktopCountdownConfig.showMilliseconds = desktop.showMilliseconds;
    candidate.desktopCountdownConfig.useLegacyPlacement = desktop.useLegacyPlacement;
    candidate.desktopCountdownConfig.rightMargin = desktop.rightMargin;
    candidate.desktopCountdownConfig.bottomMargin = desktop.bottomMargin;
    candidate.desktopCountdownConfig.scalePercent = desktop.scalePercent;
    // The cloud payload has no screen selection; keep following the local choice.
    candidate.desktopCountdownConfig.screenName = current.desktopCountdownConfig.screenName;
    result = std::move(candidate);
    return true;
}
