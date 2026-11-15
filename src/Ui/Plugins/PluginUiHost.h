#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <functional>
class PluginManager;
class PluginNativeView;
class QWidget;
class QDialog;
class PluginUiHost final : public QObject
{
    Q_OBJECT
  public:
    using GuardCompletion = std::function<void(bool)>;
    explicit PluginUiHost(PluginManager *manager, QObject *parent = nullptr);
    ~PluginUiHost() override;
    using Navigator = std::function<bool(const QString &, const QString &, const QString &, QString *)>;
    Navigator navigator;
    bool openPage(const QString &plugin, const QString &page = {}, const QString &component = {},
                  QString *error = nullptr);
    void openSettings(const QString &plugin, QWidget *parent = nullptr);
    void openComponent(const QString &instance, QWidget *parent = nullptr);
    PluginNativeView *createPage(const QString &plugin, const QString &contribution, QWidget *parent, QString *error = nullptr);
    PluginNativeView *createComponentSettings(const QString &instance, QWidget *parent, QString *error = nullptr);
    QWidget *createComponent(const QString &typeId, const QString &instance, const QJsonObject &settings,
                             QWidget *parent);
    void prepareAll(GuardCompletion completion, const QString &plugin = {});
    void prepareComponents(const QStringList &instances, GuardCompletion completion);
    void closeDialogs();
    void closePages();
    bool guarding() const
    {
        return guarding_;
    }
    QJsonArray snapshots() const;
    QJsonValue inspect(const QString &instance, const QString &method, const QJsonObject &args,
                       QString *error);
  signals:
    void guardStateChanged(bool guarding);
    void revealRequested(const QString &plugin, const QString &viewInstance);

  private:
    struct View
    {
        QPointer<PluginNativeView> widget;
        QString plugin, kind, contribution, component;
        quint64 generation;
    };
    PluginNativeView *create(const QString &plugin, const QString &kind, const QString &contribution,
                             const QString &file, const QString &component, const QJsonObject &settings,
                             QWidget *parent, QString *error = nullptr);
    void prepare(QStringList instances, GuardCompletion completion);
    void guardNext();
    void guardReady();
    void finishGuard(bool success);
    void request(const QString &plugin, const QString &method, const QJsonObject &args,
                 std::function<void(const QJsonObject &, const QJsonValue &)> completion);
    PluginManager *manager_;
    QMap<QString, View> views_;
    QStringList guardQueue_;
    QString guardToken_, guardInstance_;
    GuardCompletion completion_;
    bool guarding_ = false;
    QPointer<QDialog> prompt_;
};
