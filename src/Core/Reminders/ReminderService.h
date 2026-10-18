#pragma once

#include "../CourseRefreshService/CourseRefreshService.h"
#include "../Model/ReminderConfig.h"
#include <QTimeZone>

struct ReminderNotification
{
    QString key;
    QString occurrence;
    QString text;
    QDateTime scheduledTime;
    quint64 tableRevision = 0;
    quint64 revision = 0;
};
Q_DECLARE_METATYPE(ReminderNotification)

class ReminderService final : public QObject
{
    Q_OBJECT
public:
    explicit ReminderService(CourseRefreshService* courses, QObject* parent = nullptr);
    ReminderService(CourseRefreshService* courses, CourseRefreshService::Clock clock, QObject* parent = nullptr);
    void start();
    void stop();
    // Pure scheduling logic shared by runtime and deterministic verification.
    static QList<ReminderNotification> schedule(const CourseRefreshService::TableSnapshot& table,
        const ReminderConfig& config, const QList<ReminderRule>& rules, const QTimeZone& zone);
public slots:
    void refresh();
private:
    void reloadSettings();
    void tableChanged();
signals:
    void notification(const ReminderNotification& notification);
    // "*" invalidates all reminder notifications, leaving other sources alone.
    void invalidated(const QStringList& keys, bool dismissCurrent);
private:
    CourseRefreshService* courses_;
    CourseRefreshService::Clock clock_;
    QTimer* timer_;
    ReminderConfig config_;
    QList<ReminderRule> rules_;
    bool barEnabled_ = true;
    QDateTime previous_;
    QSet<QString> delivered_;
    quint64 revision_ = 0;
    CourseRefreshService::TableSnapshot table_;
};
