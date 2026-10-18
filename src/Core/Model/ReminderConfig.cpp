#include "ReminderConfig.h"

#include <QSet>

namespace {
bool fail(QString* error, const QString& message)
{
    if (error) *error = message;
    return false;
}
bool integer(const QJsonValue& value, int minimum, int maximum)
{
    return value.isDouble() && value.toDouble() >= minimum && value.toDouble() <= maximum &&
           value.toDouble() == value.toInt();
}
}

QString Reminders::render(QString text, const QString& subject, int period)
{
    return text.replace(QStringLiteral("{科目}"), subject)
               .replace(QStringLiteral("{节次}"), QString::number(period));
}

bool Reminders::validate(const ReminderConfig& config, QString* error)
{
    if (config.advanceMinutes < 1 || config.advanceMinutes > 1439)
        return fail(error, QStringLiteral("课前提醒提前分钟数必须为 1～1439"));
    if (config.startedText.trimmed().isEmpty() || config.endedText.trimmed().isEmpty() || config.soonText.trimmed().isEmpty())
        return fail(error, QStringLiteral("提醒文本不能为空"));
    return true;
}

bool Reminders::validate(const QList<ReminderRule>& rules, QString* error)
{
    QSet<QString> ids;
    for (const auto& rule : rules) {
        if (rule.id.trimmed().isEmpty() || ids.contains(rule.id))
            return fail(error, QStringLiteral("提醒规则 ID 为空或重复"));
        ids.insert(rule.id);
        if (rule.name.trimmed().isEmpty() || rule.text.trimmed().isEmpty())
            return fail(error, QStringLiteral("提醒规则名称和文本不能为空"));
        QSet<int> days;
        for (int day : rule.weekdays) {
            if (day < 1 || day > 7 || days.contains(day))
                return fail(error, QStringLiteral("提醒星期无效或重复"));
            days.insert(day);
        }
        if (days.isEmpty()) return fail(error, QStringLiteral("请至少选择一个星期"));
        if (rule.course != ReminderRule::Course::Any && rule.course != ReminderRule::Course::Subject && rule.course != ReminderRule::Course::Period)
            return fail(error, QStringLiteral("无效的提醒课程条件"));
        if (rule.course == ReminderRule::Course::Subject && rule.subject.trimmed().isEmpty())
            return fail(error, QStringLiteral("请选择科目"));
        if (rule.period < 1 || rule.period > 999)
            return fail(error, QStringLiteral("节次必须为 1～999"));
        if (rule.timing != ReminderRule::Timing::Fixed && rule.timing != ReminderRule::Timing::Start && rule.timing != ReminderRule::Timing::End)
            return fail(error, QStringLiteral("无效的提醒时间类型"));
        if (!rule.time.isValid() || rule.offsetMinutes < -1439 || rule.offsetMinutes > 1439)
            return fail(error, QStringLiteral("提醒时间或偏移分钟数无效"));
        if (rule.timing == ReminderRule::Timing::Fixed &&
            (rule.text.contains(QStringLiteral("{科目}")) || rule.text.contains(QStringLiteral("{节次}"))))
            return fail(error, QStringLiteral("固定时刻提醒请使用固定文本；课程占位符仅适用于相对时间提醒"));
    }
    return true;
}

QJsonObject Reminders::toJson(const ReminderConfig& c)
{
    return {{QStringLiteral("enable"), c.enable}, {QStringLiteral("classStarted"), c.classStarted},
        {QStringLiteral("classEnded"), c.classEnded}, {QStringLiteral("startingSoon"), c.startingSoon},
        {QStringLiteral("advanceMinutes"), c.advanceMinutes}, {QStringLiteral("startedText"), c.startedText},
        {QStringLiteral("endedText"), c.endedText}, {QStringLiteral("soonText"), c.soonText}};
}

QJsonArray Reminders::toJson(const QList<ReminderRule>& rules)
{
    QJsonArray result;
    for (const auto& r : rules) {
        QJsonArray days;
        for (int day : r.weekdays) days.append(day);
        result.append(QJsonObject{{QStringLiteral("id"), r.id}, {QStringLiteral("name"), r.name},
            {QStringLiteral("enabled"), r.enabled}, {QStringLiteral("text"), r.text},
            {QStringLiteral("weekdays"), days}, {QStringLiteral("course"), int(r.course)},
            {QStringLiteral("subject"), r.subject}, {QStringLiteral("period"), r.period},
            {QStringLiteral("timing"), int(r.timing)}, {QStringLiteral("time"), r.time.toString(QStringLiteral("HH:mm:ss"))},
            {QStringLiteral("offsetMinutes"), r.offsetMinutes}});
    }
    return result;
}

bool Reminders::fromJson(const QJsonValue& value, ReminderConfig& config, QString* error)
{
    if (value.isUndefined()) return true;
    if (!value.isObject()) return fail(error, QStringLiteral("reminderConfig 必须为对象"));
    auto candidate = config;
    auto object = value.toObject();
    const auto boolean = [&](const QString& key, bool& target) {
        auto v = object.value(key);
        if (v.isUndefined()) return true;
        if (!v.isBool()) return fail(error, key + QStringLiteral(" 必须为布尔值"));
        target = v.toBool(); return true;
    };
    const auto string = [&](const QString& key, QString& target) {
        auto v = object.value(key);
        if (v.isUndefined()) return true;
        if (!v.isString()) return fail(error, key + QStringLiteral(" 必须为文本"));
        target = v.toString(); return true;
    };
    if (!boolean(QStringLiteral("enable"), candidate.enable) ||
        !boolean(QStringLiteral("classStarted"), candidate.classStarted) ||
        !boolean(QStringLiteral("classEnded"), candidate.classEnded) ||
        !boolean(QStringLiteral("startingSoon"), candidate.startingSoon) ||
        !string(QStringLiteral("startedText"), candidate.startedText) ||
        !string(QStringLiteral("endedText"), candidate.endedText) ||
        !string(QStringLiteral("soonText"), candidate.soonText)) return false;
    auto minutes = object.value(QStringLiteral("advanceMinutes"));
    if (!minutes.isUndefined()) {
        if (!integer(minutes, 1, 1439)) return fail(error, QStringLiteral("advanceMinutes 必须为 1～1439 的整数"));
        candidate.advanceMinutes = minutes.toInt();
    }
    if (!validate(candidate, error)) return false;
    config = candidate;
    return true;
}

bool Reminders::fromJson(const QJsonValue& value, QList<ReminderRule>& rules, QString* error)
{
    if (value.isUndefined()) return true;
    if (!value.isArray()) return fail(error, QStringLiteral("reminderRules 必须为数组"));
    QList<ReminderRule> candidate;
    for (const auto& v : value.toArray()) {
        if (!v.isObject()) return fail(error, QStringLiteral("提醒规则必须为对象"));
        const auto o = v.toObject();
        for (const auto& key : {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("text"), QStringLiteral("subject"), QStringLiteral("time")})
            if (!o.value(key).isString()) return fail(error, key + QStringLiteral(" 必须为文本"));
        if (!o.value(QStringLiteral("enabled")).isBool() || !o.value(QStringLiteral("weekdays")).isArray() ||
            !integer(o.value(QStringLiteral("course")), 0, 2) || !integer(o.value(QStringLiteral("timing")), 0, 2) ||
            !integer(o.value(QStringLiteral("period")), 1, 999) || !integer(o.value(QStringLiteral("offsetMinutes")), -1439, 1439))
            return fail(error, QStringLiteral("提醒规则字段类型或数值无效"));
        ReminderRule r;
        r.id = o.value(QStringLiteral("id")).toString(); r.name = o.value(QStringLiteral("name")).toString();
        r.text = o.value(QStringLiteral("text")).toString(); r.subject = o.value(QStringLiteral("subject")).toString();
        r.enabled = o.value(QStringLiteral("enabled")).toBool();
        r.course = ReminderRule::Course(o.value(QStringLiteral("course")).toInt());
        r.timing = ReminderRule::Timing(o.value(QStringLiteral("timing")).toInt());
        r.period = o.value(QStringLiteral("period")).toInt(); r.offsetMinutes = o.value(QStringLiteral("offsetMinutes")).toInt();
        const auto time = o.value(QStringLiteral("time")).toString();
        r.time = QTime::fromString(time, QStringLiteral("HH:mm:ss"));
        if (!r.time.isValid() || r.time.toString(QStringLiteral("HH:mm:ss")) != time)
            return fail(error, QStringLiteral("提醒时间必须为 HH:mm:ss"));
        r.weekdays.clear();
        for (const auto& day : o.value(QStringLiteral("weekdays")).toArray()) {
            if (!integer(day, 1, 7)) return fail(error, QStringLiteral("提醒星期必须为 1～7 的整数"));
            r.weekdays.append(day.toInt());
        }
        candidate.append(r);
    }
    if (!validate(candidate, error)) return false;
    rules = candidate;
    return true;
}

void Reminders::renameSubject(QList<ReminderRule>& rules, const QString& oldName, const QString& newName)
{
    for (auto& rule : rules)
        if (rule.course == ReminderRule::Course::Subject && rule.subject == oldName) rule.subject = newName;
}
