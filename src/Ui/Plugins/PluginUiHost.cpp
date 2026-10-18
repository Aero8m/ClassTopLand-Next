#include "PluginUiHost.h"
#include "PluginSettingsDialog.h"
#include "PluginView.h"
#include "../UiWidgets.h"
#include "../../Core/Plugins/PluginManager.h"
#include "../../Core/ConfigManager/ConfigManager.h"
#include <ElaDialog.h>
#include <ElaScrollArea.h>
#include <QVBoxLayout>
#include <QScreen>
#include <QGuiApplication>
#include <QUuid>

PluginUiHost::PluginUiHost(PluginManager *manager, QObject *parent) : QObject(parent), manager_(manager)
{
    manager_->preparePluginStop = [this](const QString &plugin) { return prepareAll(plugin); };
    connect(manager_, &PluginManager::pluginStopped, this,
            [this](const QString &plugin, bool fault)
            {
                for (const auto &dialog : dialogs_)
                    if (dialog && dialog->pluginId() == plugin)
                        dialog->forceClose();
                for (const auto &window : windows_)
                    if (window && window->property("pluginId").toString() == plugin)
                        window->close();
                if (fault)
                    emit manager_->notification(
                        QStringLiteral("插件运行故障"),
                        QStringLiteral("插件已停止，未保存的表单可能丢失；已保存设置保留。"));
            });
    connect(manager_, &PluginManager::windowRequested, this,
            [this](const QString &plugin, const QString &contribution)
            { openWindow(plugin, contribution, nullptr); });
    connect(&ConfigManager::instance(), &ConfigManager::courseBarChanged, this,
            [this]
            {
                const auto components = ConfigManager::instance().config().courseBarConfig.components;
                const auto dialogs = dialogs_.values();
                for (const auto &dialog : dialogs) {
                    if (!dialog || dialog->componentInstance().isEmpty()) continue;
                    bool retained = false;
                    for (const auto &component : components)
                        if (component.instanceId == dialog->componentInstance()) { retained = true; break; }
                    if (!retained) dialog->forceClose();
                }
            });
    auto constrain = [this]
    {
        for (const auto &dialog : dialogs_)
            if (dialog)
                dialog->constrainToScreen();
    };
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, constrain);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, constrain);
}
void PluginUiHost::openSettings(const QString &plugin, QWidget *parent)
{
    auto record = manager_->record(plugin);
    if (!record || record->state != QStringLiteral("运行中"))
        return;
    if (auto dialog = dialogs_.value(plugin))
    {
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    if (record->settingsPages.isEmpty())
    {
        AppUi::error(parent, QStringLiteral("该插件没有可配置项"));
        return;
    }
    auto *dialog = new PluginSettingsDialog(manager_, plugin, record->settingsPages, parent);
    dialogs_[plugin] = dialog;
    if (geometries_.contains(plugin))
        dialog->restoreGeometry(geometries_[plugin]);
    dialog->constrainToScreen();
    connect(dialog, &QDialog::finished, this,
            [this, plugin, dialog] { geometries_[plugin] = dialog->saveGeometry(); });
    const auto targetSize = dialog->size();
    dialog->show();
    dialog->resize(targetSize);
    dialog->constrainToScreen();
}
void PluginUiHost::openComponent(const QString &instance, QWidget *parent)
{
    const auto key = QStringLiteral("component:") + instance;
    if (auto dialog = dialogs_.value(key))
    {
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
        return;
    }
    for (const auto &component : ConfigManager::instance().config().courseBarConfig.components)
        if (component.instanceId == instance)
        {
            const auto parts = component.typeId.split('/');
            if (parts.size() != 2)
                return;
            const auto definition = manager_->definition(parts[0], QStringLiteral("component"), parts[1]);
            const auto settings = definition.value(QStringLiteral("settings"));
            if (!settings.isObject())
            {
                AppUi::error(parent, QStringLiteral("组件不可用或没有实例设置"));
                return;
            }
            auto *dialog = new PluginSettingsDialog(
                manager_, parts[0], {{QStringLiteral("component"), settings.toObject()}}, parent, instance);
            dialogs_[key] = dialog;
            if (geometries_.contains(key))
                dialog->restoreGeometry(geometries_[key]);
            dialog->constrainToScreen();
            connect(dialog, &QDialog::finished, this,
                    [this, key, dialog] { geometries_[key] = dialog->saveGeometry(); });
            const auto targetSize = dialog->size();
            dialog->show();
            dialog->resize(targetSize);
            dialog->constrainToScreen();
            return;
        }
}
bool PluginUiHost::prepareAll(const QString &plugin)
{
    const auto dialogs = dialogs_.values();
    for (const auto &dialog : dialogs)
        if (dialog && (plugin.isEmpty() || dialog->pluginId() == plugin) && !dialog->prepareToClose())
            return false;
    return true;
}
bool PluginUiHost::prepareComponentRemoval(const QString &instance)
{
    const auto dialog = dialogs_.value(QStringLiteral("component:") + instance);
    return !dialog || dialog->prepareToClose();
}
void PluginUiHost::closeDialogs()
{
    for (const auto &dialog : dialogs_)
        if (dialog)
            dialog->forceClose();
}
void PluginUiHost::openWindow(const QString &plugin, const QString &contribution, QWidget *parent)
{
    dialogs_.removeIf([](const auto &entry) { return entry.value().isNull(); });
    const auto definition = manager_->definition(plugin, QStringLiteral("window"), contribution);
    int count = 0;
    for (const auto &dialog : dialogs_)
        if (dialog)
            ++count;
    if (!PluginView::check(definition).isEmpty() || count >= 16)
        return;
    const auto key = QStringLiteral("window:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto *dialog =
        new PluginSettingsDialog(manager_, plugin, {{contribution, definition}}, parent, {},
                                 QStringLiteral("window/") + contribution, QStringLiteral("window"));
    dialog->setWindowTitle(definition.value(QStringLiteral("name")).toString(contribution));
    dialogs_[key] = dialog;
    const auto targetSize = dialog->size();
    dialog->show();
    dialog->resize(targetSize);
    dialog->constrainToScreen();
}
QWidget *PluginUiHost::createComponent(const QString &typeId, const QString &instance,
                                       const QJsonObject &settings, QWidget *parent)
{
    const auto parts = typeId.split('/');
    if (parts.size() != 2)
        return nullptr;
    const auto definition = manager_->definition(parts[0], QStringLiteral("component"), parts[1]);
    if (!PluginView::check(definition).isEmpty())
        return nullptr;
    const auto viewId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto *view = new PluginView(definition, settings, parent);
    view->setProperty("componentInstanceId", instance);
    manager_->createView(parts[0], QStringLiteral("component"), parts[1], viewId, settings);
    connect(view, &PluginView::action, this,
            [this, instance = viewId](const QString &action, const QJsonValue &value)
            { manager_->action(instance, action, value); });
    connect(manager_, &PluginManager::viewUpdated, view,
            [view, instance = viewId](const QString &id, const QJsonObject &patch)
            {
                if (id == instance)
                    view->patch(patch);
            });
    connect(manager_, &PluginManager::viewTreeReplaced, view,
            [view, instance = viewId](const QString &id, const QJsonObject &tree)
            {
                if (id == instance)
                    view->setTree(tree);
            });
    connect(view, &QObject::destroyed, manager_,
            [manager = manager_, instance = viewId] { manager->releaseView(instance); });
    return view;
}
PluginUiHost::~PluginUiHost()
{
    manager_->preparePluginStop = {};
    closeDialogs();
}
