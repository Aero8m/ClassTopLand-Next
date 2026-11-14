#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QString>

namespace Plugins
{
QJsonObject widgetCatalog();
QString validateUi(const QString &path, const QString &package);
QString validateSchema(const QJsonObject &schema, int depth = 0);
QString validateJson(const QJsonValue &value, const QJsonObject &schema, int depth = 0);
QString contributionRegistrationError(const QJsonObject &declared, const QMap<QString, QJsonObject> &pages,
                                      const QMap<QString, QJsonObject> &components,
                                      const QMap<QString, QJsonObject> &actions);
} // namespace Plugins
