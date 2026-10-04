#include "CourseBar.h"

CourseBar::CourseBar(QWidget* parent) : QWidget(parent)
{
    // set config variable and profile variable
    config = ConfigManager::instance().config().courseBarConfig;
    profile = ProfileManager::instance().profile();

    initUI();
}


CourseBar::~CourseBar()
{

}

void CourseBar::initUI()
{
    // init window layout
    mainLayout = new QHBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    setLayout(mainLayout);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    adjustSize();
}
