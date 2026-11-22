#pragma once

#include <QJsonObject>

struct Profile;

namespace CloudOperations {
QString buildId();
QString activeProfileId();
QJsonObject telemetry(bool cloudProfileActive);
bool matches(const Profile& profile, const QJsonArray& changes, bool after);
bool prepare(Profile& candidate, const QJsonObject& task, QString* error);
}
