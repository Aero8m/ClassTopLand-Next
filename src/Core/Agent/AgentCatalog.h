#pragma once

#include "AgentModel.h"

class AgentCatalog final : public QObject
{
    Q_OBJECT
public:
    explicit AgentCatalog(QObject *parent = nullptr);
    void fetch(const AgentProvider &provider, const QString &key);
    void cancel();

signals:
    void fetched(const QList<AgentModelEntry> &models, bool catalogOnly, const QString &message);

private:
    void request(const QUrl &url, const QString &key, bool official, int limit);
    void officialFinished(const QJsonObject &value, const QString &error);
    void catalogFinished(const QJsonObject &value);
    QNetworkAccessManager network_;
    QNetworkReply *reply_ = nullptr;
    QString catalogId_, officialError_;
    QStringList officialIds_;
    QJsonObject catalogCache_;
    QByteArray buffer_;
};

