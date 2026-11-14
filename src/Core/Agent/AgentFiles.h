#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>

class AgentFiles
{
public:
    void reset() { observed_.clear(); }
    QJsonObject read(const QString &root, const QJsonObject &args, QString *error);
    QJsonObject search(const QString &root, const QJsonObject &args, bool contents, QString *error);
    bool apply(const QString &root, const QJsonArray &changes, QString *error, bool preflightOnly = false);
private:
    QHash<QString, QByteArray> observed_;
};
