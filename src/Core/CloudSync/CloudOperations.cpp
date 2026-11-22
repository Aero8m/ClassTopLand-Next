#include "CloudOperations.h"

#include "BuildInfo.h"
#include "../../VERSION.h"
#include "../DateSchedule/DateSchedule.h"
#include "../ProfileManager/ProfileExchange.h"
#include "../ProfileManager/ProfileManager.h"
#include "../ProfileValidation/ProfileOperations.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScreen>
#include <QTimeZone>

namespace {
QJsonArray courses(const QList<Class>& entries)
{
    QJsonArray result;
    for (const auto& entry : DateSchedule::sorted(entries))
        result.append(QJsonObject{{QStringLiteral("subject"), entry.subject},
                                  {QStringLiteral("startTime"), entry.startTime.toString(Qt::ISODate)},
                                  {QStringLiteral("endTime"), entry.endTime.toString(Qt::ISODate)}});
    return result;
}

QDate day(const QJsonObject& value)
{
    return QDate::fromString(value.value(QStringLiteral("date")).toString(), Qt::ISODate);
}

QString week(const QJsonObject& value)
{
    return value.value(QStringLiteral("week_id")).toString();
}

int period(const QJsonObject& value)
{
    return value.value(QStringLiteral("period")).toInt(-1);
}

QJsonObject snapshot(const Profile& profile, const QJsonObject& change)
{
    const auto date = day(change);
    const auto table = DateSchedule::resolve(profile, date, week(change));
    const int weekday = table.valid && table.sourceWeekday > 0 ? table.sourceWeekday : date.dayOfWeek();
    const auto* source = DateSchedule::sourceDay(profile, table.sourceWeekScheduleId, weekday);
    return {{QStringLiteral("special"), table.special},
            {QStringLiteral("source_id"), table.sourceWeekScheduleId},
            {QStringLiteral("source_weekday"), weekday},
            {QStringLiteral("source_classes"), source ? courses(source->classes) : QJsonArray{}},
            {QStringLiteral("classes"), courses(table.classes)},
            {QStringLiteral("error"), !table.valid}};
}
} // namespace

QString CloudOperations::buildId()
{
    const QString configured = QString::fromLatin1(APP_VERSION);
    if (!configured.isEmpty() && !configured.contains(QLatin1Char('|'))) return configured;
    const QString commit = QString::fromLatin1(CTX_BUILD_COMMIT);
    return commit.isEmpty() ? QStringLiteral("local") : commit.left(7) + QStringLiteral("-local");
}

QString CloudOperations::activeProfileId()
{
    const auto path = ProfileManager::instance().filePath();
    return path.isEmpty() ? QString() : QString::fromLatin1(
        QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QJsonObject CloudOperations::telemetry(bool cloudProfileActive)
{
    QJsonArray monitors;
    for (const auto* screen : QGuiApplication::screens())
        monitors.append(QJsonObject{{QStringLiteral("name"), screen->name().left(256)},
                                    {QStringLiteral("primary"), screen == QGuiApplication::primaryScreen()}});
    const auto local = QDateTime::currentDateTime();
    QJsonObject result{
        {QStringLiteral("build"), QJsonObject{{QStringLiteral("id"), buildId()},
             {QStringLiteral("commit"), QString::fromLatin1(CTX_BUILD_COMMIT)},
             {QStringLiteral("type"), QString::fromLatin1(CTX_BUILD_TYPE)},
             {QStringLiteral("built_at"), QString::fromLatin1(CTX_BUILD_TIME)}}},
        {QStringLiteral("profile_id"), activeProfileId()},
        {QStringLiteral("timezone"), QString::fromUtf8(QTimeZone::systemTimeZoneId())},
        {QStringLiteral("utc_offset"), local.offsetFromUtc()},
        {QStringLiteral("monitors"), monitors},
        {QStringLiteral("cloud_profile_active"), cloudProfileActive}};
    if (result.value(QStringLiteral("profile_id")).toString().isEmpty()) return result;
    Profile calendar = ProfileManager::instance().profile();
    calendar.timeLines.clear();
    calendar.reminderRules.clear();
    for (auto& subject : calendar.subjects) subject.teacher.clear();
    for (qsizetype index = calendar.dateOverrides.size(); index-- > 0;) {
        const auto& entry = calendar.dateOverrides[index];
        if (entry.date < local.date() || !DateSchedule::sourceMatches(calendar, entry))
            calendar.dateOverrides.removeAt(index);
    }
    QByteArray bytes;
    if (ProfileExchange::serialize(calendar, ProfileExchange::Format::NativeJson, bytes)) {
        const auto document = QJsonDocument::fromJson(bytes);
        if (document.toJson(QJsonDocument::Compact).size() < 1900000)
            result.insert(QStringLiteral("calendar"), document.object());
    }
    return result;
}

bool CloudOperations::matches(const Profile& profile, const QJsonArray& changes, bool after)
{
    if (changes.isEmpty() || changes.size() > 2) return false;
    for (const auto& change : changes) {
        const auto row = change.toObject();
        if (!day(row).isValid() ||
            snapshot(profile, row) != row.value(after ? QStringLiteral("after") : QStringLiteral("before")).toObject())
            return false;
    }
    return true;
}

bool CloudOperations::prepare(Profile& candidate, const QJsonObject& task, QString* error)
{
    const auto operation = task.value(QStringLiteral("operation")).toObject();
    const auto action = operation.value(QStringLiteral("action")).toString();
    const auto first = operation.value(QStringLiteral("first")).toObject();
    const auto second = operation.value(QStringLiteral("second")).toObject();
    const QDate date = day(first);
    if (action == QStringLiteral("reschedule"))
        return ProfileOperations::reschedule(candidate, date, operation.value(QStringLiteral("source_week")).toString(),
                                             operation.value(QStringLiteral("source_weekday")).toInt(), error);
    if (action == QStringLiteral("swap"))
        return ProfileOperations::swap(candidate, date, week(first), period(first),
                                        day(second), week(second), period(second), error);
    if (action == QStringLiteral("substitute"))
        return ProfileOperations::substitute(candidate, date, week(first), period(first),
                                              operation.value(QStringLiteral("subject")).toString(), error);
    if (action == QStringLiteral("restore"))
        return ProfileOperations::restore(candidate, date, error);
    if (action == QStringLiteral("clear")) {
        const auto table = DateSchedule::resolve(candidate, date, week(first));
        if (!table.valid || !ProfileOperations::reschedule(candidate, date, table.sourceWeekScheduleId,
                                                           table.sourceWeekday, error))
            return false;
        candidate.dateOverrides.last().classes.clear();
        return true;
    }
    return false;
}
