#include "AppComposer.h"
#include "../Plugins/PluginManager.h"
#include "../../Ui/Plugins/PluginUiHost.h"
#include "../../Ui/Plugins/PluginCenterWindow.h"
#include "../../CourseBar/CourseBarComponentRegistry.h"
#include <QScopeGuard>
#include "../../TaskbarTrayMenu/TaskbarTrayMenu.h"
#include "../../SettingsWindow/SettingsWindow.h"
#include "../../AgentWindow/AgentWindow.h"
#include "../../Ui/UiWidgets.h"
#include "../../DesktopCountdown/DesktopCountdown.h"
#include "../../ProfileEditWindow/ProfileEditWindow.h"
#include "../../ProfileEditWindow/ProfileEditSession.h"
#include "../../ScheduleAdjustments/DateScheduleWidgets.h"
#include "../ThemeManager/ThemeManager.h"
#include "../Reminders/ReminderService.h"
#include "../CloudSync/CloudSyncService.h"
#include "../Update/UpdateService.h"
#include "../WindowInterception/WindowInterceptionService.h"
#include "../../Utils/UiAccess/UiAccess.h"

#include <ElaMessageBar.h>
#include <QApplication>
#include <QCursor>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QScopedValueRollback>
#include <QSystemTrayIcon>
#include <QTimer>
#include <utility>

namespace {
constexpr int WindowReclaimDelayMs = 30000;
constexpr int ReclaimOperationCheckMs = 1000;

bool reportStartupError(const QString& logMessage, const QString& message)
{
    Logger::instance().log(Logger::Level::Error, logMessage);
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
    settingsReclaim_.setObjectName(QStringLiteral("settingsReclaimTimer"));
    settingsReclaim_.setSingleShot(true);
    settingsReclaim_.setTimerType(Qt::PreciseTimer);
    connect(&settingsReclaim_, &QTimer::timeout, this, &AppComposer::reclaimSettingsWindow);

    profileEditorReclaim_.setObjectName(QStringLiteral("profileEditorReclaimTimer"));
    profileEditorReclaim_.setSingleShot(true);
    profileEditorReclaim_.setTimerType(Qt::PreciseTimer);
    connect(&profileEditorReclaim_, &QTimer::timeout, this, [this] {
        if (!initialized_ || exitAccepted_ || !profileEditorWindow_ || profileEditorWindow_->isVisible() ||
            profileEditorWindow_->session()->isDirty()) return;
        profileEditorWindowState_ = {profileEditorWindow_->saveGeometry(), profileEditorWindow_->currentPage()};
        profileEditorWindow_.reset();
    });
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
        return reportStartupError(QStringLiteral("Could not load configuration from %1: %2").arg(configPath, error),
                                  QStringLiteral("配置加载失败：%1\n%2").arg(configPath, error));
    }
    if (startupUiAccessOverride_)
        ConfigManager::instance().config().courseBarConfig.uiAccessEnabled = *startupUiAccessOverride_;
    if (createConfig && !ConfigManager::instance().save(&error)) {
        return reportStartupError(QStringLiteral("Could not save default configuration to %1: %2").arg(configPath, error),
                                  QStringLiteral("默认配置保存失败：%1\n%2").arg(configPath, error));
    }
    ThemeManager::instance().applyConfiguredTheme();
    // init profile manager
    const QString profilePath = QDir(dataDirectory_).filePath(
        QStringLiteral("profiles/%1.json").arg(ConfigManager::instance().config().profileName));
    const bool createProfile = !QFileInfo::exists(profilePath);
    if (!ProfileManager::instance().load(profilePath, &error)) {
        return reportStartupError(QStringLiteral("Could not load profile from %1: %2").arg(profilePath, error),
                                  QStringLiteral("档案加载失败：%1\n%2").arg(profilePath, error));
    }
    if (createProfile && !ProfileManager::instance().save(&error)) {
        return reportStartupError(QStringLiteral("Could not save default profile to %1: %2").arg(profilePath, error),
                                  QStringLiteral("默认档案保存失败：%1\n%2").arg(profilePath, error));
    }
    windowInterception_ = std::make_unique<WindowInterceptionService>();
    connect(windowInterception_.get(), &WindowInterceptionService::errorOccurred, this, [](const QString& message) {
        Logger::instance().log(Logger::Level::Warning, QStringLiteral("Window interception failed: %1").arg(message));
        Logger::instance().flush();
    });
    if (windowInterception_->supported()) windowInterception_->start();
    // init course bar
    plugins_ = std::make_unique<PluginManager>(dataDirectory_, pluginsDisabled_);
    pluginUi_ = std::make_unique<PluginUiHost>(plugins_.get());
    pluginUi_->navigator = [this](const QString &plugin, const QString &page, const QString &component, QString *error) {
        showPluginCenter();
        if (!pluginCenter_) {
            if (error) *error = QStringLiteral("插件中心尚未就绪");
            return false;
        }
        return pluginCenter_->navigate(plugin, page, component, error);
    };
    componentRegistry_ = std::make_unique<CourseBarComponentRegistry>(plugins_.get(), pluginUi_.get());
    plugins_->editorDirty = [this] { return profileEditorWindow_ && profileEditorWindow_->session()->isDirty(); };
    courseBar = std::make_unique<CourseBar>(nullptr, componentRegistry_.get(), plugins_->courses());
    reminders_ = std::make_unique<ReminderService>(plugins_->courses());
    connect(reminders_.get(), &ReminderService::notification, courseBar.get(), &CourseBar::showReminder);
    connect(reminders_.get(), &ReminderService::invalidated, courseBar.get(), &CourseBar::invalidateReminders);
    connect(componentRegistry_.get(), &CourseBarComponentRegistry::changed, courseBar.get(), &CourseBar::reloadConfig);
    connect(&ConfigManager::instance(), &ConfigManager::courseBarChanged, courseBar.get(), &CourseBar::reloadConfig);
    connect(plugins_.get(), &PluginManager::notification, this, [this](const QString& title, const QString& message) {
        Logger::instance().log(Logger::Level::Warning, QStringLiteral("Plugin notification (%1): %2").arg(title, message));
        if (courseBar) courseBar->showNotification(title + QStringLiteral(": ") + message);
        showScheduleError(title + QStringLiteral(": ") + message);
    });
    connect(plugins_.get(), &PluginManager::restartRequested, this, [this] {
        requestExit(TaskbarTrayMenu::RestartExitCode);
        if (!exitAccepted_) {
            auto candidate = ConfigManager::instance().config(); candidate.pluginConfig.recoveryNextStart = false;
            QString error;
            if (!ConfigManager::instance().commit(candidate, &error))
                Logger::instance().log(Logger::Level::Error,
                                       QStringLiteral("Could not clear plugin recovery mode: %1").arg(error));
        }
    });
    connect(&ProfileManager::instance(), &ProfileManager::committed, this, [this] {
        // Refresh a clean editor synchronously, before another input event can create a stale draft.
        if (profileEditorWindow_ && !profileEditorWindow_->session()->isDirty())
            profileEditorWindow_->session()->reset(ProfileManager::instance().profile());
        else if (profileEditorWindow_)
            profileEditorWindow_->session()->syncReminderRules(ProfileManager::instance().profile().reminderRules);
        QTimer::singleShot(0, this, [this] {
            if (!initialized_) return;
            if (profileEditorWindow_ && !profileEditorWindow_->session()->isDirty()) profileEditorWindow_->session()->reset(ProfileManager::instance().profile());
            syncDateAdjustments(); if (settingsWindow_) settingsWindow_->refreshProfiles();
        });
    });
    connect(plugins_.get(), &PluginManager::profileWritten, this, [this](const QJsonArray& canceled) {
        if (canceled.isEmpty()) return;
        QStringList dates;
        for (const auto& date : canceled) dates.append(date.toString());
        showScheduleError(tr("来源课程变化，已取消特殊安排：%1").arg(dates.join(QStringLiteral("、"))));
    });
    desktopCountdown_ = std::make_unique<DesktopCountdown>();
    cloud_ = std::make_unique<CloudSyncService>(dataDirectory_);
    cloud_->setWindowRulesReload([this](QString* error) {
        return windowInterception_ && windowInterception_->reloadFromConfig(error);
    });
    cloud_->setProfileEditorDirty([this] {
        return profileEditorWindow_ && profileEditorWindow_->session()->isDirty();
    });
    cloud_->setPluginTelemetry([this] {
        QJsonArray entries;
        if (plugins_)
            for (const auto& record : plugins_->records())
                entries.append(QJsonObject{{QStringLiteral("id"), record->manifest.id},
                                            {QStringLiteral("running"), record->state == QStringLiteral("运行中")}});
        return entries;
    });
    cloud_->setManagedRestart([this](const QString& id) { return managedRestart(id); });
    cloud_->setPowerOperationGuard([this] { return managedExitBlockReason(); });
    connect(cloud_.get(), &CloudSyncService::displayConfigApplied, this, [this] {
        ThemeManager::instance().applyConfiguredTheme();
        if (desktopCountdown_) desktopCountdown_->reloadConfig();
        if (settingsWindow_) settingsWindow_->refreshCloudConfig();
    });
    connect(cloud_.get(), &CloudSyncService::cloudProfileSwitchRequested, this,
            [this](const QString& id) { switchProfile(id); });
    connect(cloud_.get(), &CloudSyncService::managedNotification, this, [this](const QString& message) {
        if (trayIcon_) trayIcon_->showMessage(tr("教室通知"), message, QSystemTrayIcon::Information);
    });
    connect(cloud_.get(), &CloudSyncService::cloudProfileUpdated, this, [this] {
        if (settingsWindow_) settingsWindow_->refreshProfiles();
    });
    trayIcon_ = std::make_unique<QSystemTrayIcon>(QIcon(QStringLiteral(":/res/images/icon.png")));
    trayIcon_->setToolTip(QStringLiteral("ClassTopLand Next"));
    connect(trayIcon_.get(), &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::Context)
            QTimer::singleShot(0, this, &AppComposer::showTrayMenu);
    });
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        Logger::instance().log(Logger::Level::Warning, "System tray is not available");
        Logger::instance().flush();
    }
    trayIcon_->show();
    update_ = std::make_unique<UpdateService>();
    cloud_->setUpdateService(update_.get(), [this] { return managedExitBlockReason(); });
    const auto loadUpdatePreference = [this] {
        if (update_) update_->setAutomaticUpdates(ConfigManager::instance().config().automaticUpdates);
    };
    connect(&ConfigManager::instance(), &ConfigManager::committed, update_.get(), loadUpdatePreference);
    connect(update_.get(), &UpdateService::changed, this, [this] {
        if (trayMenu_) trayMenu_->setUpdateOnQuit(update_->readyToInstallOnQuit());
    });
    connect(update_.get(), &UpdateService::installRequested, this, [this](bool restartAfterInstall) {
        if (update_->managedUpdate()) {
            const QString blocked = managedExitBlockReason();
            if (!blocked.isEmpty()) {
                update_->installationDeferred(blocked);
                return;
            }
            const QScopedValueRollback<bool> silent(silentManagedRestart_, true);
            requestExit(TaskbarTrayMenu::UpdateExitCode);
            if (!exitAccepted_ && update_->state() == UpdateService::State::Preparing)
                update_->installationDeferred(tr("客户端未能准备退出并安装更新。"));
            return;
        }
        if (!initialized_ || exitAccepted_ || checkingExit_ || restartPreparing_ || restartAccepted_
            || pluginExitGuardPending_ || pluginExitGuardReady_) {
            update_->installationDeferred(tr("客户端正在处理另一项退出操作，请稍后重试。"));
            return;
        }
        requestExit(restartAfterInstall ? TaskbarTrayMenu::UpdateExitCode : TaskbarTrayMenu::UpdateOnQuitExitCode);
    });
    loadUpdatePreference();
    connect(courseBar.get(), &CourseBar::scheduleStatusChanged,
            this, &AppComposer::refreshScheduleMenu);
    QApplication::setQuitOnLastWindowClosed(false);

    initialized_ = true;
    plugins_->start();
    reminders_->start();
    {
        QString cloudError;
        if (!cloud_->start(&cloudError))
            Logger::instance().log(Logger::Level::Warning,
                                   QStringLiteral("Could not start cloud sync: %1").arg(cloudError));
    }
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
    settingsReclaim_.stop();
    profileEditorReclaim_.stop();
    // Stop the cloud service before any manager persists state on exit.
    if (cloud_) {
        cloud_->stop();
        cloud_.reset();
    }
    if (reminders_) reminders_->stop();
    reminders_.reset();
    swapWindow_.reset();
    rescheduleWindow_.reset();
    if (agent_) agent_->stop();
    agentWindow_.reset();
    pluginCenter_.reset();
    if (pluginUi_) pluginUi_->closeDialogs();
    if (plugins_) plugins_->stopAll();
    settingsWindow_.reset();
    update_.reset();
    agent_.reset();
    windowInterception_.reset();
    profileEditorWindow_.reset();
    trayMenu_.reset();
    trayIcon_.reset();
    courseBar.reset();
    componentRegistry_.reset();
    pluginUi_.reset();
    plugins_.reset();
    desktopCountdown_.reset();
    QString error;
    if (!ConfigManager::instance().save(&error))
        Logger::instance().log(Logger::Level::Error, QStringLiteral("Could not save configuration: %1").arg(error));
    if (!ProfileManager::instance().save(&error))
        Logger::instance().log(Logger::Level::Error, QStringLiteral("Could not save profile: %1").arg(error));
    Logger::instance().log(Logger::Level::Info, "AppComposer dropped, Application will be stopped");
    Logger::instance().flush();
}

bool AppComposer::startApp()
{
    if (!initInstances()) return false;
    courseBar->setVisible(ConfigManager::instance().config().courseBarConfig.enable);
    return initialized_;
}

void AppComposer::showTrayMenu()
{
    if (!initialized_ || exitAccepted_ || restartPreparing_ || restartAccepted_) return;
    if (!trayMenu_) {
        trayMenu_ = std::make_unique<TaskbarTrayMenu>();
        connect(trayMenu_.get(), &TaskbarTrayMenu::pluginsRequested, this, &AppComposer::showPluginCenter);
        connect(trayMenu_.get(), &TaskbarTrayMenu::settingsRequested, this, &AppComposer::showSettingsWindow);
        connect(trayMenu_.get(), &TaskbarTrayMenu::agentRequested, this, &AppComposer::showAgentWindow);
        connect(trayMenu_.get(), &TaskbarTrayMenu::profileEditorRequested, this, &AppComposer::showProfileEditorWindow);
        connect(trayMenu_.get(), &TaskbarTrayMenu::weekScheduleRequested, this, &AppComposer::selectWeekSchedule);
        connect(trayMenu_.get(), &TaskbarTrayMenu::swapWindowRequested, this, &AppComposer::showSwapWindow);
        connect(trayMenu_.get(), &TaskbarTrayMenu::rescheduleWindowRequested, this, &AppComposer::showRescheduleWindow);
        connect(trayMenu_.get(), &TaskbarTrayMenu::restartRequested, this, [this] {
            requestExit(TaskbarTrayMenu::RestartExitCode);
        });
        connect(trayMenu_.get(), &TaskbarTrayMenu::quitRequested, this, [this] {
            if (update_->readyToInstallOnQuit()) update_->installOnQuit();
            else requestExit(0);
        });
        trayMenu_->setUpdateOnQuit(update_->readyToInstallOnQuit());
    }
    refreshScheduleMenu();
    if (!pendingScheduleError_.isEmpty())
        trayMenu_->showScheduleError(std::exchange(pendingScheduleError_, {}));
    trayMenu_->showMenu(trayIcon_->geometry());
}

void AppComposer::showScheduleError(const QString& message)
{
    if (trayMenu_) trayMenu_->showScheduleError(message);
    else pendingScheduleError_ = message;
}

void AppComposer::requestExit(int exitCode)
{
    if (!initialized_ || exitAccepted_ || checkingExit_ || pluginExitGuardPending_) return;
    {
        const QScopedValueRollback<bool> checking(checkingExit_, true);
        if (!prepareToExit(exitCode)) return;
    }
    exitAccepted_ = true;
    for (QWidget* window : {static_cast<QWidget*>(settingsWindow_.get()),
                           static_cast<QWidget*>(profileEditorWindow_.get()),
                           static_cast<QWidget*>(swapWindow_.get()),
                           static_cast<QWidget*>(rescheduleWindow_.get())}) {
        if (window) window->setEnabled(false);
    }
    if (trayMenu_) trayMenu_->setEnabled(false);
    Logger::instance().log(Logger::Level::Info, QStringLiteral("Exit requested: code=%1").arg(exitCode));
    Logger::instance().flush();
    QTimer::singleShot(0, this, [exitCode] { QCoreApplication::exit(exitCode); });
}

void AppComposer::showPluginCenter()
{
    if(!initialized_) return;
    if(!pluginCenter_) {pluginCenter_=std::make_unique<PluginCenterWindow>(plugins_.get(),pluginUi_.get());centerOnPanelScreen(pluginCenter_.get());}
    presentWindow(pluginCenter_.get());
}

void AppComposer::showAgentWindow()
{
    if (!initialized_ || restartPreparing_ || restartAccepted_) return;
    if (!agentWindow_) {
        agentWindow_ = std::make_unique<AgentWindow>(agentService());
        connect(agentWindow_.get(), &AgentWindow::settingsRequested, this, &AppComposer::showAgentSettings);
        centerOnPanelScreen(agentWindow_.get());
        presentWindow(agentWindow_.get());
        return;
    }
    presentWindow(agentWindow_.get());
}

AgentService* AppComposer::agentService()
{
    if (!agent_) agent_ = std::make_unique<AgentService>(dataDirectory_, plugins_.get());
    return agent_.get();
}

void AppComposer::showAgentSettings(const QString &draftSession)
{
    if (!initialized_ || exitAccepted_ || restartPreparing_ || restartAccepted_) return;
    settingsReclaim_.stop();
    if (!settingsWindow_) createSettingsWindow();
    settingsWindow_->openAgentPage(draftSession);
    presentWindow(settingsWindow_.get());
}

void AppComposer::showSettingsWindow()
{
    if (!initialized_ || exitAccepted_) return;
    settingsReclaim_.stop();
    if (!settingsWindow_) {
        createSettingsWindow();
        settingsWindow_->restorePage(settingsWindowState_.page);
    } else if (settingsWindow_->isVisible()) {
        settingsWindow_->refreshProfiles();
    }
    presentWindow(settingsWindow_.get());
}

void AppComposer::createSettingsWindow()
{
    settingsWindow_ = std::make_unique<SettingsWindow>(
        windowInterception_.get(), componentRegistry_.get(), cloud_.get(),
        [this] { return agentService(); }, update_.get());
    connect(settingsWindow_.get(), &SettingsWindow::profileEditorRequested, this, &AppComposer::showProfileEditorWindow);
    connect(settingsWindow_.get(), &SettingsWindow::profileSwitchRequested, this, &AppComposer::switchProfile);
    connect(settingsWindow_.get(), &SettingsWindow::desktopCountdownConfigChanged,
            desktopCountdown_.get(), &DesktopCountdown::reloadConfig);
    connect(settingsWindow_.get(), &SettingsWindow::reminderPreviewRequested,
            courseBar.get(), qOverload<const QString&>(&CourseBar::showNotification));
    connect(settingsWindow_.get(), &SettingsWindow::uiAccessChangeRequested, this, &AppComposer::changeUiAccess);
    connect(settingsWindow_.get(), &SettingsWindow::closed, this, [this] {
        settingsReclaimWaiting_ = settingsWindow_->hasPendingOperations() || pluginExitGuardPending_ ||
                                  (pluginUi_ && pluginUi_->guarding());
        settingsReclaim_.start(settingsReclaimWaiting_ ? ReclaimOperationCheckMs : WindowReclaimDelayMs);
    });
    if (settingsWindowState_.geometry.isEmpty() || !settingsWindow_->restoreGeometry(settingsWindowState_.geometry))
        centerOnPanelScreen(settingsWindow_.get());
}

void AppComposer::reclaimSettingsWindow()
{
    if (!initialized_ || exitAccepted_ || !settingsWindow_ || settingsWindow_->isVisible()) return;
    if (settingsWindow_->hasPendingOperations() || pluginExitGuardPending_ || pluginExitGuardReady_ ||
        (pluginUi_ && pluginUi_->guarding())) {
        settingsReclaimWaiting_ = true;
        settingsReclaim_.start(ReclaimOperationCheckMs);
        return;
    }
    if (std::exchange(settingsReclaimWaiting_, false)) {
        settingsReclaim_.start(WindowReclaimDelayMs);
        return;
    }
    if (settingsWindow_->hasUnsavedInput()) return;
    settingsWindowState_ = {settingsWindow_->saveGeometry(), settingsWindow_->currentPage()};
    settingsWindow_.reset();
}

void AppComposer::showProfileEditorWindow()
{
    if (!initialized_ || exitAccepted_) return;
    profileEditorReclaim_.stop();
    if (!profileEditorWindow_) {
        profileEditorWindow_ = std::make_unique<ProfileEditWindow>();
        connect(profileEditorWindow_.get(), &ProfileEditWindow::profileSaved, this, [this] {
            courseBar->reloadProfile();
            refreshScheduleMenu();
            if (swapWindow_) swapWindow_->refreshProfile(ProfileManager::instance().profile());
            if (rescheduleWindow_) rescheduleWindow_->refreshProfile(ProfileManager::instance().profile());
            if (settingsWindow_) settingsWindow_->refreshProfiles();
        });
        connect(profileEditorWindow_.get(), &ProfileEditWindow::closed, this, [this] {
            profileEditorReclaim_.start(WindowReclaimDelayMs);
        });
        if (profileEditorWindowState_.geometry.isEmpty() ||
            !profileEditorWindow_->restoreGeometry(profileEditorWindowState_.geometry))
            centerOnPanelScreen(profileEditorWindow_.get());
        profileEditorWindow_->restorePage(profileEditorWindowState_.page);
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
                [this](const QDate& firstDate, const QString& firstId, int first,
                       const QDate& secondDate, const QString& secondId, int second) {
            if (!initialized_) return;
            QString error;
            if (!ProfileManager::instance().swapDateClasses(firstDate, firstId, first, secondDate, secondId, second, &error)) {
                swapWindow_->showError(tr("换课保存失败：%1").arg(error)); return;
            }
            syncDateAdjustments();
        });
        connect(swapWindow_.get(), &SwapClassesWindow::substituteRequested, this,
                [this](const QDate& date, const QString& weekId, int classIndex, const QString& subject) {
            if (!initialized_) return;
            QString error;
            if (!ProfileManager::instance().substituteDateClass(date, weekId, classIndex, subject, &error)) {
                swapWindow_->showError(tr("代课保存失败：%1").arg(error)); return;
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
            swapWindow_->setMinimumSize(swapWindow_->minimumSize().boundedTo(available));
            swapWindow_->resize(swapWindow_->size().boundedTo(available));
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
            Logger::instance().log(Logger::Level::Warning,
                                   QStringLiteral("Could not clean up date overrides. Will retry: %1").arg(error));
            Logger::instance().flush(); lastCleanupError_ = error;
        }
        return;
    }
    lastCleanupError_.clear();
    if (previousCount != manager.profile().dateOverrides.size() || lastCleanupDate_ != today) syncDateAdjustments();
    lastCleanupDate_ = today;
    if (!canceled.isEmpty()) {
        QStringList dates;
        for (const auto& date : canceled) dates.append(date.toString(Qt::ISODate));
        showScheduleError(tr("来源日课程已变化，已取消调休、换课和代课：%1").arg(dates.join(QStringLiteral("、"))));
    }
}

void AppComposer::switchProfile(const QString& id)
{
    if (!initialized_ || exitAccepted_ || switchingProfile_ || pluginExitGuardPending_ || pluginExitGuardReady_ || restartPreparing_ || restartAccepted_ || id.isEmpty()) return;
    if (id.compare(QFileInfo(ProfileManager::instance().filePath()).completeBaseName(), Qt::CaseInsensitive) == 0) return;
    QScopedValueRollback<bool> switching(switchingProfile_, true);
    QScopedValueRollback<QString> selection(pendingProfileId_, id);
    // The shared exit guard resolves unsaved changes before committing selection.
    // A rejected exit never publishes the requested profile in memory or on disk.
    requestExit(TaskbarTrayMenu::RestartExitCode);
}

void AppComposer::setRestartHandler(std::function<bool(const Config&, const QString&, QString*)> prepare,
                                   std::function<void()> cancel)
{
    prepareRestart_ = std::move(prepare);
    cancelRestart_ = std::move(cancel);
}

void AppComposer::changeUiAccess(bool enabled)
{
    if (!initialized_ || exitAccepted_) return;
    if (checkingExit_ || pluginExitGuardPending_ || pluginExitGuardReady_ ||
        restartPreparing_ || restartAccepted_ || !UiAccess::supported() || !prepareRestart_) {
        if (settingsWindow_) settingsWindow_->setUiAccessSwitching(false);
        return;
    }
    QScopedValueRollback<std::optional<bool>> request(pendingUiAccess_, enabled);
    requestExit(TaskbarTrayMenu::RestartExitCode);
    if (!pluginExitGuardPending_ && !pluginExitGuardReady_ && !restartAccepted_ && settingsWindow_) settingsWindow_->setUiAccessSwitching(false);
}

void AppComposer::setUpdateHandler(std::function<bool(const UpdateInstaller&, bool, QString*)> prepare)
{
    prepareUpdate_ = std::move(prepare);
}

bool AppComposer::prepareToExit(int exitCode)
{
    if (restartPreparing_ || restartAccepted_) return false;
    const bool updating = TaskbarTrayMenu::isUpdateExitCode(exitCode);
    const auto deferUpdate = [this, exitCode](const QString& message) {
        update_->installationDeferred(message);
        if (exitCode == TaskbarTrayMenu::UpdateOnQuitExitCode) {
            if (trayMenu_)
                ElaMessageBar::warning(ElaMessageBarType::TopRight, tr("更新暂缓"), message, 5000, trayMenu_.get());
            else if (trayIcon_)
                trayIcon_->showMessage(tr("更新暂缓"), message, QSystemTrayIcon::Warning);
        }
    };
    if (plugins_ && plugins_->publishing()) {
        if (updating) {
            deferUpdate(tr("插件正在发布，请在发布完成后重试。"));
            return false;
        }
        if (!silentManagedRestart_)
            QMessageBox::information(agentWindow_.get(), tr("正在发布插件"), tr("请等待插件发布完成后再退出或重启。"));
        return false;
    }
    if (pluginUi_ && !pluginExitGuardReady_ && !silentManagedRestart_) {
        pluginExitGuardPending_ = true;
        const auto profileRequest = pendingProfileId_;
        const auto uiAccessRequest = pendingUiAccess_;
        pluginUi_->prepareAll([this, exitCode, profileRequest, uiAccessRequest](bool allowed) {
            if (!allowed || !initialized_ || shutdown_) {
                pluginExitGuardPending_ = false;
                if (TaskbarTrayMenu::isUpdateExitCode(exitCode) && update_)
                    update_->installationDeferred(tr("已取消退出。安装包已准备好，可以重试。"));
                if (uiAccessRequest && settingsWindow_) settingsWindow_->setUiAccessSwitching(false);
                return;
            }
            if (plugins_) plugins_->setWritesBlocked(true);
            QTimer::singleShot(0, this, [this, exitCode, profileRequest, uiAccessRequest] {
                if (!initialized_ || shutdown_ || exitAccepted_) return;
                // Keep other exit requests blocked until the original request is restored.
                pluginExitGuardPending_ = false;
                pluginExitGuardReady_ = true;
                QScopedValueRollback<QString> selection(pendingProfileId_, profileRequest);
                QScopedValueRollback<std::optional<bool>> request(pendingUiAccess_, uiAccessRequest);
                requestExit(exitCode);
                if (uiAccessRequest && !restartAccepted_ && settingsWindow_) settingsWindow_->setUiAccessSwitching(false);
            });
        });
        return false;
    }
    pluginExitGuardReady_ = false;
    QScopedValueRollback<bool> preparing(restartPreparing_, true);
    bool exitAccepted = false;
    if (plugins_) plugins_->setWritesBlocked(true);
    auto resumePlugins = qScopeGuard([this, &exitAccepted, updating] {
        if (exitAccepted) return;
        if (plugins_) plugins_->setWritesBlocked(false);
        if (updating && update_)
            update_->installationDeferred(tr("已取消退出。安装包已准备好，可以重试。"));
    });
    if (profileEditorWindow_ && profileEditorWindow_->session()->isDirty()) {
        if (silentManagedRestart_) return false;
        showProfileEditorWindow();
        if (!profileEditorWindow_->prepareToClose()) return false;
    }
    if (updating) {
        const bool restartAfterInstall = exitCode == TaskbarTrayMenu::UpdateExitCode;
        QString error;
        if (!ProfileManager::instance().save(&error) || !ConfigManager::instance().save(&error)) {
            deferUpdate(tr("无法保存当前数据：%1").arg(error));
            return false;
        }
        if (update_->managedUpdate() && !cloud_->recordUpdateHandoff(update_->installer().path, &error)) {
            deferUpdate(error);
            return false;
        }
        if (!prepareUpdate_ || !prepareUpdate_(update_->installer(), restartAfterInstall, &error)) {
            deferUpdate(error.isEmpty() ? tr("无法准备安装。") : error);
            return false;
        }
        update_->transferInstaller();
        exitAccepted = true;
        return true;
    }
    const bool restarting = exitCode == TaskbarTrayMenu::RestartExitCode;
    if (pendingProfileId_.isEmpty() && !pendingUiAccess_ && (!restarting || !prepareRestart_)) { exitAccepted = true; return true; }
    QString error;
    auto report = [this, &error](const QString& logMessage, const QString& message) {
        Logger::instance().log(Logger::Level::Error, QStringLiteral("%1: %2").arg(logMessage, error));
        if (silentManagedRestart_) return;
        if (pendingUiAccess_) QMessageBox::warning(settingsWindow_.get(), tr("增强置顶切换失败"), message);
        else if (settingsWindow_) settingsWindow_->showProfileError(message);
        else showScheduleError(message);
    };
    Config candidate = ConfigManager::instance().config();
    if (!pendingProfileId_.isEmpty()) {
        Profile target;
        if (!ProfileManager::instance().readProfile(pendingProfileId_, target, &error)) {
            report(QStringLiteral("Could not read the target profile for switching"),
                   tr("切换失败，目标档案无法读取：%1").arg(error));
            return false;
        }
        candidate.profileName = pendingProfileId_;
    }
    if (pendingUiAccess_) {
        if (settingsWindow_ && settingsWindow_->courseBarDefaultsResetPending())
            candidate.courseBarConfig = CourseBarConfig{};
        candidate.courseBarConfig.uiAccessEnabled = *pendingUiAccess_;
    }
    if (restarting && prepareRestart_) {
        if (!ProfileManager::instance().save(&error)) {
            report(QStringLiteral("Could not save the profile before restart"),
                   tr("重启失败，档案保存失败：%1").arg(error));
            return false;
        }
        if (!prepareRestart_(candidate, dataDirectory_, &error)) {
            if (cancelRestart_) cancelRestart_();
            report(QStringLiteral("Could not prepare the application restart"), error);
            return false;
        }
    }
    if (!ConfigManager::instance().commit(candidate, &error)) {
        if (cancelRestart_) cancelRestart_();
        report(QStringLiteral("Could not save configuration for switching"),
               tr("切换失败，配置保存失败：%1").arg(error));
        return false;
    }
    restartAccepted_ = restarting && bool(prepareRestart_);
    exitAccepted = true;
    return true;
}

QString AppComposer::managedExitBlockReason() const
{
    if (!initialized_ || exitAccepted_ || checkingExit_ || restartPreparing_ || pluginExitGuardPending_ || pluginExitGuardReady_)
        return tr("客户端正在处理另一项操作");
    if ((profileEditorWindow_ && profileEditorWindow_->session()->isDirty()) ||
        (plugins_ && plugins_->publishing()))
        return tr("有未保存的档案编辑或正在发布插件");
    if (pluginUi_) {
        if (pluginUi_->guarding()) return tr("插件正在处理未保存的编辑");
        for (const auto& value : pluginUi_->snapshots())
            if (value.toObject().value(QStringLiteral("dirty")).toBool())
                return tr("插件有未保存的编辑");
    }
    return {};
}

QString AppComposer::managedRestart(const QString& profileId)
{
    const auto reason = managedExitBlockReason();
    if (!reason.isEmpty()) return reason;
    const QScopedValueRollback<bool> silent(silentManagedRestart_, true);
    const QScopedValueRollback<QString> target(pendingProfileId_, profileId);
    requestExit(TaskbarTrayMenu::RestartExitCode);
    return exitAccepted_ ? QString() : tr("客户端未能准备重启，详见设备日志");
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
    if (!screen && trayIcon_) {
        const QRect anchor = trayIcon_->geometry();
        screen = QGuiApplication::screenAt(anchor.isValid() ? anchor.center() : QCursor::pos());
    }
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
