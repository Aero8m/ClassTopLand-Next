#pragma once

#include "PluginManager.h"

// Source packages are editable; published revisions and history packages are immutable.
class PluginWorkspace final : public QObject
{
    Q_OBJECT
public:
    PluginWorkspace(const QString &dataDirectory, PluginManager *manager);
    QString source(const QString &plugin, QString *error);
    bool create(const QString &plugin, QString *error);
    QJsonArray list() const;
    QString checkpoint(const QString &plugin, const QString &session, const QString &summary, QString *error);
    QJsonArray versions(const QString &plugin, QString *error) const;
    QString difference(const QString &plugin, const QString &version, QString *error) const;
    bool changed(const QString &plugin, const QString &version, QString *error) const;
    bool finish(const QString &plugin, const QString &version, const QString &status, QString *error);
    PluginManager::Package validate(const QString &plugin, QString *error);
    bool apply(const QString &plugin, PluginManager::PublishCompletion completion, QString *error);
    bool rollback(const QString &plugin, const QString &version, PluginManager::PublishCompletion completion,
                  QString *error, bool fromAgent = false);
    bool recover(QString *error);
    void setAgentBusy(bool value) { agentBusy_ = value; }
    bool agentBusy() const { return agentBusy_; }
    bool busy() const { return restoring_; }
    static QByteArray fingerprint(const QString &directory, QString *error);
signals:
    void changed();
private:
    QString historyDirectory(const QString &plugin) const;
    QString versionDirectory(const QString &plugin, const QString &version) const;
    QString sourcePath(const QString &plugin) const;
    bool restore(const QString &plugin, const QString &snapshot, QString *error);
    bool prune(const QString &plugin, QString *error);
    QString directory_;
    PluginManager *manager_;
    bool restoring_ = false;
    bool agentBusy_ = false;
};
