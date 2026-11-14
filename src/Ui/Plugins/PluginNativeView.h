#pragma once
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QWidget>

class PluginNativeView final : public QWidget
{
    Q_OBJECT
  public:
    PluginNativeView(const QString &package, const QString &file, QWidget *parent = nullptr);
    ~PluginNativeView() override;
    QString failure() const
    {
        return failure_;
    }
    bool dirty() const
    {
        return dirty_;
    }
    void setDirty(bool value)
    {
        dirty_ = value;
    }
    bool available() const
    {
        return available_;
    }
    void setAvailable(bool value)
    {
        available_ = value;
        emit availabilityChanged();
    }
    QJsonValue request(const QString &method, const QJsonObject &args, QString *error);
    QJsonObject snapshot() const;
  signals:
    void controlSignal(const QString &object, const QString &signal, const QJsonArray &arguments);
    void availabilityChanged();

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
  signals:
    void visibilityChanged(bool visible);

  private:
    QObject *find(const QString &name) const;
    void wire(QWidget *widget);
    void selectionSignals(QWidget *widget);
    bool validStyle(const QString &style) const;
    QString resolveStyle(const QString &style) const;
    QString package_, failure_;
    QWidget *root_ = nullptr;
    bool dirty_ = false, available_ = true;
    QSet<QString> subscriptions_;
    QHash<QString, QPointer<QObject>> objects_;
};
