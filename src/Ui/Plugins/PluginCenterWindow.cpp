#include "PluginCenterWindow.h"
#include "../../Core/Plugins/PluginManager.h"
#include "../../SettingsWindow/tabs/PluginSettingsTab.h"
#include "../UiWidgets.h"
#include "PluginMainPage.h"
#include "../../Core/ConfigManager/ConfigManager.h"
#include "PluginUiHost.h"
#include <ElaNavigationBar.h>
#include <QCloseEvent>
#include <QSignalBlocker>
#include <QTimer>

PluginCenterWindow::PluginCenterWindow(PluginManager *manager, PluginUiHost *ui, QWidget *parent)
    : ElaWindow(parent), manager_(manager), ui_(ui)
{
    setWindowTitle(QStringLiteral("插件中心"));
    setIsDefaultClosed(false);
    connect(this, &ElaWindow::closeButtonClicked, this, [this] { close(); });
    setWindowButtonFlag(ElaAppBarType::StayTopButtonHint, false);
    setWindowButtonFlag(ElaAppBarType::RouteBackButtonHint, false);
    setWindowButtonFlag(ElaAppBarType::RouteForwardButtonHint, false);
    setWindowButtonFlag(ElaAppBarType::ThemeChangeButtonHint, false);
    setUserInfoCardVisible(false);
    setNavigationBarDisplayMode(ElaNavigationType::Maximal);
    setIsAllowPageOpenInNewWindow(false);
    resize(AppUi::defaultWindowSize);
    setMinimumSize(AppUi::defaultWindowSize);
    management_ = new PluginSettingsTab(manager, ui, this);
    addPageNode(QStringLiteral("插件管理"), management_, ElaIconType::PuzzlePiece);
    navigationPages_.insert(QStringLiteral("management"), management_);
    connect(this, &ElaWindow::navigationNodeClicked, this,
            [this](ElaNavigationType::NavigationNodeType, const QString &nodeKey) {
        for (auto it = navigationPages_.cbegin(); it != navigationPages_.cend(); ++it)
            if (it.value()->property("ElaPageKey").toString() == nodeKey)
            {
                const auto previous = currentKey_;
                QString error;
                if (!select(it.key(), &error))
                {
                    QTimer::singleShot(0, this, [this, previous] {
                        const auto key = navigationPages_.contains(previous) ? previous : QStringLiteral("management");
                        navigation(navigationPages_.value(key)->property("ElaPageKey").toString());
                    });
                    AppUi::error(this, error);
                }
                return;
            }
    });
    connect(manager_, &PluginManager::contributionsChanged, this, &PluginCenterWindow::refresh);
    auto guardState = [this](bool guarding) {
        if (auto *bar = findChild<ElaNavigationBar *>())
            bar->setEnabled(!guarding);
        management_->setEnabled(!guarding);
        for (auto *page : plugins_)
            page->setNavigationEnabled(!guarding);
    };
    connect(ui_, &PluginUiHost::guardStateChanged, this, guardState);
    connect(&ConfigManager::instance(), &ConfigManager::courseBarChanged, this,
            &PluginCenterWindow::refresh, Qt::QueuedConnection);
    connect(ui_, &PluginUiHost::revealRequested, this, [this](const QString &plugin, const QString &instance) {
        auto *container = plugins_.value(plugin);
        if (container && container->revealView(instance))
        {
            currentKey_ = plugin;
            navigation(container->property("ElaPageKey").toString());
        }
    });
    guardState(ui_->guarding());
    refresh();
}
PluginMainPage *PluginCenterWindow::ensurePlugin(const QString &plugin)
{
    if (auto *page = plugins_.value(plugin))
        return page;
    const auto record = manager_->record(plugin);
    if (!record || record->state != QStringLiteral("运行中"))
        return nullptr;
    auto *page = new PluginMainPage(manager_, ui_, plugin, this);
    page->setNavigationEnabled(!ui_->guarding());
    addPageNode(record->manifest.name, page, record->manifest.icon);
    plugins_.insert(plugin, page);
    navigationPages_.insert(plugin, page);
    connect(page, &PluginMainPage::contentChanged, this, &PluginCenterWindow::refresh, Qt::QueuedConnection);
    return page;
}

void PluginCenterWindow::refresh()
{
    const QSignalBlocker blocker(this);
    for (const auto &record : manager_->records())
        if (record->state == QStringLiteral("运行中") && !record->pages.isEmpty())
            ensurePlugin(record->manifest.id);
    const auto keys = plugins_.keys();
    for (const auto &key : keys)
    {
        auto *page = plugins_.value(key);
        if (!page->sync())
        {
            if (currentKey_ == key)
            {
                currentKey_ = QStringLiteral("management");
                navigation(management_->property("ElaPageKey").toString());
            }
            plugins_.remove(key);
            navigationPages_.remove(key);
            removeNavigationNode(page->property("ElaPageKey").toString());
            // ElaWindow removes the route but leaves widget ownership to us.
            page->deleteLater();
        }
        else
            setNavigationNodeTitle(page->property("ElaPageKey").toString(), manager_->record(key)->manifest.name);
    }
    if (!closing_ && !ui_->guarding() && currentKey_ != QStringLiteral("management"))
    {
        QString error;
        if (!select(currentKey_, &error))
        {
            currentKey_ = QStringLiteral("management");
            AppUi::error(this, error);
        }
    }
    navigation(navigationPages_.value(currentKey_)->property("ElaPageKey").toString());
}

bool PluginCenterWindow::select(const QString &key, QString *error)
{
    if (closing_ || ui_->guarding())
    {
        if (error) *error = QStringLiteral("正在处理未保存内容，请稍后重试");
        return false;
    }
    if (key != QStringLiteral("management"))
    {
        auto *page = plugins_.value(key);
        if (!page || !page->selectPage({}, error))
            return false;
    }
    currentKey_ = key;
    return true;
}

bool PluginCenterWindow::navigate(const QString &plugin, const QString &page,
                                  const QString &componentInstance, QString *error)
{
    if (error) error->clear();
    auto fail = [error](const QString &message) {
        if (error) *error = message;
        return false;
    };
    if (closing_ || ui_->guarding())
        return fail(QStringLiteral("正在处理未保存内容，请稍后重试"));
    QString owner = plugin;
    if (!componentInstance.isEmpty())
    {
        owner.clear();
        for (const auto &component : ConfigManager::instance().config().courseBarConfig.components)
            if (component.instanceId == componentInstance)
            {
                const auto parts = component.typeId.split('/');
                if (parts.size() == 2)
                    owner = parts[0];
                break;
            }
        if (owner.isEmpty())
            return fail(QStringLiteral("组件实例不存在"));
    }
    const auto record = manager_->record(owner);
    if (!record || record->state != QStringLiteral("运行中"))
        return fail(QStringLiteral("插件未运行"));
    const auto target = page.isEmpty() ? record->manifest.contributions.value(QStringLiteral("mainPage")).toString() : page;
    if (componentInstance.isEmpty() && (target.isEmpty() || !record->pages.contains(target)))
        return fail(QStringLiteral("插件页面不存在"));
    const bool existed = plugins_.contains(owner);
    auto *container = ensurePlugin(owner);
    const bool selected = componentInstance.isEmpty() ? container->selectPage(target, error)
                                                     : container->selectComponent(componentInstance, error);
    if (!selected)
    {
        if (!existed && record->pages.isEmpty())
            refresh();
        return false;
    }
    currentKey_ = owner;
    navigation(container->property("ElaPageKey").toString());
    return true;
}

void PluginCenterWindow::closeEvent(QCloseEvent *event)
{
    if (approved_)
    {
        approved_ = false;
        ElaWindow::closeEvent(event);
        return;
    }
    event->ignore();
    if (closing_)
        return;
    closing_ = true;
    const QPointer<PluginCenterWindow> self(this);
    ui_->prepareAll([self](bool allowed) {
        if (!self)
            return;
        self->closing_ = false;
        if (!allowed)
            return;
        self->ui_->closePages();
        self->currentKey_ = QStringLiteral("management");
        self->refresh();
        self->approved_ = true;
        self->close();
    });
}
