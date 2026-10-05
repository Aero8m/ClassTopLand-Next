#include "SettingsWindow.h"
#include "AboutDialog.h"
#include "tabs/AppearanceSettingsTab.h"
#include "tabs/CourseBarSettingsTab.h"

#include <QScreen>

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
