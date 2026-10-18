#include "ProfileInfoTab.h"
#include "../ProfileEditSession.h"
#include "../../Ui/UiWidgets.h"
#include "../../Core/ProfileManager/ProfileManager.h"
#include <ElaLineEdit.h>
#include <ElaCalendarPicker.h>
#include <ElaToggleSwitch.h>
#include <ElaText.h>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QVBoxLayout>

ProfileInfoTab::ProfileInfoTab(ProfileEditSession* session, QWidget* parent) : ElaScrollPage(parent), session_(session)
{
    using namespace AppUi;
    auto* layout = page(this, tr("基本信息"));
    auto* details = card(layout);
    details->addWidget(text(tr("档案名称")));
    name_ = new ElaLineEdit; name_->setFixedHeight(38); name_->setObjectName(QStringLiteral("profileName"));
    name_->setAccessibleName(tr("档案名称")); details->addWidget(name_);
    details->addWidget(text(tr("文件路径")));
    auto* path = text(ProfileManager::instance().filePath());
    path->setIsWrapAnywhere(true); path->setTextInteractionFlags(Qt::TextSelectableByMouse); details->addWidget(path);
    auto* semester = card(layout, tr("学期设置"));
    auto* semesterRow = new QHBoxLayout;
    semesterRow->addWidget(text(tr("学期第一天")), 1);
    semesterEnabled_ = new ElaToggleSwitch;
    semesterEnabled_->setObjectName(QStringLiteral("semesterDateEnabled"));
    semesterEnabled_->setAccessibleName(tr("设置学期第一天"));
    semesterStart_ = new ElaCalendarPicker;
    semesterStart_->setObjectName(QStringLiteral("semesterStartDate"));
    semesterStart_->setAccessibleName(tr("学期第一天"));
    semesterStart_->setFixedSize(160, 38);
    semesterRow->addWidget(semesterEnabled_); semesterRow->addWidget(semesterStart_);
    semester->addLayout(semesterRow);
    auto* explanation = text(tr("所选日期所在周为第 1 周（单周），每周一开始新的一周，用于自动匹配单周 / 双周课表。"));
    explanation->setWordWrap(true); semester->addWidget(explanation);
    semesterHint_ = text({}); semesterHint_->setWordWrap(true); semester->addWidget(semesterHint_);
    counts_ = text({}); card(layout, tr("数据统计"))->addWidget(counts_);
    layout->addStretch();
    connect(name_, &ElaLineEdit::textEdited, this, [this](const QString& value) {
        session_->edit().name = value; session_->notifyChanged();
    });
    connect(semesterEnabled_, &ElaToggleSwitch::toggled, this, [this](bool enabled) {
        session_->edit().semesterStartDate = enabled ? semesterStart_->getSelectedDate() : QDate{};
        session_->notifyChanged();
    });
    connect(semesterStart_, &ElaCalendarPicker::selectedDateChanged, this, [this](const QDate& date) {
        if (!semesterEnabled_->getIsToggled()) return;
        session_->edit().semesterStartDate = date; session_->notifyChanged();
    });
    connect(session_, &ProfileEditSession::changed, this, &ProfileInfoTab::refresh);
    refresh();
}
void ProfileInfoTab::refresh()
{
    QSignalBlocker blocker(name_);
    if (name_->text() != session_->draft().name) name_->setText(session_->draft().name);
    const auto& draft = session_->draft();
    const bool hasSemester = draft.semesterStartDate.isValid();
    {
        QSignalBlocker enabledBlocker(semesterEnabled_), dateBlocker(semesterStart_);
        semesterEnabled_->setIsToggled(hasSemester);
        semesterStart_->setEnabled(hasSemester);
        if (hasSemester && semesterStart_->getSelectedDate() != draft.semesterStartDate)
            semesterStart_->setSelectedDate(draft.semesterStartDate);
    }
    semesterHint_->setText(hasSemester
        ? tr("第 1 周：%1 至 %2。保存后，托盘选择“自动”即可按单双周匹配课表。")
            .arg(draft.semesterStartDate.addDays(1 - draft.semesterStartDate.dayOfWeek()).toString(Qt::ISODate),
                 draft.semesterStartDate.addDays(7 - draft.semesterStartDate.dayOfWeek()).toString(Qt::ISODate))
        : tr("尚未设置学期第一天；单周 / 双周课表仍需手动选择。"));
    int courses = 0;
    for (const auto& week : draft.schedules) for (const auto& day : week.daySchedules) courses += day.classes.size();
    counts_->setText(tr("%1 个科目    %2 条时间线    %3 个周课表    %4 节课程")
        .arg(draft.subjects.size()).arg(draft.timeLines.size()).arg(draft.schedules.size()).arg(courses));
}
void ProfileInfoTab::focusName() { name_->setFocus(); name_->selectAll(); }
