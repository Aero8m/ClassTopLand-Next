#include "SettingsWindow.h"
#include "tabs/AgentSettingsTab.h"
#include "../Ui/UiWidgets.h"
#include <QCloseEvent>
#include <QDialog>
#include "AboutDialog.h"
#include "tabs/AppearanceSettingsTab.h"
#include "tabs/CloudSettingsTab.h"
#include "tabs/CourseBarSettingsTab.h"
#include "tabs/DesktopCountdownSettingsTab.h"
#include "tabs/ReminderSettingsTab.h"
#include "tabs/ProfileSettingsTab.h"
#include "tabs/UpdateSettingsTab.h"
#include "tabs/WindowInterceptionSettingsTab.h"

#include <QScreen>
#include <algorithm>
#include <utility>

SettingsWindow::SettingsWindow(WindowInterceptionService* interception, CourseBarComponentRegistry* registry,
                               CloudSyncService* cloud, std::function<AgentService*()> agent, UpdateService* update)
    : pages_(std::make_unique<AppUi::LazyPages>(this)), registry_(registry), cloud_(cloud),
      agent_(std::move(agent)), update_(update), interception_(interception)
{
    setIsDefaultClosed(false);
    connect(this, &ElaWindow::closeButtonClicked, this, [this] { close(); });
    initUI();
}

SettingsWindow::~SettingsWindow() = default;

void SettingsWindow::initUI()
{
    // init window properties
    setWindowTitle("设置");
    setWindowButtonFlag(ElaAppBarType::StayTopButtonHint,false);
    setWindowButtonFlag(ElaAppBarType::RouteBackButtonHint,false);
    setWindowButtonFlag(ElaAppBarType::RouteForwardButtonHint,false);
    setWindowButtonFlag(ElaAppBarType::ThemeChangeButtonHint, false);

    // init tabs
    setUserInfoCardVisible(false);
    setNavigationBarDisplayMode(ElaNavigationType::Maximal);
    setIsAllowPageOpenInNewWindow(false);
    resize(AppUi::defaultWindowSize);
    setMinimumSize(AppUi::defaultWindowSize);
    pages_->add(QStringLiteral("profiles"), tr("档案管理"), ElaIconType::FileLines, [this](QWidget* host) {
        profileTab_ = new ProfileSettingsTab(host);
        connect(profileTab_, &ProfileSettingsTab::profileEditorRequested, this, &SettingsWindow::profileEditorRequested);
        connect(profileTab_, &ProfileSettingsTab::profileSwitchRequested, this, &SettingsWindow::profileSwitchRequested);
        if (!pendingProfileError_.isEmpty())
            profileTab_->showError(std::exchange(pendingProfileError_, {}));
        return profileTab_;
    });
    pages_->add(QStringLiteral("appearance"), tr("外观"), ElaIconType::Palette, [this](QWidget* host) {
        return appearanceTab_ = new AppearanceSettingsTab(host);
    });
    pages_->add(QStringLiteral("courseBar"), tr("课程条"), ElaIconType::List, [this](QWidget* host) {
        courseBarTab_ = new CourseBarSettingsTab(host, registry_);
        connect(courseBarTab_, &CourseBarSettingsTab::configChanged, this, &SettingsWindow::courseBarConfigChanged);
        connect(courseBarTab_, &CourseBarSettingsTab::uiAccessChangeRequested, this, &SettingsWindow::uiAccessChangeRequested);
        return courseBarTab_;
    });
    pages_->add(QStringLiteral("reminders"), tr("提醒"), ElaIconType::Bell, [this](QWidget* host) {
        reminderTab_ = new ReminderSettingsTab(host);
        connect(reminderTab_, &ReminderSettingsTab::previewRequested, this, &SettingsWindow::reminderPreviewRequested);
        return reminderTab_;
    });
    pages_->add(QStringLiteral("countdown"), tr("桌面倒计时"), ElaIconType::Clock, [this](QWidget* host) {
        desktopCountdownTab_ = new DesktopCountdownSettingsTab(host);
        connect(desktopCountdownTab_, &DesktopCountdownSettingsTab::configChanged,
                this, &SettingsWindow::desktopCountdownConfigChanged);
        return desktopCountdownTab_;
    });
    if (cloud_) {
        pages_->add(QStringLiteral("cloud"), tr("集控"), ElaIconType::Cloud, [this](QWidget* host) {
            cloudTab_ = new CloudSettingsTab(cloud_, host);
            connect(cloudTab_, &CloudSettingsTab::profileSwitchRequested, this, &SettingsWindow::profileSwitchRequested);
            return cloudTab_;
        });
    }
    if (interception_) {
        pages_->add(QStringLiteral("interception"), tr("窗口拦截"), ElaIconType::Shield, [this](QWidget* host) {
            return interceptionTab_ = new WindowInterceptionSettingsTab(interception_, host);
        });
    }
    if (agent_) {
        pages_->add(QStringLiteral("agent"), tr("AI Agent"), ElaIconType::Robot, [this](QWidget* host) {
            return agentTab_ = new AgentSettingsTab(agent_(), host);
        });
    }
    if (update_) {
        pages_->add(QStringLiteral("update"), tr("更新"), ElaIconType::ArrowRotateRight, [this](QWidget* host) {
            return new UpdateSettingsTab(update_, host);
        });
    }
    addFooterNode(QStringLiteral("关于"), aboutNodeKey_, 0, ElaIconType::CircleInfo);
    connect(this, &ElaWindow::navigationNodeClicked, this,
            [this](ElaNavigationType::NavigationNodeType, const QString& nodeKey) {
        if (nodeKey == aboutNodeKey_) showAboutDialog();
    });
}

void SettingsWindow::openAgentPage(const QString &draftSession)
{
    if (!pages_->showPage(QStringLiteral("agent"))) return;
    if (!draftSession.isEmpty()) agentTab_->openDraft(draftSession);
    else agentTab_->openProviders();
}

void SettingsWindow::showAboutDialog()
{
    if (!aboutDialog_) aboutDialog_ = new AboutDialog(this);
    if (!aboutDialog_->isVisible()) {
        // Center on the settings window, keeping the dialog inside its screen.
        const QRect available = screen()->availableGeometry();
        QPoint position = frameGeometry().center() - aboutDialog_->rect().center();
        position.setX(qBound(available.left(), position.x(),
                            qMax(available.left(), available.right() - aboutDialog_->width() + 1)));
        position.setY(qBound(available.top(), position.y(),
                            qMax(available.top(), available.bottom() - aboutDialog_->height() + 1)));
        aboutDialog_->move(position);
    }
    aboutDialog_->show();
    aboutDialog_->raise();
    aboutDialog_->activateWindow();
}

void SettingsWindow::refreshProfiles()
{
    if (profileTab_ && profileTab_->isVisible()) profileTab_->scheduleRefresh();
}

void SettingsWindow::showProfileError(const QString& message)
{
    if (profileTab_) profileTab_->showError(message);
    else pendingProfileError_ = message;
}

void SettingsWindow::setUiAccessSwitching(bool switching)
{
    if (courseBarTab_) courseBarTab_->setUiAccessSwitching(switching);
}

bool SettingsWindow::courseBarDefaultsResetPending() const
{
    return courseBarTab_ && courseBarTab_->defaultsResetPending();
}

void SettingsWindow::refreshCloudConfig()
{
    // The cloud may replace these sections while a page is open; reload from the
    // committed config so a later edit cannot write back stale values.
    if (appearanceTab_) appearanceTab_->reloadFromConfig();
    if (desktopCountdownTab_) desktopCountdownTab_->reloadFromConfig();
}

QString SettingsWindow::currentPage() const
{
    return pages_->currentPage();
}

void SettingsWindow::restorePage(const QString& id)
{
    if (!id.isEmpty()) pages_->showPage(id);
}

bool SettingsWindow::hasUnsavedInput() const
{
    return (courseBarTab_ && courseBarTab_->hasUnsavedInput()) ||
           (reminderTab_ && reminderTab_->hasUnsavedInput()) ||
           (desktopCountdownTab_ && desktopCountdownTab_->hasUnsavedInput()) ||
           (cloudTab_ && cloudTab_->hasUnsavedInput()) ||
           (interceptionTab_ && interceptionTab_->hasUnsavedInput()) ||
           (agentTab_ && agentTab_->hasUnsavedInput());
}

bool SettingsWindow::hasPendingOperations() const
{
    if ((profileTab_ && profileTab_->busy()) ||
        (courseBarTab_ && courseBarTab_->hasPendingOperations()) ||
        (cloudTab_ && cloudTab_->hasPendingOperations()) ||
        (agentTab_ && agentTab_->hasPendingOperations()))
        return true;
    const auto dialogs = findChildren<QDialog*>();
    return std::any_of(dialogs.cbegin(), dialogs.cend(), [](const QDialog* dialog) {
        return !dialog->isHidden();
    });
}

void SettingsWindow::closeEvent(QCloseEvent* event)
{
    ElaWindow::closeEvent(event);
    if (event->isAccepted()) emit closed();
}
