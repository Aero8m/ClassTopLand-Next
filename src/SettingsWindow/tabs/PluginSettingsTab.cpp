#include "PluginSettingsTab.h"
#include "../../Core/Plugins/PluginManager.h"
#include "../../Core/ConfigManager/ConfigManager.h"
#include "../../Ui/Plugins/PluginUiHost.h"
#include "../../Ui/Plugins/PluginHistoryDialog.h"
#include "../../Ui/UiWidgets.h"
#include "../../Utils/UiAccess/UiAccess.h"
#include <ElaLineEdit.h>
#include <ElaPushButton.h>
#include <ElaToggleSwitch.h>
#include <ElaText.h>
#include <ElaMenu.h>
#include <ElaScrollArea.h>
#include <QJsonDocument>
#include <QPointer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QPlainTextEdit>
#include <ElaPlainTextEdit.h>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <QScrollBar>

namespace {
bool hasSettings(const std::shared_ptr<PluginManager::Record> &record) {
    for(const auto &page:record->pages) if(page.value(QStringLiteral("purpose"))==QStringLiteral("settings")) return true;
    return false;
}
}
PluginSettingsTab::PluginSettingsTab(PluginManager *manager, PluginUiHost *ui, QWidget *parent)
    : ElaScrollPage(parent), manager_(manager), ui_(ui)
{
    auto *layout = AppUi::page(this, QStringLiteral("插件"));
    auto *toolsBody = AppUi::card(layout);
    auto *cloudHint = AppUi::text(QStringLiteral("集控下发的启用偏好在下次启动生效，运行状态以当前会话为准。"), toolsBody->parentWidget(), 12);
    cloudHint->setWordWrap(true);
    auto *tools = new QHBoxLayout;
    search_ = new ElaLineEdit(toolsBody->parentWidget());
    search_->setPlaceholderText(QStringLiteral("搜索插件名称或 ID"));
    tools->addWidget(search_, 1);
    auto *more = AppUi::button(QStringLiteral("更多"), toolsBody->parentWidget());
    tools->addWidget(more);
    toolsBody->addLayout(tools);
    toolsBody->addWidget(cloudHint);
    auto *menu = new ElaMenu(this);
    menu->addAction(QStringLiteral("重新扫描"), manager_, &PluginManager::scan);
    menu->addAction(QStringLiteral("打开插件目录"), this,
                    [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(manager_->pluginsDirectory())); });
    menu->addAction(QStringLiteral("重启并跳过第三方插件"), this,
                    [this]
                    {
                        if (!AppUi::confirm(this, QStringLiteral("恢复模式"),
                                            QStringLiteral("重启后跳过所有第三方插件，保留原启用偏好。")))
                            return;
                        QString error;
                        if (!manager_->requestRecovery(&error))
                            AppUi::error(this, error);
                    });
    connect(more, &ElaPushButton::clicked, this,
            [more, menu] { menu->popup(more->mapToGlobal(QPoint(0, more->height()))); });
    stack_ = new QStackedWidget(this);
    list_ = new QWidget(stack_);
    rows_ = new QVBoxLayout(list_);
    rows_->setContentsMargins(0, 0, 0, 0);
    rows_->setSpacing(12);
    stack_->addWidget(list_);
    layout->addWidget(stack_);
    layout->addStretch();
    if (manager_->recovery())
    {
        auto *label =
            AppUi::text(QStringLiteral("恢复模式：本次启动未自动运行第三方插件，可在此手动启用。"), toolsBody->parentWidget(), 12);
        label->setWordWrap(true);
        toolsBody->addWidget(label);
    }
    connect(search_, &ElaLineEdit::textChanged, this, &PluginSettingsTab::refresh);
    connect(manager_, &PluginManager::changed, this,
            [this]
            {
                if (refreshPending_)
                    return;
                refreshPending_ = true;
                QTimer::singleShot(0, this,
                                   [this]
                                   {
                                       refreshPending_ = false;
                                       refresh();
                                       if (details_ && stack_->currentWidget() == details_)
                                           showDetails(detailKey_);
                                   });
            });
    refresh();
    connect(&ConfigManager::instance(), &ConfigManager::committed, this, [this] { refresh(); });
}
void PluginSettingsTab::refresh()
{
    while (auto *item = rows_->takeAt(0))
    {
        if (auto *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
    const auto query = search_->text().trimmed();
    int count = 0;
    for (const auto &record : manager_->records())
    {
        if (!query.isEmpty() && !record->manifest.name.contains(query, Qt::CaseInsensitive) &&
            !record->manifest.id.contains(query, Qt::CaseInsensitive))
            continue;
        ++count;
        auto *body = AppUi::card(rows_);
        auto *card = body->parentWidget();
        auto *row = new QHBoxLayout;
        auto *name = AppUi::text(record->manifest.name, card, 16);
        name->setMinimumWidth(0);
        name->setWordWrap(true);
        row->addWidget(name, 1);
        auto *toggle = new ElaToggleSwitch(card);
        toggle->setAccessibleName(QStringLiteral("启用插件：") + record->manifest.name);
        const auto preference =
            ConfigManager::instance().config().pluginConfig.plugins.value(record->manifest.id);
        toggle->setIsToggled(preference.enabled);
        toggle->setEnabled(record->manifest.valid() && record->state != QStringLiteral("启动中") &&
                           record->state != QStringLiteral("停止中"));
        row->addWidget(toggle);
        body->addLayout(row);
        auto *actions = new QHBoxLayout;
        actions->addStretch();
        auto *settings = AppUi::button(QStringLiteral("设置"), card);
        settings->setEnabled(record->state == QStringLiteral("运行中") && hasSettings(record));
        settings->setToolTip(
            !hasSettings(record) ? QStringLiteral("无可配置项；已停用插件需先启用") : QString());
        actions->addWidget(settings);
        auto *details = AppUi::button(QStringLiteral("详情"), card);
        details->setProperty("pluginKey", record->key);
        actions->addWidget(details);
        auto *summary = AppUi::text(
            record->state + (record->updatePending ? QStringLiteral(" · 待热重载") : QString()) +
                QStringLiteral(" · ") + record->manifest.description.left(100),
            card, 12);
        summary->setWordWrap(true);
        summary->setMinimumWidth(0);
        body->addWidget(summary);
        body->addLayout(actions);
        connect(toggle, &ElaToggleSwitch::toggled, this,
                [this, key = record->key](bool enabled) { this->toggle(key, enabled); });
        connect(settings, &ElaPushButton::clicked, this,
                [this, id = record->manifest.id] { ui_->openSettings(id, window()); });
        connect(details, &ElaPushButton::clicked, this, [this, key = record->key] { showDetails(key); });
    }
    if (!count)
    {
        auto *emptyBody = AppUi::card(rows_);
        auto *empty = AppUi::text(
            query.isEmpty()
                ? QStringLiteral(
                      "尚未发现插件。将插件目录放入 plugins 后，在“更多”中重新扫描；扫描不会执行插件代码。")
                : QStringLiteral("没有匹配的插件"),
            emptyBody->parentWidget());
        empty->setWordWrap(true);
        emptyBody->addWidget(empty);
    }
    rows_->addStretch();
}
void PluginSettingsTab::toggle(const QString &id, bool enabled)
{
    auto record = manager_->record(id);
    if (!record)
        return;
    if (enabled)
    {
        QString permissions =
            QString::fromUtf8(QJsonDocument(record->manifest.permissions).toJson(QJsonDocument::Indented));
        if (UiAccess::enabled())
            permissions += QStringLiteral("\n当前 UIAccess 模式保留管理员权限，插件共享宿主进程权限。");
        permissions += QStringLiteral("\n仅启用可信来源的插件；工作线程不提供进程级沙箱。");
        if (!AppUi::confirm(this, QStringLiteral("启用插件并授权"),
                            record->manifest.name + QLatin1Char('\n') + permissions))
        {
            refresh();
            return;
        }
    }
    QString error;
    if (!manager_->setEnabled(id, enabled, &error) && !error.isEmpty())
        AppUi::error(this, error);
    refresh();
}
void PluginSettingsTab::showDetails(const QString &id)
{
    auto record = manager_->record(id);
    if (!record)
        return;
    auto *scroll = findChild<ElaScrollArea *>();
    const bool fromList = stack_->currentWidget() == list_;
    const int previousScroll = scroll ? scroll->verticalScrollBar()->value() : 0;
    if (fromList) listScroll_ = previousScroll;
    detailKey_ = id;
    if (details_)
    {
        stack_->removeWidget(details_);
        details_->deleteLater();
    }
    details_ = new QWidget(stack_);
    auto *layout = new QVBoxLayout(details_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    auto *back = AppUi::button(QStringLiteral("返回插件列表"), details_);
    layout->addWidget(back, 0, Qt::AlignLeft);
    connect(back, &ElaPushButton::clicked, this,
            [this]
            {
                details_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
                list_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
                stack_->setCurrentWidget(list_);
                for (auto *button : list_->findChildren<ElaPushButton *>())
                    if (button->property("pluginKey").toString() == detailKey_) {
                        button->setFocus(); break;
                    }
                QTimer::singleShot(0, this, [this] {
                    if (auto *scroll = findChild<ElaScrollArea *>())
                        scroll->verticalScrollBar()->setValue(listScroll_);
                });
            });
    auto *informationBody = AppUi::card(layout, QStringLiteral("插件信息"));
    auto *information =
        AppUi::text(QStringLiteral("%1\nID：%2\n版本：%3\n状态：%4\n来源：%5\n%6")
                        .arg(record->manifest.name, record->manifest.id, record->manifest.version,
                             record->state, record->source.isEmpty() ? record->manifest.directory : record->source,
                             record->failure),
                    informationBody->parentWidget());
    information->setTextFormat(Qt::PlainText);
    information->setWordWrap(true);
    information->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    information->setTextInteractionFlags(Qt::TextSelectableByMouse);
    informationBody->addWidget(information);
    auto *actionsBody = AppUi::card(layout, QStringLiteral("插件操作"));
    auto *settings = AppUi::button(QStringLiteral("打开插件设置"), actionsBody->parentWidget());
    settings->setEnabled(record->state == QStringLiteral("运行中") && hasSettings(record));
    actionsBody->addWidget(settings, 0, Qt::AlignLeft);
    connect(settings, &ElaPushButton::clicked, this, [this, id] { ui_->openSettings(id, window()); });
    auto *history = AppUi::button(QStringLiteral("版本历史与回滚"), actionsBody->parentWidget());
    actionsBody->addWidget(history, 0, Qt::AlignLeft);
    connect(history, &ElaPushButton::clicked, this, [this, id] { AppUi::showPluginHistory(manager_->workspace(), id, window()); });
    if (!record->source.isEmpty() && record->key == record->manifest.id) {
        auto *reload = AppUi::button(QStringLiteral("热重载"), actionsBody->parentWidget());
        reload->setEnabled(!manager_->publishing() && record->state != QStringLiteral("启动中") &&
                           record->state != QStringLiteral("停止中"));
        actionsBody->addWidget(reload, 0, Qt::AlignLeft);
        connect(reload, &ElaPushButton::clicked, this, [this, id, source = record->source] {
            QString error;
            const auto package = manager_->stagePackage(source, &error);
            if (!package.directory || package.manifest.id != id) {
                AppUi::error(window(), error.isEmpty() ? QStringLiteral("插件 ID 已变化") : error);
                return;
            }
            const auto current = manager_->record(id);
            if (!current) return;
            const auto permissions = ConfigManager::instance().config().pluginConfig.plugins.value(id).grantedPermissions;
            const auto summary = QStringLiteral("%1\n%2 → %3\n\n已授予权限\n%4\n候选包权限\n%5\n\n插件与应用在同一进程运行。")
                .arg(package.manifest.name, current->manifest.version, package.manifest.version,
                     QString::fromUtf8(QJsonDocument(permissions).toJson()),
                     QString::fromUtf8(QJsonDocument(package.manifest.permissions).toJson()));
            if (!AppUi::confirm(window(), QStringLiteral("确认热重载与授权"), summary)) return;
            const QPointer<PluginSettingsTab> tab(this);
            if (!manager_->publish(package, PluginManager::PublishMode::PreserveEnabled, [tab](const QString &failure) {
                    if (tab && !failure.isEmpty()) AppUi::error(tab->window(), failure);
                }, &error)) {
                AppUi::error(window(), error);
            }
        });
    }
    if (record->state == QStringLiteral("运行故障") || record->state == QStringLiteral("待重新授权") ||
        (record->state == QStringLiteral("已停用") &&
         ConfigManager::instance().config().pluginConfig.plugins.value(record->manifest.id).enabled))
    {
        auto *retry = AppUi::button(QStringLiteral("重新授权并重试"), actionsBody->parentWidget());
        actionsBody->addWidget(retry, 0, Qt::AlignLeft);
        connect(retry, &ElaPushButton::clicked, this,
                [this, id]
                {
                    toggle(id, true);
                    showDetails(id);
                });
    }
    if (!record->pages.isEmpty())
    {
        auto *pagesBody = AppUi::card(layout, QStringLiteral("插件页面"));
        for (auto it = record->pages.begin(); it != record->pages.end(); ++it)
        {
            auto *open = AppUi::button(
                QStringLiteral("打开：") + it->value(QStringLiteral("name")).toString(it.key()), pagesBody->parentWidget());
            pagesBody->addWidget(open, 0, Qt::AlignLeft);
            connect(open, &ElaPushButton::clicked, this,
                    [this, id, contribution = it.key()] { emit ui_->navigationRequested(id, contribution, {}); });
        }
    }
    const auto preference =
        ConfigManager::instance().config().pluginConfig.plugins.value(record->manifest.id);
    auto *permissionsBody = AppUi::card(layout, QStringLiteral("权限信息"));
    auto *permissions =
        AppUi::text(QStringLiteral("声明权限\n%1\n已授予权限\n%2")
                        .arg(QString::fromUtf8(QJsonDocument(record->manifest.permissions).toJson()),
                             QString::fromUtf8(QJsonDocument(preference.grantedPermissions).toJson())),
                    permissionsBody->parentWidget(), 12);
    permissions->setTextFormat(Qt::PlainText);
    permissions->setWordWrap(true);
    permissions->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    permissionsBody->addWidget(permissions);
    auto *logsBody = AppUi::card(layout, QStringLiteral("运行日志"));
    auto *logs = new ElaPlainTextEdit(logsBody->parentWidget());
    logs->setReadOnly(true);
    logs->setPlainText(record->logs.join(QLatin1Char('\n')));
    logs->setMinimumHeight(120);
    logs->setMaximumHeight(220);
    logs->setPlaceholderText(QStringLiteral("暂无日志；重新进入详情以刷新"));
    logsBody->addWidget(logs);
    AppUi::enableTouchScrolling(logs);
    auto *helpBody = AppUi::card(layout);
    auto *uninstall = AppUi::text(
        QStringLiteral(
            "安装与更新：手动复制本地插件目录，新插件默认停用；替换源文件后，在详情中审阅权限并热重载。卸载"
            "：停用后移出插件目录；plugin-data 中的数据默认保留，删除数据需另行确认。"),
        helpBody->parentWidget(), 12);
    uninstall->setWordWrap(true);
    uninstall->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    helpBody->addWidget(uninstall);
    stack_->addWidget(details_);
    list_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    stack_->setCurrentWidget(details_);
    QTimer::singleShot(0, this, [this, fromList, previousScroll] {
        if (auto *scroll = findChild<ElaScrollArea *>())
            scroll->verticalScrollBar()->setValue(fromList ? 0 : previousScroll);
    });
}
