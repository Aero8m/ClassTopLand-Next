#pragma once
#include <ElaDialog.h>
#include <QMap>
#include <QJsonObject>
#include <functional>
class PluginManager;
class PluginView;
class ElaPivot;
class ElaText;
class ElaPushButton;
class QStackedWidget;
class PluginSettingsDialog final : public ElaDialog
{
    Q_OBJECT
  public:
    PluginSettingsDialog(PluginManager *manager, const QString &plugin,
                         const QMap<QString, QJsonObject> &definitions, QWidget *parent = nullptr,
                         const QString &componentInstance = {},
                         const QString &storageKey = QStringLiteral("settings"),
                         const QString &viewKind = QStringLiteral("settings"));
    bool prepareToClose();
    void forceClose();
    void constrainToScreen(bool center = false);
    QString pluginId() const
    {
        return plugin_;
    }
    QString componentInstance() const
    {
        return componentInstance_;
    }
  signals:
    void saveFinished(bool success);

  protected:
    void closeEvent(QCloseEvent *event) override;
    void reject() override;

  private:
    void load(const QMap<QString, QJsonObject> &definitions, const QJsonObject &values);
    void save();
    void refreshState();
    void selectPage(int index);
    QJsonObject values() const;
    bool dirty() const;
    PluginManager *manager_;
    QString plugin_, componentInstance_;
    QString storageKey_, viewKind_;
    QMap<QString, PluginView *> pages_;
    QMap<QString, QString> viewInstances_;
    QJsonObject baseline_, extra_;
    QJsonValue storedValue_;
    QJsonObject componentBaseline_;
    QStackedWidget *stack_;
    ElaPivot *pivot_;
    ElaText *status_;
    ElaPushButton *save_;
    ElaPushButton *discard_;
    bool loading_ = true, saving_ = false, forced_ = false;
    bool resolvingClose_ = false;
};
