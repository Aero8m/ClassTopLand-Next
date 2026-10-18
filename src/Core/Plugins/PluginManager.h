#pragma once
#include "PluginManifest.h"
#include "ProfileMutationService.h"
#include "../CourseRefreshService/CourseRefreshService.h"
#include <QThread>
#include <QMap>
#include <QSet>
#include <QJsonArray>
#include <QPointer>
#include <QElapsedTimer>
#include <functional>
#include <memory>
class PluginRuntime;
class PluginIo;
class PluginManager final : public QObject
{
    Q_OBJECT
  public:
    struct Record
    {
        Plugins::Manifest manifest;
        QString key, state = QStringLiteral("已停用"), failure;
        QStringList logs;
        quint64 generation = 0;
        std::unique_ptr<PluginRuntime> runtime;
        QMap<QString, QJsonObject> settingsPages, components, windows;
        QSet<QString> subscriptions;
        qint64 lastNotification = 0;
        qint64 lastUserAction = 0;
        QElapsedTimer uiClock;
        int uiUpdates = 0;
        int uiTrees = 0;
        bool updatePending = false;
        bool stopFault = false;
        ~Record();
    };
    PluginManager(const QString &directory, bool recovery, QObject *parent = nullptr);
    ~PluginManager() override;
    void start();
    void scan();
    QList<std::shared_ptr<Record>> records() const
    {
        return records_.values();
    }
    std::shared_ptr<Record> record(const QString &id) const
    {
        return records_.value(id);
    }
    bool setEnabled(const QString &id, bool enabled, QString *error);
    void stopAll();
    QString pluginsDirectory() const;
    bool recovery() const
    {
        return recovery_;
    }
    bool requestRecovery(QString *error);
    void setWritesBlocked(bool blocked)
    {
        writesBlocked_ = blocked;
    }
    std::function<bool()> editorDirty;
    std::function<bool(const QString &)> preparePluginStop;
    CourseRefreshService *courses() const
    {
        return courses_;
    }
    QJsonObject definition(const QString &pluginId, const QString &kind, const QString &id) const;
    QMap<QString, QString> componentTypes() const;
    void createView(const QString &pluginId, const QString &kind, const QString &contribution,
                    const QString &instanceId, const QJsonObject &settings);
    void releaseView(const QString &instanceId);
    void action(const QString &instanceId, const QString &action, const QJsonValue &value);
    void saved(const QString &pluginId, const QJsonObject &values);
    using Completion = std::function<void(const QJsonObject &, const QJsonValue &)>;
    void storage(const QString &pluginId, const QString &method, const QJsonObject &args, QObject *context,
                 Completion callback);
  signals:
    void changed();
    void contributionsChanged();
    void pluginStopped(const QString &id, bool fault);
    void notification(const QString &title, const QString &message);
    void viewUpdated(const QString &instanceId, const QJsonObject &patch);
    void viewTreeReplaced(const QString &instanceId, const QJsonObject &tree);
    void windowRequested(const QString &pluginId, const QString &contribution);
    void profileWritten(const QJsonArray &canceledDates);
    void restartRequested();

  private:
    void startRecord(const std::shared_ptr<Record> &record);
    void stopRecord(const std::shared_ptr<Record> &record, bool fault);
    void finishStop(const std::shared_ptr<Record> &record);
    void fault(const std::shared_ptr<Record> &record, const QString &message);
    void request(const std::shared_ptr<Record> &record, const QString &id, const QString &method,
                 const QJsonObject &args);
    void broadcast(const QString &name, const QJsonValue &value);
    QJsonObject courseSnapshot() const;
    QString directory_;
    QMap<QString, std::shared_ptr<Record>> records_;
    QHash<QString, QString> views_;
    QThread ioThread_;
    PluginIo *io_;
    CourseRefreshService *courses_;
    ProfileMutationService mutations_;
    quint64 generation_ = 0, sequence_ = 0, internalRequest_ = 0;
    bool recovery_ = false, writesBlocked_ = false, stopped_ = false;
    struct Pending
    {
        QPointer<QObject> context;
        Completion callback;
    };
    QHash<QString, Pending> internal_;
};
