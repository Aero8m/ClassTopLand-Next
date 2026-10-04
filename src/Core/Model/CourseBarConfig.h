#ifndef CLASSTOPLAND_NEXT_COURSEBARCONFIG_H
#define CLASSTOPLAND_NEXT_COURSEBARCONFIG_H
#include<QString>
#include<QList>

enum class CourseBarComponentType
{
    Date,          // 日期
    CourseView     // 课程查看
};

struct CourseBarComponentConfig
{
    CourseBarComponentType type;
    bool enabled = true;
};

struct CourseBarConfig
{
    int height = 49;
    QList<CourseBarComponentConfig> components{
        {CourseBarComponentType::Date, true},
        {CourseBarComponentType::CourseView, true}
    };
};
#endif //CLASSTOPLAND_NEXT_COURSEBARCONFIG_H
