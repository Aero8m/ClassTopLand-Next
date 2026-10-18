#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QJsonValue>
#include <QHash>
class PluginView final : public QWidget
{
    Q_OBJECT
  public:
    PluginView(const QJsonObject &definition, const QJsonObject &values, QWidget *parent = nullptr);
    QJsonObject values() const
    {
        return values_;
    }
    QJsonObject defaults() const
    {
        return defaults_;
    }
    bool dirty() const
    {
        return values_ != baseline_;
    }
    bool available() const
    {
        return available_;
    }
    void setValues(const QJsonObject &values);
    void markSaved();
    QString validate() const;
    void patch(const QJsonObject &patch);
    void setTree(const QJsonObject &tree);
    static QString check(const QJsonObject &definition);
  signals:
    void changed();
    void action(const QString &id, const QJsonValue &value);

  private:
    QWidget *build(const QJsonObject &node, QWidget *parent);
    void fieldChanged(const QString &id, const QJsonValue &value);
    QJsonObject values_, baseline_, defaults_;
    QHash<QString, QWidget *> widgets_;
    QHash<QString, QJsonObject> fields_;
    bool refreshing_ = false;
    bool available_ = true;
    QString rootId_;
    QString failure_;
};
