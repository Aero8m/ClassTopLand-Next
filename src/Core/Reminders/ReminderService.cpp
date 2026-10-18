#include "ReminderService.h"
#include "../ConfigManager/ConfigManager.h"
#include "../ProfileManager/ProfileManager.h"
#include "../DateSchedule/DateSchedule.h"

#include <QTimeZone>
#include <QMap>
#include <algorithm>
#include <utility>

namespace {
QString ruleKey(const ReminderRule& rule) { return QStringLiteral("rule/") + rule.id; }
QMap<QString, QJsonObject> signatures(const ReminderConfig& config, const QList<ReminderRule>& rules)
{
    QMap<QString, QJsonObject> result;
    result.insert(QStringLiteral("builtin/start"), {{QStringLiteral("enabled"), config.classStarted}, {QStringLiteral("text"), config.startedText}});
    result.insert(QStringLiteral("builtin/end"), {{QStringLiteral("enabled"), config.classEnded}, {QStringLiteral("text"), config.endedText}});
    result.insert(QStringLiteral("builtin/soon"), {{QStringLiteral("enabled"), config.startingSoon}, {QStringLiteral("text"), config.soonText}, {QStringLiteral("minutes"), config.advanceMinutes}});
    for (const auto& rule : rules) result.insert(ruleKey(rule), Reminders::toJson(QList<ReminderRule>{rule}).first().toObject());
    return result;
}
}

ReminderService::ReminderService(CourseRefreshService* courses, QObject* parent)
    : ReminderService(courses, [] { return QDateTime::currentDateTime(); }, parent) {}

ReminderService::ReminderService(CourseRefreshService* courses, CourseRefreshService::Clock clock, QObject* parent)
    : QObject(parent), courses_(courses), clock_(std::move(clock)), timer_(new QTimer(this))
{
    if (!clock_) clock_ = [] { return QDateTime::currentDateTime(); };
    qRegisterMetaType<ReminderNotification>();
    timer_->setInterval(250);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &ReminderService::refresh);
    connect(courses_, &CourseRefreshService::tableChanged, this, &ReminderService::tableChanged);
    connect(&ConfigManager::instance(), &ConfigManager::committed, this, &ReminderService::reloadSettings);
    connect(&ProfileManager::instance(), &ProfileManager::committed, this, &ReminderService::reloadSettings);
    reloadSettings();
}

void ReminderService::start()
{
    if (timer_->isActive()) return;
    previous_ = clock_();
    timer_->start();
}

void ReminderService::stop()
{
    timer_->stop();
    previous_ = {};
    emit invalidated({QStringLiteral("*")}, true);
}

void ReminderService::tableChanged()
{
    const auto& next = courses_->table();
    const bool changed = table_.date != next.date || table_.valid != next.valid ||
        table_.sourceWeekday != next.sourceWeekday || !DateSchedule::sameClasses(table_.classes, next.classes) ||
        table_.subjects.size() != next.subjects.size() ||
        !std::equal(table_.subjects.cbegin(), table_.subjects.cend(), next.subjects.cbegin(), next.subjects.cend(),
            [](const Subject& a, const Subject& b) { return a.name == b.name; });
    table_ = next;
    ++revision_;
    previous_ = clock_();
    if (changed) emit invalidated({QStringLiteral("*")}, true);
}

void ReminderService::reloadSettings()
{
    const auto& config = ConfigManager::instance().config();
    const auto& rules = ProfileManager::instance().profile().reminderRules;
    const auto before = signatures(config_, rules_);
    const auto after = signatures(config.reminderConfig, rules);
    if (before == after && config_.enable == config.reminderConfig.enable && barEnabled_ == config.courseBarConfig.enable) return;
    if (config_.enable != config.reminderConfig.enable || barEnabled_ != config.courseBarConfig.enable)
        emit invalidated({QStringLiteral("*")}, true);
    for (auto it = before.cbegin(); it != before.cend(); ++it) {
        if (!after.contains(it.key()) || after.value(it.key()) != it.value()) {
            const bool disabled = !after.contains(it.key()) || !after.value(it.key()).value(QStringLiteral("enabled")).toBool();
            emit invalidated({it.key()}, disabled);
        }
    }
    config_ = config.reminderConfig;
    rules_ = rules;
    barEnabled_ = config.courseBarConfig.enable;
    ++revision_;
    previous_ = clock_();
}

QList<ReminderNotification> ReminderService::schedule(const CourseRefreshService::TableSnapshot& table,
    const ReminderConfig& config, const QList<ReminderRule>& rules, const QTimeZone& zone)
{
    QList<ReminderNotification> result;
    if (!config.enable || !table.valid || !table.date.isValid()) return result;
    const auto add = [&](const QString& key, const QString& text, const QDateTime& when, int index) {
        if (!when.isValid() || when.date() != table.date) return;
        QString occurrence = key + QLatin1Char('/') + table.date.toString(Qt::ISODate);
        if (index >= 0) {
            const auto& course = table.classes[index];
            occurrence += QLatin1Char('/') + course.startTime.toString(Qt::ISODate) + QLatin1Char('/') + course.endTime.toString(Qt::ISODate);
        }
        result.append({key, occurrence, text, when, table.revision, 0});
    };
    for (qsizetype i = 0; i < table.classes.size(); ++i) {
        const auto& course = table.classes[i];
        const QDateTime start(table.date, course.startTime, zone), end(table.date, course.endTime, zone);
        if (config.classStarted) add(QStringLiteral("builtin/start"), Reminders::render(config.startedText, course.subject, int(i + 1)), start, int(i));
        if (config.classEnded) add(QStringLiteral("builtin/end"), Reminders::render(config.endedText, course.subject, int(i + 1)), end, int(i));
        if (config.startingSoon) add(QStringLiteral("builtin/soon"), Reminders::render(config.soonText, course.subject, int(i + 1)), start.addSecs(-config.advanceMinutes * 60), int(i));
    }
    for (const auto& rule : rules) {
        if (!rule.enabled || !rule.weekdays.contains(table.sourceWeekday)) continue;
        if (rule.course == ReminderRule::Course::Subject &&
            std::none_of(table.subjects.cbegin(), table.subjects.cend(), [&](const Subject& s) { return s.name == rule.subject; })) continue;
        const auto matches = [&](qsizetype i) {
            return rule.course == ReminderRule::Course::Any ||
                (rule.course == ReminderRule::Course::Subject && table.classes[i].subject == rule.subject) ||
                (rule.course == ReminderRule::Course::Period && i + 1 == rule.period);
        };
        if (rule.timing == ReminderRule::Timing::Fixed) {
            bool matched = rule.course == ReminderRule::Course::Any;
            for (qsizetype i = 0; i < table.classes.size(); ++i) matched |= matches(i);
            if (matched) add(ruleKey(rule), rule.text, QDateTime(table.date, rule.time, zone), -1);
        } else {
            for (qsizetype i = 0; i < table.classes.size(); ++i) {
                if (!matches(i)) continue;
                const auto& c = table.classes[i];
                const QDateTime boundary(table.date, rule.timing == ReminderRule::Timing::Start ? c.startTime : c.endTime, zone);
                add(ruleKey(rule), Reminders::render(rule.text, c.subject, int(i + 1)), boundary.addSecs(rule.offsetMinutes * 60), int(i));
            }
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.scheduledTime < b.scheduledTime; });
    return result;
}

void ReminderService::refresh()
{
    const QDateTime now = clock_();
    const auto previous = previous_;
    previous_ = now;
    if (!now.isValid() || !previous.isValid() || previous.timeZone() != now.timeZone() ||
        previous.msecsTo(now) <= 0 || previous.msecsTo(now) > 5000 || !barEnabled_ || courses_->table().date != now.date()) return;
    const auto generation = revision_;
    const auto tableRevision = courses_->table().revision;
    const auto pending = schedule(courses_->table(), config_, rules_, now.timeZone());
    for (auto item : pending) {
        if (generation != revision_ || tableRevision != courses_->table().revision) break;
        if (previous < item.scheduledTime && item.scheduledTime <= now && !delivered_.contains(item.occurrence)) {
            delivered_.insert(item.occurrence);
            item.revision = revision_;
            emit notification(item);
        }
    }
}
