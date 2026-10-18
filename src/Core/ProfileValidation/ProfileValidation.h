#pragma once
#include "../Model/Profile.h"
namespace ProfileValidation
{
enum class Page
{
    Info,
    Subjects,
    TimeLines,
    Schedules
};
struct ValidationError
{
    QString message;
    Page page = Page::Info;
    int item = -1;
    int row = -1;
    int weekday = 1;
    explicit operator bool() const
    {
        return !message.isEmpty();
    }
};
ValidationError validate(const Profile &profile);
} // namespace ProfileValidation
