#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTime>
#include <QUuid>

struct ReminderConfig
{
    bool enable = true;
    bool classStarted = true;
    bool classEnded = true;
    bool startingSoon = true;
    int advanceMinutes = 2;
    QString startedText = QStringLiteral("{科目} 已经上课，请回到座位");
    QString endedText = QStringLiteral("{科目} 已经下课，请做好下节课上课准备");
    QString soonText = QStringLiteral("{科目} 即将上课，请做好上课准备");
};

struct ReminderRule
{
    enum class Course { Any, Subject, Period };
    enum class Timing { Fixed, Start, End };
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString name;
    bool enabled = true;
    QString text;
    QList<int> weekdays{1, 2, 3, 4, 5, 6, 7};
    Course course = Course::Any;
    QString subject;
    int period = 1;
    Timing timing = Timing::Fixed;
    QTime time{8, 0};
    int offsetMinutes = 0;
};

namespace Reminders {
QString render(QString text, const QString& subject, int period);
bool validate(const ReminderConfig& config, QString* error = nullptr);
bool validate(const QList<ReminderRule>& rules, QString* error = nullptr);
QJsonObject toJson(const ReminderConfig& config);
QJsonArray toJson(const QList<ReminderRule>& rules);
bool fromJson(const QJsonValue& value, ReminderConfig& config, QString* error);
bool fromJson(const QJsonValue& value, QList<ReminderRule>& rules, QString* error);
void renameSubject(QList<ReminderRule>& rules, const QString& oldName, const QString& newName);
}
