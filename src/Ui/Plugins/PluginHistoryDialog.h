#pragma once
#include <QString>
#include <functional>
class QWidget;
class PluginWorkspace;
namespace AppUi {
void showPluginHistory(PluginWorkspace *workspace, const QString &plugin, QWidget *parent,
                       std::function<bool()> taskBusy = {});
}
