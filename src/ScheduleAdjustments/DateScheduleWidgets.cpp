#include "DateScheduleWidgets.h"
#include "../Ui/UiWidgets.h"
#include "../Core/ThemeManager/ThemeManager.h"
#include <ElaComboBox.h>
#include <ElaIcon.h>
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
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

// Keep long schedule/subject names from taking space away from the course lists.
class CompactScheduleText final : public ElaText {
public:
    CompactScheduleText(QWidget* parent, int size, int lines = 1) : ElaText({}, size, parent)
    {
        setTextFormat(Qt::PlainText);
        setWordWrap(false);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        setFixedHeight(fontMetrics().lineSpacing() * lines + 4);
    }
    void setFullText(const QString& text)
    {
        fullText_ = text;
        setToolTip(text);
        refreshText();
    }
protected:
    void resizeEvent(QResizeEvent* event) override
    {
        ElaText::resizeEvent(event);
        refreshText();
    }
private:
    void refreshText()
    {
        QStringList lines;
        for (const auto& line : fullText_.split(QLatin1Char('\n')))
            lines.append(fontMetrics().elidedText(line, Qt::ElideRight, contentsRect().width()));
        setText(lines.join(QLatin1Char('\n')));
    }
    QString fullText_;
};

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
    if (primary) AppUi::primary(button);
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
        const bool selected = option.state & QStyle::State_Selected;
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
    body->addWidget(AppUi::text(tr("选择目标日期，并指定要使用的星期课表。"), content, 14));

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
    auto* dateLabel = AppUi::text(tr("目标日期"), content);
    dateLabel->setWordWrap(false); dateLabel->setBuddy(date_);
    auto* sourceLabel = AppUi::text(tr("来源课表"), content);
    sourceLabel->setWordWrap(false); sourceLabel->setBuddy(source_);
    fields->addWidget(dateLabel, 0, 0);
    fields->addWidget(date_, 0, 1);
    fields->addWidget(sourceLabel, 1, 0);
    fields->addWidget(source_, 1, 1);
    body->addLayout(fields);

    auto* weekdaySection = new QVBoxLayout;
    weekdaySection->setSpacing(8);
    weekdaySection->addWidget(AppUi::text(tr("来源星期"), content));
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
    status_ = AppUi::text({}, content, 12);
    body->addWidget(status_);
    error_ = AppUi::text({}, content, 12);
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
    // ElaWidget's background selector uses its original object name. Keep the
    // native backdrop visible after renaming, independently of the OS palette.
    setStyleSheet(QStringLiteral("#SwapClassesWindow { background-color: transparent; }"));
    setWindowIcon(QIcon(QStringLiteral(":/res/images/icon.png")));
    setAttribute(Qt::WA_QuitOnClose, false); setIsDefaultClosed(false);
    setWindowModality(Qt::NonModal);
    setWindowButtonFlags(ElaAppBarType::CloseButtonHint | ElaAppBarType::MinimizeButtonHint);
    resize(960, 620); setMinimumSize(680, 460);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0); root->setSpacing(0);
    auto* scroll = new ElaScrollArea(this);
    scroll->setObjectName(QStringLiteral("swapContentScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumSize(0, 0);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setIsAnimation(Qt::Vertical, false);
    scroll->setStyleSheet(QStringLiteral("QScrollArea, QScrollArea > QWidget { background: transparent; border: 0; }"));
    auto* content = new QWidget;
    auto* body = new QVBoxLayout(content);
    body->setSizeConstraint(QLayout::SetMinimumSize);
    body->setContentsMargins(20, 16, 20, 18); body->setSpacing(12);
    auto* columns = new QHBoxLayout;
    columns->setSpacing(12);
    for (int i = 0; i < 2; ++i) {
        auto& side = sides_[i];
        auto* panel = new QWidget(content);
        panel->setMinimumWidth(0);
        panel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
        auto* layout = new QVBoxLayout(panel);
        layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(10);
        const QString title = i == 0 ? tr("来源课程") : tr("要换的课程");
        const QString prefix = i == 0 ? QStringLiteral("swapLeft") : QStringLiteral("swapRight");
        layout->addWidget(AppUi::text(title, panel, 18));
        auto* dateRow = new QHBoxLayout;
        side.date = new FutureDatePicker(panel);
        side.date->setObjectName(prefix + QStringLiteral("Date"));
        side.date->setAccessibleName(title + tr("日期"));
        side.date->setDate(QDate::currentDate());
        side.weekday = AppUi::text({}, panel);
        side.weekday->setWordWrap(false);
        dateRow->addWidget(side.date, 1); dateRow->addWidget(side.weekday);
        layout->addLayout(dateRow);
        side.source = new ElaComboBox(panel);
        side.source->setObjectName(prefix + QStringLiteral("Source"));
        side.source->setAccessibleName(title + tr("周课表"));
        side.source->setFixedHeight(38);
        side.source->setMinimumWidth(0);
        side.source->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        layout->addWidget(side.source);
        side.status = new CompactScheduleText(panel, 12);
        layout->addWidget(side.status);
        side.courses = new QListWidget(panel);
        side.courses->setObjectName(prefix + QStringLiteral("Courses"));
        side.courses->setItemDelegate(new CourseDelegate(side.courses));
        side.courses->setSelectionMode(QAbstractItemView::SingleSelection);
        side.courses->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        side.courses->setVerticalScrollBar(new ElaScrollBar(Qt::Vertical, side.courses));
        side.courses->setFrameShape(QFrame::NoFrame); side.courses->setSpacing(2);
        side.courses->setMinimumSize(0, 62);
        side.courses->setAccessibleName(title + tr("，选择一节课程"));
        side.courses->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: 0; }"));
        layout->addWidget(side.courses, 1);
        side.restore = action(tr("恢复此日期安排"), panel);
        side.restore->setObjectName(prefix + QStringLiteral("Restore"));
        side.restore->setToolTip(tr("清除此日期全部调休和换课安排，另一日期的安排继续保留。"));
        layout->addWidget(side.restore);
        columns->addWidget(panel, 1);
        if (i == 0) {
            auto* arrow = new QLabel(content);
            arrow->setObjectName(QStringLiteral("swapArrow"));
            arrow->setFixedSize(40, 40);
            arrow->setAccessibleName(tr("交换科目"));
            auto refreshArrow = [arrow] {
                arrow->setPixmap(ElaIcon::getInstance()->getElaIcon(ElaIconType::ArrowRightArrowLeft, 28,
                    ElaThemeColor(eTheme->getThemeMode(), BasicText)).pixmap(QSize(40, 40), arrow->devicePixelRatioF()));
            };
            refreshArrow();
            connect(eTheme, &ElaTheme::themeModeChanged, arrow, refreshArrow);
            columns->addWidget(arrow, 0, Qt::AlignVCenter);
        }
        connect(side.date, &FutureDatePicker::dateChanged, this, [this, i] {
            // A new date starts with its effective automatic/manual profile selection.
            { QSignalBlocker blocker(sides_[i].source); sides_[i].source->setCurrentIndex(0); }
            if (sides_[0].date->date() == sides_[1].date->date()) synchronizeSource(1 - i);
            error_->hide(); rebuildClasses(i); updateSelection();
        });
        connect(side.source, &QComboBox::currentIndexChanged, this, [this, i] {
            synchronizeSource(i);
            error_->hide();
            if (sides_[0].date->date() == sides_[1].date->date()) rebuildBoth();
            else { rebuildClasses(i); updateSelection(); }
        });
        connect(side.courses, &QListWidget::itemSelectionChanged, this, [this] {
            error_->hide(); updateSelection();
        });
        for (const auto key : {Qt::Key_Space, Qt::Key_Return}) {
            auto* select = new QShortcut(QKeySequence(key), side.courses);
            select->setContext(Qt::WidgetShortcut);
            connect(select, &QShortcut::activated, this, [this, i] {
                auto* item = sides_[i].courses->currentItem();
                if (item) item->setSelected(true);
            });
        }
        connect(side.restore, &ElaPushButton::clicked, this, [this, i] {
            const auto date = sides_[i].date->date();
            if (AppUi::confirm(this, tr("恢复此日期安排"),
                tr("清除 %1 的全部调休和换课安排？其他日期的安排继续保留。")
                    .arg(date.toString(Qt::ISODate))))
                emit restoreRequested(date);
        });
    }
    body->addLayout(columns, 1);
    summary_ = new CompactScheduleText(content, 16, 2);
    summary_->setObjectName(QStringLiteral("swapSummary"));
    summary_->setFullText(tr("请选择两节课程")); body->addWidget(summary_);
    error_ = AppUi::text({}, content, 13); error_->setObjectName(QStringLiteral("swapError"));
    error_->hide(); body->addWidget(error_);
    scroll->setWidget(content);
    content->setAutoFillBackground(false);
    scroll->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);
    root->addWidget(scroll, 1);
    auto* footerPanel = new DialogFooter(this);
    footerPanel->setFixedHeight(72);
    auto* footer = new QHBoxLayout(footerPanel);
    footer->setContentsMargins(20, 14, 20, 14); footer->setSpacing(8);
    confirm_ = action(tr("确认换课"), footerPanel, true); confirm_->setObjectName(QStringLiteral("confirmSwap"));
    auto* cancel = action(tr("取消"), footerPanel);
    for (auto* button : {confirm_, cancel})
        button->setMinimumWidth(qMax(112, button->fontMetrics().horizontalAdvance(button->text()) + 40));
    footer->addStretch(); footer->addWidget(confirm_); footer->addWidget(cancel); root->addWidget(footerPanel);
    connect(confirm_, &ElaPushButton::clicked, this, [this] {
        updateSelection();
        if (!confirm_->isEnabled()) return;
        const auto& left = sides_[0]; const auto& right = sides_[1];
        emit swapRequested(left.date->date(), left.table.sourceWeekScheduleId,
            left.courses->row(left.courses->selectedItems().first()),
            right.date->date(), right.table.sourceWeekScheduleId,
            right.courses->row(right.courses->selectedItems().first()));
    });
    connect(cancel, &ElaPushButton::clicked, this, &QWidget::close);
    connect(this, &ElaWidget::closeButtonClicked, this, &QWidget::close);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, &QWidget::close);
    auto refreshCourses = [this] { for (auto& side : sides_) side.courses->viewport()->update(); };
    connect(eTheme, &ElaTheme::themeModeChanged, this, refreshCourses);
    connect(&ThemeManager::instance(), &ThemeManager::themeColorChanged, this, refreshCourses);
    confirm_->setEnabled(false);
}

void SwapClassesWindow::refreshProfile(const Profile& profile)
{
    profile_ = profile;
    for (auto& side : sides_) {
        QString preferred = side.source->isEnabled() ? side.source->currentData().toString() : QString();
        {
            QSignalBlocker blocker(side.date);
            const auto previousDate = side.date->date();
            side.date->refreshMinimum();
            if (previousDate != side.date->date()) preferred.clear();
        }
        QSignalBlocker blocker(side.source);
        side.source->clear();
        side.source->addItem(tr("按日期匹配课表"), QString());
        for (const auto& week : profile_.schedules)
            side.source->addItem(week.name + QStringLiteral(" · ") + modeName(week.mode), week.id);
        side.source->setCurrentIndex(qMax(0, side.source->findData(preferred)));
    }
    synchronizeSource(0);
    rebuildBoth();
}

void SwapClassesWindow::synchronizeSource(int index)
{
    if (sides_[0].date->date() != sides_[1].date->date()) return;
    auto* other = sides_[1 - index].source;
    QSignalBlocker blocker(other);
    other->setCurrentIndex(qMax(0, other->findData(sides_[index].source->currentData())));
}

void SwapClassesWindow::rebuildBoth()
{
    error_->hide();
    rebuildClasses(0); rebuildClasses(1); updateSelection();
}

void SwapClassesWindow::rebuildClasses(int index)
{
    auto& side = sides_[index];
    QSignalBlocker blocker(side.courses);
    side.courses->clear();
    side.weekday->setText(dayNames.value(side.date->date().dayOfWeek() - 1));
    const auto* record = DateSchedule::find(profile_, side.date->date());
    if (record) {
        QSignalBlocker sourceBlocker(side.source);
        side.source->setCurrentIndex(qMax(0, side.source->findData(record->sourceWeekScheduleId)));
    }
    side.source->setEnabled(!record);
    side.restore->setEnabled(record != nullptr);
    side.table = DateSchedule::resolve(profile_, side.date->date(), side.source->currentData().toString());
    if (!side.table.valid)
        side.status->setFullText(tr("无法读取课程，请选择有效的周课表：%1").arg(side.table.error));
    else if (side.table.classes.isEmpty()) side.status->setFullText(tr("该日期无课程。"));
    else if (side.table.special)
        side.status->setFullText(tr("已有特殊安排（%1），将在当前结果上换课。").arg(sourceText(profile_, *record)));
    else side.status->setFullText(tr("当前课表：%1 · %2").arg(side.table.name, side.weekday->text()));
    if (side.table.valid) for (const auto& course : side.table.classes) {
        auto* item = new QListWidgetItem(course.subject, side.courses);
        item->setData(Qt::UserRole, course.startTime.toString(QStringLiteral("HH:mm")) + QStringLiteral(" - ") + course.endTime.toString(QStringLiteral("HH:mm")));
        item->setData(Qt::AccessibleTextRole, course.subject + QStringLiteral(" · ") + item->data(Qt::UserRole).toString());
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        item->setToolTip(course.subject + QStringLiteral(" · ") + item->data(Qt::UserRole).toString());
    }
}

void SwapClassesWindow::updateSelection()
{
    int rows[2] = {-1, -1};
    bool valid = true;
    for (int i = 0; i < 2; ++i) {
        const auto& side = sides_[i];
        const auto selected = side.courses->selectedItems();
        if (!selected.isEmpty()) rows[i] = side.courses->row(selected.first());
        valid = valid && side.table.valid && side.date->date() >= QDate::currentDate() &&
            rows[i] >= 0 && rows[i] < side.table.classes.size();
    }
    confirm_->setEnabled(false);
    if (!valid) { summary_->setFullText(tr("请在左右两侧各选择一节今天或未来的课程。")); return; }
    const auto& left = sides_[0]; const auto& right = sides_[1];
    if (left.date->date() == right.date->date() && rows[0] == rows[1]) {
        summary_->setFullText(tr("请选择两节不同的课程。")); return;
    }
    auto describe = [](const CourseSide& side, int row) {
        const auto& course = side.table.classes[row];
        return QStringLiteral("%1 %2–%3 %4").arg(side.date->date().toString(QStringLiteral("yyyy-MM-dd")),
            course.startTime.toString(QStringLiteral("HH:mm")), course.endTime.toString(QStringLiteral("HH:mm")), course.subject);
    };
    const bool different = left.table.classes[rows[0]].subject != right.table.classes[rows[1]].subject;
    summary_->setFullText(tr("来源：%1\n换为：%2%3").arg(describe(left, rows[0]), describe(right, rows[1]),
        different ? QString() : tr("（科目相同，无需交换）")));
    confirm_->setEnabled(different);
}

void SwapClassesWindow::showError(const QString& message) { error_->setText(message); error_->show(); }
void SwapClassesWindow::closeEvent(QCloseEvent* event)
{
    for (auto& side : sides_) side.courses->clearSelection();
    hide(); event->ignore();
}
