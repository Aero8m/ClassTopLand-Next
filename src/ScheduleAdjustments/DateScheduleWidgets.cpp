#include "DateScheduleWidgets.h"
#include "../ProfileEditWindow/ProfileEditWidgets.h"
#include "../Core/ThemeManager/ThemeManager.h"
#include <ElaComboBox.h>
#include <ElaPushButton.h>
#include <ElaScrollArea.h>
#include <ElaScrollBar.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPainter>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {
const QStringList dayNames = {QStringLiteral("周一"), QStringLiteral("周二"), QStringLiteral("周三"),
    QStringLiteral("周四"), QStringLiteral("周五"), QStringLiteral("周六"), QStringLiteral("周日")};

QString modeName(WeekScheduleMode mode)
{
    switch (mode) {
    case WeekScheduleMode::All: return QStringLiteral("全部周");
    case WeekScheduleMode::Odd: return QStringLiteral("单周");
    case WeekScheduleMode::Even: return QStringLiteral("双周");
    }
    return {};
}

void fillSources(ElaComboBox* combo, const Profile& profile, const QString& preferred)
{
    QSignalBlocker blocker(combo);
    combo->clear();
    combo->addItem(QStringLiteral("请选择来源课表"), QString());
    for (const auto& week : profile.schedules)
        combo->addItem(week.name + QStringLiteral(" · ") + modeName(week.mode), week.id);
    QString id = preferred;
    if (combo->findData(id) <= 0) id = profile.activeWeekScheduleId;
    if (id.isEmpty() && profile.schedules.size() == 1) id = profile.schedules[0].id;
    combo->setCurrentIndex(qMax(0, combo->findData(id)));
}

ElaPushButton* action(const QString& text, QWidget* parent, bool primary = false)
{
    auto* button = new ElaPushButton(text, parent);
    button->setFixedHeight(38);
    button->setMinimumWidth(76);
    button->setBorderRadius(6);
    if (primary) ProfileEditUi::primary(button);
    return button;
}

QString sourceText(const Profile& profile, const DateScheduleOverride& record)
{
    for (const auto& week : profile.schedules)
        if (week.id == record.sourceWeekScheduleId)
            return week.name + QStringLiteral(" · ") + dayNames.value(record.sourceWeekday - 1);
    return QStringLiteral("来源课表已删除");
}

class CourseDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override { return {300, 62}; }
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const auto mode = eTheme->getThemeMode();
        const bool selected = index.data(Qt::CheckStateRole).toInt() == Qt::Checked;
        const QRect rect = option.rect.adjusted(0, 3, -2, -3);
        painter->setPen(Qt::NoPen);
        painter->setBrush(selected ? ElaThemeColor(mode, PrimaryNormal) : ElaThemeColor(mode, BasicBase));
        painter->drawRoundedRect(rect, 7, 7);
        painter->setBrush(selected ? ElaThemeColor(mode, BasicText) : ElaThemeColor(mode, PrimaryNormal));
        painter->drawRoundedRect(QRect(rect.left() + 10, rect.top() + 14, 4, rect.height() - 28), 2, 2);
        painter->setPen(selected && mode == ElaThemeType::Light ? Qt::white : ElaThemeColor(mode, BasicText));
        QFont font = option.font;
        font.setPixelSize(16); font.setBold(true); painter->setFont(font);
        const QString times = index.data(Qt::UserRole).toString();
        const int timeWidth = QFontMetrics(font).horizontalAdvance(times) + 20;
        const QRect titleRect = rect.adjusted(26, 0, -timeWidth - 12, 0);
        painter->drawText(titleRect, Qt::AlignVCenter | Qt::AlignLeft,
                          QFontMetrics(font).elidedText(index.data().toString(), Qt::ElideRight, titleRect.width()));
        font.setBold(false); font.setPixelSize(14); painter->setFont(font);
        painter->drawText(rect.adjusted(12, 0, -12, 0), Qt::AlignVCenter | Qt::AlignRight, times);
        if (option.state & QStyle::State_HasFocus) {
            painter->setBrush(Qt::NoBrush); painter->setPen(ElaThemeColor(mode, BasicText));
            painter->drawRoundedRect(rect.adjusted(1, 1, -1, -1), 7, 7);
        }
        painter->restore();
    }
};

class DialogFooter final : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const auto mode = eTheme->getThemeMode();
        painter.fillRect(rect(), ElaThemeColor(mode, DialogLayoutArea));
        painter.setPen(ElaThemeColor(mode, BasicBorder));
        painter.drawLine(rect().topLeft(), rect().topRight());
    }
};

void styleDayButton(ElaPushButton* button)
{
    for (const auto mode : {ElaThemeType::Light, ElaThemeType::Dark}) {
        const auto background = button->isChecked() ? ElaThemeColor(mode, PrimaryNormal) : ElaThemeColor(mode, BasicBase);
        const auto hover = button->isChecked() ? ElaThemeColor(mode, PrimaryHover) : ElaThemeColor(mode, BasicHover);
        const auto press = button->isChecked() ? ElaThemeColor(mode, PrimaryPress) : ElaThemeColor(mode, BasicPress);
        const auto text = button->isChecked() && mode == ElaThemeType::Light ? QColor(Qt::white) : ElaThemeColor(mode, BasicText);
        if (mode == ElaThemeType::Light) {
            button->setLightDefaultColor(background); button->setLightHoverColor(hover);
            button->setLightPressColor(press); button->setLightTextColor(text);
        } else {
            button->setDarkDefaultColor(background); button->setDarkHoverColor(hover);
            button->setDarkPressColor(press); button->setDarkTextColor(text);
        }
    }
    button->update();
}
}

FutureDatePicker::FutureDatePicker(QWidget* parent) : ElaCalendarPicker(parent)
{
    // ElaCalendarPicker defaults to a fixed 120px width; allow the form to size it.
    setMinimumWidth(0);
    setMaximumWidth(QWIDGETSIZE_MAX);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(38);
    setBorderRadius(6);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(tr("目标日期"));
    minimumDate_ = QDate::currentDate();
    publishedDate_ = getSelectedDate();
    connect(this, &ElaCalendarPicker::selectedDateChanged, this, [this] {
        const auto selected = getSelectedDate();
        if (selected < minimumDate_) {
            QSignalBlocker blocker(this);
            setSelectedDate(qMax(minimumDate_, publishedDate_));
            update();
            return;
        }
        // Ela's setter also emits after the calendar signal, and emits for
        // rejected values. Publish only an actual accepted date change.
        if (selected != publishedDate_) {
            publishedDate_ = selected;
            emit dateChanged(selected);
        }
    });
    refreshMinimum();
}

void FutureDatePicker::setDate(const QDate& value)
{
    if (!value.isValid()) return;
    setSelectedDate(qMax(minimumDate_, value));
    if (signalsBlocked()) publishedDate_ = date();
}

void FutureDatePicker::refreshMinimum()
{
    const auto today = QDate::currentDate();
    // ElaCalendar::setMinimumDate in this bundled version writes the maximum
    // date. Enforce the lower bound here without truncating future selection.
    minimumDate_ = today;
    if (date() < today) setDate(today);
}

RescheduleWindow::RescheduleWindow(QWidget* parent) : ElaDialog(parent)
{
    setObjectName(QStringLiteral("RescheduleWindow"));
    setWindowTitle(tr("调休"));
    setWindowIcon(QIcon(QStringLiteral(":/res/images/icon.png")));
    setAttribute(Qt::WA_QuitOnClose, false);
    setIsDefaultClosed(false);
    setIsStayTop(false);
    setWindowModality(Qt::NonModal);
    setWindowButtonFlags(ElaAppBarType::CloseButtonHint);
    resize(460, 400);
    setMinimumSize(360, 320);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* scroll = new ElaScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumSize(0, 0);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setIsAnimation(Qt::Vertical, false);
    scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget { background: transparent; border: 0; }"));
    auto* content = new QWidget;
    auto* body = new QVBoxLayout(content);
    body->setSizeConstraint(QLayout::SetMinimumSize);
    body->setContentsMargins(24, 20, 24, 16);
    body->setSpacing(16);
    body->addWidget(ProfileEditUi::text(tr("选择目标日期，并指定要使用的星期课表。"), content, 14));

    auto* fields = new QGridLayout;
    fields->setHorizontalSpacing(16);
    fields->setVerticalSpacing(12);
    fields->setColumnStretch(1, 1);
    date_ = new FutureDatePicker(content);
    date_->setObjectName(QStringLiteral("rescheduleDate"));
    source_ = new ElaComboBox(content);
    source_->setObjectName(QStringLiteral("rescheduleSource"));
    source_->setAccessibleName(tr("来源周课表"));
    source_->setFixedHeight(38);
    auto* dateLabel = ProfileEditUi::text(tr("目标日期"), content);
    dateLabel->setWordWrap(false); dateLabel->setBuddy(date_);
    auto* sourceLabel = ProfileEditUi::text(tr("来源课表"), content);
    sourceLabel->setWordWrap(false); sourceLabel->setBuddy(source_);
    fields->addWidget(dateLabel, 0, 0);
    fields->addWidget(date_, 0, 1);
    fields->addWidget(sourceLabel, 1, 0);
    fields->addWidget(source_, 1, 1);
    body->addLayout(fields);

    auto* weekdaySection = new QVBoxLayout;
    weekdaySection->setSpacing(8);
    weekdaySection->addWidget(ProfileEditUi::text(tr("来源星期"), content));
    weekdays_ = new QButtonGroup(this);
    weekdays_->setExclusive(true);
    auto* days = new QGridLayout;
    days->setSpacing(4);
    for (int i = 0; i < 7; ++i) {
        auto* button = action(dayNames[i], content);
        button->setFixedHeight(36);
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        button->setCheckable(true);
        button->setObjectName(QStringLiteral("rescheduleWeekday%1").arg(i + 1));
        weekdays_->addButton(button, i + 1);
        connect(button, &ElaPushButton::toggled, button, [button] { styleDayButton(button); });
        connect(&ThemeManager::instance(), &ThemeManager::themeColorChanged, button, [button] { styleDayButton(button); });
        styleDayButton(button);
        days->setColumnStretch(i, 1);
        days->addWidget(button, 0, i);
    }
    weekdays_->button(QDate::currentDate().dayOfWeek())->setChecked(true);
    weekdaySection->addLayout(days);
    body->addLayout(weekdaySection);
    status_ = ProfileEditUi::text({}, content, 12);
    body->addWidget(status_);
    error_ = ProfileEditUi::text({}, content, 12);
    error_->setObjectName(QStringLiteral("rescheduleError"));
    error_->hide(); body->addWidget(error_);
    body->addStretch();
    scroll->setWidget(content);
    content->setAutoFillBackground(false);
    scroll->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    layout->addWidget(scroll, 1);

    auto* footer = new DialogFooter(this);
    footer->setObjectName(QStringLiteral("rescheduleFooter"));
    footer->setFixedHeight(72);
    auto* buttons = new QHBoxLayout(footer);
    buttons->setContentsMargins(20, 14, 20, 14);
    buttons->setSpacing(8);
    restore_ = action(tr("恢复原课表"), footer);
    restore_->setObjectName(QStringLiteral("restoreReschedule"));
    restore_->setMinimumWidth(100);
    restore_->setToolTip(tr("移除所选日期的全部调休和换课安排"));
    auto* cancel = action(tr("取消"), footer);
    cancel->setObjectName(QStringLiteral("cancelReschedule"));
    confirm_ = action(tr("应用调休"), footer, true);
    confirm_->setObjectName(QStringLiteral("confirmReschedule"));
    confirm_->setMinimumWidth(96);
    buttons->addWidget(restore_);
    buttons->addStretch();
    buttons->addWidget(cancel);
    buttons->addWidget(confirm_);
    layout->addWidget(footer);

    connect(date_, &FutureDatePicker::dateChanged, this, [this] { updateActions(); });
    connect(source_, &QComboBox::currentIndexChanged, this, [this] { updateActions(); });
    connect(weekdays_, &QButtonGroup::idClicked, this, [this] { updateActions(); });
    connect(confirm_, &ElaPushButton::clicked, this, [this] {
        emit rescheduleRequested(date_->date(), source_->currentData().toString(), weekdays_->checkedId());
    });
    connect(restore_, &ElaPushButton::clicked, this, [this] { emit restoreRequested(date_->date()); });
    connect(cancel, &ElaPushButton::clicked, this, &QDialog::reject);
    connect(this, &ElaDialog::closeButtonClicked, this, &QDialog::reject);
    connect(eTheme, &ElaTheme::themeModeChanged, footer, [footer] { footer->update(); });
}

void RescheduleWindow::refreshProfile(const Profile& profile)
{
    profile_ = profile;
    date_->refreshMinimum();
    fillSources(source_, profile_, source_->currentData().toString());
    updateActions();
}

void RescheduleWindow::updateActions()
{
    error_->hide();
    const auto* record = DateSchedule::find(profile_, date_->date());
    restore_->setEnabled(record != nullptr);
    const auto* day = DateSchedule::sourceDay(profile_, source_->currentData().toString(), weekdays_->checkedId());
    QString validation;
    const bool valid = day && DateSchedule::validateClasses(day->classes, &validation);
    confirm_->setEnabled(date_->date() >= QDate::currentDate() && valid);
    if (record) status_->setText(tr("已有特殊安排（%1）。确定将替换该日期全部调休和换课。").arg(sourceText(profile_, *record)));
    else if (!day) status_->setText(tr("请选择来源课表及其中已建立的星期。"));
    else if (!valid) status_->setText(tr("来源日课程无效：%1").arg(validation));
    else status_->setText(day->classes.isEmpty() ? tr("来源日无课程，确认后该日期将无课。") : tr("仅对所选日期生效，重启后保留。"));
}

void RescheduleWindow::showError(const QString& message)
{
    error_->setText(message); error_->show();
}
void RescheduleWindow::closeEvent(QCloseEvent* event) { hide(); event->ignore(); }
void RescheduleWindow::reject() { hide(); }

SwapClassesWindow::SwapClassesWindow(QWidget* parent) : ElaWidget(parent)
{
    setObjectName(QStringLiteral("SwapClassesWindow")); setWindowTitle(tr("换课"));
    setWindowIcon(QIcon(QStringLiteral(":/res/images/icon.png")));
    setAttribute(Qt::WA_QuitOnClose, false); setIsDefaultClosed(false);
    setWindowButtonFlags(ElaAppBarType::CloseButtonHint | ElaAppBarType::MinimizeButtonHint);
    resize(620, 560); setMinimumSize(430, 380);
    auto* body = new QVBoxLayout(this); body->setContentsMargins(20, 16, 20, 18); body->setSpacing(10);
    auto* selectors = new QHBoxLayout;
    date_ = new FutureDatePicker(this); date_->setObjectName(QStringLiteral("swapDate"));
    weekday_ = ProfileEditUi::text({}, this);
    selectors->addWidget(date_, 1); selectors->addWidget(weekday_); body->addLayout(selectors);
    source_ = new ElaComboBox(this); source_->setObjectName(QStringLiteral("swapSource"));
    source_->setAccessibleName(tr("来源周课表")); body->addWidget(source_);
    status_ = ProfileEditUi::text({}, this, 12); body->addWidget(status_);
    courses_ = new QListWidget(this); courses_->setObjectName(QStringLiteral("swapCourses"));
    courses_->setItemDelegate(new CourseDelegate(courses_));
    courses_->setSelectionMode(QAbstractItemView::NoSelection);
    courses_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    courses_->setVerticalScrollBar(new ElaScrollBar(Qt::Vertical, courses_));
    courses_->setFrameShape(QFrame::NoFrame); courses_->setSpacing(2);
    courses_->setAccessibleName(tr("选择两节课程进行交换"));
    courses_->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: 0; }"));
    body->addWidget(courses_, 1);
    summary_ = ProfileEditUi::text(tr("请选择两节课程"), this, 16); body->addWidget(summary_);
    error_ = ProfileEditUi::text({}, this, 13); error_->setObjectName(QStringLiteral("swapError"));
    error_->hide(); body->addWidget(error_);
    auto* footer = new QHBoxLayout;
    restore_ = action(tr("恢复原课表"), this); restore_->setObjectName(QStringLiteral("restoreSwap"));
    confirm_ = action(tr("确认换课"), this, true); confirm_->setObjectName(QStringLiteral("confirmSwap"));
    auto* cancel = action(tr("取消"), this);
    for (auto* button : {restore_, confirm_, cancel})
        button->setMinimumWidth(qMax(112, button->fontMetrics().horizontalAdvance(button->text()) + 40));
    footer->addWidget(restore_); footer->addStretch(); footer->addWidget(confirm_); footer->addWidget(cancel); body->addLayout(footer);
    auto toggle = [this](QListWidgetItem* item) {
        if (!item) return;
        const int row = courses_->row(item);
        if (selected_.contains(row)) selected_.removeAll(row);
        else if (selected_.size() < 2) selected_.append(row);
        error_->hide(); updateSelection();
    };
    connect(courses_, &QListWidget::itemClicked, this, toggle);
    auto* space = new QShortcut(QKeySequence(Qt::Key_Space), courses_);
    space->setContext(Qt::WidgetShortcut);
    connect(space, &QShortcut::activated, this, [this, toggle] { toggle(courses_->currentItem()); });
    auto* enter = new QShortcut(QKeySequence(Qt::Key_Return), courses_);
    enter->setContext(Qt::WidgetShortcut);
    connect(enter, &QShortcut::activated, this, [this, toggle] { toggle(courses_->currentItem()); });
    connect(date_, &FutureDatePicker::dateChanged, this, [this] { rebuildClasses(); });
    connect(source_, &QComboBox::currentIndexChanged, this, [this] { rebuildClasses(); });
    connect(confirm_, &ElaPushButton::clicked, this, [this] {
        if (selected_.size() == 2) emit swapRequested(date_->date(), table_.sourceWeekScheduleId, selected_[0], selected_[1]);
    });
    connect(restore_, &ElaPushButton::clicked, this, [this] { emit restoreRequested(date_->date()); });
    connect(cancel, &ElaPushButton::clicked, this, &QWidget::close);
    connect(this, &ElaWidget::closeButtonClicked, this, &QWidget::close);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &QWidget::close);
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { courses_->viewport()->update(); });
}

void SwapClassesWindow::refreshProfile(const Profile& profile)
{
    profile_ = profile;
    const QString preferred = source_->currentData().toString();
    { QSignalBlocker blocker(date_); date_->refreshMinimum(); }
    fillSources(source_, profile_, preferred);
    rebuildClasses();
}

void SwapClassesWindow::rebuildClasses()
{
    selected_.clear(); courses_->clear(); error_->hide();
    weekday_->setText(dayNames.value(date_->date().dayOfWeek() - 1));
    const auto* record = DateSchedule::find(profile_, date_->date());
    if (record) fillSources(source_, profile_, record->sourceWeekScheduleId);
    source_->setEnabled(!record);
    restore_->setEnabled(record != nullptr);
    if (!record && source_->currentData().toString().isEmpty()) {
        table_ = {}; table_.valid = false;
        status_->setText(tr("请选择来源周课表。"));
    } else {
        table_ = DateSchedule::resolve(profile_, date_->date(), source_->currentData().toString());
        if (!table_.valid) status_->setText(tr("无法读取课程：%1").arg(table_.error));
        else if (table_.classes.isEmpty()) status_->setText(tr("该日期无课程。"));
        else status_->setText(record ? tr("正在编辑已有特殊安排（%1）；仅对该日期生效。").arg(sourceText(profile_, *record))
                                     : tr("选两节课交换科目，起止时间保持原位；仅对该日期生效。"));
    }
    if (table_.valid) for (const auto& course : table_.classes) {
        auto* item = new QListWidgetItem(course.subject, courses_);
        item->setData(Qt::UserRole, course.startTime.toString(QStringLiteral("HH:mm")) + QStringLiteral(" - ") + course.endTime.toString(QStringLiteral("HH:mm")));
        item->setData(Qt::CheckStateRole, Qt::Unchecked);
        item->setData(Qt::AccessibleTextRole, course.subject + QStringLiteral(" · ") + item->data(Qt::UserRole).toString());
        // Selection is capped at two by the handlers; native checkboxes must not toggle independently.
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setToolTip(course.subject + QStringLiteral(" · ") + item->data(Qt::UserRole).toString());
    }
    updateSelection();
}

void SwapClassesWindow::updateSelection()
{
    for (int i = 0; i < courses_->count(); ++i)
        courses_->item(i)->setData(Qt::CheckStateRole, selected_.contains(i) ? Qt::Checked : Qt::Unchecked);
    const bool pair = selected_.size() == 2;
    const bool different = pair && table_.classes[selected_[0]].subject != table_.classes[selected_[1]].subject;
    confirm_->setEnabled(table_.valid && date_->date() >= QDate::currentDate() && different);
    summary_->setText(pair ? tr("%1 与 %2 互换%3").arg(table_.classes[selected_[0]].subject, table_.classes[selected_[1]].subject,
        different ? QString() : tr("（科目相同，无需交换）")) : tr("请选择两节课程（已选 %1/2）").arg(selected_.size()));
}

void SwapClassesWindow::showError(const QString& message) { error_->setText(message); error_->show(); }
void SwapClassesWindow::closeEvent(QCloseEvent* event) { selected_.clear(); hide(); event->ignore(); }
