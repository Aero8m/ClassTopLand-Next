#include "PluginCenterWindow.h"
#include "../../Core/Plugins/PluginManager.h"
#include "../../SettingsWindow/tabs/PluginSettingsTab.h"
#include "../UiWidgets.h"
#include "PluginNativeView.h"
#include "PluginUiHost.h"
#include <ElaNavigationBar.h>
#include <QCloseEvent>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

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
    setNavigationBarWidth(180);
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
                if (!select(it.key()))
                    QTimer::singleShot(0, this, [this] {
                        select(QStringLiteral("management"));
                        navigation(management_->property("ElaPageKey").toString());
                    });
                return;
            }
    });
    connect(manager_, &PluginManager::contributionsChanged, this, &PluginCenterWindow::refresh);
    auto guardState = [this](bool guarding) {
        if (auto *bar = findChild<ElaNavigationBar *>())
            bar->setEnabled(!guarding);
        management_->setEnabled(!guarding);
    };
    connect(ui_, &PluginUiHost::guardStateChanged, this, guardState);
    guardState(ui_->guarding());
    refresh();
}
void PluginCenterWindow::refresh()
{
    const auto current = currentKey_;
    const QSignalBlocker blocker(this);
    QSet<QString> available{QStringLiteral("management")};
    auto add = [this, &available](const QString &title, const QString &key) {
        available.insert(key);
        if (auto *container = navigationPages_.value(key))
        {
            setNavigationNodeTitle(container->property("ElaPageKey").toString(), title);
            return;
        }
        // Keep a stable Ela page while its plugin view is loaded on demand.
        auto *container = new QWidget(this);
        auto *layout = new QVBoxLayout(container);
        layout->setContentsMargins(12, 12, 12, 12);
        layout->setSpacing(0);
        addPageNode(title, container, key.startsWith(QStringLiteral("component:"))
                                          ? ElaIconType::Gear : ElaIconType::FileLines);
        navigationPages_.insert(key, container);
    };
    for (const auto &record : manager_->records())
        if (record->state == QStringLiteral("运行中"))
            for (const auto &item : record->manifest.contributions.value(QStringLiteral("pages")).toArray())
            {
                const auto page = item.toObject();
                if (record->pages.contains(page.value(QStringLiteral("id")).toString()))
                    add(record->manifest.name + QStringLiteral(" · ") +
                            page.value(QStringLiteral("name")).toString(),
                        record->manifest.id + QLatin1Char('/') + page.value(QStringLiteral("id")).toString());
            }
    const auto keys = pages_.keys();
    for (const auto &key : keys)
    {
        if (!pages_.value(key))
        {
            pages_.remove(key);
            continue;
        }
        if (key.startsWith(QStringLiteral("component:")))
            add(QStringLiteral("组件设置 · ") + key.mid(10), key);
    }
    const auto navigationKeys = navigationPages_.keys();
    for (const auto &key : navigationKeys)
        if (!available.contains(key))
        {
            auto *container = navigationPages_.take(key);
            removeNavigationNode(container->property("ElaPageKey").toString());
            if (auto view = pages_.take(key))
                view->deleteLater();
            // ElaWindow removes the route but leaves widget ownership to us.
            container->deleteLater();
        }
    if (!select(available.contains(current) ? current : QStringLiteral("management")))
        currentKey_ = QStringLiteral("management");
    navigation(navigationPages_.value(currentKey_)->property("ElaPageKey").toString());
}
bool PluginCenterWindow::select(const QString &key)
{
    if (closing_)
        return false;
    if (key == QStringLiteral("management"))
    {
        currentKey_ = key;
        return true;
    }
    auto *container = navigationPages_.value(key);
    if (!container)
        return false;
    auto page = pages_.value(key);
    if (!page)
    {
        if (key.startsWith(QStringLiteral("component:")))
            page = ui_->createComponentSettings(key.mid(10), container);
        else
        {
            const auto parts = key.split('/');
            if (parts.size() == 2)
                page = ui_->createPage(parts[0], parts[1], container);
        }
        if (!page)
        {
            AppUi::error(this, QStringLiteral("插件页面不可用或未声明实例设置"));
            return false;
        }
        pages_.insert(key, page);
    }
    if (page->parentWidget() != container || container->layout()->indexOf(page) < 0)
        container->layout()->addWidget(page);
    currentKey_ = key;
    return true;
}
void PluginCenterWindow::navigate(const QString &plugin, const QString &page,
                                  const QString &componentInstance)
{
    if (closing_)
        return;
    const auto key = componentInstance.isEmpty() ? plugin + QLatin1Char('/') + page
                                                 : QStringLiteral("component:") + componentInstance;
    if (!componentInstance.isEmpty() && !pages_.value(key))
    {
        auto *view = ui_->createComponentSettings(componentInstance, this);
        if (!view)
        {
            AppUi::error(this, QStringLiteral("插件页面不可用或未声明实例设置"));
            return;
        }
        pages_.insert(key, view);
    }
    refresh();
    if (select(key))
        navigation(navigationPages_.value(key)->property("ElaPageKey").toString());
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
        self->pages_.clear();
        self->currentKey_ = QStringLiteral("management");
        self->refresh();
        self->approved_ = true;
        self->close();
    });
}
