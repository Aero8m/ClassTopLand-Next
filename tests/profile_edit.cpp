#include "ProfileEditWindow/ProfileEditSession.h"
#include "test_profile.h"

#include <QtTest>

class ProfileEditTest : public QObject
{
    Q_OBJECT

private slots:
    void rejectsDuplicateSubjectsAndInvalidScheduleEntries();
    void rejectsOverlappingCoursesAndTimelinePoints();
    void renameUpdatesCourseAndReminderReferences();
    void saveAndDiscardKeepSeparateDrafts();
    void cannotSaveDeletionAfterDateOverrideReferencesSubject();
};

void ProfileEditTest::rejectsDuplicateSubjectsAndInvalidScheduleEntries()
{
    using Page = ProfileEditSession::Page;
    const Profile valid = TestProfiles::regular();
    QVERIFY(!ProfileEditSession::validate(valid));

    Profile invalid = valid;
    invalid.subjects.append(Subject(TestProfiles::math));
    auto error = ProfileEditSession::validate(invalid);
    QVERIFY(error);
    QCOMPARE(error.page, Page::Subjects);
    QCOMPARE(error.item, 3);

    invalid = valid;
    invalid.schedules[0].getDaySchedule(0).classes[0].subject.clear();
    error = ProfileEditSession::validate(invalid);
    QVERIFY(error);
    QCOMPARE(error.page, Page::Schedules);
    QCOMPARE(error.item, 0);
    QCOMPARE(error.weekday, 1);
    QCOMPARE(error.row, 0);
}

void ProfileEditTest::rejectsOverlappingCoursesAndTimelinePoints()
{
    using Page = ProfileEditSession::Page;
    Profile profile = TestProfiles::regular();
    profile.schedules[0].getDaySchedule(0).classes[1].startTime = QTime(8, 30);
    auto error = ProfileEditSession::validate(profile);
    QVERIFY(error);
    QCOMPARE(error.page, Page::Schedules);
    QCOMPARE(error.weekday, 1);
    QCOMPARE(error.row, 1);

    profile = TestProfiles::regular();
    TimeLine line(QStringLiteral("上午"));
    line.timePoints = {{QTime(8, 0), QTime(8, 45)}, {QTime(8, 30), QTime(9, 0)}};
    profile.timeLines.append(line);
    error = ProfileEditSession::validate(profile);
    QVERIFY(error);
    QCOMPARE(error.page, Page::TimeLines);
    QCOMPARE(error.item, 0);
    QCOMPARE(error.row, 1);
}

void ProfileEditTest::renameUpdatesCourseAndReminderReferences()
{
    Profile profile = TestProfiles::regular();
    const auto classes = profile.schedules.first().daySchedules.first().classes;
    profile.dateOverrides.append({QDate(2026, 8, 31), TestProfiles::weekId, 1, classes, classes});
    ReminderRule rule;
    rule.id = QStringLiteral("math-reminder");
    rule.name = QStringLiteral("上课提醒");
    rule.text = QStringLiteral("{科目}");
    rule.course = ReminderRule::Course::Subject;
    rule.subject = TestProfiles::math;
    rule.timing = ReminderRule::Timing::Start;
    profile.reminderRules.append(rule);

    ProfileEditSession session;
    session.reset(profile);
    const QString renamed = QStringLiteral("应用数学");
    QString error;
    QVERIFY2(session.updateSubject(0, Subject(renamed), &error), qPrintable(error));
    const auto& draft = session.draft();
    QVERIFY(session.isDirty());
    QCOMPARE(draft.schedules.first().daySchedules.first().classes.first().subject, renamed);
    QCOMPARE(draft.dateOverrides.first().classes.first().subject, renamed);
    QCOMPARE(draft.dateOverrides.first().sourceClasses.first().subject, TestProfiles::math);
    QCOMPARE(draft.reminderRules.first().subject, renamed);
    QCOMPARE(profile.subjects.first().name, TestProfiles::math);
    QVERIFY(!session.validateForSave());

    QVERIFY(!session.removeSubject(0, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(session.draft().subjects.first().name, renamed);
}

void ProfileEditTest::saveAndDiscardKeepSeparateDrafts()
{
    const auto original = TestProfiles::regular();
    ProfileEditSession session;
    session.reset(original);
    QVERIFY(!session.isDirty());

    session.edit().name = QStringLiteral("Saved name");
    session.notifyChanged();
    QVERIFY(session.isDirty());
    session.markSaved();
    QVERIFY(!session.isDirty());

    session.edit().name = QStringLiteral("Unsaved name");
    session.notifyChanged();
    QVERIFY(session.isDirty());
    session.syncActiveWeekSchedule(TestProfiles::weekId);
    QCOMPARE(session.draft().name, QStringLiteral("Unsaved name"));
    QVERIFY(session.isDirty());
    session.discard();
    QVERIFY(!session.isDirty());
    QCOMPARE(session.draft().name, QStringLiteral("Saved name"));
    QCOMPARE(session.draft().activeWeekScheduleId, TestProfiles::weekId);
}

void ProfileEditTest::cannotSaveDeletionAfterDateOverrideReferencesSubject()
{
    Profile profile = TestProfiles::regular();
    profile.schedules[0].getDaySchedule(1).classes.clear();
    const auto initialClasses = profile.schedules.first().daySchedules.first().classes;
    auto adjustedClasses = initialClasses;
    adjustedClasses[0].subject = TestProfiles::science;
    const DateScheduleOverride record{QDate(2026, 8, 31), TestProfiles::weekId, 1, initialClasses, adjustedClasses};

    ProfileEditSession session;
    session.reset(profile);
    QString error;
    QVERIFY2(session.removeSubject(2, &error), qPrintable(error));
    QVERIFY(!session.validateForSave());
    session.syncDateOverrides({record});

    const auto invalid = session.validateForSave();
    QVERIFY(invalid);
    QCOMPARE(invalid.page, ProfileEditSession::Page::Subjects);
    QVERIFY(session.isDirty());
    QCOMPARE(session.draft().subjects.size(), 2);
    session.discard();
    QVERIFY(!session.isDirty());
    QVERIFY(!session.validateForSave());
    QCOMPARE(session.draft().subjects.size(), 3);
    QCOMPARE(session.draft().dateOverrides.size(), 1);
    QCOMPARE(session.draft().dateOverrides.first().classes.first().subject, TestProfiles::science);
    QVERIFY(!session.removeSubject(2, &error));
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(ProfileEditTest)
#include "profile_edit.moc"
