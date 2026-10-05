#include "ProfileEditWindow.h"
#include "ProfileEditSession.h"
#include "ProfileEditWidgets.h"
#include "tabs/ProfileInfoTab.h"
#include "tabs/SubjectsTab.h"
#include "tabs/TimeLinesTab.h"
#include "tabs/SchedulesTab.h"
#include "../Core/ProfileManager/ProfileManager.h"
#include <ElaApplication.h>
#include <ElaMessageBar.h>
#include <ElaPushButton.h>
#include <ElaStatusBar.h>
#include <ElaText.h>
#include <QCloseEvent>
#include <QScopedValueRollback>
#include <QShortcut>
#include <QShowEvent>

ProfileEditWindow::ProfileEditWindow(QWidget* parent) : ElaWindow(parent)
{
    session_ = new ProfileEditSession(this);
    session_->reset(ProfileManager::instance().profile());
    initUI();
}
ProfileEditWindow::~ProfileEditWindow() = default;

void ProfileEditWindow::initUI()
{
    using namespace ProfileEditUi;
    setWindowTitle(tr("档案编辑"));
    resize(1100, 760);
    setMinimumSize(900, 620);
    setNavigationBarWidth(220);
    setUserInfoCardVisible(false);
    setIsAllowPageOpenInNewWindow(false);
    setWindowButtonFlag(ElaAppBarType::StayTopButtonHint, false);
    setWindowButtonFlag(ElaAppBarType::RouteBackButtonHint, false);
    setWindowButtonFlag(ElaAppBarType::RouteForwardButtonHint, false);
    setIsDefaultClosed(false);
    connect(this, &ElaWindow::closeButtonClicked, this, [this] { close(); });
    info_ = new ProfileInfoTab(session_, this);
    subjects_ = new SubjectsTab(session_, this);
    timeLines_ = new TimeLinesTab(session_, this);
    schedules_ = new SchedulesTab(session_, this);
    addPageNode(tr("基本信息"), info_, ElaIconType::FileLines);
    addPageNode(tr("科目管理"), subjects_, ElaIconType::BookOpen);
    addPageNode(tr("时间线管理"), timeLines_, ElaIconType::Clock);
    addPageNode(tr("课表管理"), schedules_, ElaIconType::CalendarDays);
    auto* footer = new ElaStatusBar(this);
    footer->setSizeGripEnabled(false);
    footer->setFixedHeight(72);
    footer->setContentsMargins(20, 8, 16, 8);
    state_ = text({}, footer);
    save_ = button(tr("保存"), footer, true);
    discard_ = button(tr("放弃更改"), footer);
    save_->setObjectName(QStringLiteral("saveProfile"));
    discard_->setObjectName(QStringLiteral("discardProfile"));
    footer->addWidget(state_, 1);
    footer->addPermanentWidget(discard_);
    footer->addPermanentWidget(save_);
    setStatusBar(footer);
    connect(save_, &ElaPushButton::clicked, this, [this] { saveProfile(); });
    connect(discard_, &ElaPushButton::clicked, this, [this] {
        if (ProfileEditUi::confirm(this, tr("放弃更改"), tr("确定放弃所有未保存的更改？"))) session_->discard();
    });
    auto* shortcut = new QShortcut(QKeySequence::Save, this);
    connect(shortcut, &QShortcut::activated, this, [this] { if (!resolvingClose_) saveProfile(); });
    connect(session_, &ProfileEditSession::changed, this, &ProfileEditWindow::updateState);
    updateState();
}
void ProfileEditWindow::updateState()
{
    const bool dirty = session_->isDirty();
    state_->setText(dirty ? tr("未保存") : tr("已保存"));
    setWindowTitle(dirty ? tr("档案编辑 *") : tr("档案编辑"));
    save_->setEnabled(dirty); discard_->setEnabled(dirty);
}
bool ProfileEditWindow::saveProfile()
{
    const auto failure = ProfileEditSession::validate(session_->draft());
    if (failure) {
        QWidget* target = info_;
        switch (failure.page) {
        case ProfileEditSession::Page::Info: target = info_; break;
        case ProfileEditSession::Page::Subjects: target = subjects_; break;
        case ProfileEditSession::Page::TimeLines: target = timeLines_; break;
        case ProfileEditSession::Page::Schedules: target = schedules_; break;
        }
        navigation(target->property("ElaPageKey").toString());
        switch (failure.page) {
        case ProfileEditSession::Page::Info: info_->focusName(); break;
        case ProfileEditSession::Page::Subjects: subjects_->focusItem(failure.item); break;
        case ProfileEditSession::Page::TimeLines: timeLines_->focusItem(failure.item, failure.row); break;
        case ProfileEditSession::Page::Schedules: schedules_->focusItem(failure.item, failure.weekday, failure.row); break;
        }
        ProfileEditUi::error(this, failure.message);
        return false;
    }
    QString error;
    if (!ProfileManager::instance().commit(session_->draft(), &error)) {
        ProfileEditUi::error(this, tr("保存失败：%1").arg(error)); return false;
    }
    session_->markSaved();
    emit profileSaved();
    const auto warnings = session_->warnings();
    if (warnings.isEmpty()) ElaMessageBar::success(ElaMessageBarType::TopRight, tr("保存成功"), {}, 2500, this);
    else ElaMessageBar::warning(ElaMessageBarType::TopRight, tr("已保存"), warnings.join(QLatin1Char('\n')), 8000, this);
    return true;
}
bool ProfileEditWindow::prepareToClose()
{
    if (resolvingClose_) return false;
    if (!session_->isDirty()) return true;
    QScopedValueRollback<bool> guard(resolvingClose_, true);
    switch (ProfileEditUi::unsaved(this)) {
    case ProfileEditUi::UnsavedChoice::Cancel: return false;
    case ProfileEditUi::UnsavedChoice::Discard: session_->discard(); return true;
    case ProfileEditUi::UnsavedChoice::Save: return saveProfile();
    }
    return false;
}
void ProfileEditWindow::closeEvent(QCloseEvent* event)
{
    if (prepareToClose()) ElaWindow::closeEvent(event);
    else event->ignore();
}

void ProfileEditWindow::showEvent(QShowEvent* event)
{
    ElaWindow::showEvent(event);
#ifdef Q_OS_WIN
    const auto mode = eApp->getWindowDisplayMode();
    if (mode != ElaApplicationType::Normal && mode != ElaApplicationType::ElaMica) {
        // Qt can reset the native theme on reopening. Reapply both the backdrop
        // and its theme through Ela's public API without changing the app theme.
        eApp->syncWindowDisplayMode(this, false);
        eApp->syncWindowDisplayMode(this, true);
    }
#endif
}
