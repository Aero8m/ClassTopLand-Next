#include "Core/CourseRefreshService/CourseRefreshService.h"
#include "Core/ProfileManager/ProfileManager.h"
#include "test_profile.h"

#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

namespace {
using Phase = CourseRefreshService::Phase;
using Event = CourseRefreshService::Event;
using EventType = CourseRefreshService::EventType;

const QDate monday(2026, 8, 31);

QDateTime at(QTime time, const QDate& date = monday)
{
    return QDateTime(date, time, QTimeZone::UTC);
}
}

class CourseRefreshTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void phasesAndCountdown_data();
    void phasesAndCountdown();
    void boundaryEventsAreDeliveredOnce();
    void temporaryScheduleExpiresAtMidnight();

private:
    QTemporaryDir directory_;
};

void CourseRefreshTest::init()
{
    QVERIFY2(directory_.isValid(), qPrintable(directory_.errorString()));
    const QString fileName = QStringLiteral("%1-%2.json")
        .arg(QString::fromLatin1(QTest::currentTestFunction()), QString::fromLatin1(QTest::currentDataTag()));
    const QString path = directory_.filePath(fileName);
    auto& manager = ProfileManager::instance();
    QString error;
    QVERIFY2(manager.load(path, &error), qPrintable(error));
    QVERIFY2(manager.commit(TestProfiles::regular(), &error), qPrintable(error));
}

void CourseRefreshTest::phasesAndCountdown_data()
{
    QTest::addColumn<QDate>("date");
    QTest::addColumn<QTime>("time");
    QTest::addColumn<Phase>("phase");
    QTest::addColumn<int>("current");
    QTest::addColumn<int>("next");
    QTest::addColumn<QTime>("target");
    QTest::addColumn<qint64>("remaining");
    QTest::addColumn<qint64>("duration");

    QTest::newRow("no-classes") << monday.addDays(2) << QTime(8, 0) << Phase::NoClasses
                               << -1 << -1 << QTime() << qint64(0) << qint64(0);
    QTest::newRow("just-before-start") << monday << QTime(7, 59, 59, 900) << Phase::BeforeFirstClass
                                     << -1 << 0 << QTime(8, 0) << qint64(1) << qint64(0);
    QTest::newRow("at-start") << monday << QTime(8, 0) << Phase::InClass
                            << 0 << 1 << QTime(8, 45) << qint64(2700) << qint64(2700);
    QTest::newRow("just-before-end") << monday << QTime(8, 44, 59) << Phase::InClass
                                   << 0 << 1 << QTime(8, 45) << qint64(1) << qint64(2700);
    QTest::newRow("at-end") << monday << QTime(8, 45) << Phase::Break
                          << -1 << 1 << QTime(8, 55) << qint64(600) << qint64(600);
    QTest::newRow("next-start") << monday << QTime(8, 55) << Phase::InClass
                              << 1 << -1 << QTime(9, 40) << qint64(2700) << qint64(2700);
    QTest::newRow("after-last-class") << monday << QTime(9, 40) << Phase::Finished
                                    << -1 << -1 << QTime() << qint64(0) << qint64(0);
}

void CourseRefreshTest::phasesAndCountdown()
{
    QFETCH(QDate, date);
    QFETCH(QTime, time);
    QFETCH(Phase, phase);
    QFETCH(int, current);
    QFETCH(int, next);
    QFETCH(QTime, target);
    QFETCH(qint64, remaining);
    QFETCH(qint64, duration);

    CourseRefreshService service([&] { return at(time, date); });
    service.refresh();
    QVERIFY2(service.table().valid, qPrintable(service.table().error));
    const auto& state = service.state();
    QCOMPARE(state.tableRevision, service.table().revision);
    QCOMPARE(state.phase, phase);
    QCOMPARE(state.currentClassIndex, current);
    QCOMPARE(state.nextClassIndex, next);
    if (target.isValid())
        QCOMPARE(state.countdownTarget, at(target, date));
    else
        QVERIFY(!state.countdownTarget.isValid());
    QCOMPARE(state.remainingSeconds, remaining);
    QCOMPARE(state.durationSeconds, duration);
}

void CourseRefreshTest::boundaryEventsAreDeliveredOnce()
{
    QDateTime now = at(QTime(7, 57, 59));
    CourseRefreshService service([&] { return now; });
    QSignalSpy events(&service, &CourseRefreshService::eventOccurred);
    QVERIFY(events.isValid());
    const auto refreshAt = [&](QTime time) {
        now = at(time);
        service.refresh();
    };

    service.refresh();
    QVERIFY(events.isEmpty());
    refreshAt(QTime(7, 58));
    QCOMPARE(events.size(), 1);
    refreshAt(QTime(7, 59, 59));
    refreshAt(QTime(8, 0));
    QCOMPARE(events.size(), 2);
    refreshAt(QTime(8, 0));
    refreshAt(QTime(7, 59, 59));
    refreshAt(QTime(8, 0));
    QCOMPARE(events.size(), 2);
    refreshAt(QTime(8, 44, 59));
    refreshAt(QTime(8, 45));
    QCOMPARE(events.size(), 3);

    const QList<EventType> types{EventType::ClassStartingSoon, EventType::ClassStarted, EventType::ClassEnded};
    const QList<QTime> times{QTime(7, 58), QTime(8, 0), QTime(8, 45)};
    for (qsizetype index = 0; index < events.size(); ++index) {
        const auto event = events.at(index).first().value<Event>();
        QCOMPARE(event.type, types.at(index));
        QCOMPARE(event.scheduledTime, at(times.at(index)));
        QCOMPARE(event.classIndex, 0);
        QCOMPARE(event.subject, TestProfiles::math);
    }
}

void CourseRefreshTest::temporaryScheduleExpiresAtMidnight()
{
    QDateTime now = at(QTime(23, 59, 59));
    CourseRefreshService service([&] { return now; });
    DaySchedule temporary(QStringLiteral("临时课表"), 1);
    temporary.classes = {{TestProfiles::chinese, QTime(8, 0), QTime(9, 0)}};
    service.setTemporarySchedule(temporary);
    QCOMPARE(service.table().name, temporary.name);
    QCOMPARE(service.table().classes.size(), 1);
    QCOMPARE(service.table().classes.first().subject, TestProfiles::chinese);
    const quint64 revision = service.table().revision;

    const QDate tuesday = monday.addDays(1);
    now = at(QTime(0, 0), tuesday);
    service.refresh();
    QVERIFY2(service.table().valid, qPrintable(service.table().error));
    QCOMPARE(service.table().date, tuesday);
    QCOMPARE(service.table().sourceWeekday, 2);
    QVERIFY(service.table().revision > revision);
    QCOMPARE(service.table().classes.size(), 1);
    QCOMPARE(service.table().classes.first().subject, TestProfiles::science);
    QCOMPARE(service.state().countdownTarget, at(QTime(10, 0), tuesday));
}

QTEST_GUILESS_MAIN(CourseRefreshTest)
#include "course_refresh.moc"
