#include "ProfileMutationService.h"
#include "PluginManifest.h"
#include "../ProfileManager/ProfileManager.h"
#include "../ProfileManager/ProfileExchange.h"
#include "../ProfileValidation/ProfileValidation.h"
#include "../ProfileValidation/ProfileOperations.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>

namespace
{
QJsonObject encoded(const Profile &profile)
{
    QByteArray bytes;
    ProfileExchange::serialize(profile, ProfileExchange::Format::NativeJson, bytes);
    return QJsonDocument::fromJson(bytes).object().value(QStringLiteral("profile")).toObject();
}
bool decoded(const QJsonObject &data, Profile &profile, QString *error)
{
    return ProfileExchange::decode(
        QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("profile"), data}})
            .toJson(),
        ProfileExchange::Format::NativeJson, profile, nullptr, error);
}
QJsonValue withoutRefs(const QJsonValue &value)
{
    if (value.isArray())
    {
        QJsonArray result;
        for (const auto &item : value.toArray())
            result.append(withoutRefs(item));
        return result;
    }
    if (value.isObject())
    {
        auto result = value.toObject();
        result.remove(QStringLiteral("ref"));
        for (auto it = result.begin(); it != result.end(); ++it)
            it.value() = withoutRefs(it.value());
        return result;
    }
    return value;
}
} // namespace
ProfileMutationService::ProfileMutationService()
    : session_(QUuid::createUuid().toString(QUuid::WithoutBraces))
{
}
QJsonObject ProfileMutationService::snapshot() const
{
    auto &manager = ProfileManager::instance();
    auto data = encoded(manager.profile());
    const auto prefix = session_ + QLatin1Char('/') + QString::number(manager.revision()) + QLatin1Char('/');
    for (const auto &key : {QStringLiteral("subjects"), QStringLiteral("timeLines")})
    {
        auto list = data.value(key).toArray();
        for (int i = 0; i < list.size(); ++i)
        {
            auto item = list[i].toObject();
            item.insert(QStringLiteral("ref"), prefix + key + QLatin1Char('/') + QString::number(i));
            list[i] = item;
        }
        data.insert(key, list);
    }
    auto weeks = data.value(QStringLiteral("schedules")).toArray();
    for (int w = 0; w < weeks.size(); ++w)
    {
        auto week = weeks[w].toObject();
        auto days = week.value(QStringLiteral("daySchedules")).toArray();
        for (int d = 0; d < days.size(); ++d)
        {
            auto day = days[d].toObject();
            auto classes = day.value(QStringLiteral("classes")).toArray();
            for (int c = 0; c < classes.size(); ++c)
            {
                auto course = classes[c].toObject();
                course.insert(QStringLiteral("ref"),
                              prefix + QStringLiteral("class/%1/%2/%3").arg(w).arg(d).arg(c));
                classes[c] = course;
            }
            day.insert(QStringLiteral("classes"), classes);
            days[d] = day;
        }
        week.insert(QStringLiteral("daySchedules"), days);
        weeks[w] = week;
    }
    data.insert(QStringLiteral("schedules"), weeks);
    return {{QStringLiteral("session"), session_},
            {QStringLiteral("revision"), QString::number(manager.revision())},
            {QStringLiteral("profile"), data}};
}
QJsonObject ProfileMutationService::apply(const QJsonObject &request, QJsonValue &result)
{
    auto &manager = ProfileManager::instance();
    if (request.value(QStringLiteral("session")).toString() != session_ ||
        request.value(QStringLiteral("expectedRevision")).toString() != QString::number(manager.revision()))
        return Plugins::error(QStringLiteral("CONFLICT"), QStringLiteral("档案已变化，请重新读取"));
    const auto operations = request.value(QStringLiteral("operations"));
    if (!operations.isArray() || operations.toArray().isEmpty() || operations.toArray().size() > 64)
        return Plugins::error(QStringLiteral("INVALID_ARGUMENT"),
                              QStringLiteral("operations 必须包含 1～64 个操作"));
    auto data = snapshot().value(QStringLiteral("profile")).toObject();
    QString message;
    auto invalid = [&]
    {
        return Plugins::error(QStringLiteral("INVALID_ARGUMENT"),
                              message.isEmpty() ? QStringLiteral("无效操作或实体引用") : message);
    };
    for (const auto &value : operations.toArray())
    {
        if (!value.isObject())
            return invalid();
        const auto op = value.toObject();
        const auto name = op.value(QStringLiteral("op")).toString();
        auto item = withoutRefs(op.value(QStringLiteral("value"))).toObject();
        if (name.startsWith(QStringLiteral("date.")))
        {
            Profile candidate;
            if (!decoded(data, candidate, &message))
                return invalid();
            bool ok = false;
            if (name == QStringLiteral("date.reschedule"))
                ok = ProfileOperations::reschedule(
                    candidate, QDate::fromString(op.value(QStringLiteral("date")).toString(), Qt::ISODate),
                    op.value(QStringLiteral("weekId")).toString(),
                    op.value(QStringLiteral("weekday")).toInt(), &message);
            if (name == QStringLiteral("date.restore"))
                ok = ProfileOperations::restore(
                    candidate, QDate::fromString(op.value(QStringLiteral("date")).toString(), Qt::ISODate),
                    &message);
            if (name == QStringLiteral("date.swap"))
                ok = ProfileOperations::swap(
                    candidate,
                    QDate::fromString(op.value(QStringLiteral("firstDate")).toString(), Qt::ISODate),
                    op.value(QStringLiteral("firstWeekId")).toString(),
                    op.value(QStringLiteral("first")).toInt(-1),
                    QDate::fromString(op.value(QStringLiteral("secondDate")).toString(), Qt::ISODate),
                    op.value(QStringLiteral("secondWeekId")).toString(),
                    op.value(QStringLiteral("second")).toInt(-1), &message);
            if (!ok)
                return invalid();
            auto updated = encoded(candidate);
            data.insert(QStringLiteral("dateOverrides"), updated.value(QStringLiteral("dateOverrides")));
            continue;
        }
        if (name == QStringLiteral("profile.updateInfo"))
        {
            for (auto it = item.begin(); it != item.end(); ++it)
            {
                if (it.key() != QStringLiteral("name") && it.key() != QStringLiteral("semesterStartDate"))
                    return invalid();
                data.insert(it.key(), it.value());
            }
            continue;
        }
        QString collection;
        if (name.startsWith(QStringLiteral("subject.")))
            collection = QStringLiteral("subjects");
        else if (name.startsWith(QStringLiteral("timeLine.")))
            collection = QStringLiteral("timeLines");
        else if (name.startsWith(QStringLiteral("week.")))
            collection = QStringLiteral("schedules");
        if (!collection.isEmpty())
        {
            auto list = data.value(collection).toArray();
            const bool week = collection == QStringLiteral("schedules");
            const auto identity = week ? QStringLiteral("id") : QStringLiteral("ref");
            const auto ref = op.value(week ? QStringLiteral("weekId") : QStringLiteral("ref")).toString();
            int index = -1;
            for (int i = 0; i < list.size(); ++i)
                if (!ref.isEmpty() && list[i].toObject().value(identity).toString() == ref)
                {
                    index = i;
                    break;
                }
            if (name.endsWith(QStringLiteral(".add")))
            {
                if (!op.value(QStringLiteral("value")).isObject())
                    return invalid();
                if (week)
                    item.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
                item.remove(QStringLiteral("ref"));
                list.append(item);
            }
            else if (index < 0)
                return invalid();
            else if (name.endsWith(QStringLiteral(".remove")))
            {
                if (collection == QStringLiteral("subjects"))
                {
                    const auto old = list[index].toObject().value(QStringLiteral("name")).toString();
                    for (const auto &w : data.value(QStringLiteral("schedules")).toArray())
                        for (const auto &d : w.toObject().value(QStringLiteral("daySchedules")).toArray())
                            for (const auto &c : d.toObject().value(QStringLiteral("classes")).toArray())
                                if (c.toObject().value(QStringLiteral("subject")).toString() == old)
                                {
                                    message = QStringLiteral("科目正在被课程引用");
                                    return invalid();
                                }
                }
                list.removeAt(index);
                if (week && data.value(QStringLiteral("activeWeekScheduleId")).toString() == ref)
                    data.insert(QStringLiteral("activeWeekScheduleId"), QString());
            }
            else if (name.endsWith(QStringLiteral(".update")))
            {
                auto current = list[index].toObject();
                const auto old = current.value(QStringLiteral("name")).toString();
                for (auto it = item.begin(); it != item.end(); ++it)
                {
                    if (it.key() == identity || it.key() == QStringLiteral("ref"))
                        return invalid();
                    current.insert(it.key(), it.value());
                }
                list[index] = current;
                if (collection == QStringLiteral("subjects") &&
                    current.value(QStringLiteral("name")).toString() != old)
                {
                    auto weeks = data.value(QStringLiteral("schedules")).toArray();
                    for (int w = 0; w < weeks.size(); ++w)
                    {
                        auto weekItem = weeks[w].toObject();
                        auto days = weekItem.value(QStringLiteral("daySchedules")).toArray();
                        for (int d = 0; d < days.size(); ++d)
                        {
                            auto day = days[d].toObject();
                            auto classes = day.value(QStringLiteral("classes")).toArray();
                            for (int c = 0; c < classes.size(); ++c)
                            {
                                auto course = classes[c].toObject();
                                if (course.value(QStringLiteral("subject")).toString() == old)
                                    course.insert(QStringLiteral("subject"),
                                                  current.value(QStringLiteral("name")));
                                classes[c] = course;
                            }
                            day.insert(QStringLiteral("classes"), classes);
                            days[d] = day;
                        }
                        weekItem.insert(QStringLiteral("daySchedules"), days);
                        weeks[w] = weekItem;
                    }
                    data.insert(QStringLiteral("schedules"), weeks);
                }
            }
            else
                return invalid();
            data.insert(collection, list);
            continue;
        }
        if (name == QStringLiteral("day.set") || name.startsWith(QStringLiteral("class.")))
        {
            auto weeks = data.value(QStringLiteral("schedules")).toArray();
            bool found = false;
            for (int w = 0; w < weeks.size(); ++w)
            {
                auto week = weeks[w].toObject();
                if (week.value(QStringLiteral("id")).toString() !=
                    op.value(QStringLiteral("weekId")).toString())
                    continue;
                auto days = week.value(QStringLiteral("daySchedules")).toArray();
                int di = -1;
                const int weekday = op.value(QStringLiteral("weekday")).toInt();
                if (weekday < 1 || weekday > 7)
                    return invalid();
                for (int d = 0; d < days.size(); ++d)
                    if (days[d].toObject().value(QStringLiteral("enableDay")).toInt() == weekday)
                        di = d;
                if (name == QStringLiteral("day.set"))
                {
                    item.insert(QStringLiteral("enableDay"), weekday);
                    if (di < 0)
                        days.append(item);
                    else
                        days[di] = item;
                    found = true;
                }
                else if (di >= 0)
                {
                    auto day = days[di].toObject();
                    auto classes = day.value(QStringLiteral("classes")).toArray();
                    int ci = -1;
                    for (int c = 0; c < classes.size(); ++c)
                        if (classes[c].toObject().value(QStringLiteral("ref")).toString() ==
                                op.value(QStringLiteral("ref")).toString() &&
                            !op.value(QStringLiteral("ref")).toString().isEmpty())
                            ci = c;
                    if (name == QStringLiteral("class.add"))
                    {
                        item.remove(QStringLiteral("ref"));
                        classes.append(item);
                        found = true;
                    }
                    else if (ci >= 0 && name == QStringLiteral("class.remove"))
                    {
                        classes.removeAt(ci);
                        found = true;
                    }
                    else if (ci >= 0 && name == QStringLiteral("class.update"))
                    {
                        auto course = classes[ci].toObject();
                        for (auto it = item.begin(); it != item.end(); ++it)
                        {
                            if (it.key() == QStringLiteral("ref"))
                                return invalid();
                            course.insert(it.key(), it.value());
                        }
                        classes[ci] = course;
                        found = true;
                    }
                    day.insert(QStringLiteral("classes"), classes);
                    days[di] = day;
                }
                week.insert(QStringLiteral("daySchedules"), days);
                weeks[w] = week;
                break;
            }
            if (!found)
                return invalid();
            data.insert(QStringLiteral("schedules"), weeks);
            continue;
        }
        return invalid();
    }
    Profile candidate;
    if (!decoded(data, candidate, &message))
        return invalid();
    if (auto validation = ProfileValidation::validate(candidate))
    {
        message = validation.message;
        return invalid();
    }
    QList<QDate> canceled;
    if (!manager.commit(candidate, &message, &canceled))
        return Plugins::error(QStringLiteral("SAVE_FAILED"), message);
    QJsonObject output{{QStringLiteral("session"), session_}, {QStringLiteral("revision"), QString::number(manager.revision())}};
    QJsonArray dates;
    for (const auto &date : canceled)
        dates.append(date.toString(Qt::ISODate));
    output.insert(QStringLiteral("canceledDates"), dates);
    result = output;
    return {};
}
