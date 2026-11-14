#pragma once
#include <QMap>
#include <QJsonObject>
#include <QString>

struct PluginPreference
{
    bool enabled = false;
    QJsonObject grantedPermissions;
    QString approvedVersion;
    QString fault;
    QString activeRevision;
    QString sourceName;
};
struct PluginConfig
{
    QMap<QString, PluginPreference> plugins;
    bool recoveryNextStart = false;
};
