#pragma once

#include "../Core/Model/Profile.h"
#include <QObject>
#include "../Core/ProfileValidation/ProfileValidation.h"
#include <QStringList>
#include <QHash>

class ProfileEditSession final : public QObject
{
    Q_OBJECT
public:
    using Page = ProfileValidation::Page;
    using ValidationError = ProfileValidation::ValidationError;

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
    void syncReminderRules(const QList<ReminderRule>& rules);
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
    QHash<QString, QString> subjectRenames_;
};
