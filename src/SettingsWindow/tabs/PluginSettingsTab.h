#pragma once
#include <ElaScrollPage.h>
class PluginManager;
class PluginUiHost;
class ElaLineEdit;
class QVBoxLayout;
class QStackedWidget;
class QWidget;
class PluginSettingsTab final : public ElaScrollPage
{
    Q_OBJECT
  public:
    PluginSettingsTab(PluginManager *manager, PluginUiHost *ui, QWidget *parent = nullptr);

  private:
    void refresh();
    void showDetails(const QString &id);
    void toggle(const QString &id, bool enabled);
    PluginManager *manager_;
    PluginUiHost *ui_;
    ElaLineEdit *search_;
    QStackedWidget *stack_;
    QWidget *list_;
    QWidget *details_ = nullptr;
    QVBoxLayout *rows_;
    bool refreshPending_ = false;
    QString detailKey_;
    int listScroll_ = 0;
};
