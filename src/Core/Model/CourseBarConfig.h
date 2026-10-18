#ifndef CLASSTOPLAND_NEXT_COURSEBARCONFIG_H
#define CLASSTOPLAND_NEXT_COURSEBARCONFIG_H
#include<QString>
#include<QList>
#include <QJsonObject>
#include <QUuid>

enum class CourseBarComponentType
{
    Date,          // 日期
    CourseView,    // 课程查看
    TextTip,       // 文本提示
    Countdown,     // 倒计时
    Plugin
};

struct CourseBarComponentConfig
{
    static constexpr int DefaultCourseSpacing = 28;
    CourseBarComponentType type = CourseBarComponentType::Date;
    bool enabled = true;
    QString text = QStringLiteral("文本提示");
    int courseSpacing = DefaultCourseSpacing;
    QString instanceId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString typeId;
    QJsonObject settings;
    QJsonObject extra;
};

struct CourseBarConfig
{
    static constexpr int DefaultSpacing = CourseBarComponentConfig::DefaultCourseSpacing;
    static constexpr int DefaultComponentSpacing = 14;
    static constexpr int MaximumSpacing = 100;
    bool enable = true;
    bool uiAccessEnabled = false;
    int height = 49;
    // Logical pixels at the standard 49px bar height.
    int componentSpacing = DefaultComponentSpacing;
    QList<CourseBarComponentConfig> components{
        {CourseBarComponentType::Date, true},
        {CourseBarComponentType::CourseView, true},
        {CourseBarComponentType::Countdown, true}
    };
};
#endif //CLASSTOPLAND_NEXT_COURSEBARCONFIG_H
