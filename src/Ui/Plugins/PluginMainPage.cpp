#include "PluginMainPage.h"
#include "../../Core/ConfigManager/ConfigManager.h"
#include "../../Core/Plugins/PluginManager.h"
#include "../UiWidgets.h"
#include "PluginNativeView.h"
#include "PluginUiHost.h"
#include <ElaPivot.h>
#include <QApplication>
#include <QHelpEvent>
#include <QFontMetrics>
#include <QItemSelectionModel>
#include <QListView>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace
{
class PivotDelegate final : public QStyledItemDelegate
{
  public:
    explicit PivotDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                   const QStyleOptionViewItem &, const QModelIndex &index) override
    {
        if (!index.isValid())
            return false;
        const auto titles = view->property("pluginTabTitles").toStringList();
        QToolTip::showText(event->globalPos(), titles.value(index.row(), index.data().toString()),
                          view, view->visualRect(index));
        return true;
    }
};
bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}
}

PluginMainPage::PluginMainPage(PluginManager *manager, PluginUiHost *ui, const QString &plugin,
                               QWidget *parent)
    : QWidget(parent), manager_(manager), ui_(ui), plugin_(plugin)
{
    setObjectName(QStringLiteral("pluginMainPage"));
    setProperty("pluginId", plugin);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    pivotPanel_ = new QWidget(this);
    auto *header = new QVBoxLayout(pivotPanel_);
    header->setContentsMargins(24, 12, 24, 0);
    pivot_ = new ElaPivot(pivotPanel_);
    pivot_->setAccessibleName(QStringLiteral("插件页面"));
    header->addWidget(pivot_);
    layout->addWidget(pivotPanel_);
    stack_ = new QStackedWidget(this);
    empty_ = new QWidget(stack_);
    stack_->addWidget(empty_);
    layout->addWidget(stack_, 1);
    pivotView_ = pivot_->findChild<QListView *>();
    if (pivotView_)
    {
        pivotView_->setItemDelegate(new PivotDelegate(pivotView_));
        pivotView_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        pivotView_->viewport()->installEventFilter(this);
        connect(pivotView_->selectionModel(), &QItemSelectionModel::currentChanged, this,
                [this](const QModelIndex &index) {
            if (!rebuilding_ && pivot_->isEnabled() && index.isValid() && index.row() < tabs_.size())
            {
                // Let ElaPivot finish mouse handling before restoring a failed selection.
                QTimer::singleShot(0, this, [this] {
                    const auto index = pivotView_->currentIndex();
                    if (!pivot_->isEnabled() || !index.isValid() || index.row() >= tabs_.size())
                        return;
                    QString error;
                    if (!select(tabs_[index.row()].key, &error))
                        AppUi::error(window(), error);
                });
            }
        });
    }
    connect(pivot_, &ElaPivot::pivotClicked, this, [this](int index) {
        if (rebuilding_ || !pivot_->isEnabled() || index < 0 || index >= tabs_.size())
            return;
        QString error;
        if (!select(tabs_[index].key, &error))
            AppUi::error(window(), error);
    });
    sync();
}

bool PluginMainPage::sync()
{
    const auto record = manager_->record(plugin_);
    if (!record || record->state != QStringLiteral("运行中"))
        return false;
    if (generation_ != record->generation)
    {
        for (const auto &tab : tabs_)
            if (tab.view)
                delete tab.view;
        tabs_.clear();
        current_.clear();
        generation_ = record->generation;
    }
    QVector<Tab> next;
    const auto main = record->manifest.contributions.value(QStringLiteral("mainPage")).toString();
    auto append = [&](const QJsonObject &page) {
        const auto id = page.value(QStringLiteral("id")).toString();
        if (!record->pages.contains(id))
            return;
        Tab tab{QStringLiteral("page:") + id, page.value(QStringLiteral("name")).toString(), {}, {}};
        for (const auto &old : tabs_)
            if (old.key == tab.key)
                tab.view = old.view;
        next.append(tab);
    };
    if (!main.isEmpty())
        append(record->pages.value(main));
    for (const auto &item : record->manifest.contributions.value(QStringLiteral("pages")).toArray())
        if (item.toObject().value(QStringLiteral("id")).toString() != main)
            append(item.toObject());
    const auto components = ConfigManager::instance().config().courseBarConfig.components;
    for (const auto &old : tabs_)
    {
        if (old.component.isEmpty() || !old.view)
            continue;
        for (const auto &component : components)
        {
            const auto parts = component.typeId.split('/');
            if (component.instanceId == old.component && parts.size() == 2 && parts[0] == plugin_ &&
                record->components.value(parts[1]).contains(QStringLiteral("settingsUi")))
            {
                next.append(old);
                break;
            }
        }
    }
    for (const auto &old : tabs_)
    {
        bool keep = false;
        for (const auto &tab : next)
            if (tab.key == old.key)
                keep = true;
        if (!keep && old.view)
            delete old.view;
    }
    tabs_ = next;
    rebuildPivot();
    bool found = false;
    for (const auto &tab : tabs_)
        if (tab.key == current_)
            found = true;
    if (!found)
    {
        current_.clear();
        stack_->setCurrentWidget(empty_);
    }
    updateSelection();
    return !tabs_.isEmpty();
}

void PluginMainPage::rebuildPivot()
{
    QStringList titles, fullTitles;
    QFont font = pivotView_ ? pivotView_->font() : pivot_->font();
    font.setBold(true);
    const QFontMetrics metrics(font);
    for (const auto &tab : tabs_)
    {
        fullTitles.append(tab.title);
        titles.append(metrics.elidedText(tab.title, Qt::ElideRight, 200));
    }
    if (pivotView_)
        pivotView_->setProperty("pluginTabTitles", fullTitles);
    if (titles != pivot_->getPivotTitle())
    {
        rebuilding_ = true;
        const QSignalBlocker blocker(pivot_);
        pivot_->clearPivot();
        for (const auto &title : titles)
            pivot_->appendPivot(title);
        rebuilding_ = false;
    }
    pivotPanel_->setVisible(tabs_.size() > 1);
}

void PluginMainPage::updateSelection()
{
    rebuilding_ = true;
    for (int i = 0; i < tabs_.size(); ++i)
        if (tabs_[i].key == current_)
        {
            if (pivotView_)
            {
                const auto index = pivotView_->model()->index(i, 0);
                const QSignalBlocker blocker(pivotView_->selectionModel());
                // Keyboard selection changes the list before ElaPivot can update its mark.
                // Force the public setter to run, then restore selection even if its mark is unchanged.
                if (pivotView_->currentIndex() == index)
                    pivotView_->setCurrentIndex({});
                pivot_->setCurrentIndex(i);
                pivotView_->setCurrentIndex(index);
                pivotView_->scrollTo(index, QAbstractItemView::EnsureVisible);
            }
            else
                pivot_->setCurrentIndex(i);
            break;
        }
    rebuilding_ = false;
}

bool PluginMainPage::select(const QString &key, QString *error, bool existingOnly)
{
    const auto record = manager_->record(plugin_);
    if (!record || record->state != QStringLiteral("运行中") || record->generation != generation_)
        return fail(error, QStringLiteral("插件页面已失效"));
    for (auto &tab : tabs_)
        if (tab.key == key)
        {
            if (!tab.view)
            {
                if (existingOnly || ui_->guarding())
                    return fail(error, QStringLiteral("当前不可打开新页面"));
                tab.view = tab.component.isEmpty() ? ui_->createPage(plugin_, key.mid(5), stack_, error)
                                                   : ui_->createComponentSettings(tab.component, stack_, error);
                if (!tab.view)
                {
                    updateSelection();
                    return false;
                }
                stack_->addWidget(tab.view);
                connect(tab.view, &QObject::destroyed, this, [this] {
                    QTimer::singleShot(0, this, [this] {
                        sync();
                        emit contentChanged();
                    });
                });
            }
            current_ = key;
            stack_->setCurrentWidget(tab.view);
            updateSelection();
            return true;
        }
    return fail(error, QStringLiteral("插件页面不存在"));
}

bool PluginMainPage::selectPage(const QString &page, QString *error)
{
    if (tabs_.isEmpty())
        return fail(error, QStringLiteral("插件没有页面"));
    return select(page.isEmpty() ? (current_.isEmpty() ? tabs_.first().key : current_)
                                 : QStringLiteral("page:") + page, error);
}

bool PluginMainPage::selectComponent(const QString &instance, QString *error)
{
    const auto key = QStringLiteral("component:") + instance;
    for (const auto &tab : tabs_)
        if (tab.key == key)
            return select(key, error);
    if (ui_->guarding())
        return fail(error, QStringLiteral("当前不可打开新页面"));
    QString title;
    for (const auto &component : ConfigManager::instance().config().courseBarConfig.components)
        if (component.instanceId == instance)
        {
            const auto parts = component.typeId.split('/');
            if (parts.size() == 2 && parts[0] == plugin_)
                title = manager_->definition(plugin_, QStringLiteral("component"), parts[1])
                            .value(QStringLiteral("name")).toString();
        }
    if (title.isEmpty())
        return fail(error, QStringLiteral("组件实例不存在"));
    tabs_.append({key, QStringLiteral("%1 设置 · %2").arg(title, instance), instance, {}});
    if (!select(key, error))
    {
        tabs_.removeLast();
        return false;
    }
    rebuildPivot();
    updateSelection();
    return true;
}

bool PluginMainPage::revealView(const QString &instance)
{
    for (const auto &tab : tabs_)
        if (tab.view && tab.view->property("pluginViewInstance").toString() == instance)
            return select(tab.key, nullptr, true);
    return false;
}

void PluginMainPage::setNavigationEnabled(bool enabled)
{
    pivot_->setEnabled(enabled);
}

bool PluginMainPage::eventFilter(QObject *watched, QEvent *event)
{
    if (pivotView_ && watched == pivotView_->viewport() && event->type() == QEvent::Wheel && pivot_->isEnabled())
    {
        auto *wheel = static_cast<QWheelEvent *>(event);
        const auto delta = wheel->angleDelta();
        auto *bar = pivotView_->horizontalScrollBar();
        bar->setValue(bar->value() - (delta.x() ? delta.x() : delta.y()));
        wheel->accept();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
