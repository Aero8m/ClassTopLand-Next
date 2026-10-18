#pragma once
#include "PluginManifest.h"
#include <QObject>
#include <QHash>
#include <QPointer>
#include <QJsonValue>
#include <memory>
class QNetworkAccessManager;
class QNetworkReply;
class PluginIo final : public QObject
{
    Q_OBJECT
  public:
    explicit PluginIo(const QString &directory) : directory_(directory) {}
    void attach(const QString &id, quint64 generation, const QJsonObject &permissions);
    void detach(const QString &id, quint64 generation);
    void request(const QString &id, quint64 generation, const QString &requestId, const QString &method,
                 const QJsonObject &args);
    void cancel(const QString &id, quint64 generation, const QString &requestId);
  signals:
    void completed(const QString &id, quint64 generation, const QString &requestId, const QJsonObject &error,
                   const QJsonValue &result);

  private:
    struct Session
    {
        quint64 generation = 0;
        QJsonObject permissions;
        QNetworkAccessManager *network = nullptr;
    };
    struct Transfer
    {
        QString id, requestId;
        quint64 generation;
        QJsonObject options;
        int redirects = 0;
        QByteArray body;
        QPointer<QNetworkReply> reply;
    };
    void http(const std::shared_ptr<Transfer> &transfer);
    QString key(const QString &id, quint64 generation, const QString &requestId) const;
    QHash<QString, Session> sessions_;
    QHash<QString, std::shared_ptr<Transfer>> transfers_;
    QString directory_;
};
