#pragma once

#include "../Model/Config.h"

#include <QJsonObject>
#include <QString>

// Maps the server's display_config payload onto a local Config candidate. Only
// the whitelisted fields are replaced; device-local settings such as UI access,
// plugins, window interception, reminders and the countdown screen stay as-is.
namespace CloudDisplay {

bool hasPluginComponent(const CourseBarConfig& config);

// Failure leaves result untouched. The caller still runs
// ConfigManager::validateCandidate before committing.
bool buildConfig(const QJsonObject& payload, const Config& current, Config& result,
                 QString* error = nullptr);

} // namespace CloudDisplay
