#include "PluginActionRegistry.h"
#include "../ConfigManager/ConfigManager.h"
#include "PluginContracts.h"
#include "PluginManager.h"
#include <QJsonDocument>
#include <QTimer>
#include <QUuid>

PluginActionRegistry::PluginActionRegistry(PluginManager *manager) : QObject(manager), manager_(manager)
{
}
QJsonArray PluginActionRegistry::list(const QString &plugin) const
{
    QJsonArray result;
    for (const auto &record : manager_->records())
    {
        if (record->state != QStringLiteral("运行中") || (!plugin.isEmpty() && plugin != record->manifest.id))
            continue;
        for (auto action : record->actions)
        {
            action.insert(QStringLiteral("plugin"), record->manifest.id);
            result.append(action);
        }
    }
    return result;
}
QString PluginActionRegistry::invoke(const QString &plugin, const QString &action,
                                     const QJsonValue &arguments, QObject *context, Completion completion,
                                     const QString &origin)
{
    const auto record = manager_->record(plugin);
    auto fail = [&](const QString &code, const QString &message) {
        completion(Plugins::error(code, message), {});
        return QString();
    };
    if (!manager_->acceptingWork())
        return fail(QStringLiteral("CANCELED"), QStringLiteral("插件系统正在收尾或发布"));
    if (!record || record->state != QStringLiteral("运行中") || !record->actions.contains(action))
        return fail(QStringLiteral("NOT_FOUND"), QStringLiteral("动作不可用"));
    if (pending_.size() >= 64)
        return fail(QStringLiteral("RATE_LIMITED"), QStringLiteral("动作请求过多"));
    const auto definition = record->actions.value(action);
    const auto permissions =
        ConfigManager::instance().config().pluginConfig.plugins.value(plugin).grantedPermissions;
    for (const auto &permission : definition.value(QStringLiteral("requiredPermissions")).toArray())
        if (!permissions.contains(permission.toString()) ||
            permissions.value(permission.toString()) !=
                record->manifest.permissions.value(permission.toString()))
            return fail(QStringLiteral("PERMISSION_DENIED"), QStringLiteral("动作所需权限未授予"));
    const auto validation =
        Plugins::validateJson(arguments, definition.value(QStringLiteral("inputSchema")).toObject());
    if (!validation.isEmpty())
        return fail(QStringLiteral("INVALID_ARGUMENT"), validation);
    if (QJsonDocument(QJsonArray{arguments}).toJson(QJsonDocument::Compact).size() > Plugins::MaxMessageBytes)
        return fail(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("动作输入过大"));
    const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    pending_.insert(token, {plugin, action, record->generation,
                            definition.value(QStringLiteral("outputSchema")).toObject(), context,
                            std::move(completion)});
    pending_[token].destruction =
        connect(context, &QObject::destroyed, this, [this, token] { cancel(token); });
    const int timeout = qBound(100, definition.value(QStringLiteral("timeoutMs")).toInt(30000), 60000);
    QTimer::singleShot(timeout, this, [this, token] { cancel(token, QStringLiteral("TIMEOUT")); });
    emit dispatch(plugin, QStringLiteral("actions.invoke"),
                  {{QStringLiteral("requestId"), token},
                   {QStringLiteral("actionId"), action},
                   {QStringLiteral("arguments"), arguments},
                   {QStringLiteral("origin"), origin}});
    record->logs.append(QStringLiteral("action %1 %2 [%3]").arg(action, token, origin));
    while (record->logs.size() > 200)
        record->logs.removeFirst();
    return token;
}
bool PluginActionRegistry::finish(const QString &plugin, quint64 generation, const QJsonObject &arguments)
{
    const auto token = arguments.value(QStringLiteral("requestId")).toString();
    auto it = pending_.find(token);
    if (it == pending_.end() || it->plugin != plugin || it->generation != generation)
        return false;
    const auto item = pending_.take(token);
    disconnect(item.destruction);
    auto error = arguments.value(QStringLiteral("error")).toObject();
    const auto result = arguments.value(QStringLiteral("result"));
    if (error.isEmpty())
    {
        const auto validation = Plugins::validateJson(result, item.outputSchema);
        if (!validation.isEmpty())
            error = Plugins::error(QStringLiteral("INVALID_RESULT"), validation);
    }
    if (item.context)
        item.completion(error, result);
    return true;
}
void PluginActionRegistry::cancel(const QString &token, const QString &code)
{
    if (!pending_.contains(token))
        return;
    const auto item = pending_.take(token);
    disconnect(item.destruction);
    emit dispatch(item.plugin, QStringLiteral("actions.cancel"), {{QStringLiteral("requestId"), token}});
    if (item.context)
        item.completion(Plugins::error(code, code == QStringLiteral("TIMEOUT")
                                                 ? QStringLiteral("插件动作超时")
                                                 : QStringLiteral("插件动作已取消")),
                        {});
}
void PluginActionRegistry::cancelPlugin(const QString &plugin)
{
    const auto tokens = pending_.keys();
    for (const auto &token : tokens)
        if (pending_.value(token).plugin == plugin)
            cancel(token, QStringLiteral("PLUGIN_STOPPED"));
}
