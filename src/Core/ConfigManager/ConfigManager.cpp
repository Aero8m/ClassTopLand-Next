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

#include <utility>

namespace {
constexpr int kFormatVersion = 1;

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

bool validate(const Config& config, QString* error)
{
    static const QRegularExpression hexColor(QStringLiteral("\\A#[0-9a-fA-F]{6}\\z"));
    if (!hexColor.match(config.appearanceConfig.themeColor).hasMatch())
        return fail(error, QStringLiteral("config.appearanceConfig.themeColor must be a #rrggbb hex color"));
    if (themeModeText(config.appearanceConfig.themeMode).isEmpty())
        return fail(error, QStringLiteral("config.appearanceConfig.themeMode must be system, light or dark"));
    if (config.courseBarConfig.height <= 0)
        return fail(error, QStringLiteral("config.courseBarConfig.height must be a positive integer"));
    for (qsizetype i = 0; i < config.courseBarConfig.components.size(); ++i) {
        if (componentTypeText(config.courseBarConfig.components.at(i).type).isEmpty())
            return fail(error, QStringLiteral("config.courseBarConfig.components[%1].type is invalid").arg(i));
    }
    return true;
}

QJsonObject toJson(const Config& config)
{
    QJsonArray components;
    for (const CourseBarComponentConfig& component : config.courseBarConfig.components) {
        QJsonObject item{{QStringLiteral("type"), componentTypeText(component.type)},
                         {QStringLiteral("enabled"), component.enabled}};
        if (component.type == CourseBarComponentType::TextTip)
            item.insert(QStringLiteral("text"), component.text);
        components.append(item);
    }
    const QJsonObject courseBar{{QStringLiteral("enable"), config.courseBarConfig.enable},
                                {QStringLiteral("uiAccessEnabled"), config.courseBarConfig.uiAccessEnabled},
                                {QStringLiteral("height"), config.courseBarConfig.height},
                                {QStringLiteral("components"), components}};
    const QJsonObject appearance{{QStringLiteral("themeColor"), config.appearanceConfig.themeColor.toLower()},
                                 {QStringLiteral("themeMode"), themeModeText(config.appearanceConfig.themeMode)}};
    return {{QStringLiteral("version"), kFormatVersion},
            {QStringLiteral("config"), QJsonObject{{QStringLiteral("profileName"), config.profileName},
                                                   {QStringLiteral("courseBarConfig"), courseBar},
                                                   {QStringLiteral("appearanceConfig"), appearance}}}};
}

bool fromJson(const QJsonObject& root, Config& config, QString* error)
{
    if (root.value(QStringLiteral("version")).toInt(-1) != kFormatVersion)
        return fail(error, QStringLiteral("Unsupported config format version"));
    const QJsonValue configValue = root.value(QStringLiteral("config"));
    if (!configValue.isObject()) return fail(error, QStringLiteral("config must be an object"));
    const QJsonObject data = configValue.toObject();
    const QJsonValue profileName = data.value(QStringLiteral("profileName"));
    if (!profileName.isString()) return fail(error, QStringLiteral("config.profileName must be a string"));
    config.profileName = profileName.toString();

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
            const QJsonValue type = item.value(QStringLiteral("type"));
            if (!type.isString()) return fail(error, path + QStringLiteral(".type must be a string"));
            // Retired version-1 component: omit it from the next normal save.
            if (type.toString() == QStringLiteral("countdown")) {
                const QJsonValue enabled = item.value(QStringLiteral("enabled"));
                if (!enabled.isUndefined() && !enabled.isBool())
                    return fail(error, path + QStringLiteral(".enabled must be a boolean"));
                continue;
            }
            CourseBarComponentType parsedType;
            if (type.toString() == QStringLiteral("date")) parsedType = CourseBarComponentType::Date;
            else if (type.toString() == QStringLiteral("courseView")) parsedType = CourseBarComponentType::CourseView;
            else if (type.toString() == QStringLiteral("textTip")) parsedType = CourseBarComponentType::TextTip;
            else return fail(error, path + QStringLiteral(".type is invalid"));
            CourseBarComponentConfig component{parsedType};
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
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    const QByteArray contents = file.readAll();
    if (file.error() != QFileDevice::NoError) return fail(error, file.errorString());
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(error, QStringLiteral("Invalid config JSON: %1").arg(parseError.errorString()));
    if (!document.isObject()) return fail(error, QStringLiteral("Config JSON root must be an object"));

    Config loaded;
    if (!fromJson(document.object(), loaded, error)) return false;
    config_ = std::move(loaded);
    filePath_ = path;
    return true;
}

bool ConfigManager::save(QString* error) const
{
    return writeConfig(config_, error);
}

bool ConfigManager::commit(const Config& candidate, QString* error)
{
    Config committed = candidate;
    committed.appearanceConfig.themeColor = committed.appearanceConfig.themeColor.toLower();
    if (!writeConfig(committed, error)) return false;
    config_ = std::move(committed);
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
