#pragma once
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <functional>
class PluginManager;
class PluginActionRegistry final : public QObject
{
    Q_OBJECT
  public:
    using Completion = std::function<void(const QJsonObject &, const QJsonValue &)>;
    explicit PluginActionRegistry(PluginManager *manager);
    QJsonArray list(const QString &plugin = {}) const;
    QString invoke(const QString &plugin, const QString &action, const QJsonValue &arguments,
                   QObject *context, Completion completion, const QString &origin);
    bool finish(const QString &plugin, quint64 generation, const QJsonObject &arguments);
    void cancel(const QString &token, const QString &code = QStringLiteral("CANCELED"));
    void cancelPlugin(const QString &plugin);
    bool busy() const
    {
        return !pending_.isEmpty();
    }
  signals:
    void dispatch(const QString &plugin, const QString &name, const QJsonObject &event);

  private:
    struct Pending
    {
        QString plugin, action;
        quint64 generation;
        QJsonObject outputSchema;
        QPointer<QObject> context;
        Completion completion;
        QMetaObject::Connection destruction;
    };
    PluginManager *manager_;
    QHash<QString, Pending> pending_;
};
