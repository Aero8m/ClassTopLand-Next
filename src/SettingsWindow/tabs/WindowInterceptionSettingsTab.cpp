#include "WindowInterceptionSettingsTab.h"
#include "../../Ui/UiWidgets.h"

#include <ElaFlowLayout.h>
#include <ElaLineEdit.h>
#include <ElaPushButton.h>
#include <ElaTableView.h>
#include <ElaText.h>
#include <QFileInfo>
#include <QHeaderView>
#include <QPainter>
#include <QShowEvent>
#include <QStandardItemModel>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

namespace {
// Preserve Ela's themed row painting while clipping and eliding long paths/titles.
class WindowItemDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem display(option);
        initStyleOption(&display, index);
        display.displayAlignment = Qt::AlignLeft | Qt::AlignVCenter;
        display.features.setFlag(QStyleOptionViewItem::WrapText, false);
        display.text = display.fontMetrics.elidedText(display.text, Qt::ElideRight, qMax(0, display.rect.width() - 24));
        painter->save();
        painter->setClipRect(display.rect, Qt::IntersectClip);
        display.widget->style()->drawControl(QStyle::CE_ItemViewItem, &display, painter, display.widget);
        painter->restore();
    }
};
}

WindowInterceptionSettingsTab::WindowInterceptionSettingsTab(WindowInterceptionService* service, QWidget* parent)
    : ElaScrollPage(parent), service_(service)
{
    using namespace AppUi;
    setObjectName(QStringLiteral("windowInterceptionSettingsTab"));
    auto* layout = page(this, tr("窗口拦截"), 24);
    if (!service_->supported()) {
        layout->addWidget(text(tr("窗口拦截仅支持 Windows，当前系统暂不可用。"), this));
        layout->addStretch();
        return;
    }
    monitor_ = text({}, this, 12);
    monitor_->setObjectName(QStringLiteral("windowInterceptionMonitorStatus"));
    layout->addWidget(monitor_);
    error_ = text({}, this, 12);
    error_->setObjectName(QStringLiteral("windowInterceptionError"));
    error_->hide();
    if (!service_->lastError().isEmpty() && service_->lastError() != service_->monitoringError()) {
        error_->setText(service_->lastError());
        error_->show();
    }
    layout->addWidget(error_);
    auto* currentCard = section(layout, tr("当前窗口"));
    auto* refreshButton = button(tr("刷新列表"), this);
    refreshButton->setObjectName(QStringLiteral("refreshWindows"));
    currentCard->addWidget(refreshButton, 0, Qt::AlignLeft);
    current_ = table(currentCard, {tr("窗口标题"), tr("所属程序"), tr("状态")}, currentModel_);
    current_->setObjectName(QStringLiteral("currentWindows"));
    current_->setAccessibleName(tr("当前窗口列表"));
    current_->setItemDelegate(new WindowItemDelegate(current_));
    current_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    current_->horizontalHeader()->resizeSection(2, 150);
    currentEmpty_ = text(tr("暂无可操作的窗口"), this, 12);
    currentCard->addWidget(currentEmpty_);
    title_ = new ElaLineEdit(this);
    title_->setObjectName(QStringLiteral("windowTitleContains"));
    title_->setAccessibleName(tr("标题包含（可选）"));
    title_->setPlaceholderText(tr("标题包含（可选）"));
    title_->setToolTip(tr("留空时，匹配同一程序中所有具有相同窗口类名的窗口。"));
    currentCard->addWidget(title_);
    auto* actions = new ElaFlowLayout(0, 6, 6);
    actions->setIsAnimation(false);
    hideSave_ = button(tr("隐藏并保存"), this, true);
    hideSave_->setObjectName(QStringLiteral("hideAndSaveWindow"));
    hideOnce_ = button(tr("仅隐藏一次"), this);
    hideOnce_->setObjectName(QStringLiteral("hideWindowOnce"));
    restore_ = button(tr("恢复所选窗口"), this);
    restore_->setObjectName(QStringLiteral("restoreWindow"));
    for (auto* action : {hideSave_, hideOnce_, restore_}) actions->addWidget(action);
    currentCard->addLayout(actions);

    auto* rulesCard = section(layout, tr("已保存的规则"));
    saved_ = table(rulesCard, {tr("所属程序"), tr("窗口类名"), tr("标题包含"), tr("状态")}, savedModel_);
    saved_->setObjectName(QStringLiteral("windowInterceptionRules"));
    saved_->setAccessibleName(tr("窗口拦截规则列表"));
    saved_->setItemDelegate(new WindowItemDelegate(saved_));
    saved_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    saved_->horizontalHeader()->resizeSection(3, 90);
    rulesEmpty_ = text(tr("暂无规则；选中窗口后点击“隐藏并保存”添加。"), this, 12);
    rulesCard->addWidget(rulesEmpty_);
    auto* ruleActions = new ElaFlowLayout(0, 6, 6);
    ruleActions->setIsAnimation(false);
    disable_ = button(tr("恢复并停用"), this);
    disable_->setObjectName(QStringLiteral("disableWindowRule"));
    enable_ = button(tr("启用规则"), this);
    enable_->setObjectName(QStringLiteral("enableWindowRule"));
    delete_ = button(tr("删除规则"), this);
    delete_->setObjectName(QStringLiteral("deleteWindowRule"));
    for (auto* action : {disable_, enable_, delete_}) ruleActions->addWidget(action);
    rulesCard->addLayout(ruleActions);
    layout->addWidget(text(tr("规则保存后立即生效，关闭设置后仍继续拦截。正常退出或重启时，会恢复本次隐藏的窗口。"), this, 12));
    layout->addStretch();

    connect(refreshButton, &ElaPushButton::clicked, this, &WindowInterceptionSettingsTab::refresh);
    connect(current_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &WindowInterceptionSettingsTab::updateActions);
    connect(saved_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &WindowInterceptionSettingsTab::updateActions);
    connect(service_, &WindowInterceptionService::windowsChanged, this, &WindowInterceptionSettingsTab::scheduleRefresh);
    connect(service_, &WindowInterceptionService::rulesChanged, this, &WindowInterceptionSettingsTab::scheduleRefresh);
    connect(service_, &WindowInterceptionService::errorOccurred, this, [this](const QString& message) {
        error_->setText(message); error_->show();
    });
    connect(hideOnce_, &ElaPushButton::clicked, this, [this] {
        if (const auto window = selectedWindow()) act([&](QString* error) { return service_->hideOnce(*window, error); });
    });
    connect(hideSave_, &ElaPushButton::clicked, this, [this] {
        if (const auto window = selectedWindow()) act([&](QString* error) { return service_->hideAndSave(*window, title_->text(), error); });
    });
    connect(restore_, &ElaPushButton::clicked, this, [this] {
        if (const auto window = selectedWindow()) act([&](QString* error) { return service_->restore(*window, error); });
    });
    connect(disable_, &ElaPushButton::clicked, this, [this] {
        const int index = selected(saved_);
        act([&](QString* error) { return service_->setRuleEnabled(index, false, error); });
    });
    connect(enable_, &ElaPushButton::clicked, this, [this] {
        const int index = selected(saved_);
        act([&](QString* error) { return service_->setRuleEnabled(index, true, error); });
    });
    connect(delete_, &ElaPushButton::clicked, this, [this] {
        const int index = selected(saved_);
        act([&](QString* error) { return service_->deleteRule(index, error); });
    });
    refresh();
}

std::optional<InterceptedWindow> WindowInterceptionSettingsTab::selectedWindow() const
{
    if (!current_) return std::nullopt;
    const int index = AppUi::selected(current_);
    return index >= 0 && index < windows_.size() ? std::optional<InterceptedWindow>(windows_[index]) : std::nullopt;
}

void WindowInterceptionSettingsTab::updateActions()
{
    if (!current_) return;
    const auto window = selectedWindow();
    hideOnce_->setEnabled(window && window->visible);
    hideSave_->setEnabled(window.has_value());
    restore_->setEnabled(window && !window->visible);
    const int index = AppUi::selected(saved_);
    const bool valid = index >= 0 && index < rules_.size();
    disable_->setEnabled(valid && rules_[index].enabled);
    enable_->setEnabled(valid && !rules_[index].enabled);
    delete_->setEnabled(valid);
}

void WindowInterceptionSettingsTab::act(const std::function<bool(QString*)>& action)
{
    error_->hide();
    QString error;
    if (!action(&error)) { error_->setText(error); error_->show(); }
    scheduleRefresh();
}

void WindowInterceptionSettingsTab::scheduleRefresh()
{
    if (refreshPending_) return;
    refreshPending_ = true;
    QTimer::singleShot(0, this, [this] { refreshPending_ = false; refresh(); });
}

void WindowInterceptionSettingsTab::refresh()
{
    using namespace AppUi;
    if (!current_) return;
    const auto selection = selectedWindow();
    const int oldRuleIndex = selected(saved_);
    const auto oldRules = rules_;
    QString error;
    windows_ = service_->windows(&error);
    if (!error.isEmpty()) { error_->setText(error); error_->show(); }
    currentModel_->removeRows(0, currentModel_->rowCount());
    int windowSelection = -1;
    for (int i = 0; i < windows_.size(); ++i) {
        const auto& window = windows_[i];
        appendRow(currentModel_, {window.title.isEmpty() ? tr("无标题窗口") : window.title,
            QFileInfo(window.exePath).fileName(), window.visible ? tr("显示中")
                : window.hiddenByUs ? tr("已隐藏") : tr("已隐藏（来源未知）")}, i);
        currentModel_->item(i, 0)->setToolTip(tr("%1\n窗口类名：%2").arg(window.title, window.className));
        currentModel_->item(i, 1)->setToolTip(window.exePath);
        if (selection && WindowInterceptionService::sameWindow(*selection, window)) windowSelection = i;
    }
    select(current_, windowSelection);
    emptyState(current_, currentEmpty_);
    rules_ = service_->rules();
    savedModel_->removeRows(0, savedModel_->rowCount());
    int ruleSelection = -1;
    for (int i = 0; i < rules_.size(); ++i) {
        const auto& rule = rules_[i];
        appendRow(savedModel_, {QFileInfo(rule.exePath).fileName(), rule.className,
            rule.titleContains.isEmpty() ? tr("不限") : rule.titleContains,
            rule.enabled ? tr("已启用") : tr("已停用")}, i);
        savedModel_->item(i, 0)->setToolTip(rule.exePath);
        if (oldRuleIndex >= 0 && oldRuleIndex < oldRules.size()) {
            const auto& old = oldRules[oldRuleIndex];
            if (old.exePath == rule.exePath && old.className == rule.className && old.titleContains == rule.titleContains)
                ruleSelection = i;
        }
    }
    select(saved_, ruleSelection >= 0 ? ruleSelection : qMin(oldRuleIndex, static_cast<int>(rules_.size()) - 1));
    emptyState(saved_, rulesEmpty_);
    monitor_->setText(service_->monitoringError());
    monitor_->setVisible(!service_->monitoringError().isEmpty());
    updateActions();
}

void WindowInterceptionSettingsTab::showEvent(QShowEvent* event)
{
    ElaScrollPage::showEvent(event);
    refresh();
}
