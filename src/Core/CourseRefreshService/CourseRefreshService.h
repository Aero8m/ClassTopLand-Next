#ifndef CLASSTOPLAND_NEXT_COURSEREFRESHSERVICE_H
#define CLASSTOPLAND_NEXT_COURSEREFRESHSERVICE_H

#include "../Model/Schedule.h"
#include "../Model/Subject.h"

#include <QDateTime>
#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <functional>
#include <optional>

// All methods must be called in the object's thread. Only QtCore is required.
class CourseRefreshService final : public QObject
{
    Q_OBJECT

public:
    enum class Phase {
        NoClasses,
        BeforeFirstClass,
        InClass,
        Break,
        Finished,
        InvalidTable
    };
    Q_ENUM(Phase)

    enum class EventType { ClassStarted, ClassEnded, ClassStartingSoon };
    Q_ENUM(EventType)

    struct TableSnapshot {
        quint64 revision = 0;
        QDate date;
        int weekScheduleIndex = -1;
        QString name;
        QList<Class> classes;
        QList<Subject> subjects;
        bool valid = true;
        QString error;
    };

    struct State {
        quint64 tableRevision = 0;
        Phase phase = Phase::NoClasses;
        int currentClassIndex = -1;
        int nextClassIndex = -1;
        QDateTime countdownTarget;
        qint64 remainingSeconds = 0;
    };

    struct Event {
        quint64 tableRevision = 0;
        EventType type = EventType::ClassStarted;
        int classIndex = -1;
        QString subject;
        QDateTime scheduledTime;
    };

    using Clock = std::function<QDateTime()>;

    explicit CourseRefreshService(QObject* parent = nullptr);
    // An injectable clock allows deterministic verification of time boundaries.
    explicit CourseRefreshService(Clock clock, QObject* parent = nullptr);

    const TableSnapshot& table() const noexcept { return table_; }
    const State& state() const noexcept { return state_; }
    bool isRunning() const noexcept { return timer_->isActive(); }

    // -1: automatic, unique matching schedule. An explicit index bypasses mode.
    void setWeekScheduleIndex(int index);
    // The containing Monday is week one. Invalid date clears the reference.
    void setWeekReferenceDate(const QDate& date);
    // Temporary selections expire when the actual calendar date changes.
    void setTemporaryWeekday(int weekday);
    void setTemporarySchedule(const DaySchedule& schedule);
    void clearTemporarySelection();

public slots:
    void start();
    void stop();
    // Rebuild from ProfileManager's current in-memory profile after an edit/load.
    // This does not load files or modify the manager's data.
    void reloadTable();
    void refresh();

signals:
    // Connect receivers before start(). Tables are published before their state.
    void tableChanged(const CourseRefreshService::TableSnapshot& table);
    void stateChanged(const CourseRefreshService::State& state);
    void eventOccurred(const CourseRefreshService::Event& event);
    void errorOccurred(const QString& message);

private:
    TableSnapshot resolveTable(const QDate& date) const;
    State calculateState(const QDateTime& now) const;
    QList<Event> detectEvents(const QDateTime& now);
    void reportError(const QString& message);

    QTimer* timer_;
    Clock clock_;
    int weekScheduleIndex_ = -1;
    QDate weekOneMonday_;
    int temporaryWeekday_ = 0;
    std::optional<DaySchedule> temporarySchedule_;
    QDate temporaryDate_;
    TableSnapshot table_;
    State state_;
    bool tableDirty_ = true;
    bool hasState_ = false;
    quint64 revision_ = 0;
    QDateTime previousTime_;
    QSet<quint64> deliveredEvents_;
    QString lastError_;
};

Q_DECLARE_METATYPE(CourseRefreshService::TableSnapshot)
Q_DECLARE_METATYPE(CourseRefreshService::State)
Q_DECLARE_METATYPE(CourseRefreshService::Event)

#endif //CLASSTOPLAND_NEXT_COURSEREFRESHSERVICE_H
