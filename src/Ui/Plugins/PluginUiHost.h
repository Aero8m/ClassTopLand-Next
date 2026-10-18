#pragma once
#include <QObject>
#include <QPointer>
#include <QMap>
#include <QByteArray>
#include <QJsonObject>
class PluginManager;
class PluginSettingsDialog;
class PluginView;
class QWidget;
class PluginUiHost final : public QObject
{
    Q_OBJECT
  public:
    explicit PluginUiHost(PluginManager *manager, QObject *parent = nullptr);
    ~PluginUiHost() override;
    void openSettings(const QString &plugin, QWidget *parent);
    void openComponent(const QString &instance, QWidget *parent);
    void openWindow(const QString &plugin, const QString &contribution, QWidget *parent);
    bool prepareAll(const QString &plugin = {});
    bool prepareComponentRemoval(const QString &instance);
    void closeDialogs();
    QWidget *createComponent(const QString &typeId, const QString &instance, const QJsonObject &settings,
                             QWidget *parent);

  private:
    PluginManager *manager_;
    QMap<QString, QPointer<PluginSettingsDialog>> dialogs_;
    QMap<QString, QByteArray> geometries_;
    QList<QPointer<QWidget>> windows_;
};
