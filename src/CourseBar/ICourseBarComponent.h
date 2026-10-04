#ifndef CLASSTOPLAND_NEXT_ICOURSEBARCOMPONENT_H
#define CLASSTOPLAND_NEXT_ICOURSEBARCOMPONENT_H
#include<QWidget>
#include<QString>
class ICourseBarComponent : public QWidget
{
public:
    explicit ICourseBarComponent(QWidget* parent = nullptr) : QWidget(parent) {}
    virtual QString getName() = 0;
};
#endif //CLASSTOPLAND_NEXT_ICOURSEBARCOMPONENT_H