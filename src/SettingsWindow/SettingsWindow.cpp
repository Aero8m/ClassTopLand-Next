#include "SettingsWindow.h"
#include "tabs/AppearanceSettingsTab.h"
#include "tabs/CourseBarSettingsTab.h"

SettingsWindow::SettingsWindow(QWidget* parent) : ElaWindow(parent)
{
    initUI();
}

SettingsWindow::~SettingsWindow()
{

}

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
    setNavigationBarWidth(220);
    setIsAllowPageOpenInNewWindow(false);
    resize(1000, 720);
    setMinimumSize(900, 600);
    profileTab_ = new ProfileSettingsTab(this);
    addPageNode("档案管理", profileTab_, ElaIconType::FileLines);
    connect(profileTab_, &ProfileSettingsTab::profileEditorRequested,
            this, &SettingsWindow::profileEditorRequested);
    connect(profileTab_, &ProfileSettingsTab::profileSwitchRequested,
            this, &SettingsWindow::profileSwitchRequested);
    addPageNode(QStringLiteral("外观"), new AppearanceSettingsTab(this), ElaIconType::Palette);
    auto* courseBarTab = new CourseBarSettingsTab(this);
    addPageNode(QStringLiteral("课程条"), courseBarTab, ElaIconType::List);
    connect(courseBarTab, &CourseBarSettingsTab::configChanged,
            this, &SettingsWindow::courseBarConfigChanged);
}

void SettingsWindow::refreshProfiles()
{
    profileTab_->refreshProfiles();
}

void SettingsWindow::showProfileError(const QString& message)
{
    profileTab_->showError(message);
}
