#ifndef CLASSTOPLAND_NEXT_APPCOMPOSER_H
#define CLASSTOPLAND_NEXT_APPCOMPOSER_H
#include<QObject>
#include"../ProfileManager/ProfileManager.h"
#include"../Logger/Logger.h"
#include"../ConfigManager/ConfigManager.h"
#include"../../CourseBar/CourseBar.h"
#include<QDir>
#include<QMessageBox>
#include <memory>

class TaskbarTrayMenu;
class SettingsWindow;
class ProfileEditWindow;

#define DATA_PATH (QDir::homePath()+"/ClassTopLand-Next_Data")
#define CONFIG_FILE_PATH (DATA_PATH+"/MainConfig.json")
#define PROFILES_PATH (DATA_PATH+"/profiles")
#define GET_PROFILE_PATH(profileName) (PROFILES_PATH+"/"+profileName+".json")
class AppComposer : public QObject
{
    Q_OBJECT
public:
    AppComposer(QObject *parent = nullptr);
    explicit AppComposer(const QString& dataDirectory, QObject* parent = nullptr);
    ~AppComposer();
    bool startApp();
    // Deterministic teardown: destroys the windows, saves config and profile, and
    // drains pending deferred deletions. Called explicitly by main right after the
    // event loop returns, so cleanup no longer depends on scope-exit timing.
    // Safe to call more than once.
    void shutdown();

private slots:
    // Create each window on demand and always reuse that one instance.
    void showSettingsWindow();
    void showProfileEditorWindow();
    void refreshScheduleMenu();
    void selectWeekSchedule(const QString& id);
    void switchProfile(const QString& id);

private:
    bool initInstances();
    void dropInstances();
    void printLogo();
    // Places a window on the screen holding the panel, the first time it opens.
    void centerOnPanelScreen(QWidget* window);
    // Hides the panel and brings an already created window to the front.
    void presentWindow(QWidget* window);
    bool prepareToExit();
    QString dataDirectory_;
    QString pendingProfileId_;
    bool switchingProfile_ = false;
    bool initialized_ = false;
    bool shutdown_ = false;
    std::unique_ptr<CourseBar> courseBar;
    std::unique_ptr<TaskbarTrayMenu> trayMenu_;
    std::unique_ptr<SettingsWindow> settingsWindow_;
    std::unique_ptr<ProfileEditWindow> profileEditorWindow_;
    // Only the first opening is positioned; later openings keep the user's placement.
    bool settingsWindowPositioned_ = false;
    bool profileEditorWindowPositioned_ = false;
};


#endif //CLASSTOPLAND_NEXT_APPCOMPOSER_H
