#pragma once
#include <ElaWindow.h>
#include <QMap>
#include <QPointer>
class PluginManager;
class PluginUiHost;
class PluginMainPage;
class PluginCenterWindow final : public ElaWindow
{
    Q_OBJECT
  public:
    PluginCenterWindow(PluginManager *manager, PluginUiHost *ui, QWidget *parent = nullptr);
    bool navigate(const QString &plugin, const QString &page, const QString &componentInstance = {},
                  QString *error = nullptr);

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    void refresh();
    bool select(const QString &key, QString *error = nullptr);
    PluginMainPage *ensurePlugin(const QString &plugin);
    PluginManager *manager_;
    PluginUiHost *ui_;
    QWidget *management_;
    QMap<QString, QWidget *> navigationPages_;
    QMap<QString, PluginMainPage *> plugins_;
    QString currentKey_ = QStringLiteral("management");
    bool closing_ = false, approved_ = false;
};
