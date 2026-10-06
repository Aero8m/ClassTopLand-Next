#pragma once

#include "../Core/Model/Profile.h"
#include <QObject>
#include <QStringList>

class ProfileEditSession final : public QObject
{
    Q_OBJECT
public:
    enum class Page { Info, Subjects, TimeLines, Schedules };
    struct ValidationError {
        QString message;
        Page page = Page::Info;
        int item = -1;
        int row = -1;
        int weekday = 1;
        explicit operator bool() const { return !message.isEmpty(); }
    };

    explicit ProfileEditSession(QObject* parent = nullptr);
    const Profile& draft() const { return draft_; }
    Profile& edit() { return draft_; }
    bool isDirty() const { return dirty_; }
    void notifyChanged();
    void reset(const Profile& profile);
    void markSaved();
    void discard();
    // Apply a separately persisted tray selection without resetting draft edits.
    void syncActiveWeekSchedule(const QString& id);
    void syncDateOverrides(const QList<DateScheduleOverride>& records);
    bool updateSubject(int index, const Subject& subject, QString* error = nullptr);
    bool removeSubject(int index, QString* error = nullptr);
    bool replaceTimePoints(int lineIndex, const QList<TimeLinePoint>& points, QString* error = nullptr);
    static bool validateTimePoints(const QList<TimeLinePoint>& points, QString* error = nullptr);
    bool applyTimeLine(int weekIndex, int weekday, int lineIndex, QString* error = nullptr);
    static ValidationError validate(const Profile& profile);
    QStringList warnings() const;

signals:
    void changed();
    void draftReset();

private:
    Profile draft_;
    Profile saved_;
    bool dirty_ = false;
};
