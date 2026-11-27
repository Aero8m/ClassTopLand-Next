#include "ProfileEditWindow.h"
#include "ProfileEditSession.h"
#include "../Ui/UiWidgets.h"
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

ProfileEditWindow::ProfileEditWindow(QWidget* parent)
    : ElaWindow(parent), pages_(std::make_unique<AppUi::LazyPages>(this))
{
    session_ = new ProfileEditSession(this);
    session_->reset(ProfileManager::instance().profile());
    initUI();
}
ProfileEditWindow::~ProfileEditWindow() = default;

void ProfileEditWindow::initUI()
{
    using namespace AppUi;
    setWindowTitle(tr("档案编辑"));
    resize(850, 550);
    setMinimumSize(850, 550);
    setNavigationBarDisplayMode(ElaNavigationType::Maximal);
    setUserInfoCardVisible(false);
    setIsAllowPageOpenInNewWindow(false);
    setWindowButtonFlag(ElaAppBarType::StayTopButtonHint, false);
    setWindowButtonFlag(ElaAppBarType::RouteBackButtonHint, false);
    setWindowButtonFlag(ElaAppBarType::RouteForwardButtonHint, false);
    setWindowButtonFlag(ElaAppBarType::ThemeChangeButtonHint, false);
    setIsDefaultClosed(false);
    connect(this, &ElaWindow::closeButtonClicked, this, [this] { close(); });
    pages_->add(QStringLiteral("info"), tr("基本信息"), ElaIconType::FileLines, [this](QWidget* host) {
        return new ProfileInfoTab(session_, host);
    });
    pages_->add(QStringLiteral("subjects"), tr("科目管理"), ElaIconType::BookOpen, [this](QWidget* host) {
        return new SubjectsTab(session_, host);
    });
    pages_->add(QStringLiteral("timelines"), tr("时间线管理"), ElaIconType::Clock, [this](QWidget* host) {
        return new TimeLinesTab(session_, host);
    });
    pages_->add(QStringLiteral("schedules"), tr("课表管理"), ElaIconType::CalendarDays, [this](QWidget* host) {
        return new SchedulesTab(session_, host);
    });
    auto* footer = new ElaStatusBar(this);
    footer->setSizeGripEnabled(false);
    footer->setFixedHeight(64);
    footer->setContentsMargins(20, 4, 16, 4);
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
        if (AppUi::confirm(this, tr("放弃更改"), tr("确定放弃所有未保存的更改？"))) session_->discard();
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
    const auto failure = session_->validateForSave();
    if (failure) {
        switch (failure.page) {
        case ProfileEditSession::Page::Info:
            static_cast<ProfileInfoTab*>(pages_->showPage(QStringLiteral("info")))->focusName();
            break;
        case ProfileEditSession::Page::Subjects:
            static_cast<SubjectsTab*>(pages_->showPage(QStringLiteral("subjects")))->focusItem(failure.item);
            break;
        case ProfileEditSession::Page::TimeLines:
            static_cast<TimeLinesTab*>(pages_->showPage(QStringLiteral("timelines")))->focusItem(failure.item, failure.row);
            break;
        case ProfileEditSession::Page::Schedules:
            static_cast<SchedulesTab*>(pages_->showPage(QStringLiteral("schedules")))->focusItem(failure.item, failure.weekday, failure.row);
            break;
        }
        AppUi::error(this, failure.message);
        return false;
    }
    QString error;
    QList<QDate> canceled;
    if (!ProfileManager::instance().commit(session_->draft(), &error, &canceled)) {
        AppUi::error(this, tr("保存失败：%1").arg(error)); return false;
    }
    session_->reset(ProfileManager::instance().profile());
    emit profileSaved();
    auto warnings = session_->warnings();
    if (!canceled.isEmpty()) {
        QStringList dates;
        for (const auto& date : canceled) dates.append(date.toString(Qt::ISODate));
        warnings.append(tr("来源日课程已变化，已取消以下日期的调休、换课和代课：%1").arg(dates.join(QStringLiteral("、"))));
    }
    if (warnings.isEmpty()) ElaMessageBar::success(ElaMessageBarType::TopRight, tr("保存成功"), {}, 2500, this);
    else ElaMessageBar::warning(ElaMessageBarType::TopRight, tr("已保存"), warnings.join(QLatin1Char('\n')), 8000, this);
    return true;
}
bool ProfileEditWindow::prepareToClose()
{
    if (resolvingClose_) return false;
    if (!session_->isDirty()) return true;
    QScopedValueRollback<bool> guard(resolvingClose_, true);
    switch (AppUi::unsaved(this)) {
    case AppUi::UnsavedChoice::Cancel: return false;
    case AppUi::UnsavedChoice::Discard: session_->discard(); return true;
    case AppUi::UnsavedChoice::Save: return saveProfile();
    }
    return false;
}
QString ProfileEditWindow::currentPage() const
{
    return pages_->currentPage();
}

void ProfileEditWindow::restorePage(const QString& id)
{
    if (!id.isEmpty()) pages_->showPage(id);
}

void ProfileEditWindow::closeEvent(QCloseEvent* event)
{
    if (!prepareToClose()) {
        event->ignore();
        return;
    }
    ElaWindow::closeEvent(event);
    if (event->isAccepted()) emit closed();
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
