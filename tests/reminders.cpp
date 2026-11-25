#include "Core/Reminders/ReminderService.h"
#include "test_profile.h"

#include <QtTest>

namespace {
const QTimeZone utc(QTimeZone::UTC);

CourseRefreshService::TableSnapshot mondayTable()
{
    const auto profile = TestProfiles::regular();
    CourseRefreshService::TableSnapshot table;
    table.date = QDate(2026, 8, 31);
    table.revision = 7;
    table.classes = profile.schedules.first().daySchedules.first().classes;
    table.subjects = profile.subjects;
    return table;
}
}

class ReminderTest : public QObject
{
    Q_OBJECT

private slots:
    void builtInSwitchesTemplatesAndTimes();
    void matchesCustomTimingAndCourseConditions();
    void ignoresNotificationsOutsideScheduleDate();
};

void ReminderTest::builtInSwitchesTemplatesAndTimes()
{
    auto table = mondayTable();
    table.classes.removeLast();
    ReminderConfig config;
    config.advanceMinutes = 5;
    config.soonText = QStringLiteral("第{节次}节 {科目}");
    const auto scheduled = ReminderService::schedule(table, config, {}, utc);
    QCOMPARE(scheduled.size(), 3);
    QCOMPARE(scheduled.at(0).key, QStringLiteral("builtin/soon"));
    QCOMPARE(scheduled.at(0).text, QStringLiteral("第1节 数学"));
    QCOMPARE(scheduled.at(0).scheduledTime, QDateTime(table.date, QTime(7, 55), utc));
    QCOMPARE(scheduled.at(0).tableRevision, table.revision);
    QCOMPARE(scheduled.at(1).key, QStringLiteral("builtin/start"));
    QCOMPARE(scheduled.at(1).scheduledTime, QDateTime(table.date, QTime(8, 0), utc));
    QCOMPARE(scheduled.at(2).key, QStringLiteral("builtin/end"));
    QCOMPARE(scheduled.at(2).scheduledTime, QDateTime(table.date, QTime(8, 45), utc));

    config.classStarted = false;
    config.startingSoon = false;
    auto remaining = ReminderService::schedule(table, config, {}, utc);
    QCOMPARE(remaining.size(), 1);
    QCOMPARE(remaining.first().key, QStringLiteral("builtin/end"));
    config.classStarted = true;
    config.classEnded = false;
    remaining = ReminderService::schedule(table, config, {}, utc);
    QCOMPARE(remaining.size(), 1);
    QCOMPARE(remaining.first().key, QStringLiteral("builtin/start"));
    config.enable = false;
    QVERIFY(ReminderService::schedule(table, config, {}, utc).isEmpty());
}

void ReminderTest::matchesCustomTimingAndCourseConditions()
{
    auto table = mondayTable();
    table.sourceWeekday = 2; // A moved schedule uses its source weekday.
    ReminderConfig config;
    config.classStarted = config.classEnded = config.startingSoon = false;

    ReminderRule fixed;
    fixed.id = QStringLiteral("fixed");
    fixed.text = QStringLiteral("请做好准备");
    fixed.weekdays = {2};
    fixed.time = QTime(7, 30);

    ReminderRule subject = fixed;
    subject.id = QStringLiteral("subject");
    subject.course = ReminderRule::Course::Subject;
    subject.subject = TestProfiles::math;
    subject.timing = ReminderRule::Timing::Start;
    subject.offsetMinutes = -3;
    subject.text = QStringLiteral("{科目}即将上课");

    ReminderRule period = fixed;
    period.id = QStringLiteral("period");
    period.course = ReminderRule::Course::Period;
    period.period = 2;
    period.timing = ReminderRule::Timing::End;
    period.offsetMinutes = 2;
    period.text = QStringLiteral("第{节次}节 {科目}已结束");

    ReminderRule otherWeekday = fixed;
    otherWeekday.id = QStringLiteral("other-weekday");
    otherWeekday.weekdays = {1};
    ReminderRule otherSubject = subject;
    otherSubject.id = QStringLiteral("other-subject");
    otherSubject.subject = TestProfiles::science;
    ReminderRule otherPeriod = period;
    otherPeriod.id = QStringLiteral("other-period");
    otherPeriod.period = 3;
    ReminderRule disabled = fixed;
    disabled.id = QStringLiteral("disabled");
    disabled.enabled = false;

    const QList<ReminderRule> rules{period, otherWeekday, subject, disabled, fixed, otherSubject, otherPeriod};
    const auto scheduled = ReminderService::schedule(table, config, rules, utc);
    QCOMPARE(scheduled.size(), 3);
    QCOMPARE(scheduled.at(0).key, QStringLiteral("rule/fixed"));
    QCOMPARE(scheduled.at(0).text, fixed.text);
    QCOMPARE(scheduled.at(0).scheduledTime, QDateTime(table.date, QTime(7, 30), utc));
    QCOMPARE(scheduled.at(1).key, QStringLiteral("rule/subject"));
    QCOMPARE(scheduled.at(1).text, QStringLiteral("数学即将上课"));
    QCOMPARE(scheduled.at(1).scheduledTime, QDateTime(table.date, QTime(7, 57), utc));
    QCOMPARE(scheduled.at(2).key, QStringLiteral("rule/period"));
    QCOMPARE(scheduled.at(2).text, QStringLiteral("第2节 语文已结束"));
    QCOMPARE(scheduled.at(2).scheduledTime, QDateTime(table.date, QTime(9, 42), utc));
}

void ReminderTest::ignoresNotificationsOutsideScheduleDate()
{
    auto table = mondayTable();
    table.classes = {{TestProfiles::math, QTime(0, 1), QTime(0, 45)},
                     {TestProfiles::chinese, QTime(23, 50), QTime(23, 58)}};
    ReminderConfig config;
    config.classStarted = config.classEnded = false;
    config.advanceMinutes = 2;

    ReminderRule afterLastClass;
    afterLastClass.id = QStringLiteral("last-class");
    afterLastClass.course = ReminderRule::Course::Period;
    afterLastClass.period = 2;
    afterLastClass.timing = ReminderRule::Timing::End;
    afterLastClass.offsetMinutes = 5;
    afterLastClass.text = QStringLiteral("晚间提醒");

    const auto scheduled = ReminderService::schedule(table, config, {afterLastClass}, utc);
    QCOMPARE(scheduled.size(), 1);
    QCOMPARE(scheduled.first().key, QStringLiteral("builtin/soon"));
    QCOMPARE(scheduled.first().scheduledTime, QDateTime(table.date, QTime(23, 48), utc));
}

QTEST_GUILESS_MAIN(ReminderTest)
#include "reminders.moc"
