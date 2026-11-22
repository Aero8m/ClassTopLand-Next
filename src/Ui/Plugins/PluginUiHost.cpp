#include "PluginUiHost.h"
#include "../../Core/ConfigManager/ConfigManager.h"
#include "../../Core/Plugins/PluginManager.h"
#include "../UiWidgets.h"
#include "PluginNativeView.h"
#include <QTimer>
#include <QUuid>

PluginUiHost::PluginUiHost(PluginManager *manager, QObject *parent) : QObject(parent), manager_(manager)
{
    manager_->preparePluginStop = [this](const QString &plugin, GuardCompletion completion) {
        prepareAll(std::move(completion), plugin);
    };
    manager_->uiRequest = [this](const QString &plugin, const QString &method, const QJsonObject &args,
                                 PluginManager::Completion completion) {
        request(plugin, method, args, std::move(completion));
    };
    manager_->guardRequest = [this](const QString &plugin, const QJsonObject &args) {
        return guarding_ && !guardToken_.isEmpty() &&
               guardToken_ == args.value(QStringLiteral("_guard")).toString() &&
               views_.value(guardInstance_).plugin == plugin;
    };
    manager_->pageNavigation = [this](const QString &plugin, const QString &page) {
        QString error;
        return openPage(plugin, page, {}, &error) ? QJsonObject()
             : Plugins::error(QStringLiteral("NOT_FOUND"), error);
    };
    connect(manager_, &PluginManager::guardFinished, this,
            [this](const QString &token, const QJsonObject &error) {
                if (token != guardToken_ || !guarding_)
                    return;
                if (!error.isEmpty())
                {
                    if (auto view = views_.value(guardInstance_).widget)
                        AppUi::error(view->window(), error.value(QStringLiteral("message")).toString());
                    finishGuard(false);
                    return;
                }
                if (auto view = views_.value(guardInstance_).widget; view && view->dirty())
                {
                    AppUi::error(view->window(),
                                 QStringLiteral("保存期间页面再次改变，已保留新草稿并取消关闭。"));
                    finishGuard(false);
                    return;
                }
                guardToken_.clear();
                guardNext();
            });
    connect(manager_, &PluginManager::pluginStopped, this, [this](const QString &plugin, bool fault) {
        if (guarding_ && views_.value(guardInstance_).plugin == plugin)
            finishGuard(false);
        const auto instances = views_.keys();
        for (const auto &instance : instances)
            if (views_.value(instance).plugin == plugin)
            {
                if (auto view = views_.value(instance).widget)
                {
                    view->setEnabled(false);
                    view->setAvailable(false);
                    if (views_.value(instance).kind != QStringLiteral("component"))
                        view->deleteLater();
                }
                manager_->releaseView(instance);
                views_.remove(instance);
            }
        if (fault)
            emit manager_->notification(QStringLiteral("插件运行故障"),
                                        QStringLiteral("未保存更改可能丢失，已保存数据保留。"));
    });
    connect(&ConfigManager::instance(), &ConfigManager::courseBarChanged, this, [this] {
        const auto components = ConfigManager::instance().config().courseBarConfig.components;
        const auto instances = views_.keys();
        for (const auto &instance : instances)
        {
            const auto view = views_.value(instance);
            if (view.kind != QStringLiteral("componentSettings"))
                continue;
            const auto typeId = view.plugin + QLatin1Char('/') + view.contribution;
            bool exists = false;
            for (const auto &component : components)
                if (component.instanceId == view.component && component.typeId == typeId)
                    exists = true;
            if (!exists && view.widget)
                view.widget->deleteLater();
        }
    });
}
PluginUiHost::~PluginUiHost()
{
    manager_->preparePluginStop = {};
    manager_->uiRequest = {};
    manager_->guardRequest = {};
    manager_->pageNavigation = {};
    if (guarding_)
        finishGuard(false);
    closeDialogs();
}
bool PluginUiHost::openPage(const QString &plugin, const QString &page, const QString &component,
                            QString *error)
{
    if (error) error->clear();
    QString failure;
    const bool opened = !guarding_ && navigator && navigator(plugin, page, component, &failure);
    if (!opened)
    {
        if (failure.isEmpty())
            failure = guarding_ ? QStringLiteral("正在处理未保存内容，请稍后重试") : QStringLiteral("插件导航不可用");
        if (error)
            *error = failure;
        else
            emit manager_->notification(QStringLiteral("插件页面打开失败"), failure);
    }
    return opened;
}
void PluginUiHost::openSettings(const QString &plugin, QWidget *)
{
    const auto record = manager_->record(plugin);
    if (record)
        for (const auto &item : record->manifest.contributions.value(QStringLiteral("pages")).toArray())
        {
            const auto page = item.toObject();
            if (page.value(QStringLiteral("purpose")) == QStringLiteral("settings") &&
                record->pages.contains(page.value(QStringLiteral("id")).toString()))
            {
                openPage(plugin, page.value(QStringLiteral("id")).toString());
                return;
            }
        }
}
void PluginUiHost::openComponent(const QString &instance, QWidget *)
{
    openPage({}, {}, instance);
}
PluginNativeView *PluginUiHost::createPage(const QString &plugin, const QString &contribution,
                                           QWidget *parent, QString *error)
{
    const auto definition = manager_->definition(plugin, QStringLiteral("page"), contribution);
    if (definition.isEmpty())
    {
        if (error) *error = QStringLiteral("插件页面不存在");
        return nullptr;
    }
    return create(plugin, QStringLiteral("page"), contribution,
                  definition.value(QStringLiteral("ui")).toString(), {}, {}, parent, error);
}
PluginNativeView *PluginUiHost::createComponentSettings(const QString &instance, QWidget *parent, QString *error)
{
    if (error) *error = QStringLiteral("组件不存在或未声明实例设置");
    for (const auto &component : ConfigManager::instance().config().courseBarConfig.components)
        if (component.instanceId == instance)
        {
            const auto parts = component.typeId.split('/');
            if (parts.size() != 2)
                return nullptr;
            const auto definition = manager_->definition(parts[0], QStringLiteral("component"), parts[1]);
            if (!definition.contains(QStringLiteral("settingsUi")))
                return nullptr;
            return create(parts[0], QStringLiteral("componentSettings"), parts[1],
                          definition.value(QStringLiteral("settingsUi")).toString(), instance,
                          component.settings, parent, error);
        }
    return nullptr;
}
QWidget *PluginUiHost::createComponent(const QString &typeId, const QString &instance,
                                       const QJsonObject &settings, QWidget *parent)
{
    const auto parts = typeId.split('/');
    if (parts.size() != 2)
        return nullptr;
    const auto definition = manager_->definition(parts[0], QStringLiteral("component"), parts[1]);
    if (definition.isEmpty())
        return nullptr;
    return create(parts[0], QStringLiteral("component"), parts[1],
                  definition.value(QStringLiteral("ui")).toString(), instance, settings, parent);
}
PluginNativeView *PluginUiHost::create(const QString &plugin, const QString &kind,
                                       const QString &contribution, const QString &file,
                                       const QString &component, const QJsonObject &settings, QWidget *parent, QString *error)
{
    const auto record = manager_->record(plugin);
    if (!record || record->state != QStringLiteral("运行中") ||
        (guarding_ && kind != QStringLiteral("component")))
    {
        if (error) *error = QStringLiteral("插件未运行或正在处理未保存内容");
        return nullptr;
    }
    if (error) error->clear();
    const auto definition = manager_->definition(plugin, kind == QStringLiteral("page") ? QStringLiteral("page") : QStringLiteral("component"), contribution);
    const auto preset = kind == QStringLiteral("component") ? QString() : definition.value(kind == QStringLiteral("componentSettings")
        ? QStringLiteral("settingsLayoutPreset") : QStringLiteral("layoutPreset")).toString();
    const auto title = definition.value(QStringLiteral("name")).toString();
    auto *view = new PluginNativeView(record->manifest.directory, file, parent,
        kind == QStringLiteral("component") ? Plugins::UiKind::Component :
        kind == QStringLiteral("componentSettings") ? Plugins::UiKind::ComponentSettings : Plugins::UiKind::Page, preset, title);
    if (!view->failure().isEmpty())
    {
        if (error) *error = view->failure();
        else emit manager_->notification(QStringLiteral("插件页面加载失败"), view->failure());
        delete view;
        return nullptr;
    }
    for (const auto &entry : view->diagnostics())
    {
        const auto diagnostic = entry.toObject();
        record->logs.append(QStringLiteral("WARN %1 [%2] %3").arg(diagnostic.value(QStringLiteral("code")).toString(),
            diagnostic.value(QStringLiteral("object")).toString(), diagnostic.value(QStringLiteral("message")).toString()));
    }
    while (record->logs.size() > 200) record->logs.removeFirst();
    const auto instance = QUuid::createUuid().toString(QUuid::WithoutBraces);
    view->setProperty("pluginViewInstance", instance);
    views_.insert(instance, {view, plugin, kind, contribution, component, record->generation});
    connect(
        view, &PluginNativeView::controlSignal, this,
        [this, plugin, instance](const QString &object, const QString &signal, const QJsonArray &arguments) {
            manager_->viewEvent(plugin, QStringLiteral("ui.signal"),
                                {{QStringLiteral("instanceId"), instance},
                                 {QStringLiteral("object"), object},
                                 {QStringLiteral("signal"), signal},
                                 {QStringLiteral("arguments"), arguments}});
        });
    connect(view, &PluginNativeView::visibilityChanged, this, [this, plugin, instance](bool visible) {
        manager_->viewEvent(plugin, QStringLiteral("ui.visibility"),
                            {{QStringLiteral("instanceId"), instance}, {QStringLiteral("visible"), visible}});
    });
    connect(view, &QObject::destroyed, this, [this, instance] {
        if (guarding_ && guardInstance_ == instance)
            finishGuard(false);
        manager_->releaseView(instance);
        views_.remove(instance);
    });
    manager_->createView(plugin, kind, contribution, instance, settings, component);
    if (guarding_)
    {
        view->setEnabled(false);
        guardQueue_.append(instance);
    }
    return view;
}
void PluginUiHost::request(const QString &plugin, const QString &method, const QJsonObject &args,
                           PluginManager::Completion completion)
{
    const auto instance = args.value(QStringLiteral("instanceId")).toString();
    const auto entry = views_.value(instance);
    const auto record = manager_->record(plugin);
    if (!entry.widget || !record || entry.plugin != plugin || entry.generation != record->generation)
    {
        completion(Plugins::error(QStringLiteral("NOT_FOUND"), QStringLiteral("视图已销毁")), {});
        return;
    }
    if (method == QStringLiteral("barrierReady"))
    {
        if (guarding_ && guardInstance_ == instance &&
            guardToken_ == args.value(QStringLiteral("token")).toString())
        {
            guardToken_.clear();
            completion({}, true);
            guardReady();
        }
        else
            completion(Plugins::error(QStringLiteral("CANCELED"), QStringLiteral("关闭检查已结束")), {});
        return;
    }
    if (method == QStringLiteral("setDirty"))
    {
        entry.widget->setDirty(args.value(QStringLiteral("value")).toBool());
        completion({}, true);
        return;
    }
    if (method == QStringLiteral("setAvailable"))
    {
        entry.widget->setAvailable(args.value(QStringLiteral("value")).toBool());
        completion({}, true);
        return;
    }
    if (method == QStringLiteral("config.read") || method == QStringLiteral("config.commit"))
    {
        const bool commit = method == QStringLiteral("config.commit");
        if (commit && !manager_->acceptingWork() &&
            !(manager_->guardRequest && manager_->guardRequest(plugin, args)))
        {
            completion(Plugins::error(QStringLiteral("CANCELED"), QStringLiteral("当前不可保存")), {});
            return;
        }
        if (entry.kind != QStringLiteral("component") && entry.kind != QStringLiteral("componentSettings"))
        {
            auto options = args;
            options.insert(QStringLiteral("key"), QStringLiteral("settings"));
            manager_->storage(plugin,
                              commit ? QStringLiteral("storage.commit") : QStringLiteral("storage.read"),
                              options, entry.widget, std::move(completion));
            return;
        }
        auto candidate = ConfigManager::instance().config();
        const auto typeId = entry.plugin + QLatin1Char('/') + entry.contribution;
        for (auto &component : candidate.courseBarConfig.components)
            if (component.instanceId == entry.component && component.typeId == typeId)
            {
                if (!commit)
                {
                    completion({}, QJsonObject{{QStringLiteral("revision"),
                                                QString::number(ConfigManager::instance().revision())},
                                               {QStringLiteral("value"), component.settings},
                                               {QStringLiteral("exists"), true}});
                    return;
                }
                if (args.value(QStringLiteral("expectedRevision")).toString() !=
                    QString::number(ConfigManager::instance().revision()))
                {
                    completion(Plugins::error(QStringLiteral("CONFLICT"), QStringLiteral("组件配置已变化")),
                               {});
                    return;
                }
                if (!args.value(QStringLiteral("value")).isObject())
                {
                    completion(Plugins::error(QStringLiteral("INVALID_ARGUMENT"),
                                              QStringLiteral("组件配置必须为对象")),
                               {});
                    return;
                }
                component.settings = args.value(QStringLiteral("value")).toObject();
                QString error;
                if (!ConfigManager::instance().commit(candidate, &error))
                    completion(Plugins::error(QStringLiteral("SAVE_FAILED"), error), {});
                else
                    completion({}, QJsonObject{{QStringLiteral("revision"),
                                                QString::number(ConfigManager::instance().revision())}});
                return;
            }
        completion(Plugins::error(QStringLiteral("NOT_FOUND"), QStringLiteral("组件已移除")), {});
        return;
    }
    QString error;
    const auto result = entry.widget->request(method, args, &error);
    if (!error.isEmpty())
    {
        record->logs.append(QStringLiteral("WARN UI_REQUEST_REJECTED [%1] %2").arg(args.value(QStringLiteral("object")).toString(), error));
        while (record->logs.size() > 200) record->logs.removeFirst();
    }
    completion(error.isEmpty() ? QJsonObject() : Plugins::error(QStringLiteral("INVALID_ARGUMENT"), error),
               result);
}
void PluginUiHost::prepareAll(GuardCompletion completion, const QString &plugin)
{
    QStringList instances;
    for (auto it = views_.cbegin(); it != views_.cend(); ++it)
        if (plugin.isEmpty() || plugin == it->plugin)
            instances.append(it.key());
    prepare(instances, std::move(completion));
}
void PluginUiHost::prepareComponents(const QStringList &components, GuardCompletion completion)
{
    QStringList instances;
    for (auto it = views_.cbegin(); it != views_.cend(); ++it)
        if (components.contains(it->component))
            instances.append(it.key());
    prepare(instances, std::move(completion));
}
void PluginUiHost::prepare(QStringList instances, GuardCompletion completion)
{
    if (guarding_)
    {
        completion(false);
        return;
    }
    guarding_ = true;
    emit guardStateChanged(true);
    completion_ = std::move(completion);
    guardQueue_ = instances;
    manager_->setQuiescing(true);
    for (const auto &view : views_)
        if (view.widget)
            view.widget->setEnabled(false);
    guardNext();
}
void PluginUiHost::guardNext()
{
    if (!guarding_)
        return;
    while (!guardQueue_.isEmpty())
    {
        guardInstance_ = guardQueue_.takeFirst();
        const auto entry = views_.value(guardInstance_);
        if (!entry.widget)
            continue;
        // Drain control events already sent to the worker before examining dirty.
        // In particular, a user can type and immediately request close/restart.
        guardToken_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto token = guardToken_;
        QTimer::singleShot(10000, this, [this, token] {
            if (guarding_ && token == guardToken_)
            {
                finishGuard(false);
            }
        });
        manager_->viewEvent(
            entry.plugin, QStringLiteral("ui.barrier"),
            {{QStringLiteral("instanceId"), guardInstance_}, {QStringLiteral("token"), token}});
        return;
    }
    finishGuard(true);
}
void PluginUiHost::guardReady()
{
    if (!guarding_)
        return;
    const auto entry = views_.value(guardInstance_);
    if (!entry.widget || !entry.widget->dirty())
    {
        guardNext();
        return;
    }
    emit revealRequested(entry.plugin, guardInstance_);
    auto *prompt = new AppUi::UnsavedDialog(entry.widget, QStringLiteral("插件页面有未保存的更改"));
    prompt->setWindowModality(Qt::WindowModal);
    prompt_ = prompt;
    const auto instance = guardInstance_;
    connect(prompt, &QDialog::finished, this, [this, prompt, instance] {
        const auto choice = prompt->choice();
        prompt->deleteLater();
        prompt_.clear();
        if (!guarding_ || instance != guardInstance_)
            return;
        if (choice == AppUi::UnsavedChoice::Cancel)
        {
            finishGuard(false);
            return;
        }
        guardToken_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto token = guardToken_;
        QTimer::singleShot(10000, this, [this, token] {
            if (guarding_ && token == guardToken_)
            {
                if (auto view = views_.value(guardInstance_).widget)
                    AppUi::error(view->window(), QStringLiteral("插件保存或放弃操作超时，已取消关闭。"));
                finishGuard(false);
            }
        });
        manager_->dispatchGuard(
            guardInstance_,
            choice == AppUi::UnsavedChoice::Save ? QStringLiteral("save") : QStringLiteral("discard"), token);
    });
    prompt->show();
    return;
}
void PluginUiHost::finishGuard(bool success)
{
    if (!guarding_)
        return;
    guarding_ = false;
    if (prompt_)
    {
        prompt_->reject();
        prompt_.clear();
    }
    guardToken_.clear();
    guardInstance_.clear();
    guardQueue_.clear();
    manager_->setQuiescing(false);
    emit guardStateChanged(false);
    for (const auto &view : views_)
        if (view.widget)
            view.widget->setEnabled(true);
    auto completion = std::move(completion_);
    if (completion)
        completion(success);
}
void PluginUiHost::closePages()
{
    const auto instances = views_.keys();
    for (const auto &instance : instances)
        if (views_.value(instance).kind != QStringLiteral("component"))
            if (auto view = views_.value(instance).widget)
                delete view;
}
void PluginUiHost::closeDialogs()
{
    closePages();
}
QJsonArray PluginUiHost::snapshots() const
{
    QJsonArray result;
    for (auto it = views_.cbegin(); it != views_.cend(); ++it)
        if (it->widget)
        {
            auto snapshot = it->widget->snapshot();
            snapshot.insert(QStringLiteral("instanceId"), it.key());
            snapshot.insert(QStringLiteral("plugin"), it->plugin);
            snapshot.insert(QStringLiteral("contribution"), it->contribution);
            snapshot.insert(QStringLiteral("kind"), it->kind);
            snapshot.insert(QStringLiteral("componentInstance"), it->component);
            result.append(snapshot);
        }
    return result;
}
QJsonValue PluginUiHost::inspect(const QString &instance, const QString &method, const QJsonObject &args,
                                 QString *error)
{
    auto view = views_.value(instance).widget;
    if (!view)
    {
        *error = QStringLiteral("视图不存在");
        return {};
    }
    return view->request(method, args, error);
}
