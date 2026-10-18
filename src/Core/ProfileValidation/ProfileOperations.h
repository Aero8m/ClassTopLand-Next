#pragma once
#include "../Model/Profile.h"
namespace ProfileOperations
{
bool reschedule(Profile &profile, const QDate &date, const QString &weekId, int weekday, QString *error);
bool swap(Profile &profile, const QDate &firstDate, const QString &firstWeekId, int first,
          const QDate &secondDate, const QString &secondWeekId, int second, QString *error);
bool restore(Profile &profile, const QDate &date, QString *error);
} // namespace ProfileOperations
