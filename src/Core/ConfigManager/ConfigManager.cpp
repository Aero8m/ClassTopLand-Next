#include "ConfigManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QRegularExpression>
#include <QSet>

#include <utility>

namespace {
constexpr int kFormatVersion = 2;

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

QString componentTypeText(CourseBarComponentType type)
{
    switch (type) {
    case CourseBarComponentType::Date: return QStringLiteral("date");
    case CourseBarComponentType::CourseView: return QStringLiteral("courseView");
    case CourseBarComponentType::TextTip: return QStringLiteral("textTip");
    case CourseBarComponentType::Countdown: return QStringLiteral("countdown");
    case CourseBarComponentType::Plugin: return QStringLiteral("plugin");
    }
    return {};
}

QString themeModeText(AppearanceThemeMode mode)
{
    switch (mode) {
    case AppearanceThemeMode::System: return QStringLiteral("system");
    case AppearanceThemeMode::Light: return QStringLiteral("light");
    case AppearanceThemeMode::Dark: return QStringLiteral("dark");
    }
    return {};
}

QString windowEffectText(AppearanceWindowEffect effect)
{
    switch (effect) {
    case AppearanceWindowEffect::Normal: return QStringLiteral("normal");
    case AppearanceWindowEffect::ElaMica: return QStringLiteral("elaMica");
    case AppearanceWindowEffect::Mica: return QStringLiteral("mica");
    case AppearanceWindowEffect::MicaAlt: return QStringLiteral("micaAlt");
    case AppearanceWindowEffect::Acrylic: return QStringLiteral("acrylic");
    case AppearanceWindowEffect::DwmBlur: return QStringLiteral("dwmBlur");
    }
    return {};
}

bool validate(const Config& config, QString* error)
{
    if (!Reminders::validate(config.reminderConfig, error)) return false;
    const auto& countdown = config.desktopCountdownConfig;
    if (countdown.enable && !countdown.targetDateTime.isValid())
        return fail(error, QStringLiteral("config.desktopCountdownConfig.targetDateTime must be valid when enabled"));
    if (countdown.scalePercent < 50 || countdown.scalePercent > 200)
        return fail(error, QStringLiteral("config.desktopCountdownConfig.scalePercent must be between 50 and 200"));
    if (countdown.rightMargin < 0 || countdown.bottomMargin < 0 ||
        countdown.rightMargin > DesktopCountdownConfig::MaximumMargin ||
        countdown.bottomMargin > DesktopCountdownConfig::MaximumMargin)
        return fail(error, QStringLiteral("config.desktopCountdownConfig margins must be between 0 and 10000"));
    for (qsizetype i = 0; i < config.windowInterceptionConfig.rules.size(); ++i) {
        const auto& rule = config.windowInterceptionConfig.rules.at(i);
        if (rule.exePath.trimmed().isEmpty() || rule.className.trimmed().isEmpty())
            return fail(error, QStringLiteral("config.windowInterceptionConfig.rules[%1]: exePath and className must not be empty").arg(i));
    }
    static const QRegularExpression hexColor(QStringLiteral("\\A#[0-9a-fA-F]{6}\\z"));
    if (!hexColor.match(config.appearanceConfig.themeColor).hasMatch())
        return fail(error, QStringLiteral("config.appearanceConfig.themeColor must be a #rrggbb hex color"));
    if (themeModeText(config.appearanceConfig.themeMode).isEmpty())
        return fail(error, QStringLiteral("config.appearanceConfig.themeMode must be system, light or dark"));
    if (windowEffectText(config.appearanceConfig.windowEffect).isEmpty())
        return fail(error, QStringLiteral("config.appearanceConfig.windowEffect must be normal, elaMica, mica, micaAlt, acrylic or dwmBlur"));
    if (config.courseBarConfig.height <= 0)
        return fail(error, QStringLiteral("config.courseBarConfig.height must be a positive integer"));
    if (config.courseBarConfig.componentSpacing < 0 || config.courseBarConfig.componentSpacing > CourseBarConfig::MaximumSpacing)
        return fail(error, QStringLiteral("config.courseBarConfig.componentSpacing must be an integer between 0 and %1")
            .arg(CourseBarConfig::MaximumSpacing));
    QSet<QString> instanceIds;
    for (qsizetype i = 0; i < config.courseBarConfig.components.size(); ++i) {
        const auto& instance = config.courseBarConfig.components.at(i);
        if (instance.instanceId.isEmpty() || instanceIds.contains(instance.instanceId))
            return fail(error, QStringLiteral("Duplicate or empty component instanceId"));
        instanceIds.insert(instance.instanceId);
        if (instance.type == CourseBarComponentType::Plugin && (instance.typeId.isEmpty() || instance.typeId.startsWith(QStringLiteral("builtin/"))))
            return fail(error, QStringLiteral("Invalid plugin component typeId"));
        if (componentTypeText(config.courseBarConfig.components.at(i).type).isEmpty())
            return fail(error, QStringLiteral("config.courseBarConfig.components[%1].type is invalid").arg(i));
        const auto& component = config.courseBarConfig.components.at(i);
        if (component.type == CourseBarComponentType::CourseView &&
            (component.courseSpacing < 0 || component.courseSpacing > CourseBarConfig::MaximumSpacing))
            return fail(error, QStringLiteral("config.courseBarConfig.components[%1].courseSpacing must be an integer between 0 and %2")
                .arg(i).arg(CourseBarConfig::MaximumSpacing));
    }
    return true;
}

QJsonObject toJson(const Config& config)
{
    QJsonArray rules;
    for (const auto& rule : config.windowInterceptionConfig.rules) {
        rules.append(QJsonObject{{QStringLiteral("exePath"), normalizedWindowPath(rule.exePath)},
                                 {QStringLiteral("className"), rule.className},
                                 {QStringLiteral("titleContains"), rule.titleContains},
                                 {QStringLiteral("enabled"), rule.enabled}});
    }
    QJsonArray components;
    for (const CourseBarComponentConfig& component : config.courseBarConfig.components) {
        QJsonObject item = component.extra;
        item.insert(QStringLiteral("type"), componentTypeText(component.type));
        item.insert(QStringLiteral("typeId"), component.type == CourseBarComponentType::Plugin ? component.typeId : QStringLiteral("builtin/") + componentTypeText(component.type));
        item.insert(QStringLiteral("instanceId"), component.instanceId);
        item.insert(QStringLiteral("enabled"), component.enabled);
        item.insert(QStringLiteral("settings"), component.settings);
        if (component.type == CourseBarComponentType::TextTip)
            item.insert(QStringLiteral("text"), component.text);
        if (component.type == CourseBarComponentType::CourseView)
            item.insert(QStringLiteral("courseSpacing"), component.courseSpacing);
        components.append(item);
    }
    const QJsonObject courseBar{{QStringLiteral("enable"), config.courseBarConfig.enable},
                                {QStringLiteral("uiAccessEnabled"), config.courseBarConfig.uiAccessEnabled},
                                {QStringLiteral("height"), config.courseBarConfig.height},
                                {QStringLiteral("componentSpacing"), config.courseBarConfig.componentSpacing},
                                {QStringLiteral("components"), components}};
    const QJsonObject appearance{{QStringLiteral("themeColor"), config.appearanceConfig.themeColor.toLower()},
                                 {QStringLiteral("themeMode"), themeModeText(config.appearanceConfig.themeMode)},
                                 {QStringLiteral("windowEffect"), windowEffectText(config.appearanceConfig.windowEffect)}};
    const auto& desktop = config.desktopCountdownConfig;
    const QJsonObject desktopCountdown{
        {QStringLiteral("enable"), desktop.enable},
        {QStringLiteral("targetDateTime"), desktop.targetDateTime.isValid()
            ? desktop.targetDateTime.toLocalTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")) : QString()},
        {QStringLiteral("labelTag"), desktop.labelTag},
        {QStringLiteral("englishTag"), desktop.englishTag},
        {QStringLiteral("showMilliseconds"), desktop.showMilliseconds},
        {QStringLiteral("screenName"), desktop.screenName},
        {QStringLiteral("useLegacyPlacement"), desktop.useLegacyPlacement},
        {QStringLiteral("rightMargin"), desktop.rightMargin},
        {QStringLiteral("bottomMargin"), desktop.bottomMargin},
        {QStringLiteral("scalePercent"), desktop.scalePercent}};
    QJsonObject plugins;
    for (auto it = config.pluginConfig.plugins.cbegin(); it != config.pluginConfig.plugins.cend(); ++it)
        plugins.insert(it.key(), QJsonObject{{QStringLiteral("enabled"), it->enabled}, {QStringLiteral("grantedPermissions"), it->grantedPermissions},
            {QStringLiteral("approvedVersion"), it->approvedVersion}, {QStringLiteral("fault"), it->fault}});
    return {{QStringLiteral("version"), kFormatVersion},
            {QStringLiteral("config"), QJsonObject{{QStringLiteral("profileName"), config.profileName},
                                                   {QStringLiteral("reminderConfig"), Reminders::toJson(config.reminderConfig)},
                                                   {QStringLiteral("pluginConfig"), QJsonObject{{QStringLiteral("plugins"), plugins}, {QStringLiteral("recoveryNextStart"), config.pluginConfig.recoveryNextStart}}},
                                                   {QStringLiteral("desktopCountdownConfig"), desktopCountdown},
                                                   {QStringLiteral("courseBarConfig"), courseBar},
                                                   {QStringLiteral("appearanceConfig"), appearance},
                                                   {QStringLiteral("windowInterceptionConfig"),
                                                    QJsonObject{{QStringLiteral("rules"), rules}}}}}};
}

bool fromJson(const QJsonObject& root, Config& config, QString* error)
{
    const int version = root.value(QStringLiteral("version")).toInt(-1);
    if (version != 1 && version != kFormatVersion)
        return fail(error, QStringLiteral("Unsupported config format version"));
    const QJsonValue configValue = root.value(QStringLiteral("config"));
    if (!configValue.isObject()) return fail(error, QStringLiteral("config must be an object"));
    const QJsonObject data = configValue.toObject();
    if (!Reminders::fromJson(data.value(QStringLiteral("reminderConfig")), config.reminderConfig, error)) return false;
    const QJsonValue profileName = data.value(QStringLiteral("profileName"));
    if (!profileName.isString()) return fail(error, QStringLiteral("config.profileName must be a string"));
    config.profileName = profileName.toString();
    const auto pluginsValue = data.value(QStringLiteral("pluginConfig"));
    if (!pluginsValue.isUndefined()) {
        if (!pluginsValue.isObject()) return fail(error, QStringLiteral("pluginConfig must be an object"));
        const auto object = pluginsValue.toObject();
        const auto recovery = object.value(QStringLiteral("recoveryNextStart"));
        if (!recovery.isUndefined() && !recovery.isBool()) return fail(error, QStringLiteral("recoveryNextStart must be boolean"));
        config.pluginConfig.recoveryNextStart = recovery.toBool();
        const auto entries = object.value(QStringLiteral("plugins"));
        if (!entries.isUndefined() && !entries.isObject()) return fail(error, QStringLiteral("plugins must be an object"));
        const auto plugins = entries.toObject();
        for (auto it = plugins.begin(); it != plugins.end(); ++it) {
            if (!it.value().isObject()) return fail(error, QStringLiteral("Invalid plugin preference"));
            const auto value = it.value().toObject();
            if (!value.value(QStringLiteral("enabled")).isBool() || !value.value(QStringLiteral("grantedPermissions")).isObject() ||
                !value.value(QStringLiteral("approvedVersion")).isString() || !value.value(QStringLiteral("fault")).isString())
                return fail(error, QStringLiteral("Invalid plugin preference fields"));
            config.pluginConfig.plugins.insert(it.key(), {value.value(QStringLiteral("enabled")).toBool(), value.value(QStringLiteral("grantedPermissions")).toObject(),
                value.value(QStringLiteral("approvedVersion")).toString(), value.value(QStringLiteral("fault")).toString()});
        }
    }

    const auto desktopValue = data.value(QStringLiteral("desktopCountdownConfig"));
    if (!desktopValue.isUndefined()) {
        const QString path = QStringLiteral("config.desktopCountdownConfig");
        if (!desktopValue.isObject()) return fail(error, path + QStringLiteral(" must be an object"));
        const auto desktop = desktopValue.toObject();
        auto& countdown = config.desktopCountdownConfig;
        const auto readBool = [&](const QString& key, bool& value) {
            const auto item = desktop.value(key);
            if (item.isUndefined()) return true;
            if (!item.isBool()) return fail(error, path + '.' + key + QStringLiteral(" must be a boolean"));
            value = item.toBool(); return true;
        };
        const auto readString = [&](const QString& key, QString& value) {
            const auto item = desktop.value(key);
            if (item.isUndefined()) return true;
            if (!item.isString()) return fail(error, path + '.' + key + QStringLiteral(" must be a string"));
            value = item.toString(); return true;
        };
        const auto readInt = [&](const QString& key, int& value, int minimum, int maximum) {
            const auto item = desktop.value(key);
            if (item.isUndefined()) return true;
            if (!item.isDouble() || item.toDouble() < minimum || item.toDouble() > maximum ||
                item.toDouble() != item.toInt())
                return fail(error, path + '.' + key + QStringLiteral(" must be an integer between %1 and %2").arg(minimum).arg(maximum));
            value = item.toInt(); return true;
        };
        QString target;
        if (!readBool(QStringLiteral("enable"), countdown.enable) ||
            !readBool(QStringLiteral("showMilliseconds"), countdown.showMilliseconds) ||
            !readBool(QStringLiteral("useLegacyPlacement"), countdown.useLegacyPlacement) ||
            !readString(QStringLiteral("labelTag"), countdown.labelTag) ||
            !readString(QStringLiteral("englishTag"), countdown.englishTag) ||
            !readString(QStringLiteral("screenName"), countdown.screenName) ||
            !readString(QStringLiteral("targetDateTime"), target) ||
            !readInt(QStringLiteral("rightMargin"), countdown.rightMargin, 0, DesktopCountdownConfig::MaximumMargin) ||
            !readInt(QStringLiteral("bottomMargin"), countdown.bottomMargin, 0, DesktopCountdownConfig::MaximumMargin) ||
            !readInt(QStringLiteral("scalePercent"), countdown.scalePercent, 50, 200)) return false;
        if (!target.isEmpty()) {
            countdown.targetDateTime = QDateTime::fromString(target, QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
            if (!countdown.targetDateTime.isValid() ||
                countdown.targetDateTime.toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")) != target)
                return fail(error, path + QStringLiteral(".targetDateTime must use yyyy-MM-ddTHH:mm:ss local time"));
        }
    }

    const auto interception = data.value(QStringLiteral("windowInterceptionConfig"));
    if (!interception.isUndefined()) {
        if (!interception.isObject())
            return fail(error, QStringLiteral("config.windowInterceptionConfig must be an object"));
        const auto rules = interception.toObject().value(QStringLiteral("rules"));
        if (!rules.isUndefined() && !rules.isArray())
            return fail(error, QStringLiteral("config.windowInterceptionConfig.rules must be an array"));
        for (const auto& value : rules.toArray()) {
            const QString path = QStringLiteral("config.windowInterceptionConfig.rules[%1]")
                                     .arg(config.windowInterceptionConfig.rules.size());
            if (!value.isObject()) return fail(error, path + QStringLiteral(" must be an object"));
            const auto item = value.toObject();
            const auto exe = item.value(QStringLiteral("exePath"));
            const auto cls = item.value(QStringLiteral("className"));
            const auto title = item.value(QStringLiteral("titleContains"));
            const auto enabled = item.value(QStringLiteral("enabled"));
            if (!exe.isString() || exe.toString().trimmed().isEmpty())
                return fail(error, path + QStringLiteral(".exePath must be a non-empty string"));
            if (!cls.isString() || cls.toString().trimmed().isEmpty())
                return fail(error, path + QStringLiteral(".className must be a non-empty string"));
            if (!title.isUndefined() && !title.isString())
                return fail(error, path + QStringLiteral(".titleContains must be a string"));
            if (!enabled.isUndefined() && !enabled.isBool())
                return fail(error, path + QStringLiteral(".enabled must be a boolean"));
            config.windowInterceptionConfig.rules.append({normalizedWindowPath(exe.toString()), cls.toString(),
                                                          title.toString(), enabled.toBool(true)});
        }
    }

    const QJsonValue appearanceValue = data.value(QStringLiteral("appearanceConfig"));
    if (!appearanceValue.isUndefined()) {
        if (!appearanceValue.isObject())
            return fail(error, QStringLiteral("config.appearanceConfig must be an object"));
        const QJsonObject appearance = appearanceValue.toObject();
        const QJsonValue themeColor = appearance.value(QStringLiteral("themeColor"));
        if (!themeColor.isUndefined()) {
            if (!themeColor.isString())
                return fail(error, QStringLiteral("config.appearanceConfig.themeColor must be a string"));
            config.appearanceConfig.themeColor = themeColor.toString().toLower();
        }
        const QJsonValue themeMode = appearance.value(QStringLiteral("themeMode"));
        if (!themeMode.isUndefined()) {
            if (!themeMode.isString())
                return fail(error, QStringLiteral("config.appearanceConfig.themeMode must be a string"));
            if (themeMode.toString() == QStringLiteral("system"))
                config.appearanceConfig.themeMode = AppearanceThemeMode::System;
            else if (themeMode.toString() == QStringLiteral("light"))
                config.appearanceConfig.themeMode = AppearanceThemeMode::Light;
            else if (themeMode.toString() == QStringLiteral("dark"))
                config.appearanceConfig.themeMode = AppearanceThemeMode::Dark;
            else
                return fail(error, QStringLiteral("config.appearanceConfig.themeMode must be system, light or dark"));
        }
        const QJsonValue windowEffect = appearance.value(QStringLiteral("windowEffect"));
        if (!windowEffect.isUndefined()) {
            if (!windowEffect.isString())
                return fail(error, QStringLiteral("config.appearanceConfig.windowEffect must be a string"));
            bool found = false;
            for (const auto effect : {AppearanceWindowEffect::Normal, AppearanceWindowEffect::ElaMica,
                                      AppearanceWindowEffect::Mica, AppearanceWindowEffect::MicaAlt,
                                      AppearanceWindowEffect::Acrylic, AppearanceWindowEffect::DwmBlur}) {
                if (windowEffect.toString() == windowEffectText(effect)) {
                    config.appearanceConfig.windowEffect = effect;
                    found = true;
                    break;
                }
            }
            if (!found)
                return fail(error, QStringLiteral("config.appearanceConfig.windowEffect must be normal, elaMica, mica, micaAlt, acrylic or dwmBlur"));
        }
    }

    // Version 1 configs written before course bar settings were added use model defaults.
    const QJsonValue courseBarValue = data.value(QStringLiteral("courseBarConfig"));
    if (!courseBarValue.isUndefined() && !courseBarValue.isObject())
        return fail(error, QStringLiteral("config.courseBarConfig must be an object"));
    const QJsonObject courseBar = courseBarValue.toObject();
    const QJsonValue uiAccess = courseBar.value(QStringLiteral("uiAccessEnabled"));
    if (!uiAccess.isUndefined()) {
        if (!uiAccess.isBool())
            return fail(error, QStringLiteral("config.courseBarConfig.uiAccessEnabled must be a boolean"));
        config.courseBarConfig.uiAccessEnabled = uiAccess.toBool();
    }
    const QJsonValue enable = courseBar.value(QStringLiteral("enable"));
    if (!enable.isUndefined()) {
        if (!enable.isBool())
            return fail(error, QStringLiteral("config.courseBarConfig.enable must be a boolean"));
        config.courseBarConfig.enable = enable.toBool();
    }
    const QJsonValue height = courseBar.value(QStringLiteral("height"));
    if (!height.isUndefined()) {
        if (!height.isDouble() || height.toInt(-1) <= 0)
            return fail(error, QStringLiteral("config.courseBarConfig.height must be a positive integer"));
        config.courseBarConfig.height = height.toInt();
    }

    int legacyCourseSpacing = CourseBarConfig::DefaultSpacing;
    const QJsonValue spacing = courseBar.value(QStringLiteral("spacing"));
    if (!spacing.isUndefined()) {
        if (!spacing.isDouble() || spacing.toInt(-1) < 0 ||
            spacing.toInt(-1) > CourseBarConfig::MaximumSpacing || spacing.toDouble() != spacing.toInt(-1))
            return fail(error, QStringLiteral("config.courseBarConfig.spacing must be an integer between 0 and %1")
                .arg(CourseBarConfig::MaximumSpacing));
        legacyCourseSpacing = spacing.toInt();
    }
    // Migrate the former shared gap, including files using default components.
    for (auto& component : config.courseBarConfig.components)
        if (component.type == CourseBarComponentType::CourseView) component.courseSpacing = legacyCourseSpacing;

    const QJsonValue componentSpacing = courseBar.value(QStringLiteral("componentSpacing"));
    if (!componentSpacing.isUndefined()) {
        if (!componentSpacing.isDouble() || componentSpacing.toInt(-1) < 0 ||
            componentSpacing.toInt(-1) > CourseBarConfig::MaximumSpacing ||
            componentSpacing.toDouble() != componentSpacing.toInt(-1))
            return fail(error, QStringLiteral("config.courseBarConfig.componentSpacing must be an integer between 0 and %1")
                .arg(CourseBarConfig::MaximumSpacing));
        config.courseBarConfig.componentSpacing = componentSpacing.toInt();
    }

    const QJsonValue componentsValue = courseBar.value(QStringLiteral("components"));
    if (!componentsValue.isUndefined()) {
        if (!componentsValue.isArray())
            return fail(error, QStringLiteral("config.courseBarConfig.components must be an array"));
        const QJsonArray components = componentsValue.toArray();
        config.courseBarConfig.components.clear();
        for (qsizetype i = 0; i < components.size(); ++i) {
            const QString path = QStringLiteral("config.courseBarConfig.components[%1]").arg(i);
            if (!components.at(i).isObject())
                return fail(error, path + QStringLiteral(" must be an object"));
            const QJsonObject item = components.at(i).toObject();
            const QJsonValue type = version == 1 ? item.value(QStringLiteral("type")) : QJsonValue(item.value(QStringLiteral("typeId")).toString().startsWith(QStringLiteral("builtin/")) ? item.value(QStringLiteral("typeId")).toString().mid(8) : QStringLiteral("plugin"));
            if (!type.isString()) return fail(error, path + QStringLiteral(".type must be a string"));
            CourseBarComponentType parsedType;
            if (type.toString() == QStringLiteral("date")) parsedType = CourseBarComponentType::Date;
            else if (type.toString() == QStringLiteral("courseView")) parsedType = CourseBarComponentType::CourseView;
            else if (type.toString() == QStringLiteral("textTip")) parsedType = CourseBarComponentType::TextTip;
            else if (type.toString() == QStringLiteral("countdown")) parsedType = CourseBarComponentType::Countdown;
            else if (version == 2) parsedType = CourseBarComponentType::Plugin;
            else return fail(error, path + QStringLiteral(".type is invalid"));
            CourseBarComponentConfig component{parsedType};
            if (version == 2) {
                if (!item.value(QStringLiteral("typeId")).isString() || !item.value(QStringLiteral("instanceId")).isString() || !item.value(QStringLiteral("settings")).isObject())
                    return fail(error, path + QStringLiteral(" requires typeId, instanceId and settings"));
                component.typeId = item.value(QStringLiteral("typeId")).toString();
                component.instanceId = item.value(QStringLiteral("instanceId")).toString();
                component.settings = item.value(QStringLiteral("settings")).toObject();
                component.extra = item;
            }
            if (parsedType == CourseBarComponentType::CourseView) {
                const QJsonValue courseSpacing = item.value(QStringLiteral("courseSpacing"));
                component.courseSpacing = legacyCourseSpacing;
                if (!courseSpacing.isUndefined()) {
                    if (!courseSpacing.isDouble() || courseSpacing.toInt(-1) < 0 ||
                        courseSpacing.toInt(-1) > CourseBarConfig::MaximumSpacing ||
                        courseSpacing.toDouble() != courseSpacing.toInt(-1))
                        return fail(error, path + QStringLiteral(".courseSpacing must be an integer between 0 and %1")
                            .arg(CourseBarConfig::MaximumSpacing));
                    component.courseSpacing = courseSpacing.toInt();
                }
            }
            const QJsonValue enabled = item.value(QStringLiteral("enabled"));
            if (!enabled.isUndefined()) {
                if (!enabled.isBool()) return fail(error, path + QStringLiteral(".enabled must be a boolean"));
                component.enabled = enabled.toBool();
            }
            if (parsedType == CourseBarComponentType::TextTip) {
                const QJsonValue text = item.value(QStringLiteral("text"));
                if (!text.isUndefined()) {
                    if (!text.isString()) return fail(error, path + QStringLiteral(".text must be a string"));
                    component.text = text.toString();
                }
            }
            config.courseBarConfig.components.append(component);
        }
    }
    return validate(config, error);
}
} // namespace

ConfigManager& ConfigManager::instance()
{
    static ConfigManager manager;
    return manager;
}

bool ConfigManager::load(const QString& filePath, QString* error)
{
    if (error) error->clear();
    if (filePath.trimmed().isEmpty()) return fail(error, QStringLiteral("Config file path is empty"));
    const QString path = QFileInfo(filePath).absoluteFilePath();
    if (!QFileInfo::exists(path)) {
        config_ = Config{};
        filePath_ = path;
        emit committed(++revision_);
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    const QByteArray contents = file.readAll();
    if (file.error() != QFileDevice::NoError) return fail(error, file.errorString());
    file.close(); // Windows must release the read handle before atomic v1 migration.
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(error, QStringLiteral("Invalid config JSON: %1").arg(parseError.errorString()));
    if (!document.isObject()) return fail(error, QStringLiteral("Config JSON root must be an object"));

    Config loaded;
    if (!fromJson(document.object(), loaded, error)) return false;
    if (document.object().value(QStringLiteral("version")).toInt() == 1) {
        const auto previousPath = filePath_;
        filePath_ = path;
        if (!writeConfig(loaded, error)) { filePath_ = previousPath; return false; }
    }
    config_ = std::move(loaded);
    filePath_ = path;
    emit committed(++revision_);
    return true;
}

bool ConfigManager::save(QString* error) const
{
    return writeConfig(config_, error);
}

bool ConfigManager::validateCandidate(const Config& candidate, QString* error)
{
    if (error) error->clear();
    return validate(candidate, error);
}

bool ConfigManager::commit(const Config& candidate, QString* error)
{
    Config committed = candidate;
    for (auto& rule : committed.windowInterceptionConfig.rules)
        if (!rule.exePath.trimmed().isEmpty()) rule.exePath = normalizedWindowPath(rule.exePath);
    committed.appearanceConfig.themeColor = committed.appearanceConfig.themeColor.toLower();
    if (!writeConfig(committed, error)) return false;
    const bool courseBarChanged = toJson(config_).value(QStringLiteral("config")).toObject().value(QStringLiteral("courseBarConfig")) != toJson(committed).value(QStringLiteral("config")).toObject().value(QStringLiteral("courseBarConfig"));
    config_ = std::move(committed);
    emit this->committed(++revision_);
    if (courseBarChanged) emit this->courseBarChanged();
    return true;
}

bool ConfigManager::writeConfig(const Config& candidate, QString* error) const
{
    if (error) error->clear();
    if (filePath_.isEmpty()) return fail(error, QStringLiteral("No config file path has been loaded"));
    if (!validate(candidate, error)) return false;
    if (!QDir().mkpath(QFileInfo(filePath_).absolutePath()))
        return fail(error, QStringLiteral("Cannot create config directory"));

    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly)) return fail(error, file.errorString());
    const QByteArray contents = QJsonDocument(toJson(candidate)).toJson(QJsonDocument::Indented);
    if (file.write(contents) != contents.size()) return fail(error, file.errorString());
    if (!file.commit()) return fail(error, file.errorString());
    return true;
}
