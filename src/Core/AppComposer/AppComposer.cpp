#include "AppComposer.h"
#include "../../TaskbarTrayMenu/TaskbarTrayMenu.h"
#include "../../SettingsWindow/SettingsWindow.h"
#include "../../ProfileEditWindow/ProfileEditWindow.h"
#include "../../ProfileEditWindow/ProfileEditSession.h"

#include <QApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>

namespace {
bool reportStartupError(const QString& message)
{
    Logger::instance().log(Logger::Level::Error, message);
    Logger::instance().flush();
    QMessageBox::critical(nullptr, QStringLiteral("错误"), message);
    return false;
}
}

AppComposer::AppComposer(QObject* parent) : QObject(parent)
{

}

AppComposer::~AppComposer()
{
    shutdown();
}

void AppComposer::shutdown()
{
    dropInstances();
    // The tray icon and panel may have queued deferred deletions while hiding.
    // Drain them here so the process is not left with a half-destroyed UI.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

bool AppComposer::initInstances()
{
    if (initialized_) return true;
    // init logger
    Logger::instance().addConsoleOutput(true);
    printLogo();
    Logger::instance().log(Logger::Level::Info, "Initializing AppComposer...");
    Logger::instance().flush();

    // init config manager
    QString error;
    const QString configPath = CONFIG_FILE_PATH;
    const bool createConfig = !QFileInfo::exists(configPath);
    if (!ConfigManager::instance().load(configPath, &error)) {
        return reportStartupError(QStringLiteral("配置加载失败：%1\n%2").arg(configPath, error));
    }
    if (createConfig && !ConfigManager::instance().save(&error)) {
        return reportStartupError(QStringLiteral("默认配置保存失败：%1\n%2").arg(configPath, error));
    }
    // init profile manager
    const QString profilePath = GET_PROFILE_PATH(ConfigManager::instance().config().profileName);
    const bool createProfile = !QFileInfo::exists(profilePath);
    if (!ProfileManager::instance().load(profilePath, &error)) {
        return reportStartupError(QStringLiteral("档案加载失败：%1\n%2").arg(profilePath, error));
    }
    if (createProfile && !ProfileManager::instance().save(&error)) {
        return reportStartupError(QStringLiteral("默认档案保存失败：%1\n%2").arg(profilePath, error));
    }
    // init course bar
    courseBar = std::make_unique<CourseBar>();
    trayMenu_ = std::make_unique<TaskbarTrayMenu>();
    trayMenu_->setExitGuard([this] {
        if (!profileEditorWindow_ || !profileEditorWindow_->session()->isDirty()) return true;
        showProfileEditorWindow();
        return profileEditorWindow_->prepareToClose();
    });
    connect(trayMenu_.get(), &TaskbarTrayMenu::settingsRequested,
            this, &AppComposer::showSettingsWindow);
    connect(trayMenu_.get(), &TaskbarTrayMenu::profileEditorRequested,
            this, &AppComposer::showProfileEditorWindow);
    connect(trayMenu_.get(), &TaskbarTrayMenu::weekScheduleRequested,
            this, &AppComposer::selectWeekSchedule);
    connect(trayMenu_.get(), &TaskbarTrayMenu::scheduleListRefreshRequested,
            this, &AppComposer::refreshScheduleMenu);
    connect(courseBar.get(), &CourseBar::scheduleStatusChanged,
            this, &AppComposer::refreshScheduleMenu);
    refreshScheduleMenu();
    QApplication::setQuitOnLastWindowClosed(false);

    initialized_ = true;
    Logger::instance().log(Logger::Level::Info,
                           QStringLiteral("AppComposer initialized. Config: %1; Profile: %2")
                               .arg(configPath, profilePath));
    Logger::instance().flush();
    return true;
}

void AppComposer::dropInstances()
{
    if (!initialized_) return;
    initialized_ = false;
    if (shutdown_) return;
    shutdown_ = true;
    settingsWindow_.reset();
    profileEditorWindow_.reset();
    trayMenu_.reset();
    courseBar.reset();
    QString error;
    if (!ConfigManager::instance().save(&error))
        Logger::instance().log(Logger::Level::Error, QStringLiteral("配置保存失败：%1").arg(error));
    if (!ProfileManager::instance().save(&error))
        Logger::instance().log(Logger::Level::Error, QStringLiteral("档案保存失败：%1").arg(error));
    Logger::instance().log(Logger::Level::Info, "AppComposer dropped, Application will be stopped");
    Logger::instance().flush();
}

bool AppComposer::startApp()
{
    if (!initInstances()) return false;
    courseBar->show();
    return initialized_;
}

void AppComposer::showSettingsWindow()
{
    // A click queued by a panel that is already being torn down must not revive the window.
    if (!initialized_) return;
    if (!settingsWindow_) settingsWindow_ = std::make_unique<SettingsWindow>();
    if (!settingsWindowPositioned_) {
        centerOnPanelScreen(settingsWindow_.get());
        settingsWindowPositioned_ = true;
    }
    presentWindow(settingsWindow_.get());
}

void AppComposer::showProfileEditorWindow()
{
    if (!initialized_) return;
    if (!profileEditorWindow_) {
        profileEditorWindow_ = std::make_unique<ProfileEditWindow>();
        connect(profileEditorWindow_.get(), &ProfileEditWindow::profileSaved,
                this, [this] {
            courseBar->reloadProfile();
            refreshScheduleMenu();
        });
    }
    if (!profileEditorWindowPositioned_) {
        centerOnPanelScreen(profileEditorWindow_.get());
        profileEditorWindowPositioned_ = true;
    }
    presentWindow(profileEditorWindow_.get());
}

void AppComposer::refreshScheduleMenu()
{
    if (!trayMenu_ || !courseBar) return;
    const auto& profile = std::as_const(ProfileManager::instance()).profile();
    QList<TaskbarTrayMenu::ScheduleChoice> choices;
    const int weekday = QDate::currentDate().dayOfWeek();
    for (const auto& week : profile.schedules) {
        int count = 0;
        for (const auto& day : week.daySchedules) if (day.enableDay == weekday) count += day.classes.size();
        QString mode;
        switch (week.mode) {
        case WeekScheduleMode::All: mode = tr("全部周"); break;
        case WeekScheduleMode::Odd: mode = tr("单周"); break;
        case WeekScheduleMode::Even: mode = tr("双周"); break;
        }
        const QString detail = count ? tr("%1 · 今日 %2 节").arg(mode).arg(count)
                                     : tr("%1 · 今日无课程").arg(mode);
        choices.append({week.id, week.name, detail});
    }
    trayMenu_->setScheduleChoices(choices, profile.activeWeekScheduleId);
    trayMenu_->setScheduleStatus(courseBar->scheduleStatus(), courseBar->hasCourseViews());
}

void AppComposer::selectWeekSchedule(const QString& id)
{
    if (!initialized_ || !trayMenu_ || !courseBar) return;
    auto& manager = ProfileManager::instance();
    if (manager.profile().activeWeekScheduleId == id) return;
    QString error;
    if (!manager.selectWeekSchedule(id, &error)) {
        trayMenu_->showScheduleError(tr("切换失败：%1").arg(error));
        return;
    }
    if (profileEditorWindow_) profileEditorWindow_->session()->syncActiveWeekSchedule(id);
    courseBar->reloadProfile();
    refreshScheduleMenu();
}

void AppComposer::centerOnPanelScreen(QWidget* window)
{
    // Center on the screen that holds the panel, so a tray on a secondary monitor
    // does not open the window on the primary one. The window has no handle yet,
    // which is why ElaWindow::moveToCenter() (primary screen at that point) is not used.
    QScreen* screen = trayMenu_ ? trayMenu_->screen() : nullptr;
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    const QRect available = screen->availableGeometry();
    window->move(available.center() - window->rect().center());
}

void AppComposer::presentWindow(QWidget* window)
{
    // Idempotent: returns immediately when the panel is hidden or already closing.
    if (trayMenu_) trayMenu_->hideMenu();
    if (window->isMinimized()) window->showNormal();
    window->show();
    window->raise();
    window->activateWindow();
}

void AppComposer::printLogo()
{
    QFile logoFile(":/res/logo.txt");
    if (logoFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        Logger::instance().print(Logger::Level::Debug, QString(logoFile.readAll()));
        Logger::instance().print(Logger::Level::Debug, "----------------------------------------------------------------------------------------\n");
        Logger::instance().print(Logger::Level::Debug, "                                Powered By Aero8m\n");
        Logger::instance().flush();
    }
    logoFile.close();
}
