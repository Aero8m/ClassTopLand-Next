#ifndef CLASSTOPLAND_NEXT_SETTINGSWINDOW_H
#define CLASSTOPLAND_NEXT_SETTINGSWINDOW_H
#include"ElaWindow.h"

class SettingsWindow : public ElaWindow
{
    Q_OBJECT
public:
    SettingsWindow(QWidget *parent = nullptr);
    ~SettingsWindow();
};


#endif