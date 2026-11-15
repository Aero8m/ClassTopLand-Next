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
class QTemporaryDir;
class PluginWorkspace;
class PluginActionRegistry;
class PluginManager final : public QObject
{
    Q_OBJECT
  public:
    struct Record
    {
        Plugins::Manifest manifest;
        QString key, source, state = QStringLiteral("已停用"), failure;
        QStringList logs;
        quint64 generation = 0;
        std::unique_ptr<PluginRuntime> runtime;
        QMap<QString, QJsonObject> pages, components, actions;
        QSet<QString> subscriptions;
        qint64 lastNotification = 0;
        bool updatePending = false;
        bool stopFault = false;
        ~Record();
    };
    PluginManager(const QString &directory, bool recovery, QObject *parent = nullptr);
    ~PluginManager() override;
    void start();
    void setPreviewMode(bool value) { previewMode_=value; }
    void scan();
    QList<std::shared_ptr<Record>> records() const
    {
        return records_.values();
    }
    std::shared_ptr<Record> record(const QString &id) const
    {
        return records_.value(id);
    }
    bool setEnabled(const QString &id, bool enabled, QString *error, bool prepared = false);
    struct Package
    {
        Plugins::Manifest manifest;
        std::shared_ptr<QTemporaryDir> directory;
        QString source;
    };
    Package stagePackage(const QString &source, QString *error) const;
    using PublishCompletion = std::function<void(const QString &)>;
    enum class PublishMode { PreserveEnabled, EnableOnFirstInstall };
    bool publish(const Package &package, PublishMode mode, PublishCompletion completion, QString *error);
    bool publishing() const { return bool(publication_); }
    bool canPublish() const;
    PluginWorkspace *workspace() const { return workspace_; }
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
    using GuardCompletion = std::function<void(bool)>;
    std::function<void(const QString &, GuardCompletion)> preparePluginStop;
    using Completion = std::function<void(const QJsonObject &, const QJsonValue &)>;
    std::function<void(const QString &, const QString &, const QJsonObject &, Completion)> uiRequest;
    std::function<bool(const QString &, const QJsonObject &)> guardRequest;
    std::function<QJsonObject(const QString &, const QString &)> pageNavigation;
    void setQuiescing(bool value) { quiescing_ = value; }
    bool acceptingWork() const { return !quiescing_ && !writesBlocked_ && !stopped_ && !publication_; }
    PluginActionRegistry *actions() const { return actions_; }
    void viewEvent(const QString &plugin, const QString &name, const QJsonObject &event);
    void dispatchGuard(const QString &instance, const QString &operation, const QString &token);
    CourseRefreshService *courses() const
    {
        return courses_;
    }
    QJsonObject definition(const QString &pluginId, const QString &kind, const QString &id) const;
    QMap<QString, QString> componentTypes() const;
    void createView(const QString &pluginId, const QString &kind, const QString &contribution,
                    const QString &instanceId, const QJsonObject &settings, const QString &componentInstance = {});
    void releaseView(const QString &instanceId);
    void storage(const QString &pluginId, const QString &method, const QJsonObject &args, QObject *context,
                 Completion callback);
  signals:
    void changed();
    void contributionsChanged();
    void pluginStopped(const QString &id, bool fault);
    void notification(const QString &title, const QString &message);
    void guardFinished(const QString &token, const QJsonObject &error);
    void profileWritten(const QJsonArray &canceledDates);
    void restartRequested();

  private:
    void startRecord(const std::shared_ptr<Record> &record);
    void stopRecord(const std::shared_ptr<Record> &record, bool fault);
    void finishStop(const std::shared_ptr<Record> &record);
    bool pin(const std::shared_ptr<Record> &record, bool enable, QString *error);
    void activatePublication();
    void finishPublication(const QString &error);
    Plugins::Manifest installedRevision(const QString &revision) const;
    struct Publication
    {
        Package package;
        std::shared_ptr<Record> previous, candidate;
        PublishCompletion completion;
        QString failure;
        PublishMode mode = PublishMode::PreserveEnabled;
        bool wasRunning = false, prepared = false;
    };
    std::unique_ptr<Publication> publication_;
    void fault(const std::shared_ptr<Record> &record, const QString &message);
    bool contributionRequest(const std::shared_ptr<Record> &record, const QString &id, const QString &method, const QJsonObject &args);
    void request(const std::shared_ptr<Record> &record, const QString &id, const QString &method,
                 const QJsonObject &args);
    void broadcast(const QString &name, const QJsonValue &value);
    QJsonObject courseSnapshot() const;
    QString directory_;
    PluginWorkspace *workspace_;
    PluginActionRegistry *actions_;
    QHash<QString, QString> actionRequests_;
    QMap<QString, std::shared_ptr<Record>> records_;
    QHash<QString, QString> views_;
    QThread ioThread_;
    PluginIo *io_;
    CourseRefreshService *courses_;
    ProfileMutationService mutations_;
    quint64 generation_ = 0, sequence_ = 0, internalRequest_ = 0;
    bool recovery_ = false, writesBlocked_ = false, stopped_ = false, quiescing_ = false, previewMode_ = false;
    struct Pending
    {
        QPointer<QObject> context;
        Completion callback;
    };
    QHash<QString, Pending> internal_;
};
