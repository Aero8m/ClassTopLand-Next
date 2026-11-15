#include "PluginPageWidgets.h"
#include <QVBoxLayout>

void PluginContentSlot::lockLayout()
{
    locked_ = true;
    if (auto* body = qobject_cast<QVBoxLayout*>(layout())) {
        body->setContentsMargins(0, 0, 0, 0); body->setSpacing(12);
        body->setSizeConstraint(QLayout::SetMinimumSize);
    }
}
bool PluginSection::attachSlot(PluginContentSlot* slot)
{
    if (!slot || (slot->layout() && !qobject_cast<QVBoxLayout*>(slot->layout())) || (slot_ && slot_ != slot)) return false;
    slot_ = slot; bodyLayout()->addWidget(slot); slot->lockLayout(); return true;
}
void PluginSection::detachSlot(PluginContentSlot* slot)
{
    if (slot_ == slot) { bodyLayout()->removeWidget(slot); slot_.clear(); }
}
namespace PluginPageWidgets {
bool composite(QObject* object)
{
    return qobject_cast<PluginSection*>(object) || qobject_cast<PluginContentSlot*>(object) ||
        qobject_cast<PluginSettingRow*>(object) || qobject_cast<PluginActionBar*>(object);
}
bool managedEditor(QObject* object)
{
    auto* widget = qobject_cast<QWidget*>(object);
    return widget && (qobject_cast<PluginSettingRow*>(widget->parentWidget()) || qobject_cast<PluginActionBar*>(widget->parentWidget()));
}
bool managedLayout(QObject* object)
{
    auto* layout = qobject_cast<QLayout*>(object);
    if (!layout) return false;
    auto* slot = qobject_cast<PluginContentSlot*>(layout->parentWidget());
    return slot && slot->locked();
}
bool childAllowed(QWidget* parent, const QString& type, QString* error)
{
    bool allowed = true;
    if (qobject_cast<PluginSection*>(parent)) allowed = type == QStringLiteral("PluginContentSlot");
    else if (auto* slot = qobject_cast<PluginContentSlot*>(parent); slot && slot->locked())
        allowed = !type.endsWith(QStringLiteral("Layout")) || type == QStringLiteral("QVBoxLayout");
    else if (qobject_cast<PluginActionBar*>(parent)) allowed = type == QStringLiteral("ElaPushButton");
    else if (qobject_cast<PluginSettingRow*>(parent))
        allowed = !type.startsWith(QStringLiteral("Plugin")) && !type.endsWith(QStringLiteral("Layout")) && type != QStringLiteral("CourseBarItem");
    if (!allowed) *error = QStringLiteral("控件不符合标准布局的内容槽规则：%1").arg(type);
    return allowed;
}
}
