#include "AppComposer.h"
#include "../../TaskbarTrayMenu/TaskbarTrayMenu.h"
#include "../../SettingsWindow/SettingsWindow.h"
#include "../../ProfileEditWindow/ProfileEditWindow.h"
#include "../../ProfileEditWindow/ProfileEditSession.h"
#include "../../ScheduleAdjustments/DateScheduleWidgets.h"
#include "../ThemeManager/ThemeManager.h"

#include <QApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QScopedValueRollback>
#include <QTimer>

namespace {
bool reportStartupError(const QString& message)
{
    Logger::instance().log(Logger::Level::Error, message);
    Logger::instance().flush();
    QMessageBox::critical(nullptr, QStringLiteral("错误"), message);
    return false;
}
}

AppComposer::AppComposer(QObject* parent) : AppComposer(DATA_PATH, parent)
{
}

AppComposer::AppComposer(const QString& dataDirectory, QObject* parent)
    : QObject(parent), dataDirectory_(QDir(dataDirectory).absolutePath())
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
    const QString configPath = QDir(dataDirectory_).filePath(QStringLiteral("MainConfig.json"));
    const bool createConfig = !QFileInfo::exists(configPath);
    if (!ConfigManager::instance().load(configPath, &error)) {
        return reportStartupError(QStringLiteral("配置加载失败：%1\n%2").arg(configPath, error));
    }
    if (createConfig && !ConfigManager::instance().save(&error)) {
        return reportStartupError(QStringLiteral("默认配置保存失败：%1\n%2").arg(configPath, error));
    }
    ThemeManager::instance().applyConfiguredTheme();
    // init profile manager
    const QString profilePath = QDir(dataDirectory_).filePath(
        QStringLiteral("profiles/%1.json").arg(ConfigManager::instance().config().profileName));
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
    trayMenu_->setExitGuard([this] { return prepareToExit(); });
    connect(trayMenu_.get(), &TaskbarTrayMenu::exitAccepted, this, [this] {
        // Do not allow queued settings actions to delete or change the selected
        // target between acceptance and the deferred application exit.
        if (settingsWindow_) settingsWindow_->setEnabled(false);
        if (profileEditorWindow_) profileEditorWindow_->setEnabled(false);
        if (swapWindow_) swapWindow_->setEnabled(false);
        if (rescheduleWindow_) rescheduleWindow_->setEnabled(false);
    });
    connect(trayMenu_.get(), &TaskbarTrayMenu::settingsRequested,
            this, &AppComposer::showSettingsWindow);
    connect(trayMenu_.get(), &TaskbarTrayMenu::profileEditorRequested,
            this, &AppComposer::showProfileEditorWindow);
    connect(trayMenu_.get(), &TaskbarTrayMenu::weekScheduleRequested,
            this, &AppComposer::selectWeekSchedule);
    connect(trayMenu_.get(), &TaskbarTrayMenu::scheduleListRefreshRequested,
            this, &AppComposer::refreshScheduleMenu);
    connect(trayMenu_.get(), &TaskbarTrayMenu::swapWindowRequested, this, &AppComposer::showSwapWindow);
    connect(trayMenu_.get(), &TaskbarTrayMenu::rescheduleWindowRequested,
            this, &AppComposer::showRescheduleWindow);
    connect(courseBar.get(), &CourseBar::scheduleStatusChanged,
            this, &AppComposer::refreshScheduleMenu);
    refreshScheduleMenu();
    QApplication::setQuitOnLastWindowClosed(false);

    initialized_ = true;
    cleanupDateOverrides();
    dateCleanupTimer_ = new QTimer(this);
    dateCleanupTimer_->setObjectName(QStringLiteral("dateScheduleCleanupTimer"));
    dateCleanupTimer_->setInterval(60000);
    connect(dateCleanupTimer_, &QTimer::timeout, this, &AppComposer::cleanupDateOverrides);
    dateCleanupTimer_->start();
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
    if (dateCleanupTimer_) dateCleanupTimer_->stop();
    swapWindow_.reset();
    rescheduleWindow_.reset();
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
    courseBar->setVisible(ConfigManager::instance().config().courseBarConfig.enable);
    return initialized_;
}

void AppComposer::showSettingsWindow()
{
    // A click queued by a panel that is already being torn down must not revive the window.
    if (!initialized_) return;
    if (!settingsWindow_) {
        settingsWindow_ = std::make_unique<SettingsWindow>();
        connect(settingsWindow_.get(), &SettingsWindow::profileEditorRequested,
                this, &AppComposer::showProfileEditorWindow);
        connect(settingsWindow_.get(), &SettingsWindow::profileSwitchRequested,
                this, &AppComposer::switchProfile);
        connect(settingsWindow_.get(), &SettingsWindow::courseBarConfigChanged,
                courseBar.get(), &CourseBar::reloadConfig);
    }
    settingsWindow_->refreshProfiles();
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
            if (swapWindow_) swapWindow_->refreshProfile(ProfileManager::instance().profile());
            if (rescheduleWindow_) rescheduleWindow_->refreshProfile(ProfileManager::instance().profile());
            if (settingsWindow_) settingsWindow_->refreshProfiles();
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

void AppComposer::showRescheduleWindow()
{
    if (!initialized_) return;
    if (!rescheduleWindow_) {
        rescheduleWindow_ = std::make_unique<RescheduleWindow>();
        connect(rescheduleWindow_.get(), &RescheduleWindow::rescheduleRequested, this,
                [this](const QDate& date, const QString& id, int weekday) {
            if (!initialized_) return;
            QString error;
            if (!ProfileManager::instance().setDateReschedule(date, id, weekday, &error)) {
                rescheduleWindow_->showError(tr("调休保存失败：%1").arg(error)); return;
            }
            rescheduleWindow_->close();
            syncDateAdjustments();
        });
        connect(rescheduleWindow_.get(), &RescheduleWindow::restoreRequested, this, [this](const QDate& date) {
            if (!initialized_) return;
            QString error;
            if (!ProfileManager::instance().restoreDate(date, &error)) {
                rescheduleWindow_->showError(tr("恢复失败：%1").arg(error)); return;
            }
            syncDateAdjustments();
        });
    }
    rescheduleWindow_->refreshProfile(ProfileManager::instance().profile());
    if (!rescheduleWindowPositioned_) {
        auto* screen = trayMenu_ ? trayMenu_->screen() : QGuiApplication::primaryScreen();
        if (screen) {
            const auto available = screen->availableGeometry().size();
            rescheduleWindow_->setMinimumSize(qMin(360, available.width()), qMin(320, available.height()));
            rescheduleWindow_->resize(qMin(460, available.width()), qMin(400, available.height()));
        }
        centerOnPanelScreen(rescheduleWindow_.get()); rescheduleWindowPositioned_ = true;
    }
    presentWindow(rescheduleWindow_.get());
}

void AppComposer::showSwapWindow()
{
    if (!initialized_) return;
    if (!swapWindow_) {
        swapWindow_ = std::make_unique<SwapClassesWindow>();
        connect(swapWindow_.get(), &SwapClassesWindow::swapRequested, this,
                [this](const QDate& date, const QString& id, int first, int second) {
            if (!initialized_) return;
            QString error;
            if (!ProfileManager::instance().swapDateClasses(date, id, first, second, &error)) {
                swapWindow_->showError(tr("换课保存失败：%1").arg(error)); return;
            }
            syncDateAdjustments();
        });
        connect(swapWindow_.get(), &SwapClassesWindow::restoreRequested, this, [this](const QDate& date) {
            if (!initialized_) return;
            QString error;
            if (!ProfileManager::instance().restoreDate(date, &error)) {
                swapWindow_->showError(tr("恢复失败：%1").arg(error)); return;
            }
            syncDateAdjustments();
        });
    }
    swapWindow_->refreshProfile(ProfileManager::instance().profile());
    if (!swapWindowPositioned_) {
        auto* screen = trayMenu_ ? trayMenu_->screen() : QGuiApplication::primaryScreen();
        if (screen) {
            const auto available = screen->availableGeometry().size();
            swapWindow_->setMinimumSize(qMin(430, available.width()), qMin(380, available.height()));
            swapWindow_->resize(qMin(620, available.width()), qMin(560, available.height()));
        }
        centerOnPanelScreen(swapWindow_.get()); swapWindowPositioned_ = true;
    }
    presentWindow(swapWindow_.get());
}

void AppComposer::syncDateAdjustments()
{
    const auto& profile = std::as_const(ProfileManager::instance()).profile();
    if (profileEditorWindow_) profileEditorWindow_->session()->syncDateOverrides(profile.dateOverrides);
    if (courseBar) courseBar->reloadProfile();
    refreshScheduleMenu();
    if (swapWindow_) swapWindow_->refreshProfile(profile);
    if (rescheduleWindow_) rescheduleWindow_->refreshProfile(profile);
}

void AppComposer::cleanupDateOverrides()
{
    if (!initialized_ || shutdown_) return;
    auto& manager = ProfileManager::instance();
    const auto previousCount = manager.profile().dateOverrides.size();
    const auto today = QDate::currentDate();
    QString error;
    QList<QDate> canceled;
    if (!manager.cleanPastDateOverrides(today, &error, &canceled)) {
        if (error != lastCleanupError_) {
            Logger::instance().log(Logger::Level::Warning, tr("日期安排清理失败，将重试：%1").arg(error));
            Logger::instance().flush(); lastCleanupError_ = error;
        }
        return;
    }
    lastCleanupError_.clear();
    if (previousCount != manager.profile().dateOverrides.size() || lastCleanupDate_ != today) syncDateAdjustments();
    lastCleanupDate_ = today;
    if (!canceled.isEmpty() && trayMenu_) {
        QStringList dates;
        for (const auto& date : canceled) dates.append(date.toString(Qt::ISODate));
        trayMenu_->showScheduleError(tr("来源日课程已变化，已取消调休和换课：%1").arg(dates.join(QStringLiteral("、"))));
    }
}

void AppComposer::switchProfile(const QString& id)
{
    if (!initialized_ || !trayMenu_ || switchingProfile_ || id.isEmpty()) return;
    if (id.compare(QFileInfo(ProfileManager::instance().filePath()).completeBaseName(), Qt::CaseInsensitive) == 0) return;
    QScopedValueRollback<bool> switching(switchingProfile_, true);
    QScopedValueRollback<QString> selection(pendingProfileId_, id);
    // The shared exit guard resolves unsaved changes before committing selection.
    // A rejected exit never publishes the requested profile in memory or on disk.
    trayMenu_->requestExit(TaskbarTrayMenu::RestartExitCode);
}

bool AppComposer::prepareToExit()
{
    if (profileEditorWindow_ && profileEditorWindow_->session()->isDirty()) {
        showProfileEditorWindow();
        if (!profileEditorWindow_->prepareToClose()) return false;
    }
    if (pendingProfileId_.isEmpty()) return true;
    QString error;
    Profile target;
    if (!ProfileManager::instance().readProfile(pendingProfileId_, target, &error)) {
        if (settingsWindow_) settingsWindow_->showProfileError(tr("切换失败，目标档案无法读取：%1").arg(error));
        return false;
    }
    Config candidate = ConfigManager::instance().config();
    candidate.profileName = pendingProfileId_;
    if (!ConfigManager::instance().commit(candidate, &error)) {
        if (settingsWindow_) settingsWindow_->showProfileError(tr("切换失败，配置保存失败：%1").arg(error));
        return false;
    }
    return true;
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
    syncDateAdjustments();
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
