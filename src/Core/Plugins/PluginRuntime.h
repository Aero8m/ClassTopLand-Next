#pragma once
#include "PluginManifest.h"
#include <QObject>
#include <QThread>
#include <QJsonValue>
#include <memory>
#include <QQueue>

class PluginWorker;
class PluginRuntime final : public QObject
{
    Q_OBJECT
  public:
    PluginRuntime(const Plugins::Manifest &manifest, quint64 generation, QObject *parent = nullptr);
    ~PluginRuntime() override;
    void start();
    void stop();
    void beginStop();
    void deliver(const QString &requestId, const QJsonObject &error, const QJsonValue &result);
    void event(const QString &name, const QJsonValue &value);
  signals:
    void started();
    void stopped();
    void failed(const QString &message);
    void request(const QString &requestId, const QString &method, const QJsonObject &arguments);
    void cancelRequest(const QString &requestId);

  private:
    QThread thread_;
    PluginWorker *worker_;
    bool stopping_ = false;
    bool eventPending_ = false;
    QQueue<QPair<QString, QJsonValue>> events_;
    void nextEvent();
};
