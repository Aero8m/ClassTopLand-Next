#include "SettingsWindow.h"
#include "tabs/PluginSettingsTab.h"
#include "../Ui/Plugins/PluginUiHost.h"
#include <QCloseEvent>
#include "AboutDialog.h"
#include "tabs/AppearanceSettingsTab.h"
#include "tabs/CloudSettingsTab.h"
#include "tabs/CourseBarSettingsTab.h"
#include "tabs/DesktopCountdownSettingsTab.h"
#include "tabs/ReminderSettingsTab.h"
#include "tabs/WindowInterceptionSettingsTab.h"

#include <QScreen>

SettingsWindow::SettingsWindow(QWidget* parent, WindowInterceptionService* interception, PluginManager* plugins, PluginUiHost* pluginUi, CourseBarComponentRegistry* registry, CloudSyncService* cloud)
    : ElaWindow(parent), plugins_(plugins), pluginUi_(pluginUi), registry_(registry),
      cloud_(cloud), interception_(interception)
{
    setIsDefaultClosed(false);
    connect(this, &ElaWindow::closeButtonClicked, this, [this] { close(); });
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
    setNavigationBarWidth(180);
    setNavigationBarDisplayMode(ElaNavigationType::Maximal);
    setIsAllowPageOpenInNewWindow(false);
    resize(850, 550);
    setMinimumSize(850, 550);
    profileTab_ = new ProfileSettingsTab(this);
    addPageNode("档案管理", profileTab_, ElaIconType::FileLines);
    connect(profileTab_, &ProfileSettingsTab::profileEditorRequested,
            this, &SettingsWindow::profileEditorRequested);
    connect(profileTab_, &ProfileSettingsTab::profileSwitchRequested,
            this, &SettingsWindow::profileSwitchRequested);
    appearanceTab_ = new AppearanceSettingsTab(this);
    addPageNode(QStringLiteral("外观"), appearanceTab_, ElaIconType::Palette);
    courseBarTab_ = new CourseBarSettingsTab(this, registry_);
    addPageNode(QStringLiteral("课程条"), courseBarTab_, ElaIconType::List);
    connect(courseBarTab_, &CourseBarSettingsTab::configChanged,
            this, &SettingsWindow::courseBarConfigChanged);
    connect(courseBarTab_, &CourseBarSettingsTab::uiAccessChangeRequested,
            this, &SettingsWindow::uiAccessChangeRequested);

    desktopCountdownTab_ = new DesktopCountdownSettingsTab(this);
    auto* reminderTab = new ReminderSettingsTab(this);
    addPageNode(QStringLiteral("提醒"), reminderTab, ElaIconType::Bell);
    connect(reminderTab, &ReminderSettingsTab::previewRequested, this, &SettingsWindow::reminderPreviewRequested);
    addPageNode(QStringLiteral("桌面倒计时"), desktopCountdownTab_, ElaIconType::Clock);
    connect(desktopCountdownTab_, &DesktopCountdownSettingsTab::configChanged,
            this, &SettingsWindow::desktopCountdownConfigChanged);

    if (cloud_) {
        cloudTab_ = new CloudSettingsTab(cloud_, this);
        addPageNode(QStringLiteral("集控"), cloudTab_, ElaIconType::Cloud);
        connect(cloudTab_, &CloudSettingsTab::profileSwitchRequested,
                this, &SettingsWindow::profileSwitchRequested);
    }

    if (interception_)
        addPageNode(QStringLiteral("窗口拦截"), new WindowInterceptionSettingsTab(interception_, this), ElaIconType::Shield);
    if (plugins_ && pluginUi_) addPageNode(QStringLiteral("插件"), new PluginSettingsTab(plugins_, pluginUi_, this), ElaIconType::PuzzlePiece);
    addFooterNode(QStringLiteral("关于"), aboutNodeKey_, 0, ElaIconType::CircleInfo);
    connect(this, &ElaWindow::navigationNodeClicked, this,
            [this](ElaNavigationType::NavigationNodeType, const QString& nodeKey) {
        if (nodeKey == aboutNodeKey_) showAboutDialog();
    });
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
    profileTab_->refreshProfiles();
}

void SettingsWindow::showProfileError(const QString& message)
{
    profileTab_->showError(message);
}

void SettingsWindow::setUiAccessSwitching(bool switching)
{
    courseBarTab_->setUiAccessSwitching(switching);
}

bool SettingsWindow::courseBarDefaultsResetPending() const
{
    return courseBarTab_->defaultsResetPending();
}

void SettingsWindow::refreshCloudConfig()
{
    // The cloud may replace these sections while a page is open; reload from the
    // committed config so a later edit cannot write back stale values.
    if (appearanceTab_) appearanceTab_->reloadFromConfig();
    if (courseBarTab_) courseBarTab_->reloadFromConfig();
    if (desktopCountdownTab_) desktopCountdownTab_->reloadFromConfig();
}

void SettingsWindow::closeEvent(QCloseEvent* event)
{
    if (pluginUi_ && !pluginUi_->prepareAll()) { event->ignore(); return; }
    if (pluginUi_) pluginUi_->closeDialogs();
    ElaWindow::closeEvent(event);
}
