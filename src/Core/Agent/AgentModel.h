#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QStringList>
#include <QUrl>

class QNetworkReply;

struct AgentConnection
{
    QString id, name, preset, baseUrl, model;
    QString reasoningEffort;
    QUrl endpoint() const;
    QStringList reasoningEfforts() const;
    QString effectiveReasoningEffort() const;
    QJsonObject requestBody(const QJsonArray &messages, const QJsonArray &tools) const;
};

struct AgentModelEntry
{
    QString id, name;
    int contextWindow = 0;
};

struct AgentProvider
{
    QString id, name, preset, baseUrl;
    QList<AgentModelEntry> models;
    AgentConnection connection(const QString &model = {}) const;
    QJsonObject toJson() const;
    static AgentProvider fromJson(const QJsonObject &value);
};

struct AgentPreset
{
    QString id, label, baseUrl, catalogId;
};
const QList<AgentPreset> &agentPresets();

class AgentModel final : public QObject
{
    Q_OBJECT
public:
    explicit AgentModel(QObject *parent = nullptr);
    void request(const AgentConnection &connection, const QString &key, const QString &session,
                 const QJsonArray &messages, const QJsonArray &tools);
    void cancel();

signals:
    void textReceived(const QString &text);
    void reasoningReceived(const QString &text);
    void completed(const QJsonObject &message, const QString &error);

private:
    void consume();
    void receiveEvent();
    void finish();
    QNetworkAccessManager network_;
    QNetworkReply *reply_ = nullptr;
    QByteArray buffer_, event_;
    QString content_, reasoning_, error_, reason_;
    QMap<int, QJsonObject> calls_;
    qsizetype bytes_ = 0, payloadBytes_ = 0;
    bool ended_ = false;
    bool reasoningPresent_ = false;
    bool readScheduled_ = false;
};

