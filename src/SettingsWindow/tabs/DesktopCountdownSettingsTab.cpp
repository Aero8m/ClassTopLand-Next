#include "DesktopCountdownSettingsTab.h"
#include "../../Core/ConfigManager/ConfigManager.h"
#include "../../Ui/UiWidgets.h"

#include <ElaCalendarPicker.h>
#include <ElaComboBox.h>
#include <ElaLineEdit.h>
#include <ElaSpinBox.h>
#include <ElaText.h>
#include <ElaToggleSwitch.h>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QScopedValueRollback>
#include <QScreen>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

DesktopCountdownSettingsTab::DesktopCountdownSettingsTab(QWidget* parent) : ElaScrollPage(parent)
{
    using namespace AppUi;
    setObjectName(QStringLiteral("desktopCountdownSettingsTab"));
    auto* layout = page(this, tr("桌面倒计时"), 24);
    auto* basics = section(layout, tr("基本设置"));
    const auto row = [this](QVBoxLayout* body, const QString& title, QWidget* editor, const QString& name) {
        auto* line = new QHBoxLayout;
        line->setSpacing(16);
        line->addWidget(text(title, this), 1);
        editor->setObjectName(name);
        editor->setAccessibleName(title);
        line->addWidget(editor);
        body->addLayout(line);
    };
    enable_ = new ElaToggleSwitch(this);
    row(basics, tr("启用桌面倒计时"), enable_, QStringLiteral("desktopCountdownEnable"));
    label_ = new ElaLineEdit(this); label_->setFixedWidth(280);
    row(basics, tr("中文事件名称"), label_, QStringLiteral("desktopCountdownLabelTag"));
    english_ = new ElaLineEdit(this); english_->setFixedWidth(280);
    row(basics, tr("英文事件名称"), english_, QStringLiteral("desktopCountdownEnglishTag"));
    date_ = new ElaCalendarPicker(this); date_->setFixedSize(198, 38);
    row(basics, tr("目标日期"), date_, QStringLiteral("desktopCountdownDate"));
    time_ = new TimePicker(QTime(0, 0), this);
    row(basics, tr("目标时间"), time_, QStringLiteral("desktopCountdownTime"));
    precision_ = new ElaComboBox(this); precision_->setFixedWidth(198);
    precision_->addItem(tr("毫秒（50ms 刷新）"), true);
    precision_->addItem(tr("秒（每秒刷新）"), false);
    row(basics, tr("显示精度"), precision_, QStringLiteral("desktopCountdownPrecision"));
    auto* placement = section(layout, tr("位置与大小"));
    screen_ = new ElaComboBox(this); screen_->setFixedWidth(280);
    row(placement, tr("显示屏"), screen_, QStringLiteral("desktopCountdownScreen"));
    placement_ = new ElaComboBox(this); placement_->setFixedWidth(280);
    placement_->addItem(tr("默认"), true);
    placement_->addItem(tr("自定义可见边距"), false);
    row(placement, tr("布局方式"), placement_, QStringLiteral("desktopCountdownPlacement"));
    const auto spin = [&](const QString& title, const QString& name, int minimum, int maximum, const QString& suffix) {
        auto* control = new ElaSpinBox(this);
        control->setRange(minimum, maximum);
        control->setSuffix(suffix);
        control->setKeyboardTracking(false);
        control->setFixedWidth(160);
        row(placement, title, control, name);
        return control;
    };
    right_ = spin(tr("右侧边距"), QStringLiteral("desktopCountdownRightMargin"), 0,
                  DesktopCountdownConfig::MaximumMargin, QStringLiteral(" px"));
    bottom_ = spin(tr("底部边距"), QStringLiteral("desktopCountdownBottomMargin"), 0,
                   DesktopCountdownConfig::MaximumMargin, QStringLiteral(" px"));
    scale_ = spin(tr("整体缩放"), QStringLiteral("desktopCountdownScale"), 50, 200, QStringLiteral("%"));
    placement->addWidget(text(tr("默认沿用原版 ClassTopLand 的整体留白和右下角位置。选择自定义可见边距后，可按逻辑像素设置内容到屏幕可用区域的距离。屏幕断开时临时使用主屏。"), this, 12));
    layout->addStretch();
    refresh();
    connect(enable_, &ElaToggleSwitch::toggled, this, &DesktopCountdownSettingsTab::save);
    connect(label_, &ElaLineEdit::editingFinished, this, &DesktopCountdownSettingsTab::save);
    connect(english_, &ElaLineEdit::editingFinished, this, &DesktopCountdownSettingsTab::save);
    connect(date_, &ElaCalendarPicker::selectedDateChanged, this, &DesktopCountdownSettingsTab::save);
    connect(time_, &TimePicker::timeChanged, this, &DesktopCountdownSettingsTab::save);
    connect(precision_, &ElaComboBox::currentIndexChanged, this, &DesktopCountdownSettingsTab::save);
    connect(screen_, &ElaComboBox::currentIndexChanged, this, &DesktopCountdownSettingsTab::save);
    connect(placement_, &ElaComboBox::currentIndexChanged, this, &DesktopCountdownSettingsTab::save);
    for (auto* control : {right_, bottom_, scale_})
        connect(control, &ElaSpinBox::valueChanged, this, &DesktopCountdownSettingsTab::save);
    const auto screensChanged = [this] {
        QTimer::singleShot(0, this, &DesktopCountdownSettingsTab::refreshScreens);
    };
    connect(qGuiApp, &QGuiApplication::screenAdded, this, screensChanged);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, screensChanged);
    connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this, screensChanged);
}

void DesktopCountdownSettingsTab::refreshScreens()
{
    const QSignalBlocker blocker(screen_);
    const QString selected = ConfigManager::instance().config().desktopCountdownConfig.screenName;
    screen_->clear();
    screen_->addItem(tr("跟随主屏"), QString());
    for (auto* screen : QGuiApplication::screens())
        screen_->addItem(screen->name(), screen->name());
    int index = screen_->findData(selected);
    if (index < 0) {
        screen_->addItem(tr("%1（未连接，暂用主屏）").arg(selected), selected);
        index = screen_->count() - 1;
    }
    screen_->setCurrentIndex(index);
}

void DesktopCountdownSettingsTab::refresh()
{
    QScopedValueRollback<bool> guard(refreshing_, true);
    const auto& config = ConfigManager::instance().config().desktopCountdownConfig;
    const QSignalBlocker b1(enable_), b2(label_), b3(english_), b4(date_), b5(time_),
        b6(precision_), b7(right_), b8(bottom_), b9(scale_), b10(placement_);
    enable_->setIsToggled(config.enable);
    label_->setText(config.labelTag);
    english_->setText(config.englishTag);
    const QDateTime target = config.targetDateTime.isValid() ? config.targetDateTime.toLocalTime()
        : QDateTime(QDate::currentDate().addDays(1), QTime(0, 0));
    date_->setSelectedDate(target.date());
    time_->setTime(target.time());
    precision_->setCurrentIndex(precision_->findData(config.showMilliseconds));
    placement_->setCurrentIndex(placement_->findData(config.useLegacyPlacement));
    right_->setEnabled(!config.useLegacyPlacement);
    bottom_->setEnabled(!config.useLegacyPlacement);
    right_->setValue(config.rightMargin);
    bottom_->setValue(config.bottomMargin);
    scale_->setValue(config.scalePercent);
    refreshScreens();
}

void DesktopCountdownSettingsTab::save()
{
    if (refreshing_) return;
    Config candidate = ConfigManager::instance().config();
    auto& config = candidate.desktopCountdownConfig;
    config.enable = enable_->getIsToggled();
    config.labelTag = label_->text();
    config.englishTag = english_->text();
    config.targetDateTime = QDateTime(date_->getSelectedDate(), time_->time());
    config.showMilliseconds = precision_->currentData().toBool();
    config.screenName = screen_->currentData().toString();
    config.useLegacyPlacement = placement_->currentData().toBool();
    config.rightMargin = right_->value();
    config.bottomMargin = bottom_->value();
    config.scalePercent = scale_->value();
    QString error;
    if (!ConfigManager::instance().commit(candidate, &error)) {
        refresh();
        AppUi::error(this, tr("桌面倒计时配置保存失败：%1").arg(error));
        return;
    }
    right_->setEnabled(!config.useLegacyPlacement);
    bottom_->setEnabled(!config.useLegacyPlacement);
    emit configChanged();
}

void DesktopCountdownSettingsTab::reloadFromConfig()
{
    refresh();
}

void DesktopCountdownSettingsTab::showEvent(QShowEvent* event)
{
    ElaScrollPage::showEvent(event);
    refresh();
}
