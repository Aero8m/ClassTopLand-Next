#ifndef CLASSTOPLAND_NEXT_SETTINGSWINDOW_H
#define CLASSTOPLAND_NEXT_SETTINGSWINDOW_H
#include"ElaWindow.h"
#include <functional>
#include <memory>

class ProfileSettingsTab;
class ReminderSettingsTab;
namespace AppUi { class LazyPages; }
class CourseBarComponentRegistry;
class AboutDialog;
class UpdateService;
class CourseBarSettingsTab;
class CloudSettingsTab;
class CloudSyncService;
class AppearanceSettingsTab;
class DesktopCountdownSettingsTab;
class WindowInterceptionService;
class WindowInterceptionSettingsTab;
class AgentService;
class AgentSettingsTab;

class SettingsWindow : public ElaWindow
{
    Q_OBJECT
public:
    SettingsWindow(WindowInterceptionService* interception, CourseBarComponentRegistry* registry,
                   CloudSyncService* cloud, std::function<AgentService*()> agent, UpdateService* update);
    ~SettingsWindow() override;
    void refreshProfiles();
    void openAgentPage(const QString &draftSession);
    void showProfileError(const QString& message);
    void setUiAccessSwitching(bool switching);
    // Refresh pages whose configuration may have been replaced by the cloud.
    void refreshCloudConfig();
    bool courseBarDefaultsResetPending() const;
    bool hasUnsavedInput() const;
    bool hasPendingOperations() const;
    QString currentPage() const;
    void restorePage(const QString& id);
signals:
    void closed();
    void profileEditorRequested();
    void profileSwitchRequested(const QString& id);
    void courseBarConfigChanged();
    void desktopCountdownConfigChanged();
    void reminderPreviewRequested(const QString& text);
    void uiAccessChangeRequested(bool enabled);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    std::unique_ptr<AppUi::LazyPages> pages_;
    CourseBarComponentRegistry* registry_ = nullptr;
    CloudSyncService* cloud_ = nullptr;
    std::function<AgentService*()> agent_;
    UpdateService* update_ = nullptr;
    void initUI();
    WindowInterceptionService* interception_ = nullptr;
    void showAboutDialog();
    QString aboutNodeKey_;
    AboutDialog* aboutDialog_ = nullptr;
    QString pendingProfileError_;
    ProfileSettingsTab* profileTab_ = nullptr;
    ReminderSettingsTab* reminderTab_ = nullptr;
    CourseBarSettingsTab* courseBarTab_ = nullptr;
    AppearanceSettingsTab* appearanceTab_ = nullptr;
    DesktopCountdownSettingsTab* desktopCountdownTab_ = nullptr;
    CloudSettingsTab* cloudTab_ = nullptr;
    WindowInterceptionSettingsTab* interceptionTab_ = nullptr;
    AgentSettingsTab* agentTab_ = nullptr;
};


#endif
