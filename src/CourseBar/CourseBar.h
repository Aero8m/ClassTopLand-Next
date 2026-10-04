#ifndef CLASSTOPLAND_NEXT_COURSEBAR_H
#define CLASSTOPLAND_NEXT_COURSEBAR_H
#include <QHBoxLayout>
#include<QWidget>
#include"../Core/ConfigManager/ConfigManager.h"
#include"../Core/ProfileManager/ProfileManager.h"
#include"../Core/Model/Config.h"
#include"../Core/Model/Profile.h"
class CourseBar : public QWidget
{
public:
    CourseBar(QWidget *parent = nullptr);
    ~CourseBar();
private:
    void initUI();
    CourseBarConfig config;
    Profile profile;
    QHBoxLayout* mainLayout;
};


#endif //CLASSTOPLAND_NEXT_COURSEBAR_H