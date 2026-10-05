#include "SchedulesTab.h"
#include "../ProfileEditSession.h"
#include "../ProfileEditWidgets.h"
#include <ElaComboBox.h>
#include <ElaLineEdit.h>
#include <ElaMessageBar.h>
#include <ElaPushButton.h>
#include <ElaTableView.h>
#include <ElaText.h>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QStringList dayNames() { return {QStringLiteral("星期一"), QStringLiteral("星期二"), QStringLiteral("星期三"),
    QStringLiteral("星期四"), QStringLiteral("星期五"), QStringLiteral("星期六"), QStringLiteral("星期日")}; }
QString modeText(WeekScheduleMode mode)
{
    switch (mode) { case WeekScheduleMode::All: return QStringLiteral("全部周");
    case WeekScheduleMode::Odd: return QStringLiteral("单周"); case WeekScheduleMode::Even: return QStringLiteral("双周"); }
    return QStringLiteral("无效模式");
}
}

SchedulesTab::SchedulesTab(ProfileEditSession* session, QWidget* parent) : ElaScrollPage(parent), session_(session)
{
    using namespace ProfileEditUi;
    auto* layout = page(this, tr("课表管理"));
    auto* settings = card(layout);
    settings->addWidget(text(tr("周课表")));
    weeks_ = new ElaComboBox; weeks_->setFixedHeight(38); weeks_->setAccessibleName(tr("周课表")); settings->addWidget(weeks_);
    auto* weekActions = new QHBoxLayout;
    auto* create = button(tr("新增课表"), nullptr, true);
    editWeek_ = button(tr("编辑课表")); removeWeek_ = button(tr("删除课表")); mode_ = text({});
    weekActions->addWidget(create); weekActions->addWidget(editWeek_); weekActions->addWidget(removeWeek_);
    weekActions->addStretch(); weekActions->addWidget(mode_); settings->addLayout(weekActions);
    auto* courses = card(layout);
    auto* days = new QHBoxLayout;
    weekdays_ = new ElaComboBox; weekdays_->addItems(dayNames()); weekdays_->setMinimumWidth(120); weekdays_->setFixedHeight(38);
    weekdays_->setAccessibleName(tr("适用星期"));
    dayName_ = text({}); editDay_ = button(tr("编辑日课表")); removeDay_ = button(tr("移除星期"));
    days->addWidget(weekdays_); days->addWidget(dayName_, 1); days->addWidget(editDay_); days->addWidget(removeDay_); courses->addLayout(days);
    auto* courseActions = new QHBoxLayout;
    add_ = button(tr("新增课程"), nullptr, true); edit_ = button(tr("编辑")); remove_ = button(tr("删除"));
    courseActions->addWidget(add_); courseActions->addWidget(edit_); courseActions->addWidget(remove_); courseActions->addStretch(); courses->addLayout(courseActions);
    empty_ = text(tr("暂无课程")); courses->addWidget(empty_);
    table_ = table(courses, {tr("序号"), tr("科目"), tr("开始时间"), tr("结束时间")}, model_);
    auto* timeLines = card(layout, tr("应用时间线"));
    auto* lineActions = new QHBoxLayout;
    lines_ = new ElaComboBox; lines_->setFixedHeight(38); lines_->setAccessibleName(tr("时间线"));
    apply_ = button(tr("应用到当天")); lineActions->addWidget(lines_, 1); lineActions->addWidget(apply_); timeLines->addLayout(lineActions);
    layout->addStretch();
    connect(create, &ElaPushButton::clicked, this, [this] { editWeek(true); });
    connect(editWeek_, &ElaPushButton::clicked, this, [this] { editWeek(false); });
    connect(removeWeek_, &ElaPushButton::clicked, this, &SchedulesTab::removeWeek);
    connect(editDay_, &ElaPushButton::clicked, this, &SchedulesTab::editDay);
    connect(removeDay_, &ElaPushButton::clicked, this, &SchedulesTab::removeDay);
    connect(weeks_, qOverload<int>(&ElaComboBox::currentIndexChanged), this, &SchedulesTab::refreshCourses);
    connect(weekdays_, qOverload<int>(&ElaComboBox::currentIndexChanged), this, &SchedulesTab::refreshCourses);
    connect(lines_, qOverload<int>(&ElaComboBox::currentIndexChanged), this, [this] {
        apply_->setEnabled(weeks_->currentIndex() >= 0 && lines_->currentIndex() >= 0);
    });
    connect(add_, &ElaPushButton::clicked, this, [this] { editCourse(-1); });
    connect(edit_, &ElaPushButton::clicked, this, [this] { editCourse(ProfileEditUi::selected(table_)); });
    connect(remove_, &ElaPushButton::clicked, this, &SchedulesTab::removeCourse);
    connect(apply_, &ElaPushButton::clicked, this, &SchedulesTab::applyLine);
    connect(table_, &ElaTableView::doubleClicked, this, [this] { editCourse(ProfileEditUi::selected(table_)); });
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        const bool enabled = ProfileEditUi::selected(table_) >= 0; edit_->setEnabled(enabled); remove_->setEnabled(enabled);
    });
    connect(session_, &ProfileEditSession::changed, this, &SchedulesTab::refresh);
    refresh();
}

int SchedulesTab::dayIndex() const
{
    const int week = weeks_->currentIndex();
    if (week < 0 || week >= session_->draft().schedules.size()) return -1;
    const auto& days = session_->draft().schedules[week].daySchedules;
    for (int i = 0; i < days.size(); ++i) if (days[i].enableDay == weekdays_->currentIndex() + 1) return i;
    return -1;
}
void SchedulesTab::refresh()
{
    const int week = weeks_->currentIndex(), line = lines_->currentIndex();
    { QSignalBlocker blocker(weeks_); weeks_->clear();
      for (const auto& value : session_->draft().schedules) weeks_->addItem(value.name);
      if (weeks_->count()) weeks_->setCurrentIndex(qBound(0, week, weeks_->count() - 1)); }
    { QSignalBlocker blocker(lines_); lines_->clear();
      for (const auto& value : session_->draft().timeLines) lines_->addItem(value.name);
      if (lines_->count()) lines_->setCurrentIndex(qBound(0, line, lines_->count() - 1)); }
    refreshCourses();
}
void SchedulesTab::refreshCourses()
{
    using namespace ProfileEditUi;
    const int selection = selected(table_);
    model_->removeRows(0, model_->rowCount());
    const int week = weeks_->currentIndex(), day = dayIndex();
    const bool validWeek = week >= 0 && week < session_->draft().schedules.size();
    editWeek_->setEnabled(validWeek); removeWeek_->setEnabled(validWeek); weekdays_->setEnabled(validWeek);
    editDay_->setEnabled(validWeek); editDay_->setText(day < 0 ? tr("添加星期") : tr("编辑日课表"));
    removeDay_->setEnabled(day >= 0); add_->setEnabled(day >= 0);
    apply_->setEnabled(validWeek && lines_->currentIndex() >= 0);
    mode_->setText(validWeek ? modeText(session_->draft().schedules[week].mode) : QString());
    dayName_->setText(day >= 0 ? session_->draft().schedules[week].daySchedules[day].name : QString());
    if (day >= 0) {
        const auto& courses = session_->draft().schedules[week].daySchedules[day].classes;
        QList<int> order;
        for (int i = 0; i < courses.size(); ++i) order.append(i);
        std::stable_sort(order.begin(), order.end(), [&courses](int a, int b) { return courses[a].startTime < courses[b].startTime; });
        for (int i = 0; i < order.size(); ++i) {
            const auto& course = courses[order[i]];
            appendRow(model_, {QString::number(i + 1), course.subject.isEmpty() ? tr("未设置") : course.subject,
                course.startTime.toString("HH:mm:ss"), course.endTime.toString("HH:mm:ss")}, order[i]);
        }
    }
    select(table_, selection); emptyState(table_, empty_);
    edit_->setEnabled(selected(table_) >= 0); remove_->setEnabled(selected(table_) >= 0);
}
void SchedulesTab::editWeek(bool create)
{
    using namespace ProfileEditUi;
    const int index = weeks_->currentIndex();
    if (!create && index < 0) return;
    auto* dialog = new FormDialog(this, create ? tr("新增周课表") : tr("编辑周课表"));
    auto* name = dialog->line(tr("名称"), create ? QString() : session_->draft().schedules[index].name);
    auto* mode = new ElaComboBox; mode->addItems({tr("全部周"), tr("单周"), tr("双周")});
    mode->setCurrentIndex(create ? 0 : static_cast<int>(session_->draft().schedules[index].mode)); dialog->field(tr("周模式"), mode);
    dialog->submit = [this, create, index, name, mode] {
        if (name->text().trimmed().isEmpty()) return tr("课表名称不能为空");
        const auto selectedMode = static_cast<WeekScheduleMode>(mode->currentIndex());
        if (create) {
            WeekSchedule week(name->text().trimmed(), selectedMode);
            const auto names = dayNames();
            for (int day = 1; day <= 7; ++day) week.daySchedules.append(DaySchedule(names[day - 1], day));
            session_->edit().schedules.append(week);
        } else { auto& week = session_->edit().schedules[index]; week.name = name->text().trimmed(); week.mode = selectedMode; }
        session_->notifyChanged(); return QString();
    };
    if (run(dialog)) {
        if (create) weeks_->setCurrentIndex(weeks_->count() - 1);
        if (session_->draft().schedules[weeks_->currentIndex()].mode != WeekScheduleMode::All)
            ElaMessageBar::warning(ElaMessageBarType::TopRight, tr("单双周配置"), tr("单双周自动运行需要第一周日期配置，当前尚未提供。"), 6000, window());
    }
}
void SchedulesTab::removeWeek()
{
    const int index = weeks_->currentIndex();
    if (index < 0 || !ProfileEditUi::confirm(this, tr("删除课表"), tr("确定删除“%1”及其全部课程？").arg(weeks_->currentText()))) return;
    session_->edit().schedules.removeAt(index); session_->notifyChanged();
}
void SchedulesTab::editDay()
{
    using namespace ProfileEditUi;
    const int week = weeks_->currentIndex(), day = dayIndex(), weekday = weekdays_->currentIndex() + 1;
    if (week < 0) return;
    auto* dialog = new FormDialog(this, day < 0 ? tr("添加星期") : tr("编辑日课表"));
    auto* name = dialog->line(tr("日课表名称"), day < 0 ? dayNames()[weekday - 1] : session_->draft().schedules[week].daySchedules[day].name);
    auto* enabledDay = new ElaComboBox; enabledDay->addItems(dayNames()); enabledDay->setCurrentIndex(weekday - 1);
    dialog->field(tr("适用星期"), enabledDay);
    dialog->submit = [this, week, day, name, enabledDay] {
        if (name->text().trimmed().isEmpty()) return tr("日课表名称不能为空");
        auto& days = session_->edit().schedules[week].daySchedules;
        const int weekday = enabledDay->currentIndex() + 1;
        for (int i = 0; i < days.size(); ++i) if (i != day && days[i].enableDay == weekday) return tr("该星期已存在日课表");
        if (day < 0) days.append(DaySchedule(name->text().trimmed(), weekday));
        else { days[day].name = name->text().trimmed(); days[day].enableDay = weekday; }
        session_->notifyChanged(); return QString();
    };
    if (run(dialog)) weekdays_->setCurrentIndex(enabledDay->currentIndex());
}
void SchedulesTab::removeDay()
{
    const int week = weeks_->currentIndex(), day = dayIndex();
    if (day < 0 || !ProfileEditUi::confirm(this, tr("移除星期"), tr("确定移除%1及其全部课程？").arg(weekdays_->currentText()))) return;
    session_->edit().schedules[week].daySchedules.removeAt(day); session_->notifyChanged();
}
void SchedulesTab::editCourse(int index)
{
    using namespace ProfileEditUi;
    const int week = weeks_->currentIndex(), day = dayIndex();
    if (day < 0) return;
    const auto& courses = session_->draft().schedules[week].daySchedules[day].classes;
    if (index < -1 || index >= courses.size()) return;
    Class value({}, QTime(8, 0), QTime(8, 45));
    if (index >= 0) value = courses[index];
    auto* dialog = new FormDialog(this, index < 0 ? tr("新增课程") : tr("编辑课程"));
    auto* subject = new ElaComboBox; subject->setEditable(true);
    for (const auto& item : session_->draft().subjects) subject->addItem(item.name);
    subject->setCurrentText(value.subject); dialog->field(tr("科目"), subject);
    auto* from = new TimePicker(value.startTime); auto* to = new TimePicker(value.endTime);
    dialog->field(tr("开始时间"), from); dialog->field(tr("结束时间"), to);
    dialog->submit = [this, week, day, index, subject, from, to] {
        if (subject->currentText().trimmed().isEmpty()) return tr("请选择课程科目");
        const QTime start = from->time(), end = to->time();
        if (!start.isValid() || !end.isValid() || start >= end) return tr("开始时间必须早于结束时间");
        auto& courses = session_->edit().schedules[week].daySchedules[day].classes;
        for (int i = 0; i < courses.size(); ++i)
            if (i != index && start < courses[i].endTime && courses[i].startTime < end) return tr("当天课程时间重叠");
        Class course(subject->currentText().trimmed(), start, end);
        if (index < 0) courses.append(course); else courses[index] = course;
        session_->notifyChanged(); return QString();
    };
    if (run(dialog)) select(table_, index < 0 ? session_->draft().schedules[week].daySchedules[day].classes.size() - 1 : index);
}
void SchedulesTab::removeCourse()
{
    const int week = weeks_->currentIndex(), day = dayIndex(), index = ProfileEditUi::selected(table_);
    if (day < 0 || index < 0 || !ProfileEditUi::confirm(this, tr("删除课程"), tr("确定删除所选课程？"))) return;
    session_->edit().schedules[week].daySchedules[day].classes.removeAt(index); session_->notifyChanged();
}
void SchedulesTab::applyLine()
{
    using namespace ProfileEditUi;
    const int week = weeks_->currentIndex(), day = dayIndex(), line = lines_->currentIndex();
    if (week < 0 || line < 0) return;
    const int oldCount = day < 0 ? 0 : session_->draft().schedules[week].daySchedules[day].classes.size();
    const int newCount = session_->draft().timeLines[line].timePoints.size();
    const auto message = tr("将“%1”应用到%2：\n课程 %3 → %4，保留 %5 个科目，新增 %6 节，删除 %7 节。").arg(lines_->currentText(), weekdays_->currentText())
        .arg(oldCount).arg(newCount).arg(qMin(oldCount, newCount)).arg(qMax(0, newCount - oldCount)).arg(qMax(0, oldCount - newCount));
    if (!confirm(this, tr("应用时间线"), message)) return;
    QString failure;
    if (!session_->applyTimeLine(week, weekdays_->currentIndex() + 1, line, &failure)) error(this, failure);
}
void SchedulesTab::focusItem(int index, int weekday, int row)
{
    weeks_->setCurrentIndex(index); weekdays_->setCurrentIndex(qBound(0, weekday - 1, 6));
    ProfileEditUi::select(table_, row); table_->setFocus();
}
