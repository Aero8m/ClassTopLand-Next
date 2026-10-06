#pragma once

#include "../Model/Profile.h"

namespace DateSchedule {
struct Result {
    QList<Class> classes;
    QString name;
    QString sourceWeekScheduleId;
    int sourceWeekday = 1;
    int weekScheduleIndex = -1;
    bool special = false;
    bool valid = true;
    QString error;
};

QList<Class> sorted(QList<Class> classes);
bool sameClasses(const QList<Class>& left, const QList<Class>& right);
bool validateClasses(const QList<Class>& classes, QString* error = nullptr);
bool validateOverrides(const QList<DateScheduleOverride>& records, QString* error = nullptr);
const DaySchedule* sourceDay(const Profile& profile, const QString& weekId, int weekday);
const DateScheduleOverride* find(const Profile& profile, const QDate& date);
bool sourceMatches(const Profile& profile, const DateScheduleOverride& record);
// Explicit weekday/index are the legacy temporary-selection overrides.
Result resolve(const Profile& profile, const QDate& date, const QString& weekId = {},
               int weekday = 0, int weekIndex = -1, const QDate& weekOneMonday = {});
}
