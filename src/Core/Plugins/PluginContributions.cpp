#include "PluginActionRegistry.h"
#include "PluginContracts.h"
#include "PluginManager.h"
#include "PluginRuntime.h"
#include <QJsonArray>

bool PluginManager::contributionRequest(const std::shared_ptr<Record> &record, const QString &id,
                                        const QString &method, const QJsonObject &args)
{
    const auto generation = record->generation;
    auto reply = [record, generation, id](const QJsonObject &error = QJsonObject(),
                                          const QJsonValue &result = QJsonValue()) {
        if (record->runtime && record->generation == generation)
            record->runtime->deliver(id, error, result);
    };
    if (method == QStringLiteral("ui.catalog"))
    {
        reply({}, Plugins::widgetCatalog());
        return true;
    }
    if (method == QStringLiteral("ui.registerPage") || method == QStringLiteral("ui.registerComponent") ||
        method == QStringLiteral("actions.register"))
    {
        if (record->state != QStringLiteral("启动中"))
        {
            reply(Plugins::error(QStringLiteral("INVALID_ARGUMENT"),
                                 QStringLiteral("贡献只能在 activate 中注册")));
            return true;
        }
        const auto kind = method == QStringLiteral("ui.registerPage") ? QStringLiteral("pages")
                          : method == QStringLiteral("ui.registerComponent")
                              ? QStringLiteral("courseBarComponents")
                              : QStringLiteral("actions");
        auto &target = kind == QStringLiteral("pages")     ? record->pages
                       : kind == QStringLiteral("actions") ? record->actions
                                                           : record->components;
        const auto local = args.value(QStringLiteral("id")).toString();
        QJsonObject declared;
        for (const auto &contribution : record->manifest.contributions.value(kind).toArray())
            if (contribution.toObject().value(QStringLiteral("id")).toString() == local)
                declared = contribution.toObject();
        if (declared.isEmpty() || target.contains(local))
        {
            reply(Plugins::error(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("贡献未声明或重复注册")));
            return true;
        }
        if (kind == QStringLiteral("courseBarComponents") &&
            declared.contains(QStringLiteral("settingsUi")) &&
            !args.value(QStringLiteral("hasSettings")).toBool())
        {
            reply(Plugins::error(QStringLiteral("INVALID_ARGUMENT"),
                                 QStringLiteral("组件声明 settingsUi 时必须注册 createSettings")));
            return true;
        }
        target.insert(local, declared);
        reply();
        return true;
    }
    if (method == QStringLiteral("actions.complete"))
    {
        const bool accepted = actions_->finish(record->manifest.id, generation, args);
        reply(accepted ? QJsonObject()
                       : Plugins::error(QStringLiteral("CANCELED"), QStringLiteral("动作已结束")));
        return true;
    }
    if (method == QStringLiteral("actions.invoke"))
    {
        const auto key = record->manifest.id + QLatin1Char('/') + id;
        const auto token = actions_->invoke(
            record->manifest.id, args.value(QStringLiteral("id")).toString(),
            args.value(QStringLiteral("arguments")), this,
            [this, key, reply](const QJsonObject &error, const QJsonValue &result) {
                actionRequests_.remove(key);
                reply(error, result);
            },
            QStringLiteral("plugin"));
        if (!token.isEmpty())
            actionRequests_.insert(key, token);
        return true;
    }
    if (method == QStringLiteral("ui.openPage"))
    {
        const auto page = args.value(QStringLiteral("id")).toString();
        if (!record->pages.contains(page) || !acceptingWork())
            reply(Plugins::error(QStringLiteral("NOT_FOUND"), QStringLiteral("页面不可用")));
        else
        {
            reply(pageNavigation ? pageNavigation(record->manifest.id, page)
                                 : Plugins::error(QStringLiteral("NOT_FOUND"), QStringLiteral("插件导航不可用")));
        }
        return true;
    }
    if (method == QStringLiteral("ui.guardComplete"))
    {
        if (guardRequest && guardRequest(record->manifest.id, args))
        {
            emit guardFinished(args.value(QStringLiteral("_guard")).toString(),
                               args.value(QStringLiteral("error")).toObject());
            reply();
        }
        else
            reply(Plugins::error(QStringLiteral("CANCELED"), QStringLiteral("保存守卫已结束")));
        return true;
    }
    if (method.startsWith(QStringLiteral("ui.")))
    {
        const auto instance = args.value(QStringLiteral("instanceId")).toString();
        if (views_.value(instance) != record->manifest.id || !uiRequest)
        {
            reply(Plugins::error(QStringLiteral("NOT_FOUND"), QStringLiteral("视图实例不存在或已失效")));
            return true;
        }
        uiRequest(record->manifest.id, method.mid(3), args, reply);
        return true;
    }
    return false;
}
