#pragma once

#include "CloudApi.h"

#include <QTimer>
#include <memory>

class CloudScreen final : public QObject
{
public:
    explicit CloudScreen(QObject* parent = nullptr);
    ~CloudScreen() override;
    static bool supported();
    void configure(const QString& origin, const QByteArray& bearer);
    void stop();

private:
    struct Session;
    void poll();
    void begin(const QJsonObject& target);
    void frame();
    void end(const QString& reason = {});
    void reportFailure(const QString& id, const QString& reason);

    CloudApi api_;
    QTimer pollTimer_;
    QTimer frameTimer_;
    QString origin_;
    QByteArray bearer_;
    QString lastRequestId_;
    bool polling_ = false;
    std::unique_ptr<Session> session_;
};
