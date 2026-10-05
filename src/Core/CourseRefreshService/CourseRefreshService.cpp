#include "CourseRefreshService.h"
#include "../ProfileManager/ProfileManager.h"

#include <QThread>
#include <QTimeZone>

#include <algorithm>
#include <utility>

namespace {
constexpr int kRefreshIntervalMs = 250;
constexpr qint64 kReminderSeconds = 120;
// Resume and large clock corrections synchronize state without replaying events.
constexpr qint64 kMaximumEventGapMs = 5000;

bool sameState(const CourseRefreshService::State& left,
               const CourseRefreshService::State& right)
{
    return left.tableRevision == right.tableRevision && left.phase == right.phase &&
           left.currentClassIndex == right.currentClassIndex &&
           left.nextClassIndex == right.nextClassIndex &&
           left.countdownTarget == right.countdownTarget &&
           left.remainingSeconds == right.remainingSeconds;
}
} // namespace

CourseRefreshService::CourseRefreshService(QObject* parent)
    : CourseRefreshService([] { return QDateTime::currentDateTime(); }, parent)
{
}

CourseRefreshService::CourseRefreshService(Clock clock, QObject* parent)
    : QObject(parent), timer_(new QTimer(this)), clock_(std::move(clock))
{
    if (!clock_) clock_ = [] { return QDateTime::currentDateTime(); };
    qRegisterMetaType<TableSnapshot>();
    qRegisterMetaType<State>();
    qRegisterMetaType<Event>();
    timer_->setInterval(kRefreshIntervalMs);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &CourseRefreshService::refresh);
}

void CourseRefreshService::setWeekScheduleIndex(int index)
{
    if (weekScheduleIndex_ == index) return;
    weekScheduleIndex_ = index;
    reloadTable();
}

void CourseRefreshService::setWeekReferenceDate(const QDate& date)
{
    const QDate monday = date.isValid() ? date.addDays(1 - date.dayOfWeek()) : QDate{};
    if (weekOneMonday_ == monday) return;
    weekOneMonday_ = monday;
    reloadTable();
}

void CourseRefreshService::setTemporaryWeekday(int weekday)
{
    if (weekday < 1 || weekday > 7) {
        reportError(QStringLiteral("Temporary weekday must be 1..7"));
        return;
    }
    const QDateTime now = clock_();
    if (!now.isValid()) {
        reportError(QStringLiteral("Clock returned an invalid date/time"));
        return;
    }
    temporarySchedule_.reset();
    temporaryWeekday_ = weekday;
    temporaryDate_ = now.date();
    reloadTable();
}

void CourseRefreshService::setTemporarySchedule(const DaySchedule& schedule)
{
    const QDateTime now = clock_();
    if (!now.isValid()) {
        reportError(QStringLiteral("Clock returned an invalid date/time"));
        return;
    }
    temporaryWeekday_ = 0;
    temporarySchedule_ = schedule;
    temporaryDate_ = now.date();
    reloadTable();
}

void CourseRefreshService::clearTemporarySelection()
{
    if (!temporaryDate_.isValid()) return;
    temporarySchedule_.reset();
    temporaryWeekday_ = 0;
    temporaryDate_ = {};
    reloadTable();
}

void CourseRefreshService::start()
{
    Q_ASSERT(QThread::currentThread() == thread());
    if (timer_->isActive()) return;
    previousTime_ = {};
    timer_->start();
    // Read the manager and publish an initial snapshot after connecting UI.
    reloadTable();
}

void CourseRefreshService::stop()
{
    Q_ASSERT(QThread::currentThread() == thread());
    timer_->stop();
    previousTime_ = {};
}

void CourseRefreshService::reloadTable()
{
    Q_ASSERT(QThread::currentThread() == thread());
    tableDirty_ = true;
    refresh();
}

void CourseRefreshService::reportError(const QString& message)
{
    if (lastError_ == message) return;
    lastError_ = message;
    emit errorOccurred(message);
}

CourseRefreshService::TableSnapshot CourseRefreshService::resolveTable(const QDate& date) const
{
    TableSnapshot result;
    const Profile& profile = std::as_const(ProfileManager::instance()).profile();
    result.date = date;
    result.subjects = profile.subjects;
    auto fail = [&result](const QString& error) {
        result.valid = false;
        result.error = error;
        result.classes.clear();
        return result;
    };

    if (temporarySchedule_) {
        result.name = temporarySchedule_->name;
        result.classes = temporarySchedule_->classes;
    } else {
        const int weekday = temporaryWeekday_ ? temporaryWeekday_ : date.dayOfWeek();
        int selected = weekScheduleIndex_;
        if (selected < -1 || selected >= profile.schedules.size())
            return fail(QStringLiteral("Week schedule index is out of range"));

        if (selected == -1 && !profile.activeWeekScheduleId.isEmpty()) {
            for (qsizetype i = 0; i < profile.schedules.size(); ++i) {
                if (profile.schedules[i].id == profile.activeWeekScheduleId) {
                    selected = static_cast<int>(i);
                    break;
                }
            }
        }
        if (selected == -1) {
            for (qsizetype i = 0; i < profile.schedules.size(); ++i) {
                const WeekSchedule& week = profile.schedules[i];
                const bool hasDay = std::any_of(week.daySchedules.cbegin(), week.daySchedules.cend(),
                    [weekday](const DaySchedule& day) { return day.enableDay == weekday; });
                if (!hasDay) continue;
                if (week.mode != WeekScheduleMode::All) {
                    if (week.mode != WeekScheduleMode::Odd && week.mode != WeekScheduleMode::Even)
                        return fail(QStringLiteral("Week schedule has an invalid mode"));
                    if (!weekOneMonday_.isValid())
                        return fail(QStringLiteral("Odd/even schedules require a week reference date"));
                    const QDate monday = date.addDays(1 - date.dayOfWeek());
                    const qint64 weekNumber = weekOneMonday_.daysTo(monday) / 7 + 1;
                    const bool odd = weekNumber % 2 != 0;
                    if ((week.mode == WeekScheduleMode::Odd) != odd) continue;
                }
                if (selected != -1)
                    return fail(QStringLiteral("Multiple week schedules match; select an explicit index"));
                selected = static_cast<int>(i);
            }
        }

        if (selected != -1) {
            const WeekSchedule& week = profile.schedules[selected];
            result.weekScheduleIndex = selected;
            result.name = week.name;
            QSet<int> days;
            for (const DaySchedule& day : week.daySchedules) {
                if (day.enableDay < 1 || day.enableDay > 7 || days.contains(day.enableDay))
                    return fail(QStringLiteral("Week schedule has an invalid or duplicate weekday"));
                days.insert(day.enableDay);
                if (day.enableDay == weekday) result.classes = day.classes;
            }
        }
    }

    for (const Class& course : result.classes) {
        if (course.subject.trimmed().isEmpty() || !course.startTime.isValid() ||
            !course.endTime.isValid() || course.startTime >= course.endTime)
            return fail(QStringLiteral("Course has an empty subject or invalid time range"));
    }
    std::stable_sort(result.classes.begin(), result.classes.end(),
        [](const Class& left, const Class& right) { return left.startTime < right.startTime; });
    for (qsizetype i = 1; i < result.classes.size(); ++i) {
        if (result.classes[i].startTime < result.classes[i - 1].endTime)
            return fail(QStringLiteral("Course time ranges overlap"));
    }
    return result;
}

CourseRefreshService::State CourseRefreshService::calculateState(const QDateTime& now) const
{
    State result;
    result.tableRevision = table_.revision;
    if (!table_.valid) {
        result.phase = Phase::InvalidTable;
        return result;
    }
    if (table_.classes.isEmpty()) return result;

    for (qsizetype i = 0; i < table_.classes.size(); ++i) {
        const Class& course = table_.classes[i];
        const QDateTime start(table_.date, course.startTime, now.timeZone());
        const QDateTime end(table_.date, course.endTime, now.timeZone());
        if (now < start) {
            result.phase = i == 0 ? Phase::BeforeFirstClass : Phase::Break;
            result.nextClassIndex = static_cast<int>(i);
            result.countdownTarget = start;
        } else if (now < end) {
            result.phase = Phase::InClass;
            result.currentClassIndex = static_cast<int>(i);
            if (i + 1 < table_.classes.size()) result.nextClassIndex = static_cast<int>(i + 1);
            result.countdownTarget = end;
        } else {
            continue;
        }
        const qint64 milliseconds = now.msecsTo(result.countdownTarget);
        result.remainingSeconds = std::max<qint64>(0, (milliseconds + 999) / 1000);
        return result;
    }
    result.phase = Phase::Finished;
    return result;
}

QList<CourseRefreshService::Event> CourseRefreshService::detectEvents(const QDateTime& now)
{
    QList<Event> events;
    if (!table_.valid || !previousTime_.isValid()) return events;
    const qint64 elapsed = previousTime_.msecsTo(now);
    if (elapsed <= 0 || elapsed > kMaximumEventGapMs || previousTime_.timeZone() != now.timeZone())
        return events;

    auto append = [this, &events, &now](qsizetype index, EventType type, const QDateTime& boundary) {
        const quint64 key = static_cast<quint64>(index) * 3 + static_cast<quint64>(type);
        if (previousTime_ < boundary && now >= boundary && !deliveredEvents_.contains(key)) {
            deliveredEvents_.insert(key);
            events.append(Event{table_.revision, type, static_cast<int>(index),
                                table_.classes[index].subject, boundary});
        }
    };
    for (qsizetype i = 0; i < table_.classes.size(); ++i) {
        const Class& course = table_.classes[i];
        const QDateTime start(table_.date, course.startTime, now.timeZone());
        const QDateTime end(table_.date, course.endTime, now.timeZone());
        if (now < start) append(i, EventType::ClassStartingSoon, start.addSecs(-kReminderSeconds));
        if (now < end) append(i, EventType::ClassStarted, start);
        append(i, EventType::ClassEnded, end);
    }
    std::stable_sort(events.begin(), events.end(), [](const Event& left, const Event& right) {
        return left.scheduledTime < right.scheduledTime;
    });
    return events;
}

void CourseRefreshService::refresh()
{
    Q_ASSERT(QThread::currentThread() == thread());
    const QDateTime now = clock_();
    if (!now.isValid()) {
        previousTime_ = {};
        reportError(QStringLiteral("Clock returned an invalid date/time"));
        return;
    }
    if (temporaryDate_.isValid() && temporaryDate_ != now.date()) {
        temporarySchedule_.reset();
        temporaryWeekday_ = 0;
        temporaryDate_ = {};
        tableDirty_ = true;
    }

    const bool rebuilt = tableDirty_ || table_.date != now.date();
    if (rebuilt) {
        table_ = resolveTable(now.date());
        table_.revision = ++revision_;
        tableDirty_ = false;
        deliveredEvents_.clear();
        previousTime_ = {};
    }
    const QList<Event> events = detectEvents(now);
    const State next = calculateState(now);
    const bool changed = !hasState_ || !sameState(state_, next);
    state_ = next;
    hasState_ = true;
    previousTime_ = now;

    // Copy publication data: a direct receiver may reload the table reentrantly.
    const TableSnapshot publishedTable = table_;
    const State publishedState = state_;
    if (rebuilt) emit tableChanged(publishedTable);
    if (table_.revision != publishedTable.revision) return;
    if (changed) emit stateChanged(publishedState);
    if (table_.revision != publishedTable.revision) return;
    if (!publishedTable.valid) {
        reportError(publishedTable.error);
    } else {
        lastError_.clear();
        for (const Event& event : events) {
            emit eventOccurred(event);
            if (table_.revision != publishedTable.revision) return;
        }
    }
}
