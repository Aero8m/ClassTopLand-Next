#ifndef CLASSTOPLAND_NEXT_SETTINGSWINDOW_H
#define CLASSTOPLAND_NEXT_SETTINGSWINDOW_H
#include"ElaWindow.h"
#include"ElaApplication.h"
#include"tabs/ProfileSettingsTab.h"

class AboutDialog;
class CourseBarSettingsTab;

class SettingsWindow : public ElaWindow
{
    Q_OBJECT
public:
    SettingsWindow(QWidget *parent = nullptr);
    ~SettingsWindow();
    void refreshProfiles();
    void showProfileError(const QString& message);
    void setUiAccessSwitching(bool switching);
    bool courseBarDefaultsResetPending() const;
signals:
    void profileEditorRequested();
    void profileSwitchRequested(const QString& id);
    void courseBarConfigChanged();
    void uiAccessChangeRequested(bool enabled);
private:
    void initUI();
    void showAboutDialog();
    QString aboutNodeKey_;
    AboutDialog* aboutDialog_ = nullptr;
    ProfileSettingsTab* profileTab_;
    CourseBarSettingsTab* courseBarTab_ = nullptr;
};


#endif
