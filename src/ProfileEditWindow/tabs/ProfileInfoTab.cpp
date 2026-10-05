#include "ProfileInfoTab.h"
#include "../ProfileEditSession.h"
#include "../ProfileEditWidgets.h"
#include "../../Core/ProfileManager/ProfileManager.h"
#include <ElaLineEdit.h>
#include <ElaText.h>
#include <QSignalBlocker>
#include <QVBoxLayout>

ProfileInfoTab::ProfileInfoTab(ProfileEditSession* session, QWidget* parent) : ElaScrollPage(parent), session_(session)
{
    using namespace ProfileEditUi;
    auto* layout = page(this, tr("基本信息"));
    auto* details = card(layout);
    details->addWidget(text(tr("档案名称")));
    name_ = new ElaLineEdit; name_->setFixedHeight(38); name_->setObjectName(QStringLiteral("profileName"));
    name_->setAccessibleName(tr("档案名称")); details->addWidget(name_);
    details->addWidget(text(tr("文件路径")));
    auto* path = text(ProfileManager::instance().filePath());
    path->setIsWrapAnywhere(true); path->setTextInteractionFlags(Qt::TextSelectableByMouse); details->addWidget(path);
    counts_ = text({}); card(layout, tr("数据统计"))->addWidget(counts_);
    layout->addStretch();
    connect(name_, &ElaLineEdit::textEdited, this, [this](const QString& value) {
        session_->edit().name = value; session_->notifyChanged();
    });
    connect(session_, &ProfileEditSession::changed, this, &ProfileInfoTab::refresh);
    refresh();
}
void ProfileInfoTab::refresh()
{
    QSignalBlocker blocker(name_);
    if (name_->text() != session_->draft().name) name_->setText(session_->draft().name);
    const auto& draft = session_->draft();
    int courses = 0;
    for (const auto& week : draft.schedules) for (const auto& day : week.daySchedules) courses += day.classes.size();
    counts_->setText(tr("%1 个科目    %2 条时间线    %3 个周课表    %4 节课程")
        .arg(draft.subjects.size()).arg(draft.timeLines.size()).arg(draft.schedules.size()).arg(courses));
}
void ProfileInfoTab::focusName() { name_->setFocus(); name_->selectAll(); }
