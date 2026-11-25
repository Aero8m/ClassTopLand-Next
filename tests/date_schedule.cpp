#include "Core/DateSchedule/DateSchedule.h"
#include "Core/ProfileValidation/ProfileOperations.h"
#include "test_profile.h"

#include <QtTest>

namespace {
QDate futureMonday()
{
    const QDate today = QDate::currentDate();
    // The adjustment APIs only accept today or a future date.
    return today.addDays(15 - today.dayOfWeek());
}
}

class DateScheduleTest : public QObject
{
    Q_OBJECT

private slots:
    void semesterWeeks_data();
    void semesterWeeks();
    void ambiguousAutomaticMatchAllowsManualSelection();
    void rescheduleAndRestoreEmptyDay();
    void sameDaySwapAndSubstitutionKeepTimeSlots();
    void crossDaySwapCanRestoreOneEndpoint();
};

void DateScheduleTest::semesterWeeks_data()
{
    QTest::addColumn<QDate>("date");
    QTest::addColumn<QString>("expectedId");
    QTest::newRow("before-semester-week") << QDate(2026, 8, 30) << QString();
    QTest::newRow("monday-before-midweek-semester-start") << QDate(2026, 8, 31) << QStringLiteral("odd");
    QTest::newRow("sunday-of-first-week") << QDate(2026, 9, 6) << QStringLiteral("odd");
    QTest::newRow("monday-of-second-week") << QDate(2026, 9, 7) << QStringLiteral("even");
    QTest::newRow("monday-of-third-week") << QDate(2026, 9, 14) << QStringLiteral("odd");
}

void DateScheduleTest::semesterWeeks()
{
    QFETCH(QDate, date);
    QFETCH(QString, expectedId);
    Profile profile = TestProfiles::regular();
    profile.semesterStartDate = QDate(2026, 9, 2);
    profile.schedules[0].id = QStringLiteral("odd");
    profile.schedules[0].mode = WeekScheduleMode::Odd;
    WeekSchedule even = profile.schedules.first();
    even.id = QStringLiteral("even");
    even.mode = WeekScheduleMode::Even;
    profile.schedules.append(even);

    const auto result = DateSchedule::resolve(profile, date);
    QVERIFY2(result.valid, qPrintable(result.error));
    QCOMPARE(result.sourceWeekScheduleId, expectedId);
    QCOMPARE(result.sourceWeekday, date.dayOfWeek());
}

void DateScheduleTest::ambiguousAutomaticMatchAllowsManualSelection()
{
    Profile profile = TestProfiles::regular();
    WeekSchedule other = profile.schedules.first();
    other.id = QStringLiteral("other-week");
    other.getDaySchedule(0).classes[0].subject = TestProfiles::science;
    profile.schedules.append(other);
    const QDate monday(2026, 8, 31);

    const auto automatic = DateSchedule::resolve(profile, monday);
    QVERIFY(!automatic.valid);
    QVERIFY(!automatic.error.isEmpty());
    QVERIFY(automatic.classes.isEmpty());

    profile.activeWeekScheduleId = other.id;
    const auto selected = DateSchedule::resolve(profile, monday);
    QVERIFY2(selected.valid, qPrintable(selected.error));
    QCOMPARE(selected.sourceWeekScheduleId, other.id);
    QCOMPARE(selected.classes.size(), 2);
    QCOMPARE(selected.classes[0].subject, TestProfiles::science);

    const auto explicitSelection = DateSchedule::resolve(profile, monday, TestProfiles::weekId);
    QVERIFY2(explicitSelection.valid, qPrintable(explicitSelection.error));
    QCOMPARE(explicitSelection.sourceWeekScheduleId, TestProfiles::weekId);
}

void DateScheduleTest::rescheduleAndRestoreEmptyDay()
{
    Profile profile = TestProfiles::regular();
    const QDate monday = futureMonday();
    QString error;
    QVERIFY2(ProfileOperations::reschedule(profile, monday, TestProfiles::weekId, 2, &error), qPrintable(error));
    const auto tuesdayTable = DateSchedule::resolve(profile, monday);
    QVERIFY2(tuesdayTable.valid, qPrintable(tuesdayTable.error));
    QVERIFY(tuesdayTable.special);
    QCOMPARE(tuesdayTable.sourceWeekday, 2);
    QCOMPARE(tuesdayTable.classes.size(), 1);
    QCOMPARE(tuesdayTable.classes[0].subject, TestProfiles::science);

    QVERIFY2(ProfileOperations::reschedule(profile, monday, TestProfiles::weekId, 3, &error), qPrintable(error));
    const auto emptyTable = DateSchedule::resolve(profile, monday);
    QVERIFY2(emptyTable.valid, qPrintable(emptyTable.error));
    QVERIFY(emptyTable.special);
    QVERIFY(emptyTable.classes.isEmpty());
    QCOMPARE(profile.dateOverrides.size(), 1);

    QVERIFY2(ProfileOperations::restore(profile, monday, &error), qPrintable(error));
    const auto restored = DateSchedule::resolve(profile, monday);
    QVERIFY2(restored.valid, qPrintable(restored.error));
    QVERIFY(!restored.special);
    QCOMPARE(restored.classes.size(), 2);
}

void DateScheduleTest::sameDaySwapAndSubstitutionKeepTimeSlots()
{
    Profile profile = TestProfiles::regular();
    const QDate monday = futureMonday();
    QString error;
    QVERIFY2(ProfileOperations::swap(profile, monday, {}, 0, monday, {}, 1, &error), qPrintable(error));
    const auto swapped = DateSchedule::resolve(profile, monday);
    QVERIFY2(swapped.valid, qPrintable(swapped.error));
    QVERIFY(swapped.special);
    QCOMPARE(swapped.classes.size(), 2);
    QCOMPARE(swapped.classes[0].subject, TestProfiles::chinese);
    QCOMPARE(swapped.classes[1].subject, TestProfiles::math);

    QVERIFY2(ProfileOperations::substitute(profile, monday, {}, 0, TestProfiles::science, &error), qPrintable(error));
    const auto edited = DateSchedule::resolve(profile, monday);
    QVERIFY2(edited.valid, qPrintable(edited.error));
    QVERIFY(edited.special);
    QCOMPARE(edited.classes.size(), 2);
    QCOMPARE(edited.classes[0].subject, TestProfiles::science);
    QCOMPARE(edited.classes[1].subject, TestProfiles::math);
    QCOMPARE(edited.classes[0].startTime, QTime(8, 0));
    QCOMPARE(edited.classes[0].endTime, QTime(8, 45));
    QCOMPARE(edited.classes[1].startTime, QTime(8, 55));
    QCOMPARE(edited.classes[1].endTime, QTime(9, 40));
    const auto normal = DateSchedule::resolve(profile, monday.addDays(7));
    QCOMPARE(normal.classes.size(), 2);
    QCOMPARE(normal.classes[0].subject, TestProfiles::math);
}

void DateScheduleTest::crossDaySwapCanRestoreOneEndpoint()
{
    Profile profile = TestProfiles::regular();
    const QDate monday = futureMonday();
    const QDate tuesday = monday.addDays(1);
    QString error;
    QVERIFY2(ProfileOperations::swap(profile, monday, {}, 0, tuesday, {}, 0, &error), qPrintable(error));
    QCOMPARE(profile.dateOverrides.size(), 2);
    const auto left = DateSchedule::resolve(profile, monday);
    const auto right = DateSchedule::resolve(profile, tuesday);
    QVERIFY2(left.valid, qPrintable(left.error));
    QVERIFY2(right.valid, qPrintable(right.error));
    QVERIFY(left.special);
    QVERIFY(right.special);
    QCOMPARE(left.classes.size(), 2);
    QCOMPARE(right.classes.size(), 1);
    QCOMPARE(left.classes[0].subject, TestProfiles::science);
    QCOMPARE(left.classes[0].startTime, QTime(8, 0));
    QCOMPARE(right.classes[0].subject, TestProfiles::math);
    QCOMPARE(right.classes[0].startTime, QTime(10, 0));

    QVERIFY2(ProfileOperations::restore(profile, monday, &error), qPrintable(error));
    QCOMPARE(profile.dateOverrides.size(), 1);
    const auto restored = DateSchedule::resolve(profile, monday);
    QVERIFY(!restored.special);
    QCOMPARE(restored.classes.size(), 2);
    QCOMPARE(restored.classes[0].subject, TestProfiles::math);
    const auto kept = DateSchedule::resolve(profile, tuesday);
    QVERIFY(kept.special);
    QCOMPARE(kept.classes.size(), 1);
    QCOMPARE(kept.classes[0].subject, TestProfiles::math);
}

QTEST_GUILESS_MAIN(DateScheduleTest)
#include "date_schedule.moc"
