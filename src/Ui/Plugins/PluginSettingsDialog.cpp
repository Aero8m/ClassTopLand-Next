#include "PluginSettingsDialog.h"
#include "PluginView.h"
#include "../../Core/Plugins/PluginManager.h"
#include "../../Core/ConfigManager/ConfigManager.h"
#include "../UiWidgets.h"
#include <ElaPivot.h>
#include <ElaText.h>
#include <ElaPushButton.h>
#include <ElaScrollArea.h>
#include <QStackedWidget>
#include <QCloseEvent>
#include <QScreen>
#include <QGuiApplication>
#include <QWindow>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QEventLoop>
#include <QTimer>
#include <QUuid>
#include <QScopedValueRollback>
#include <ElaContentDialog.h>
#include <QJsonDocument>
#include <QListView>
#include <QStyledItemDelegate>
#include <QHelpEvent>
#include <QToolTip>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QItemSelectionModel>

namespace
{
class SettingsPageDelegate final : public QStyledItemDelegate
{
  public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setWidth(qMin(size.width(), 220));
        return size;
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        auto item = option;
        initStyleOption(&item, index);
        auto *style = item.widget->style();
        const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, &item, item.widget);
        item.text = item.fontMetrics.elidedText(item.text, Qt::ElideRight,
                                               qMax(0, item.rect.width() - 2 * margin));
        style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);
    }
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &,
                   const QModelIndex &index) override
    {
        if (!event || event->type() != QEvent::ToolTip || !index.isValid()) return false;
        QToolTip::showText(event->globalPos(), index.data().toString(), view->viewport());
        return true;
    }
};
}

PluginSettingsDialog::PluginSettingsDialog(PluginManager *manager, const QString &plugin,
                                           const QMap<QString, QJsonObject> &definitions, QWidget *parent,
                                           const QString &componentInstance, const QString &storageKey,
                                           const QString &viewKind)
    : ElaDialog(parent), manager_(manager), plugin_(plugin), componentInstance_(componentInstance),
      storageKey_(storageKey), viewKind_(viewKind)
{
    auto record = manager_->record(plugin);
    setWindowTitle((record ? record->manifest.name : plugin) + QStringLiteral(" · 设置"));
    setObjectName(QStringLiteral("pluginSettingsDialog"));
    setWindowModality(Qt::NonModal);
    setIsStayTop(false);
    setIsFixedSize(false);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowButtonFlags(ElaAppBarType::MinimizeButtonHint | ElaAppBarType::MaximizeButtonHint |
                         ElaAppBarType::CloseButtonHint);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);
    pivot_ = new ElaPivot(this);
    pivot_->setAccessibleName(QStringLiteral("插件设置页面"));
    pivot_->setTextPixelSize(16);
    pivot_->setMinimumWidth(0);
    pivot_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    pivot_->setVisible(definitions.size() > 1);
    layout->addWidget(pivot_);
    stack_ = new QStackedWidget(this);
    stack_->setMinimumSize(0, 0);
    layout->addWidget(stack_, 1);
    auto *footer = new QHBoxLayout;
    status_ = AppUi::text(QStringLiteral("正在读取设置…"), this, 12);
    status_->setWordWrap(true);
    status_->setMinimumWidth(0);
    status_->setMaximumHeight(52);
    footer->addWidget(status_, 1);
    discard_ = AppUi::button(QStringLiteral("放弃修改"), this);
    save_ = AppUi::button(QStringLiteral("保存"), this, true);
    footer->addWidget(discard_);
    footer->addWidget(save_);
    layout->addLayout(footer);
    connect(pivot_, &ElaPivot::pivotClicked, this, &PluginSettingsDialog::selectPage);
    if (auto *view = pivot_->findChild<QListView *>()) {
        view->setItemDelegate(new SettingsPageDelegate(view));
        view->setWrapping(false);
        view->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        view->setAccessibleName(QStringLiteral("插件设置页签"));
        connect(view->horizontalScrollBar(), &QScrollBar::rangeChanged, this,
                [this, view](int minimum, int maximum) {
                    pivot_->setFixedHeight(40 + (maximum > minimum ? view->horizontalScrollBar()->sizeHint().height() : 0));
                });
        // ElaPivot only emits pivotClicked for mouse clicks. Keep keyboard navigation in sync too.
        connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this,
                [this, view] {
                    QTimer::singleShot(0, this, [this, view] {
                        const int index = view->currentIndex().row();
                        if (index != stack_->currentIndex()) selectPage(index);
                        view->scrollTo(view->currentIndex());
                    });
                });
    }
    connect(save_, &ElaPushButton::clicked, this, &PluginSettingsDialog::save);
    connect(discard_, &ElaPushButton::clicked, this,
            [this]
            {
                for (auto it = pages_.begin(); it != pages_.end(); ++it)
                {
                    it.value()->setValues(baseline_.value(it.key()).toObject());
                    it.value()->markSaved();
                }
                status_->clear();
                refreshState();
            });
    connect(manager_, &PluginManager::viewUpdated, this,
            [this](const QString &instance, const QJsonObject &patch)
            {
                for (auto it = viewInstances_.begin(); it != viewInstances_.end(); ++it)
                    if (it.value() == instance && pages_.contains(it.key()))
                        pages_[it.key()]->patch(patch);
            });
    connect(manager_, &PluginManager::viewTreeReplaced, this,
            [this](const QString &instance, const QJsonObject &tree)
            {
                for (auto it = viewInstances_.begin(); it != viewInstances_.end(); ++it)
                    if (it.value() == instance && pages_.contains(it.key()))
                        pages_[it.key()]->setTree(tree);
            });
    refreshState();
    resize(680, 500);
    constrainToScreen(true);
    if (!componentInstance_.isEmpty())
    {
        bool found = false;
        for (const auto &component : ConfigManager::instance().config().courseBarConfig.components)
            if (component.instanceId == componentInstance_)
            {
                found = true;
                componentBaseline_ = component.settings;
                load(definitions, QJsonObject{{QStringLiteral("component"), component.settings}});
                break;
            }
        if (!found)
        {
            loading_ = false;
            status_->setText(QStringLiteral("组件实例不存在"));
        }
    }
    else
        manager_->storage(plugin_, QStringLiteral("storage.get"),
                          QJsonObject{{QStringLiteral("key"), storageKey_}}, this,
                          [this, definitions](const QJsonObject &error, const QJsonValue &value)
                          {
                              if (!error.isEmpty())
                              {
                                  status_->setText(error.value(QStringLiteral("message")).toString());
                                  return;
                              }
                              if (!value.isNull() && !value.isUndefined() && !value.isObject())
                              {
                                  status_->setText(QStringLiteral("插件设置格式错误，原数据已保留"));
                                  return;
                              }
                              storedValue_ = value.isUndefined() ? QJsonValue(QJsonValue::Null) : value;
                              load(definitions, value.toObject());
                          });
}
void PluginSettingsDialog::load(const QMap<QString, QJsonObject> &definitions, const QJsonObject &values)
{
    extra_ = values;
    for (auto it = definitions.begin(); it != definitions.end(); ++it)
    {
        if (!PluginView::check(it.value()).isEmpty())
        {
            status_->setText(PluginView::check(it.value()));
            return;
        }
        auto *scroll = new ElaScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *page = new PluginView(it.value(), values.value(it.key()).toObject(), scroll);
        page->setMinimumWidth(0);
        scroll->setWidget(page);
        scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
        scroll->viewport()->setAutoFillBackground(false);
        page->setAutoFillBackground(false);
        AppUi::enableTouchScrolling(scroll);
        pages_[it.key()] = page;
        stack_->addWidget(scroll);
        const auto title = it->value(QStringLiteral("name")).toString().trimmed();
        pivot_->appendPivot(title.isEmpty() ? it.key() : title);
        connect(page, &PluginView::changed, this, &PluginSettingsDialog::refreshState);
        const auto instance = QUuid::createUuid().toString(QUuid::WithoutBraces);
        viewInstances_[it.key()] = instance;
        manager_->createView(plugin_,
                             componentInstance_.isEmpty() ? viewKind_ : QStringLiteral("componentSettings"),
                             it.key(), instance, page->values());
        connect(page, &PluginView::action, this,
                [this, instance](const QString &action, const QJsonValue &value)
                { manager_->action(instance, action, value); });
        connect(this, &QObject::destroyed, manager_,
                [manager = manager_, instance] { manager->releaseView(instance); });
    }
    baseline_ = this->values();
    selectPage(0);
    loading_ = false;
    status_->clear();
    refreshState();
}
QJsonObject PluginSettingsDialog::values() const
{
    auto values = extra_;
    for (auto it = pages_.begin(); it != pages_.end(); ++it)
        values.insert(it.key(), it.value()->values());
    return values;
}
void PluginSettingsDialog::selectPage(int index)
{
    if (index < 0 || index >= stack_->count()) return;
    pivot_->setCurrentIndex(index);
    if (auto *view = pivot_->findChild<QListView *>()) {
        // ElaPivot's setter updates its indicator; synchronize the native selection as well.
        const QSignalBlocker blocker(view->selectionModel());
        const auto item = view->model()->index(index, 0);
        view->setCurrentIndex(item);
        view->scrollTo(item);
    }
    stack_->setCurrentIndex(index);
}
bool PluginSettingsDialog::dirty() const
{
    for (const auto *page : pages_)
        if (page->dirty())
            return true;
    return false;
}
void PluginSettingsDialog::refreshState()
{
    const bool changed = dirty();
    save_->setEnabled(!loading_ && !saving_ && changed);
    discard_->setEnabled(!loading_ && !saving_ && changed);
    stack_->setEnabled(!loading_ && !saving_);
    pivot_->setEnabled(!saving_);
    if (!loading_ && !saving_ && status_->text().isEmpty() && changed)
        status_->setText(QStringLiteral("有未保存修改"));
    else if (!changed && status_->text() == QStringLiteral("有未保存修改"))
        status_->clear();
}
void PluginSettingsDialog::save()
{
    if (loading_ || saving_)
        return;
    if (QJsonDocument(values()).toJson(QJsonDocument::Compact).size() > Plugins::MaxMessageBytes)
    {
        status_->setText(QStringLiteral("设置超过 256 KiB，请缩短输入后重试"));
        emit saveFinished(false);
        return;
    }
    for (auto it = pages_.begin(); it != pages_.end(); ++it)
    {
        const auto error = it.value()->validate();
        if (!error.isEmpty())
        {
            const int index = pages_.keys().indexOf(it.key());
            selectPage(index);
            status_->setText(error);
            emit saveFinished(false);
            return;
        }
    }
    saving_ = true;
    status_->setText(QStringLiteral("正在保存…"));
    refreshState();
    const auto candidate = values();
    auto complete = [this, candidate](const QJsonObject &error, const QJsonValue &)
    {
        saving_ = false;
        if (error.isEmpty())
        {
            baseline_ = candidate;
            storedValue_ = candidate;
            componentBaseline_ = candidate.value(QStringLiteral("component")).toObject();
            for (auto *page : pages_)
                page->markSaved();
            status_->setText(QStringLiteral("已保存"));
            manager_->saved(plugin_, candidate);
        }
        else
            status_->setText(QStringLiteral("保存失败：") +
                             error.value(QStringLiteral("message")).toString());
        refreshState();
        emit saveFinished(error.isEmpty());
    };
    if (componentInstance_.isEmpty())
        manager_->storage(plugin_, QStringLiteral("storage.set"),
                          QJsonObject{{QStringLiteral("key"), storageKey_},
                                      {QStringLiteral("value"), candidate},
                                      {QStringLiteral("expected"), storedValue_}},
                          this, complete);
    else
    {
        auto config = ConfigManager::instance().config();
        bool found = false;
        QJsonObject error;
        for (auto &component : config.courseBarConfig.components)
            if (component.instanceId == componentInstance_)
            {
                found = true;
                if (component.settings != componentBaseline_)
                    error = Plugins::error(QStringLiteral("CONFLICT"),
                                           QStringLiteral("组件设置已由其他操作更新，请重新打开"));
                else
                    component.settings = candidate.value(QStringLiteral("component")).toObject();
                break;
            }
        QString message;
        if (!found)
            error = Plugins::error(QStringLiteral("NOT_FOUND"), QStringLiteral("组件已移除"));
        else if (error.isEmpty() && !ConfigManager::instance().commit(config, &message))
            error = Plugins::error(QStringLiteral("SAVE_FAILED"), message);
        complete(error, {});
    }
}
bool PluginSettingsDialog::prepareToClose()
{
    if (forced_)
        return true;
    if (saving_ || resolvingClose_)
        return false;
    if (!dirty())
        return true;
    QScopedValueRollback<bool> resolving(resolvingClose_, true);
    const auto choice = AppUi::unsaved(this, QStringLiteral("插件设置有未保存的更改"));
    if (forced_)
    {
        QTimer::singleShot(0, this, [this] { close(); });
        return true;
    }
    if (choice == AppUi::UnsavedChoice::Cancel)
        return false;
    if (choice == AppUi::UnsavedChoice::Discard)
        return true;
    QEventLoop loop;
    bool success = false;
    QPointer<PluginSettingsDialog> guard = this;
    connect(this, &PluginSettingsDialog::saveFinished, &loop,
            [&](bool ok)
            {
                success = ok;
                loop.quit();
            });
    connect(this, &QObject::destroyed, &loop, &QEventLoop::quit);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    save();
    if (saving_)
        loop.exec();
    if (guard && forced_)
        QTimer::singleShot(0, this, [this] { close(); });
    return guard && (success || forced_);
}
void PluginSettingsDialog::closeEvent(QCloseEvent *event)
{
    if (prepareToClose())
    {
        forced_ = true; // QDialog::closeEvent invokes reject(); do not prompt twice.
        ElaDialog::closeEvent(event);
    }
    else
        event->ignore();
}
void PluginSettingsDialog::reject()
{
    if (prepareToClose())
    {
        forced_ = true;
        ElaDialog::reject();
    }
}
void PluginSettingsDialog::forceClose()
{
    forced_ = true;
    if (resolvingClose_)
    {
        for (auto *prompt : findChildren<ElaContentDialog *>())
            if (prompt->isVisible())
                prompt->reject();
        if (saving_)
            emit saveFinished(false);
        return;
    }
    close();
}
void PluginSettingsDialog::constrainToScreen(bool center)
{
    if (isMaximized())
        return;
    auto *target = center && parentWidget()
                       ? parentWidget()->screen()
                       : QGuiApplication::screenAt(frameGeometry().center());
    if (!target && parentWidget())
        target = parentWidget()->screen();
    if (!target)
        target = QGuiApplication::primaryScreen();
    if (!target)
        return;
    const auto available = target->availableGeometry();
    setMinimumSize(qMin(520, available.width()), qMin(360, available.height()));
    resize(qMin(width(), available.width()), qMin(height(), available.height()));
    auto position = center ? available.center() - rect().center() : pos();
    if (center && parentWidget())
        position = parentWidget()->frameGeometry().center() - rect().center();
    position.setX(
        qBound(available.left(), position.x(), qMax(available.left(), available.right() - width() + 1)));
    position.setY(
        qBound(available.top(), position.y(), qMax(available.top(), available.bottom() - height() + 1)));
    move(position);
}
