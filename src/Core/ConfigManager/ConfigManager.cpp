#include "ConfigManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

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
    }
    return {};
}

bool validate(const Config& config, QString* error)
{
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
        components.append(QJsonObject{{QStringLiteral("type"), componentTypeText(component.type)},
                                      {QStringLiteral("enabled"), component.enabled}});
    }
    const QJsonObject courseBar{{QStringLiteral("height"), config.courseBarConfig.height},
                                {QStringLiteral("components"), components}};
    return {{QStringLiteral("version"), kFormatVersion},
            {QStringLiteral("config"), QJsonObject{{QStringLiteral("profileName"), config.profileName},
                                                   {QStringLiteral("courseBarConfig"), courseBar}}}};
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

    // Version 1 configs written before course bar settings were added use model defaults.
    const QJsonValue courseBarValue = data.value(QStringLiteral("courseBarConfig"));
    if (courseBarValue.isUndefined()) return true;
    if (!courseBarValue.isObject())
        return fail(error, QStringLiteral("config.courseBarConfig must be an object"));
    const QJsonObject courseBar = courseBarValue.toObject();
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
            else return fail(error, path + QStringLiteral(".type is invalid"));
            CourseBarComponentConfig component{parsedType};
            const QJsonValue enabled = item.value(QStringLiteral("enabled"));
            if (!enabled.isUndefined()) {
                if (!enabled.isBool()) return fail(error, path + QStringLiteral(".enabled must be a boolean"));
                component.enabled = enabled.toBool();
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
