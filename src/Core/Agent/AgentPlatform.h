#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QTimer>
#include <memory>

namespace AgentPlatform
{
bool canExecute();
QString readKey(const QString &id, const QString &endpoint);
bool saveKey(const QString &id, const QString &endpoint, const QString &key);
bool removeKey(const QString &id, const QString &endpoint);
QString readText(const QString &root, const QString &relative, QString *error);
bool writeText(const QString &root, const QString &relative, const QString &content, QString *error);
QString listDirectory(const QString &root, const QString &relative, QString *error);
// Empty root accepts a structurally validated absolute path; AgentService must authorize it first.
QString resolvePath(const QString &root, const QString &path, bool allowMissing, QString *error);
QString safePath(const QString &root, const QString &relative, bool allowMissing, QString *error);
bool openApplication(const QString &file, QString *error);

class Command final : public QObject
{
    Q_OBJECT
public:
    explicit Command(QObject *parent = nullptr);
    ~Command() override;
    bool start(const QString &script, const QString &directory, QString *error, int timeoutSeconds = 60);
    void stop();
    QJsonObject result(bool complete = false) const;

signals:
    void finished(const QJsonObject &result, const QString &error);
    void outputReceived(const QJsonObject &result);

private:
    struct State;
    std::unique_ptr<State> state_;
    QTimer timer_;
    void poll();
};
}

