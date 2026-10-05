#include "ProfileManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QHash>

#include <utility>

namespace {
constexpr int kFormatVersion = 1;

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
    return true;
}

QJsonObject timeRange(const QTime& start, const QTime& end)
{
    return {{QStringLiteral("startTime"), timeText(start)},
            {QStringLiteral("endTime"), timeText(end)}};
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
    return validate(profile, error);
}
} // namespace

ProfileManager& ProfileManager::instance()
{
    static ProfileManager manager;
    return manager;
}

bool ProfileManager::load(const QString& filePath, QString* error)
{
    if (error) error->clear();
    if (filePath.trimmed().isEmpty()) return fail(error, QStringLiteral("Profile file path is empty"));
    const QString path = QFileInfo(filePath).absoluteFilePath();
    if (!QFileInfo::exists(path)) {
        profile_ = Profile{};
        filePath_ = path;
        return true;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return fail(error, file.errorString());
    const QByteArray contents = file.readAll();
    if (file.error() != QFileDevice::NoError) return fail(error, file.errorString());
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(contents, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return fail(error, QStringLiteral("Invalid profile JSON: %1").arg(parseError.errorString()));

    Profile loaded;
    if (!fromJson(document.object(), loaded, error)) return false;
    profile_ = std::move(loaded);
    filePath_ = path;
    return true;
}

bool ProfileManager::save(QString* error) const
{
    return writeProfile(profile_, error);
}

bool ProfileManager::commit(const Profile& candidate, QString* error)
{
    Profile committed = candidate;
    if (!writeProfile(committed, error)) return false;
    profile_ = std::move(committed);
    return true;
}

bool ProfileManager::selectWeekSchedule(const QString& id, QString* error)
{
    if (error) error->clear();
    if (!id.isEmpty()) {
        bool found = false;
        for (const auto& week : profile_.schedules) if (week.id == id) { found = true; break; }
        if (!found) return fail(error, QStringLiteral("Selected week schedule does not exist"));
    }
    if (profile_.activeWeekScheduleId == id) return true;
    Profile candidate = profile_;
    candidate.activeWeekScheduleId = id;
    return commit(candidate, error);
}

bool ProfileManager::writeProfile(const Profile& candidate, QString* error) const
{
    if (error) error->clear();
    if (filePath_.isEmpty()) return fail(error, QStringLiteral("No profile file path has been loaded"));
    if (!validate(candidate, error)) return false;
    if (!QDir().mkpath(QFileInfo(filePath_).absolutePath()))
        return fail(error, QStringLiteral("Cannot create profile directory"));

    QSaveFile file(filePath_);
    if (!file.open(QIODevice::WriteOnly)) return fail(error, file.errorString());
    const QByteArray contents = QJsonDocument(toJson(candidate)).toJson(QJsonDocument::Indented);
    if (file.write(contents) != contents.size()) return fail(error, file.errorString());
    if (!file.commit()) return fail(error, file.errorString());
    return true;
}
