#pragma once

#include "AgentModel.h"
#include "AgentPlatform.h"
#include "AgentFiles.h"
#include "../Plugins/PluginManager.h"
#include "../Plugins/PluginPreview.h"
#include <QHash>
#include <QElapsedTimer>
#include <atomic>
#include <functional>
#include <memory>

class QThread;

class AgentService final : public QObject
{
    Q_OBJECT
public:
    enum class Permission { ReadOnly, Ask, Yolo };
    struct Session
    {
        QString id, title, connection;
        QJsonArray events;
        QString model, workspace;
        Permission permission = Permission::Ask;
        QString reasoningEffort;
        bool workspaceChosen = false;
        QString plugin;
        QJsonArray plan;
        bool resumable = false;
        bool titleCustomized = false;
    };
    struct Review
    {
        PluginManager::Package package;
        QString session, beforeManifest, beforeScript, manifest, script;
        QJsonObject beforePermissions;
        QByteArray baseline;
        bool installed = false;
    };

    AgentService(const QString &dataDirectory, PluginManager *plugins, QObject *parent = nullptr);
    ~AgentService() override;
    const QList<Session> &sessions() const { return sessions_; }
    const Session *session(const QString &id) const;
    QString createSession(const QString &provider = {}, const QString &model = {}, const QString &workspace = {},
                          Permission permission = Permission::Ask);
    void renameSession(const QString &id, const QString &title);
    void deleteSession(const QString &id);
    void deleteSessions(const QStringList &ids);
    bool regenerateTitle(const QString &id, QString *error = nullptr);
    void setModel(const QString &session, const QString &provider, const QString &model);
    void setWorkspace(const QString &session, const QString &workspace);
    void setPermission(const QString &session, Permission permission);
    void setReasoningEffort(const QString &session, const QString &effort);
    void clearHistory();
    bool saveHistory() const { return saveHistory_; }
    bool setSaveHistory(bool enabled, QString *error);
    QList<AgentProvider> providers() const;
    AgentConnection resolveModel(const QString &provider, const QString &model) const;
    AgentConnection defaultModel() const;
    bool setDefaultModel(const QString &provider, const QString &model, QString *error);
    bool saveProvider(AgentProvider &provider, const QString &key, QString *error);
    bool removeProvider(const QString &id, QString *error);
    QStringList workspaces() const;
    QString defaultWorkspace() const;
    bool setWorkspaces(const QStringList &paths, const QString &defaultPath, QString *error);
    QString storedKey(const AgentConnection &connection) const;
    bool testConnection(const AgentConnection &connection, const QString &key, bool tool, QString *error);
    void cancelTest();

    bool start(const QString &session, const QString &prompt, QString *error);
    bool busy() const { return !active_.isEmpty(); }
    QString activeSession() const { return active_; }
    const QString &liveText() const { return liveText_; }
    const QString &liveReasoning() const { return liveReasoning_; }
    QString reasoningText(const QString &session, const QString &reasoningId) const;
    const QList<QJsonObject> &liveTools() const { return liveTools_; }
    QString migrationError() const { return migrationError_; }
    void stop();
    void decide(const QString &tool, bool allow);
    bool continueSession(const QString &session, QString *error);
    PluginWorkspace *pluginWorkspace() const { return plugins_->workspace(); }
    bool rollbackPlugin(const QString &plugin, const QString &version, PluginManager::PublishCompletion done, QString *error);
    bool recoverDraft(const QString &session, QString *error);

    QString draftName(const QString &session) const;
    QStringList draftSessions() const;
    Review review(const QString &session, QString *error) const;
    bool canPublish() const { return !plugins_->recovery() && plugins_->canPublish() && AgentPlatform::canExecute(); }
    bool canPublish(const QString &session) const;

signals:
    void sessionsChanged();
    void configurationChanged();
    void eventAdded(const QString &session, const QJsonObject &event);
    void textReceived(const QString &session, const QString &text);
    void reasoningReceived(const QString &session, const QString &text);
    void toolUpdated(const QString &session, const QJsonObject &tool);
    void busyChanged(bool busy);
    void storageError(const QString &message);
    void testFinished(const QString &error);
    void draftChanged();
    void titleGenerated(const QString &session, const QString &error);

private:
    Session *findSession(const QString &id);
    void persistSessions();
    void appendEvent(const QString &kind, const QString &text, const QString &tool = {},
                     const QString &reasoningId = {}, const QString &detail = {}, const QString &output = {}, bool final = false);
    void requestStep();
    void refreshTools();
    void finalizePlugins();
    void finalizeNextPlugin();
    bool preparePluginWrite(QString *error, const QString &plugin = {});
    QString pluginForPath(const QString &path) const;
    bool writableTarget(const QString &path, QString *error) const;
    void authorizeFileAccess(const QStringList &paths, bool write, const QString &detail,
                             const QStringList &plugins, std::function<void(bool)> action);
    void authorizeFilePlugins(QStringList plugins, const QString &detail, std::function<void(bool)> action);
    void authorizeWrite(const QString &detail, std::function<void(bool)> action);
    void authorizePlugin(const QString &plugin, const QString &detail, std::function<void(bool)> action);
    void saveContinuation();
    QString recoverySummary() const;
    void recordPluginChange(const QString &plugin, const QString &version, const QString &message);
    void modelCompleted(const QJsonObject &message, const QString &error);
    void requestTitle(const QString &id, const QString &firstPrompt, const QString &fallback,
                      const AgentConnection &connection, const QString &key, bool manual = false);
    void cancelTitleRequest(const QString &id = {});
    void titleCompleted(const QJsonObject &reply, const QString &error);
    void nextTool();
    void runTool(const QString &name, const QJsonObject &args);
    void startSearch(const QJsonObject &args, bool contents);
    void cancelSearch();
    bool toolAllowed(const QString &name) const;
    void updateTool(const QString &state, const QString &detail = {}, const QString &output = {});
    void ask(const QString &title, const QString &detail, std::function<void(bool)> action, bool required = false);
    void sendResult(const QString &output, const QString &error = {});
    void complete(const QString &status, bool success);
    QJsonObject draft(const QString &session) const;
    bool writeDraft(const QString &session, const QJsonObject &draft, QString *error) const;

    QString directory_;
    QString migrationError_;
    PluginManager *plugins_;
    AgentModel model_, tester_, titleModel_;
    AgentPlatform::Command command_;
    AgentFiles files_;
    PluginPreview preview_;
    QString activeAction_;
    QMap<QString, QString> checkpoints_;
    QHash<QString, QByteArray> pluginFingerprints_;
    QSet<QString> authorizedPlugins_;
    QStringList finalizeQueue_;
    QString workspaceForTurn_;
    QStringList readRoots_, writeRoots_, deniedReadRoots_, deniedWriteRoots_;
    QMap<QString, QString> pluginDirectories_;
    QString finalText_, commandOutput_;
    bool finalizing_ = false, finishing_ = false;
    quint64 generation_ = 0;
    QList<Session> sessions_;
    bool saveHistory_ = true, historyReadable_ = true;
    QString active_, directoryForTurn_, key_, callId_, toolName_;
    Permission permission_ = Permission::Ask;
    QString liveText_, liveReasoning_;
    QHash<QString, QList<QPair<QString, QString>>> reasoning_;
    QList<QJsonObject> liveTools_;
    AgentConnection connection_;
    QJsonArray toolSchemas_, history_, turn_, pendingCalls_;
    int callIndex_ = 0;
    QElapsedTimer turnClock_;
    std::function<void(bool)> approval_;
    struct Context
    {
        QString route;
        QList<QJsonArray> turns;
    };
    QHash<QString, Context> contexts_;
    struct Test
    {
        AgentConnection connection;
        QString key, session;
        QJsonArray messages;
        bool needsTool = false;
    };
    std::unique_ptr<Test> test_;
    struct TitleTask
    {
        QString sessionId;
        QString fallback;
        bool manual = false;
    };
    std::unique_ptr<TitleTask> titleTask_;
    std::shared_ptr<std::atomic_bool> searchCancel_;
    QThread *searchThread_ = nullptr;
};

