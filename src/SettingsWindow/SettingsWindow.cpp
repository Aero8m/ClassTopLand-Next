#include "SettingsWindow.h"

SettingsWindow::SettingsWindow(QWidget* parent) : ElaWindow(parent)
{
    initUI();
}

SettingsWindow::~SettingsWindow()
{

}

void SettingsWindow::initUI()
{
    setWindowTitle("设置");
    setWindowButtonFlag(ElaAppBarType::StayTopButtonHint,false);
    setWindowButtonFlag(ElaAppBarType::RouteBackButtonHint,false);
    setWindowButtonFlag(ElaAppBarType::RouteForwardButtonHint,false);

}