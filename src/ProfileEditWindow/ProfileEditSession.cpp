#include "ProfileEditSession.h"
#include "../Core/DateSchedule/DateSchedule.h"
#include <QSet>
#include <algorithm>

namespace {
bool fail(QString* error, const QString& text)
{
    if (error) *error = text;
    return false;
}

bool equal(const Profile& a, const Profile& b)
{
    if (Reminders::toJson(a.reminderRules) != Reminders::toJson(b.reminderRules)) return false;
    if (a.name != b.name || a.semesterStartDate != b.semesterStartDate ||
        a.activeWeekScheduleId != b.activeWeekScheduleId || a.subjects.size() != b.subjects.size() ||
        a.timeLines.size() != b.timeLines.size() || a.schedules.size() != b.schedules.size() ||
        a.dateOverrides.size() != b.dateOverrides.size()) return false;
    for (qsizetype i = 0; i < a.dateOverrides.size(); ++i) {
        const auto& x = a.dateOverrides[i]; const auto& y = b.dateOverrides[i];
        if (x.date != y.date || x.sourceWeekScheduleId != y.sourceWeekScheduleId || x.sourceWeekday != y.sourceWeekday ||
            !DateSchedule::sameClasses(x.sourceClasses, y.sourceClasses) || !DateSchedule::sameClasses(x.classes, y.classes))
            return false;
    }
    for (qsizetype i = 0; i < a.subjects.size(); ++i) {
        const auto& x = a.subjects[i]; const auto& y = b.subjects[i];
        if (x.name != y.name || x.simplifiedName != y.simplifiedName || x.teacher != y.teacher) return false;
    }
    for (qsizetype i = 0; i < a.timeLines.size(); ++i) {
        const auto& x = a.timeLines[i]; const auto& y = b.timeLines[i];
        if (x.name != y.name || x.timePoints.size() != y.timePoints.size()) return false;
        for (qsizetype j = 0; j < x.timePoints.size(); ++j)
            if (x.timePoints[j].startTime != y.timePoints[j].startTime || x.timePoints[j].endTime != y.timePoints[j].endTime) return false;
    }
    for (qsizetype i = 0; i < a.schedules.size(); ++i) {
        const auto& x = a.schedules[i]; const auto& y = b.schedules[i];
        if (x.id != y.id || x.name != y.name || x.mode != y.mode || x.daySchedules.size() != y.daySchedules.size()) return false;
        for (qsizetype j = 0; j < x.daySchedules.size(); ++j) {
            const auto& xd = x.daySchedules[j]; const auto& yd = y.daySchedules[j];
            if (xd.name != yd.name || xd.enableDay != yd.enableDay || xd.classes.size() != yd.classes.size()) return false;
            for (qsizetype k = 0; k < xd.classes.size(); ++k)
                if (xd.classes[k].subject != yd.classes[k].subject || xd.classes[k].startTime != yd.classes[k].startTime ||
                    xd.classes[k].endTime != yd.classes[k].endTime) return false;
        }
    }
    return true;
}
bool validRange(const QTime& start, const QTime& end) { return start.isValid() && end.isValid() && start < end; }
}

ProfileEditSession::ProfileEditSession(QObject* parent) : QObject(parent) {}

void ProfileEditSession::notifyChanged()
{
    // Draft deletion has no effect on the live profile until the draft is saved.
    const bool selectedExists = std::any_of(draft_.schedules.cbegin(), draft_.schedules.cend(),
        [this](const auto& week) { return week.id == draft_.activeWeekScheduleId; });
    if (!selectedExists) draft_.activeWeekScheduleId.clear();
    dirty_ = !equal(draft_, saved_);
    emit changed();
}

void ProfileEditSession::reset(const Profile& profile)
{
    subjectRenames_.clear();
    saved_ = draft_ = profile;
    dirty_ = false;
    emit changed();
    emit draftReset();
}

void ProfileEditSession::markSaved() { saved_ = draft_; subjectRenames_.clear(); dirty_ = false; emit changed(); }
void ProfileEditSession::discard() { reset(saved_); }

void ProfileEditSession::syncActiveWeekSchedule(const QString& id)
{
    saved_.activeWeekScheduleId = id;
    draft_.activeWeekScheduleId = id;
    notifyChanged();
}

void ProfileEditSession::syncDateOverrides(const QList<DateScheduleOverride>& records)
{
    saved_.dateOverrides = records;
    draft_.dateOverrides = records;
    notifyChanged();
}

void ProfileEditSession::syncReminderRules(const QList<ReminderRule>& rules)
{
    saved_.reminderRules = rules;
    draft_.reminderRules = rules;
    // Remap once against saved names: chained renames and swaps must not cascade.
    for (auto& rule : draft_.reminderRules)
        if (rule.course == ReminderRule::Course::Subject && subjectRenames_.contains(rule.subject))
            rule.subject = subjectRenames_.value(rule.subject);
    notifyChanged();
}

bool ProfileEditSession::validateTimePoints(const QList<TimeLinePoint>& points, QString* error)
{
    if (error) error->clear();
    auto sorted = points;
    for (const auto& point : sorted)
        if (!validRange(point.startTime, point.endTime)) return fail(error, tr("开始时间必须早于结束时间，且不能跨日"));
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.startTime < b.startTime; });
    for (int i = 1; i < sorted.size(); ++i)
        if (sorted[i].startTime < sorted[i - 1].endTime) return fail(error, tr("时间线时段重叠"));
    return true;
}

bool ProfileEditSession::replaceTimePoints(int lineIndex, const QList<TimeLinePoint>& points, QString* error)
{
    if (lineIndex < 0 || lineIndex >= draft_.timeLines.size()) return fail(error, tr("时间线不存在"));
    if (!validateTimePoints(points, error)) return false;
    draft_.timeLines[lineIndex].timePoints = points;
    notifyChanged();
    return true;
}

bool ProfileEditSession::updateSubject(int index, const Subject& subject, QString* error)
{
    if (index < -1 || index >= draft_.subjects.size()) return fail(error, tr("科目不存在"));
    const QString name = subject.name.trimmed();
    if (name.isEmpty()) return fail(error, tr("科目名称不能为空"));
    for (qsizetype i = 0; i < draft_.subjects.size(); ++i)
        if (i != index && draft_.subjects[i].name.trimmed() == name) return fail(error, tr("科目名称已存在"));
    Subject updated(name, subject.simplifiedName.trimmed(), subject.teacher.trimmed());
    if (index == -1) draft_.subjects.append(updated);
    else {
        const QString oldName = draft_.subjects[index].name;
        bool tracked = false;
        for (auto it = subjectRenames_.begin(); it != subjectRenames_.end(); ++it) {
            if (it.value() == oldName) { it.value() = name; tracked = true; }
        }
        if (!tracked) subjectRenames_.insert(oldName, name);
        Reminders::renameSubject(draft_.reminderRules, oldName, name);
        draft_.subjects[index] = updated;
        for (auto& week : draft_.schedules) for (auto& day : week.daySchedules) for (auto& course : day.classes)
            if (course.subject == oldName) course.subject = name;
    }
    notifyChanged();
    return true;
}

bool ProfileEditSession::removeSubject(int index, QString* error)
{
    if (index < 0 || index >= draft_.subjects.size()) return fail(error, tr("科目不存在"));
    for (const auto& week : draft_.schedules) for (const auto& day : week.daySchedules) for (const auto& course : day.classes)
        if (course.subject == draft_.subjects[index].name) return fail(error, tr("科目正在被课表使用，请先修改对应课程"));
    draft_.subjects.removeAt(index);
    notifyChanged();
    return true;
}

bool ProfileEditSession::applyTimeLine(int weekIndex, int weekday, int lineIndex, QString* error)
{
    if (weekIndex < 0 || weekIndex >= draft_.schedules.size() || lineIndex < 0 || lineIndex >= draft_.timeLines.size() ||
        weekday < 1 || weekday > 7) return fail(error, tr("请选择课表、星期和时间线"));
    auto points = draft_.timeLines[lineIndex].timePoints;
    std::stable_sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.startTime < b.startTime; });
    for (qsizetype i = 0; i < points.size(); ++i) {
        if (!validRange(points[i].startTime, points[i].endTime)) return fail(error, tr("时间线包含无效时段"));
        if (i && points[i].startTime < points[i - 1].endTime) return fail(error, tr("时间线时段重叠"));
    }
    auto& days = draft_.schedules[weekIndex].daySchedules;
    auto it = std::find_if(days.begin(), days.end(), [weekday](const auto& day) { return day.enableDay == weekday; });
    if (it == days.end()) { days.append(DaySchedule(tr("星期 %1").arg(weekday), weekday)); it = days.end() - 1; }
    auto old = it->classes;
    std::stable_sort(old.begin(), old.end(), [](const auto& a, const auto& b) { return a.startTime < b.startTime; });
    it->classes.clear();
    for (qsizetype i = 0; i < points.size(); ++i)
        it->classes.append(Class(i < old.size() ? old[i].subject : QString(), points[i].startTime, points[i].endTime));
    notifyChanged();
    return true;
}

ProfileEditSession::ValidationError ProfileEditSession::validate(const Profile& profile)
{
    return ProfileValidation::validate(profile);
}

QStringList ProfileEditSession::warnings() const
{
    QStringList result;
    QSet<QString> names;
    for (const auto& subject : draft_.subjects) names.insert(subject.name);
    bool oddEven = false, unknown = false, conflict = false;
    for (int i = 0; i < draft_.schedules.size(); ++i) {
        const auto& week = draft_.schedules[i];
        oddEven |= week.mode != WeekScheduleMode::All;
        for (const auto& day : week.daySchedules) {
            for (const auto& course : day.classes) unknown |= !names.contains(course.subject);
            for (int j = 0; j < i; ++j) {
                const auto& other = draft_.schedules[j];
                if (week.mode != other.mode && week.mode != WeekScheduleMode::All && other.mode != WeekScheduleMode::All) continue;
                for (const auto& otherDay : other.daySchedules) conflict |= day.enableDay == otherDay.enableDay;
            }
        }
    }
    if (oddEven && !draft_.semesterStartDate.isValid())
        result.append(tr("单双周自动运行需要在“基本信息”中设置学期第一天。"));
    if (conflict) result.append(tr("多个周课表适用于同一星期，课程栏自动选择可能冲突。"));
    if (unknown) result.append(tr("部分课程使用未定义科目名称，课程栏将使用原名称。"));
    return result;
}
