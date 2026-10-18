#include "PluginSettingsTab.h"
#include "../../Core/Plugins/PluginManager.h"
#include "../../Core/ConfigManager/ConfigManager.h"
#include "../../Ui/Plugins/PluginUiHost.h"
#include "../../Ui/UiWidgets.h"
#include "../../Utils/UiAccess/UiAccess.h"
#include <ElaLineEdit.h>
#include <ElaPushButton.h>
#include <ElaToggleSwitch.h>
#include <ElaText.h>
#include <ElaMenu.h>
#include <ElaScrollArea.h>
#include <QJsonDocument>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QPlainTextEdit>
#include <ElaPlainTextEdit.h>
#include <QDesktopServices>
#include <QUrl>
#include <QTimer>
#include <QScrollBar>

PluginSettingsTab::PluginSettingsTab(PluginManager *manager, PluginUiHost *ui, QWidget *parent)
    : ElaScrollPage(parent), manager_(manager), ui_(ui)
{
    auto *layout = AppUi::page(this, QStringLiteral("插件"));
    auto *tools = new QHBoxLayout;
    search_ = new ElaLineEdit(this);
    search_->setPlaceholderText(QStringLiteral("搜索插件名称或 ID"));
    tools->addWidget(search_, 1);
    auto *more = AppUi::button(QStringLiteral("更多"), this);
    tools->addWidget(more);
    layout->addLayout(tools);
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
            AppUi::text(QStringLiteral("恢复模式：本次启动未自动运行第三方插件，可在此手动启用。"), this, 12);
        label->setWordWrap(true);
        layout->insertWidget(1, label);
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
        auto *card = new QWidget(list_);
        auto *body = new QVBoxLayout(card);
        body->setContentsMargins(12, 8, 12, 8);
        body->setSpacing(6);
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
        auto *settings = AppUi::button(QStringLiteral("设置"), card);
        settings->setEnabled(record->state == QStringLiteral("运行中") && !record->settingsPages.isEmpty());
        settings->setToolTip(
            record->settingsPages.isEmpty() ? QStringLiteral("无可配置项；已停用插件需先启用") : QString());
        row->addWidget(settings);
        auto *details = AppUi::button(QStringLiteral("详情"), card);
        details->setProperty("pluginKey", record->key);
        row->addWidget(details);
        body->addLayout(row);
        auto *summary = AppUi::text(
            record->state + (record->updatePending ? QStringLiteral(" · 更新需重启") : QString()) +
                QStringLiteral(" · ") + record->manifest.description.left(100),
            card, 12);
        summary->setWordWrap(true);
        summary->setMinimumWidth(0);
        body->addWidget(summary);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        rows_->addWidget(card, 0, Qt::AlignTop);
        connect(toggle, &ElaToggleSwitch::toggled, this,
                [this, key = record->key](bool enabled) { this->toggle(key, enabled); });
        connect(settings, &ElaPushButton::clicked, this,
                [this, id = record->manifest.id] { ui_->openSettings(id, window()); });
        connect(details, &ElaPushButton::clicked, this, [this, key = record->key] { showDetails(key); });
    }
    if (!count)
    {
        auto *empty = AppUi::text(
            query.isEmpty()
                ? QStringLiteral(
                      "尚未发现插件。将插件目录放入 plugins 后，在“更多”中重新扫描；扫描不会执行插件代码。")
                : QStringLiteral("没有匹配的插件"),
            list_);
        empty->setWordWrap(true);
        rows_->addWidget(empty);
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
    auto *information =
        AppUi::text(QStringLiteral("%1\nID：%2\n版本：%3\n状态：%4\n来源：%5\n%6")
                        .arg(record->manifest.name, record->manifest.id, record->manifest.version,
                             record->state, record->manifest.directory, record->failure),
                    details_);
    information->setTextFormat(Qt::PlainText);
    information->setWordWrap(true);
    information->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    information->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(information);
    auto *settings = AppUi::button(QStringLiteral("打开插件设置"), details_);
    settings->setEnabled(record->state == QStringLiteral("运行中") && !record->settingsPages.isEmpty());
    layout->addWidget(settings, 0, Qt::AlignLeft);
    connect(settings, &ElaPushButton::clicked, this, [this, id] { ui_->openSettings(id, window()); });
    if (record->state == QStringLiteral("运行故障") || record->state == QStringLiteral("待重新授权") ||
        (record->state == QStringLiteral("已停用") &&
         ConfigManager::instance().config().pluginConfig.plugins.value(record->manifest.id).enabled))
    {
        auto *retry = AppUi::button(QStringLiteral("重新授权并重试"), details_);
        layout->addWidget(retry, 0, Qt::AlignLeft);
        connect(retry, &ElaPushButton::clicked, this,
                [this, id]
                {
                    toggle(id, true);
                    showDetails(id);
                });
    }
    for (auto it = record->windows.begin(); it != record->windows.end(); ++it)
    {
        auto *open = AppUi::button(
            QStringLiteral("打开：") + it->value(QStringLiteral("name")).toString(it.key()), details_);
        layout->addWidget(open, 0, Qt::AlignLeft);
        connect(open, &ElaPushButton::clicked, this,
                [this, id, contribution = it.key()] { ui_->openWindow(id, contribution, window()); });
    }
    const auto preference =
        ConfigManager::instance().config().pluginConfig.plugins.value(record->manifest.id);
    auto *permissions =
        AppUi::text(QStringLiteral("声明权限\n%1\n已授予权限\n%2")
                        .arg(QString::fromUtf8(QJsonDocument(record->manifest.permissions).toJson()),
                             QString::fromUtf8(QJsonDocument(preference.grantedPermissions).toJson())),
                    details_, 12);
    permissions->setTextFormat(Qt::PlainText);
    permissions->setWordWrap(true);
    permissions->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(permissions);
    auto *logs = new ElaPlainTextEdit(details_);
    logs->setReadOnly(true);
    logs->setPlainText(record->logs.join(QLatin1Char('\n')));
    logs->setMinimumHeight(120);
    logs->setMaximumHeight(220);
    logs->setPlaceholderText(QStringLiteral("暂无日志；重新进入详情以刷新"));
    layout->addWidget(logs);
    AppUi::enableTouchScrolling(logs);
    auto *uninstall = AppUi::text(
        QStringLiteral(
            "安装与更新：手动复制本地插件目录，新插件默认停用；更新前停用并备份旧目录，重启后使用新版本。卸载"
            "：停用后移出插件目录；plugin-data 中的数据默认保留，删除数据需另行确认。"),
        details_, 12);
    uninstall->setWordWrap(true);
    uninstall->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(uninstall);
    stack_->addWidget(details_);
    list_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    stack_->setCurrentWidget(details_);
    QTimer::singleShot(0, this, [this, fromList, previousScroll] {
        if (auto *scroll = findChild<ElaScrollArea *>())
            scroll->verticalScrollBar()->setValue(fromList ? 0 : previousScroll);
    });
}
