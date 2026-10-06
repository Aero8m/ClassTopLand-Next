#include "ProfileExchange.h"
#include "../DateSchedule/DateSchedule.h"
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QHash>
#include <QRegularExpression>
#include <QStringDecoder>
#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace {
constexpr int kFormatVersion = 1;
const QStringList kTableDays = {QStringLiteral("Mon"), QStringLiteral("Tue"), QStringLiteral("Wed"),
    QStringLiteral("Thu"), QStringLiteral("Fri"), QStringLiteral("Sat"), QStringLiteral("Sun")};
const QStringList kTableDayNames = {QStringLiteral("星期一"), QStringLiteral("星期二"), QStringLiteral("星期三"),
    QStringLiteral("星期四"), QStringLiteral("星期五"), QStringLiteral("星期六"), QStringLiteral("星期日")};

bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}

bool stringField(const QJsonObject& object, const QString& key, QString& result,
                 QString* error, const QString& path)
{
    const QJsonValue value = object.value(key);
    if (!value.isString()) return fail(error, path + QStringLiteral(".") + key + QStringLiteral(" must be a string"));
    result = value.toString();
    return true;
}

bool arrayField(const QJsonObject& object, const QString& key, QJsonArray& result,
                QString* error, const QString& path)
{
    const QJsonValue value = object.value(key);
    if (!value.isArray()) return fail(error, path + QStringLiteral(".") + key + QStringLiteral(" must be an array"));
    result = value.toArray();
    return true;
}

bool objectAt(const QJsonArray& array, qsizetype index, QJsonObject& result,
              QString* error, const QString& path)
{
    if (!array.at(index).isObject())
        return fail(error, path + QStringLiteral("[%1] must be an object").arg(index));
    result = array.at(index).toObject();
    return true;
}

QString timeText(const QTime& time)
{
    return time.toString(QStringLiteral("HH:mm:ss"));
}

bool readTimeRange(const QJsonObject& object, QTime& start, QTime& end,
                   QString* error, const QString& path)
{
    QString startText, endText;
    if (!stringField(object, QStringLiteral("startTime"), startText, error, path) ||
        !stringField(object, QStringLiteral("endTime"), endText, error, path)) return false;
    start = QTime::fromString(startText, QStringLiteral("HH:mm:ss"));
    end = QTime::fromString(endText, QStringLiteral("HH:mm:ss"));
    if (!start.isValid() || !end.isValid() || start >= end)
        return fail(error, path + QStringLiteral(" has an invalid time range"));
    return true;
}

QString modeText(WeekScheduleMode mode)
{
    switch (mode) {
    case WeekScheduleMode::All: return QStringLiteral("all");
    case WeekScheduleMode::Odd: return QStringLiteral("odd");
    case WeekScheduleMode::Even: return QStringLiteral("even");
    }
    return {};
}

// Repair legacy/malformed identities only while loading. A duplicated ID must
// never silently select the first of several schedules.
void repairScheduleIds(Profile& profile)
{
    QHash<QString, int> counts;
    for (const auto& week : profile.schedules) ++counts[week.id];
    QSet<QString> used;
    for (const auto& week : profile.schedules)
        if (!week.id.trimmed().isEmpty()) used.insert(week.id);
    if (counts.value(profile.activeWeekScheduleId) != 1 || profile.activeWeekScheduleId.trimmed().isEmpty())
        profile.activeWeekScheduleId.clear();
    for (auto& week : profile.schedules) {
        if (!week.id.trimmed().isEmpty() && counts.value(week.id) == 1) continue;
        do { week.id = QUuid::createUuid().toString(QUuid::WithoutBraces); } while (used.contains(week.id));
        used.insert(week.id);
    }
}

bool validate(const Profile& profile, QString* error)
{
    for (const Subject& subject : profile.subjects) {
        if (subject.name.trimmed().isEmpty())
            return fail(error, QStringLiteral("Subject name cannot be empty"));
    }
    for (const TimeLine& line : profile.timeLines) {
        for (const TimeLinePoint& point : line.timePoints) {
            if (!point.startTime.isValid() || !point.endTime.isValid() || point.startTime >= point.endTime)
                return fail(error, QStringLiteral("TimeLine '%1' has an invalid time range").arg(line.name));
        }
    }
    QSet<QString> ids;
    for (const WeekSchedule& week : profile.schedules) {
        if (week.id.trimmed().isEmpty() || ids.contains(week.id))
            return fail(error, QStringLiteral("WeekSchedule ID is empty or duplicated"));
        ids.insert(week.id);
        if (modeText(week.mode).isEmpty())
            return fail(error, QStringLiteral("WeekSchedule '%1' has an invalid mode").arg(week.name));
        QSet<int> days;
        for (const DaySchedule& day : week.daySchedules) {
            if (day.enableDay < 1 || day.enableDay > 7 || days.contains(day.enableDay))
                return fail(error, QStringLiteral("WeekSchedule '%1' has an invalid or duplicate day").arg(week.name));
            days.insert(day.enableDay);
            for (const Class& course : day.classes) {
                if (course.subject.trimmed().isEmpty() || !course.startTime.isValid() ||
                    !course.endTime.isValid() || course.startTime >= course.endTime)
                    return fail(error, QStringLiteral("WeekSchedule '%1', day %2 has an invalid class")
                                      .arg(week.name).arg(day.enableDay));
            }
        }
    }
    if (!profile.activeWeekScheduleId.isEmpty() && !ids.contains(profile.activeWeekScheduleId))
        return fail(error, QStringLiteral("Selected week schedule does not exist"));
    if (!DateSchedule::validateOverrides(profile.dateOverrides, error)) return false;
    return true;
}

QJsonObject timeRange(const QTime& start, const QTime& end)
{
    return {{QStringLiteral("startTime"), timeText(start)},
            {QStringLiteral("endTime"), timeText(end)}};
}

QJsonArray classesJson(const QList<Class>& classes)
{
    QJsonArray array;
    for (const auto& course : classes) {
        auto item = timeRange(course.startTime, course.endTime);
        item.insert(QStringLiteral("subject"), course.subject);
        array.append(item);
    }
    return array;
}

QJsonObject toJson(const Profile& profile)
{
    QJsonObject data;
    data.insert(QStringLiteral("name"), profile.name);
    data.insert(QStringLiteral("activeWeekScheduleId"), profile.activeWeekScheduleId);

    QJsonArray subjects;
    for (const Subject& subject : profile.subjects) {
        subjects.append(QJsonObject{{QStringLiteral("name"), subject.name},
                                    {QStringLiteral("simplifiedName"), subject.simplifiedName},
                                    {QStringLiteral("teacher"), subject.teacher}});
    }
    data.insert(QStringLiteral("subjects"), subjects);

    QJsonArray timeLines;
    for (const TimeLine& line : profile.timeLines) {
        QJsonArray points;
        for (const TimeLinePoint& point : line.timePoints)
            points.append(timeRange(point.startTime, point.endTime));
        timeLines.append(QJsonObject{{QStringLiteral("name"), line.name},
                                     {QStringLiteral("timePoints"), points}});
    }
    data.insert(QStringLiteral("timeLines"), timeLines);

    QJsonArray schedules;
    for (const WeekSchedule& week : profile.schedules) {
        QJsonArray days;
        for (const DaySchedule& day : week.daySchedules) {
            QJsonArray classes;
            for (const Class& course : day.classes) {
                QJsonObject item = timeRange(course.startTime, course.endTime);
                item.insert(QStringLiteral("subject"), course.subject);
                classes.append(item);
            }
            days.append(QJsonObject{{QStringLiteral("name"), day.name},
                                    {QStringLiteral("enableDay"), day.enableDay},
                                    {QStringLiteral("classes"), classes}});
        }
        schedules.append(QJsonObject{{QStringLiteral("id"), week.id},
                                     {QStringLiteral("name"), week.name},
                                     {QStringLiteral("mode"), modeText(week.mode)},
                                     {QStringLiteral("daySchedules"), days}});
    }
    data.insert(QStringLiteral("schedules"), schedules);
    QJsonArray overrides;
    for (const auto& record : profile.dateOverrides)
        overrides.append(QJsonObject{{QStringLiteral("date"), record.date.toString(Qt::ISODate)},
            {QStringLiteral("sourceWeekScheduleId"), record.sourceWeekScheduleId},
            {QStringLiteral("sourceWeekday"), record.sourceWeekday},
            {QStringLiteral("sourceClasses"), classesJson(record.sourceClasses)},
            {QStringLiteral("classes"), classesJson(record.classes)}});
    data.insert(QStringLiteral("dateOverrides"), overrides);
    return {{QStringLiteral("version"), kFormatVersion},
            {QStringLiteral("profile"), data}};
}

bool fromJson(const QJsonObject& root, Profile& profile, QString* error)
{
    if (root.value(QStringLiteral("version")).toInt(-1) != kFormatVersion)
        return fail(error, QStringLiteral("Unsupported profile format version"));
    const QJsonValue profileValue = root.value(QStringLiteral("profile"));
    if (!profileValue.isObject()) return fail(error, QStringLiteral("profile must be an object"));
    const QJsonObject data = profileValue.toObject();
    if (!stringField(data, QStringLiteral("name"), profile.name, error, QStringLiteral("profile"))) return false;

    const auto selection = data.value(QStringLiteral("activeWeekScheduleId"));
    if (!selection.isUndefined() && !selection.isString())
        return fail(error, QStringLiteral("profile.activeWeekScheduleId must be a string"));
    profile.activeWeekScheduleId = selection.toString();

    QJsonArray subjects;
    if (!arrayField(data, QStringLiteral("subjects"), subjects, error, QStringLiteral("profile"))) return false;
    for (qsizetype i = 0; i < subjects.size(); ++i) {
        QJsonObject item;
        const QString path = QStringLiteral("profile.subjects[%1]").arg(i);
        if (!objectAt(subjects, i, item, error, QStringLiteral("profile.subjects"))) return false;
        QString name, simplifiedName, teacher;
        if (!stringField(item, QStringLiteral("name"), name, error, path) ||
            !stringField(item, QStringLiteral("simplifiedName"), simplifiedName, error, path) ||
            !stringField(item, QStringLiteral("teacher"), teacher, error, path)) return false;
        profile.subjects.append(Subject(name, simplifiedName, teacher));
    }

    QJsonArray timeLines;
    if (!arrayField(data, QStringLiteral("timeLines"), timeLines, error, QStringLiteral("profile"))) return false;
    for (qsizetype i = 0; i < timeLines.size(); ++i) {
        QJsonObject item;
        const QString path = QStringLiteral("profile.timeLines[%1]").arg(i);
        if (!objectAt(timeLines, i, item, error, QStringLiteral("profile.timeLines"))) return false;
        QString name;
        QJsonArray points;
        if (!stringField(item, QStringLiteral("name"), name, error, path) ||
            !arrayField(item, QStringLiteral("timePoints"), points, error, path)) return false;
        TimeLine line(name);
        for (qsizetype j = 0; j < points.size(); ++j) {
            QJsonObject point;
            const QString pointPath = path + QStringLiteral(".timePoints[%1]").arg(j);
            if (!objectAt(points, j, point, error, path + QStringLiteral(".timePoints"))) return false;
            QTime start, end;
            if (!readTimeRange(point, start, end, error, pointPath)) return false;
            line.timePoints.append(TimeLinePoint(start, end));
        }
        profile.timeLines.append(line);
    }

    QJsonArray schedules;
    if (!arrayField(data, QStringLiteral("schedules"), schedules, error, QStringLiteral("profile"))) return false;
    for (qsizetype i = 0; i < schedules.size(); ++i) {
        QJsonObject item;
        const QString path = QStringLiteral("profile.schedules[%1]").arg(i);
        if (!objectAt(schedules, i, item, error, QStringLiteral("profile.schedules"))) return false;
        QString name, mode;
        QJsonArray days;
        if (!stringField(item, QStringLiteral("name"), name, error, path) ||
            !stringField(item, QStringLiteral("mode"), mode, error, path) ||
            !arrayField(item, QStringLiteral("daySchedules"), days, error, path)) return false;
        WeekScheduleMode parsedMode;
        if (mode == QStringLiteral("all")) parsedMode = WeekScheduleMode::All;
        else if (mode == QStringLiteral("odd")) parsedMode = WeekScheduleMode::Odd;
        else if (mode == QStringLiteral("even")) parsedMode = WeekScheduleMode::Even;
        else return fail(error, path + QStringLiteral(".mode is invalid"));
        WeekSchedule week(name, parsedMode);
        const auto id = item.value(QStringLiteral("id"));
        if (!id.isUndefined() && !id.isString())
            return fail(error, path + QStringLiteral(".id must be a string"));
        if (id.isString()) week.id = id.toString();
        for (qsizetype j = 0; j < days.size(); ++j) {
            QJsonObject dayObject;
            const QString dayPath = path + QStringLiteral(".daySchedules[%1]").arg(j);
            if (!objectAt(days, j, dayObject, error, path + QStringLiteral(".daySchedules"))) return false;
            QString dayName;
            QJsonArray classes;
            if (!stringField(dayObject, QStringLiteral("name"), dayName, error, dayPath) ||
                !arrayField(dayObject, QStringLiteral("classes"), classes, error, dayPath)) return false;
            const int enableDay = dayObject.value(QStringLiteral("enableDay")).toInt(-1);
            if (enableDay < 1 || enableDay > 7)
                return fail(error, dayPath + QStringLiteral(".enableDay must be 1..7"));
            DaySchedule day(dayName, enableDay);
            for (qsizetype k = 0; k < classes.size(); ++k) {
                QJsonObject classObject;
                const QString classPath = dayPath + QStringLiteral(".classes[%1]").arg(k);
                if (!objectAt(classes, k, classObject, error, dayPath + QStringLiteral(".classes"))) return false;
                QString subject;
                QTime start, end;
                if (!stringField(classObject, QStringLiteral("subject"), subject, error, classPath) ||
                    !readTimeRange(classObject, start, end, error, classPath)) return false;
                day.classes.append(Class(subject, start, end));
            }
            week.daySchedules.append(day);
        }
        profile.schedules.append(week);
    }
    repairScheduleIds(profile);
    const auto overrideValue = data.value(QStringLiteral("dateOverrides"));
    if (!overrideValue.isUndefined()) {
        if (!overrideValue.isArray()) return fail(error, QStringLiteral("profile.dateOverrides must be an array"));
        const auto records = overrideValue.toArray();
        for (qsizetype i = 0; i < records.size(); ++i) {
            QJsonObject item;
            const QString path = QStringLiteral("profile.dateOverrides[%1]").arg(i);
            if (!objectAt(records, i, item, error, QStringLiteral("profile.dateOverrides"))) return false;
            DateScheduleOverride record;
            QString dateText;
            if (!stringField(item, QStringLiteral("date"), dateText, error, path) ||
                !stringField(item, QStringLiteral("sourceWeekScheduleId"), record.sourceWeekScheduleId, error, path)) return false;
            record.date = QDate::fromString(dateText, Qt::ISODate);
            if (!record.date.isValid() || record.date.toString(Qt::ISODate) != dateText)
                return fail(error, path + QStringLiteral(".date must be an ISO date"));
            record.sourceWeekday = item.value(QStringLiteral("sourceWeekday")).toInt(-1);
            auto readClasses = [&](const QString& key, QList<Class>& result) {
                QJsonArray array;
                if (!arrayField(item, key, array, error, path)) return false;
                for (qsizetype j = 0; j < array.size(); ++j) {
                    QJsonObject course;
                    const auto coursePath = path + QLatin1Char('.') + key + QStringLiteral("[%1]").arg(j);
                    QString subject;
                    QTime start, end;
                    if (!objectAt(array, j, course, error, path + QLatin1Char('.') + key) ||
                        !stringField(course, QStringLiteral("subject"), subject, error, coursePath) ||
                        !readTimeRange(course, start, end, error, coursePath)) return false;
                    result.append(Class(subject, start, end));
                }
                return true;
            };
            if (!readClasses(QStringLiteral("sourceClasses"), record.sourceClasses) ||
                !readClasses(QStringLiteral("classes"), record.classes)) return false;
            profile.dateOverrides.append(record);
        }
    }
    return validate(profile, error);
}

bool fromTables(const QJsonObject& root, Profile& profile, QStringList& warnings, QString* error)
{
    const auto appendix = root.value(QStringLiteral("appendixTables"));
    if (!appendix.isUndefined()) {
        if (!appendix.isObject()) return fail(error, QStringLiteral("appendixTables: 必须是对象"));
        const auto tables = appendix.toObject();
        for (auto it = tables.begin(); it != tables.end(); ++it) {
            const QString path = QStringLiteral("appendixTables[%1]").arg(it.key());
            if (!it.value().isArray()) return fail(error, path + QStringLiteral(": 必须是课程数组"));
            if (!it.value().toArray().isEmpty())
                return fail(error, path + QStringLiteral(": 暂不支持导入非空附加课表，请先移除或转换为常规周课表"));
        }
        if (!tables.isEmpty()) warnings.append(QStringLiteral("已忽略空附加课表：%1。").arg(tables.keys().join(QStringLiteral("、"))));
    }
    profile.name = QStringLiteral("导入课表");
    WeekSchedule week(QStringLiteral("常规课表"), WeekScheduleMode::All);
    QSet<QString> subjects;
    bool foundDay = false;
    static const QRegularExpression minuteTime(QStringLiteral("^(?:[01][0-9]|2[0-3]):[0-5][0-9]$"));
    for (int d = 0; d < kTableDays.size(); ++d) {
        const QString key = kTableDays[d];
        DaySchedule day(kTableDayNames[d], d + 1);
        if (root.contains(key)) {
            foundDay = true;
            QJsonArray courses;
            if (!arrayField(root, key, courses, error, QStringLiteral("tables"))) return false;
            for (qsizetype i = 0; i < courses.size(); ++i) {
                const QString path = key + QStringLiteral("[%1]").arg(i);
                QJsonObject course;
                QString name, startText, endText;
                if (!objectAt(courses, i, course, error, key) ||
                    !stringField(course, QStringLiteral("name"), name, error, path) ||
                    !stringField(course, QStringLiteral("start"), startText, error, path) ||
                    !stringField(course, QStringLiteral("end"), endText, error, path)) return false;
                name = name.trimmed();
                if (name.isEmpty()) return fail(error, path + QStringLiteral(".name: 科目名称不能为空"));
                if (!minuteTime.match(startText).hasMatch()) return fail(error, path + QStringLiteral(".start: 必须为 HH:mm"));
                if (!minuteTime.match(endText).hasMatch()) return fail(error, path + QStringLiteral(".end: 必须为 HH:mm"));
                const QTime start = QTime::fromString(startText, QStringLiteral("HH:mm"));
                const QTime end = QTime::fromString(endText, QStringLiteral("HH:mm"));
                if (!start.isValid() || !end.isValid() || start >= end)
                    return fail(error, path + QStringLiteral(": 开始时间必须早于结束时间，且不能跨日"));
                day.classes.append(Class(name, start, end));
                if (!subjects.contains(name)) {
                    subjects.insert(name);
                    profile.subjects.append(Subject(name));
                }
            }
        }
        QList<int> order;
        for (int i = 0; i < day.classes.size(); ++i) order.append(i);
        std::stable_sort(order.begin(), order.end(), [&day](int a, int b) {
            return day.classes[a].startTime < day.classes[b].startTime;
        });
        for (int i = 1; i < order.size(); ++i)
            if (day.classes[order[i]].startTime < day.classes[order[i - 1]].endTime)
                return fail(error, key + QStringLiteral("[%1]: 与其他课程时段重叠").arg(order[i]));
        std::stable_sort(day.classes.begin(), day.classes.end(), [](const Class& a, const Class& b) {
            return a.startTime < b.startTime;
        });
        week.daySchedules.append(day);
    }
    if (!foundDay) return fail(error, QStringLiteral("tables: 至少需要一个星期键（Mon～Sun）"));
    profile.schedules.append(week);
    return ProfileExchange::validateTransfer(profile, error);
}

bool toTables(const Profile& profile, const QString& weekId, QByteArray& contents,
              QStringList* warnings, QString* error)
{
    if (profile.schedules.isEmpty()) return fail(error, QStringLiteral("没有可导出的周课表"));
    if (weekId.isEmpty() && profile.schedules.size() != 1)
        return fail(error, QStringLiteral("档案包含多个周课表，请明确选择要导出的周课表"));
    const WeekSchedule* selected = nullptr;
    for (const auto& week : profile.schedules)
        if (weekId.isEmpty() || week.id == weekId) { selected = &week; break; }
    if (!selected) return fail(error, QStringLiteral("选择的周课表 ID 不存在：%1").arg(weekId));
    if (!ProfileExchange::validateTransfer(profile, error)) return false;
    QStringList messages;
    messages.append(QStringLiteral("仅导出周课表“%1”的课程名称和时间；不保留其他周课表、档案名称、周课表名称和 ID、日课表名称、科目简称与教师、未使用科目、时间线及当前课表选择。完整备份请使用原生 JSON。").arg(selected->name));
    if (selected->mode != WeekScheduleMode::All)
        messages.append(QStringLiteral("所选课表的单双周规则无法保存，导出后按每周课表使用。"));
    if (!profile.dateOverrides.isEmpty())
        messages.append(QStringLiteral("指定日期的调休和换课安排不会导出；完整备份请使用原生 JSON。"));
    QJsonObject root;
    for (const auto& key : kTableDays) root.insert(key, QJsonArray{});
    root.insert(QStringLiteral("appendixTables"), QJsonObject{});
    for (const auto& day : selected->daySchedules) {
        QList<Class> courses = day.classes;
        std::stable_sort(courses.begin(), courses.end(), [](const Class& a, const Class& b) { return a.startTime < b.startTime; });
        QJsonArray array;
        const QString key = kTableDays[day.enableDay - 1];
        for (int i = 0; i < courses.size(); ++i) {
            const auto& course = courses[i];
            const QTime start(course.startTime.hour(), course.startTime.minute());
            const QTime end(course.endTime.hour(), course.endTime.minute());
            const QString path = key + QStringLiteral("[%1]（%2）").arg(i).arg(course.subject);
            if (start >= end) return fail(error, path + QStringLiteral(": 截去秒后开始与结束落在同一分钟，无法导出"));
            if (course.startTime.second() || course.endTime.second() || course.startTime.msec() || course.endTime.msec())
                messages.append(path + QStringLiteral(": 时间精度损失，截去秒：%1–%2 → %3–%4")
                    .arg(timeText(course.startTime), timeText(course.endTime), start.toString(QStringLiteral("HH:mm")), end.toString(QStringLiteral("HH:mm"))));
            array.append(QJsonObject{{QStringLiteral("name"), course.subject},
                {QStringLiteral("start"), start.toString(QStringLiteral("HH:mm"))},
                {QStringLiteral("end"), end.toString(QStringLiteral("HH:mm"))}});
        }
        root.insert(key, array);
    }
    contents = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (warnings) *warnings = messages;
    return true;
}

} // namespace

namespace {
void invalid(const QString& path, const QString& message)
{
    throw std::runtime_error((path + QStringLiteral(": ") + message).toUtf8().constData());
}
void yamlMap(const YAML::Node& node, const QString& path)
{
    if (!node.IsMap()) invalid(path, QStringLiteral("必须是对象"));
    QSet<QString> keys;
    for (const auto& entry : node) {
        if (!entry.first.IsScalar()) invalid(path, QStringLiteral("字段名必须是字符串"));
        const QString key = QString::fromStdString(entry.first.Scalar());
        if (keys.contains(key)) invalid(path + QLatin1Char('.') + key, QStringLiteral("字段重复"));
        keys.insert(key);
    }
}
void yamlSequence(const YAML::Node& node, const QString& path)
{
    if (!node.IsSequence()) invalid(path, QStringLiteral("必须是数组"));
}
QString yamlString(const YAML::Node& map, const char* key, const QString& path, bool optional = false)
{
    const auto node = map[key];
    const QString field = path + QLatin1Char('.') + QString::fromLatin1(key);
    if (!node.IsDefined() && optional) return {};
    if (!node.IsScalar()) invalid(field, QStringLiteral("必须是字符串"));
    const QString text = QString::fromStdString(node.Scalar());
    // Quoted scalars have tag "!"; plain scalars need YAML's basic type resolution.
    static const QRegularExpression nonString(QStringLiteral(
        "^(?:[-+]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][-+]?[0-9]+)?|0[xX][0-9a-fA-F]+|true|false|~|null|\\.inf|\\.nan)$"),
        QRegularExpression::CaseInsensitiveOption);
    if ((node.Tag() == "?" && nonString.match(text).hasMatch()) ||
        (node.Tag() != "?" && node.Tag() != "!" && node.Tag() != "tag:yaml.org,2002:str"))
        invalid(field, QStringLiteral("必须是字符串（数字名称请加引号）"));
    return text;
}
int yamlInteger(const YAML::Node& map, const char* key, const QString& path)
{
    const auto node = map[key];
    const QString field = path + QLatin1Char('.') + QString::fromLatin1(key);
    static const QRegularExpression integer(QStringLiteral("^[+-]?[0-9]+$"));
    if (!node.IsScalar() || (node.Tag() != "?" && node.Tag() != "tag:yaml.org,2002:int") ||
        !integer.match(QString::fromStdString(node.Scalar())).hasMatch())
        invalid(field, QStringLiteral("必须是整数"));
    bool ok = false;
    const int result = QString::fromStdString(node.Scalar()).toInt(&ok);
    if (!ok) invalid(field, QStringLiteral("整数超出范围"));
    return result;
}
QTime yamlTime(const YAML::Node& map, const char* key, const QString& path)
{
    const QString value = yamlString(map, key, path);
    static const QRegularExpression time(QStringLiteral("^(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]$"));
    if (!time.match(value).hasMatch()) invalid(path + QLatin1Char('.') + QString::fromLatin1(key), QStringLiteral("必须为 HH:MM:SS"));
    return QTime::fromString(value, QStringLiteral("HH:mm:ss"));
}
void exchangeWarnings(Profile& profile, QStringList& warnings, bool addMissing)
{
    QSet<QString> names;
    for (const auto& subject : profile.subjects) names.insert(subject.name);
    bool parity = false, conflict = false;
    for (int i = 0; i < profile.schedules.size(); ++i) {
        const auto& week = profile.schedules[i];
        parity |= week.mode != WeekScheduleMode::All;
        for (const auto& day : week.daySchedules) {
            for (int j = 0; j < i; ++j) {
                const auto& other = profile.schedules[j];
                if (week.mode != other.mode && week.mode != WeekScheduleMode::All && other.mode != WeekScheduleMode::All) continue;
                for (const auto& otherDay : other.daySchedules) conflict |= otherDay.enableDay == day.enableDay;
            }
            for (const auto& course : day.classes) {
                if (addMissing && !names.contains(course.subject)) {
                    profile.subjects.append(Subject(course.subject));
                    names.insert(course.subject);
                    warnings.append(QStringLiteral("已补充未定义科目：%1").arg(course.subject));
                }
            }
        }
    }
    if (conflict) warnings.append(QStringLiteral("已保留全部重复生效安排，课程栏自动匹配可能冲突。"));
    if (parity) warnings.append(QStringLiteral("单双周自动运行需要第一周日期配置，当前尚未提供。"));
}
bool fromCses(const QByteArray& bytes, Profile& profile, QString* error)
{
    try {
        const auto documents = YAML::LoadAll(std::string(bytes.constData(), size_t(bytes.size())));
        if (documents.size() != 1) invalid(QStringLiteral("root"), QStringLiteral("只支持一个 YAML 文档"));
        const auto root = documents[0];
        yamlMap(root, QStringLiteral("root"));
        if (yamlInteger(root, "version", QStringLiteral("root")) != 1)
            invalid(QStringLiteral("version"), QStringLiteral("仅支持版本 1"));
        const auto subjects = root["subjects"];
        yamlSequence(subjects, QStringLiteral("subjects"));
        for (size_t i = 0; i < subjects.size(); ++i) {
            const QString path = QStringLiteral("subjects[%1]").arg(i);
            const auto subject = subjects[i];
            yamlMap(subject, path);
            profile.subjects.append(Subject(yamlString(subject, "name", path),
                yamlString(subject, "simplified_name", path, true), yamlString(subject, "teacher", path, true)));
        }
        const auto schedules = root["schedules"];
        yamlSequence(schedules, QStringLiteral("schedules"));
        for (size_t i = 0; i < schedules.size(); ++i) {
            const QString path = QStringLiteral("schedules[%1]").arg(i);
            const auto schedule = schedules[i];
            yamlMap(schedule, path);
            const QString name = yamlString(schedule, "name", path);
            const int day = yamlInteger(schedule, "enable_day", path);
            if (day < 1 || day > 7) invalid(path + QStringLiteral(".enable_day"), QStringLiteral("必须为 1–7"));
            const QString mode = yamlString(schedule, "weeks", path);
            WeekScheduleMode parsed;
            QString groupName;
            if (mode == QStringLiteral("all")) { parsed = WeekScheduleMode::All; groupName = QStringLiteral("全部周"); }
            else if (mode == QStringLiteral("odd")) { parsed = WeekScheduleMode::Odd; groupName = QStringLiteral("单周"); }
            else if (mode == QStringLiteral("even")) { parsed = WeekScheduleMode::Even; groupName = QStringLiteral("双周"); }
            else invalid(path + QStringLiteral(".weeks"), QStringLiteral("必须为 all、odd 或 even"));
            const auto classes = schedule["classes"];
            yamlSequence(classes, path + QStringLiteral(".classes"));
            DaySchedule daily(name, day);
            for (size_t j = 0; j < classes.size(); ++j) {
                const QString classPath = path + QStringLiteral(".classes[%1]").arg(j);
                const auto course = classes[j];
                yamlMap(course, classPath);
                daily.classes.append(Class(yamlString(course, "subject", classPath),
                    yamlTime(course, "start_time", classPath), yamlTime(course, "end_time", classPath)));
            }
            int target = -1, count = 0;
            for (int j = 0; j < profile.schedules.size(); ++j) {
                const auto& week = profile.schedules[j];
                if (week.mode != parsed) continue;
                ++count;
                const bool hasDay = std::any_of(week.daySchedules.cbegin(), week.daySchedules.cend(),
                    [day](const auto& item) { return item.enableDay == day; });
                if (target < 0 && !hasDay) target = j;
            }
            if (target < 0) {
                profile.schedules.append(WeekSchedule(count ? groupName + QStringLiteral("（%1）").arg(count + 1) : groupName, parsed));
                target = profile.schedules.size() - 1;
            }
            profile.schedules[target].daySchedules.append(daily);
        }
        return true;
    } catch (const YAML::Exception& exception) {
        return fail(error, QStringLiteral("YAML 第 %1 行，第 %2 列：%3")
            .arg(exception.mark.line + 1).arg(exception.mark.column + 1).arg(QString::fromUtf8(exception.what())));
    } catch (const std::exception& exception) {
        return fail(error, QString::fromUtf8(exception.what()));
    }
}
}

bool ProfileExchange::validateTransfer(const Profile& profile, QString* error)
{
    if (error) error->clear();
    if (profile.name.trimmed().isEmpty()) return fail(error, QStringLiteral("profile.name: 档案名称不能为空"));
    QSet<QString> names;
    for (int i = 0; i < profile.subjects.size(); ++i) {
        const QString name = profile.subjects[i].name.trimmed();
        if (name.isEmpty() || names.contains(name))
            return fail(error, QStringLiteral("subjects[%1].name: 科目名称为空或重复").arg(i));
        names.insert(name);
    }
    auto ranges = [error](const auto& points, const QString& path) {
        QList<int> order;
        for (int i = 0; i < points.size(); ++i) {
            if (!points[i].startTime.isValid() || !points[i].endTime.isValid() || points[i].startTime >= points[i].endTime)
                return fail(error, path + QStringLiteral("[%1]: 开始时间必须早于结束时间，且不能跨日").arg(i));
            order.append(i);
        }
        std::stable_sort(order.begin(), order.end(), [&points](int a, int b) { return points[a].startTime < points[b].startTime; });
        for (int i = 1; i < order.size(); ++i)
            if (points[order[i]].startTime < points[order[i - 1]].endTime)
                return fail(error, path + QStringLiteral("[%1]: 时段重叠").arg(order[i]));
        return true;
    };
    for (int i = 0; i < profile.timeLines.size(); ++i) {
        const auto& line = profile.timeLines[i];
        const QString path = QStringLiteral("timeLines[%1]").arg(i);
        if (line.name.trimmed().isEmpty()) return fail(error, path + QStringLiteral(".name: 名称不能为空"));
        if (!ranges(line.timePoints, path + QStringLiteral(".timePoints"))) return false;
    }
    for (int i = 0; i < profile.schedules.size(); ++i) {
        const auto& week = profile.schedules[i];
        const QString path = QStringLiteral("schedules[%1]").arg(i);
        if (week.name.trimmed().isEmpty()) return fail(error, path + QStringLiteral(".name: 名称不能为空"));
        for (int j = 0; j < week.daySchedules.size(); ++j) {
            const auto& day = week.daySchedules[j];
            const QString daily = path + QStringLiteral(".daySchedules[%1]").arg(j);
            if (day.name.trimmed().isEmpty()) return fail(error, daily + QStringLiteral(".name: 名称不能为空"));
            for (int k = 0; k < day.classes.size(); ++k)
                if (day.classes[k].subject.trimmed().isEmpty()) return fail(error, daily + QStringLiteral(".classes[%1].subject: 科目不能为空").arg(k));
            if (!ranges(day.classes, daily + QStringLiteral(".classes"))) return false;
        }
    }
    return validate(profile, error);
}

bool ProfileExchange::decode(const QByteArray& contents, Format format, Profile& result, QStringList* warnings, QString* error)
{
    if (error) error->clear();
    if (warnings) warnings->clear();
    Profile loaded;
    QStringList messages;
    if (format == Format::NativeJson || format == Format::ClassTopLandTablesJson) {
        if (format == Format::ClassTopLandTablesJson) {
            QStringDecoder decoder(QStringDecoder::Utf8);
            const QString decoded = decoder(contents);
            Q_UNUSED(decoded);
            if (decoder.hasError()) return fail(error, QStringLiteral("ClassTopLand 课表必须为 UTF-8 编码"));
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(contents, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return fail(error, QStringLiteral("Invalid profile JSON: %1").arg(parseError.errorString()));
        if (format == Format::NativeJson) {
            if (!fromJson(document.object(), loaded, error)) return false;
        } else if (!fromTables(document.object(), loaded, messages, error)) return false;
    } else if (format == Format::CsesYaml) {
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString decoded = decoder(contents);
        if (decoder.hasError()) return fail(error, QStringLiteral("CSES 文件必须为 UTF-8 编码"));
        loaded.name = QStringLiteral("导入课表");
        if (!fromCses(decoded.toUtf8(), loaded, error) || !validateTransfer(loaded, error)) return false;
    } else return fail(error, QStringLiteral("不支持的交换格式"));
    if (format != Format::ClassTopLandTablesJson) exchangeWarnings(loaded, messages, format == Format::CsesYaml);
    if (format == Format::CsesYaml && !validateTransfer(loaded, error)) return false;
    if (warnings) *warnings = messages;
    result = std::move(loaded);
    return true;
}

bool ProfileExchange::readFile(const QString& path, Format format, Profile& result, QStringList* warnings, QString* error)
{
    if (error) error->clear();
    if (warnings) warnings->clear();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return fail(error, file.errorString());
    Profile loaded;
    if (!decode(bytes, format, loaded, warnings, error)) return false;
    if (format != Format::NativeJson) loaded.name = QFileInfo(path).completeBaseName();
    result = std::move(loaded);
    return true;
}

bool ProfileExchange::readImportFile(const QString& path, Profile& result, Format& detectedFormat,
                                     QStringList* warnings, QString* error)
{
    if (error) error->clear();
    if (warnings) warnings->clear();
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != QStringLiteral("json") && suffix != QStringLiteral("yaml") && suffix != QStringLiteral("yml"))
        return fail(error, QStringLiteral("请选择 JSON 或 YAML 文件"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) return fail(error, file.errorString());
    Format format = Format::CsesYaml;
    if (suffix == QStringLiteral("json")) {
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return fail(error, QStringLiteral("JSON 根节点必须是对象：%1").arg(parseError.errorString()));
        const auto root = document.object();
        // Native markers win even when malformed: never silently reinterpret a broken archive.
        if (root.contains(QStringLiteral("profile")) || root.contains(QStringLiteral("version"))) format = Format::NativeJson;
        else if (root.contains(QStringLiteral("appendixTables")) ||
                 std::any_of(kTableDays.cbegin(), kTableDays.cend(), [&root](const QString& key) { return root.contains(key); }))
            format = Format::ClassTopLandTablesJson;
        else return fail(error, QStringLiteral("无法识别 JSON：需要原生档案结构或 ClassTopLand 星期课表键"));
    }
    Profile loaded;
    QStringList messages;
    if (!decode(bytes, format, loaded, &messages, error)) return false;
    if (format != Format::NativeJson) loaded.name = QFileInfo(path).completeBaseName();
    if (!validateTransfer(loaded, error)) return false;
    result = std::move(loaded);
    detectedFormat = format;
    if (warnings) *warnings = messages;
    return true;
}

bool ProfileExchange::serialize(const Profile& profile, Format format, QByteArray& contents, QStringList* warnings, QString* error,
                                const QString& weekScheduleId)
{
    if (error) error->clear();
    if (warnings) warnings->clear();
    if (format == Format::ClassTopLandTablesJson) return toTables(profile, weekScheduleId, contents, warnings, error);
    if (format != Format::NativeJson && format != Format::CsesYaml)
        return fail(error, QStringLiteral("不支持的交换格式"));
    // Native persistence retains its existing, more permissive legacy validation.
    if (!validate(profile, error)) return false;
    if (format == Format::NativeJson) {
        contents = QJsonDocument(toJson(profile)).toJson(QJsonDocument::Indented);
        return true;
    }
    if (!validateTransfer(profile, error)) return false;
    Profile candidate = profile;
    QStringList messages;
    if (!profile.dateOverrides.isEmpty())
        messages.append(QStringLiteral("指定日期的调休和换课安排不会导出；完整备份请使用原生 JSON。"));
    exchangeWarnings(candidate, messages, true);
    if (!validateTransfer(candidate, error)) return false;
    YAML::Emitter out;
    out << YAML::BeginMap << YAML::Key << "version" << YAML::Value << 1;
    out << YAML::Key << "subjects" << YAML::Value << YAML::BeginSeq;
    for (const auto& subject : candidate.subjects) {
        out << YAML::BeginMap << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted << subject.name.toStdString();
        out << YAML::Key << "simplified_name" << YAML::Value << YAML::DoubleQuoted << subject.simplifiedName.toStdString();
        out << YAML::Key << "teacher" << YAML::Value << YAML::DoubleQuoted << subject.teacher.toStdString() << YAML::EndMap;
    }
    out << YAML::EndSeq << YAML::Key << "schedules" << YAML::Value << YAML::BeginSeq;
    for (const auto& week : candidate.schedules) for (const auto& day : week.daySchedules) {
        out << YAML::BeginMap << YAML::Key << "name" << YAML::Value << YAML::DoubleQuoted << day.name.toStdString();
        out << YAML::Key << "enable_day" << YAML::Value << day.enableDay;
        out << YAML::Key << "weeks" << YAML::Value << modeText(week.mode).toStdString();
        out << YAML::Key << "classes" << YAML::Value << YAML::BeginSeq;
        for (const auto& course : day.classes) {
            out << YAML::BeginMap << YAML::Key << "subject" << YAML::Value << YAML::DoubleQuoted << course.subject.toStdString();
            out << YAML::Key << "start_time" << YAML::Value << YAML::DoubleQuoted << timeText(course.startTime).toStdString();
            out << YAML::Key << "end_time" << YAML::Value << YAML::DoubleQuoted << timeText(course.endTime).toStdString() << YAML::EndMap;
        }
        out << YAML::EndSeq << YAML::EndMap;
    }
    out << YAML::EndSeq << YAML::EndMap;
    if (!out.good()) return fail(error, QString::fromStdString(out.GetLastError()));
    contents = QByteArray(out.c_str()) + '\n';
    if (warnings) *warnings = messages;
    return true;
}

bool ProfileExchange::writeFile(const QString& path, const QByteArray& contents, QString* error)
{
    if (error) error->clear();
    if (path.trimmed().isEmpty()) return fail(error, QStringLiteral("文件路径不能为空"));
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return fail(error, file.errorString());
    if (file.write(contents) != contents.size()) return fail(error, file.errorString());
    if (!file.commit()) return fail(error, file.errorString());
    return true;
}
