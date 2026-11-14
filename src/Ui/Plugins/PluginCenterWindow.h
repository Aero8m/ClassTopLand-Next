#pragma once
#include <ElaWindow.h>
#include <QMap>
#include <QPointer>
class PluginManager;
class PluginUiHost;
class PluginNativeView;
class PluginCenterWindow final : public ElaWindow
{
    Q_OBJECT
  public:
    PluginCenterWindow(PluginManager *manager, PluginUiHost *ui, QWidget *parent = nullptr);
    void navigate(const QString &plugin, const QString &page, const QString &componentInstance = {});

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    void refresh();
    bool select(const QString &key);
    PluginManager *manager_;
    PluginUiHost *ui_;
    QWidget *management_;
    QMap<QString, QWidget *> navigationPages_;
    QMap<QString, QPointer<PluginNativeView>> pages_;
    QString currentKey_ = QStringLiteral("management");
    bool closing_ = false, approved_ = false;
};
