#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
class ProfileMutationService
{
  public:
    ProfileMutationService();
    QJsonObject snapshot() const;
    const QString &session() const
    {
        return session_;
    }
    QJsonObject apply(const QJsonObject &request, QJsonValue &result);

  private:
    QString session_;
};
