#ifndef CLASSTOPLAND_NEXT_SETTINGSWINDOW_H
#define CLASSTOPLAND_NEXT_SETTINGSWINDOW_H
#include"ElaWindow.h"
#include"ElaApplication.h"
#include"tabs/ProfileSettingsTab.h"

class PluginManager;
class PluginUiHost;
class CourseBarComponentRegistry;
class AboutDialog;
class CourseBarSettingsTab;
class WindowInterceptionService;

class SettingsWindow : public ElaWindow
{
    Q_OBJECT
public:
    SettingsWindow(QWidget *parent = nullptr, WindowInterceptionService* interception = nullptr, PluginManager* plugins = nullptr, PluginUiHost* pluginUi = nullptr, CourseBarComponentRegistry* registry = nullptr);
    ~SettingsWindow();
    void refreshProfiles();
    void showProfileError(const QString& message);
    void setUiAccessSwitching(bool switching);
    bool courseBarDefaultsResetPending() const;
signals:
    void profileEditorRequested();
    void profileSwitchRequested(const QString& id);
    void courseBarConfigChanged();
    void desktopCountdownConfigChanged();
    void reminderPreviewRequested(const QString& text);
    void uiAccessChangeRequested(bool enabled);
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    PluginManager* plugins_ = nullptr;
    PluginUiHost* pluginUi_ = nullptr;
    CourseBarComponentRegistry* registry_ = nullptr;
    void initUI();
    WindowInterceptionService* interception_ = nullptr;
    void showAboutDialog();
    QString aboutNodeKey_;
    AboutDialog* aboutDialog_ = nullptr;
    ProfileSettingsTab* profileTab_;
    CourseBarSettingsTab* courseBarTab_ = nullptr;
};


#endif
