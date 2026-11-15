#pragma once
#include <QPointer>
#include <QVector>
#include <QWidget>

class ElaPivot;
class QListView;
class QStackedWidget;
class PluginManager;
class PluginUiHost;
class PluginNativeView;

// One stable navigation page per plugin. Native views remain alive while hidden.
class PluginMainPage final : public QWidget
{
    Q_OBJECT
  public:
    PluginMainPage(PluginManager *manager, PluginUiHost *ui, const QString &plugin,
                   QWidget *parent = nullptr);
    bool sync();
    bool selectPage(const QString &page, QString *error = nullptr);
    bool selectComponent(const QString &instance, QString *error = nullptr);
    bool revealView(const QString &instance);
    void setNavigationEnabled(bool enabled);

  signals:
    void contentChanged();

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    struct Tab
    {
        QString key, title, component;
        QPointer<PluginNativeView> view;
    };
    bool select(const QString &key, QString *error, bool existingOnly = false);
    void rebuildPivot();
    void updateSelection();
    PluginManager *manager_;
    PluginUiHost *ui_;
    QString plugin_, current_;
    quint64 generation_ = 0;
    QVector<Tab> tabs_;
    ElaPivot *pivot_;
    QListView *pivotView_;
    QWidget *pivotPanel_, *empty_;
    QStackedWidget *stack_;
    bool rebuilding_ = false;
};
