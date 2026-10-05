#ifndef CLASSTOPLAND_NEXT_SETTINGSWINDOW_H
#define CLASSTOPLAND_NEXT_SETTINGSWINDOW_H
#include"ElaWindow.h"
#include"ElaApplication.h"
#include"tabs/ProfileSettingsTab.h"

class SettingsWindow : public ElaWindow
{
    Q_OBJECT
public:
    SettingsWindow(QWidget *parent = nullptr);
    ~SettingsWindow();
    void refreshProfiles();
    void showProfileError(const QString& message);
signals:
    void profileEditorRequested();
    void profileSwitchRequested(const QString& id);
    void courseBarConfigChanged();
private:
    void initUI();
    ProfileSettingsTab* profileTab_;
};


#endif
