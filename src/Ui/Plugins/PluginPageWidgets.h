#pragma once
#include "../SettingsLayout.h"

class PluginContentSlot final : public QWidget
{
    Q_OBJECT
public:
    explicit PluginContentSlot(QWidget* parent = nullptr) : QWidget(parent) {}
    void lockLayout();
    bool locked() const { return locked_; }
private:
    bool locked_ = false;
};
class PluginSection final : public AppUi::SettingsSection
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title WRITE setTitle)
public:
    explicit PluginSection(QWidget* parent = nullptr) : AppUi::SettingsSection(parent) {}
    bool attachSlot(PluginContentSlot* slot);
    void detachSlot(PluginContentSlot* slot);
    PluginContentSlot* slot() const { return slot_; }
private:
    QPointer<PluginContentSlot> slot_;
};
class PluginSettingRow final : public AppUi::SettingsRow
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title WRITE setTitle)
    Q_PROPERTY(QString description READ description WRITE setDescription)
public:
    explicit PluginSettingRow(QWidget* parent = nullptr) : AppUi::SettingsRow(parent) {}
};
class PluginActionBar final : public AppUi::SettingsActions
{
    Q_OBJECT
    Q_PROPERTY(QString message READ message WRITE setMessage)
public:
    explicit PluginActionBar(QWidget* parent = nullptr) : AppUi::SettingsActions(parent) {}
};
namespace PluginPageWidgets {
bool composite(QObject* object);
bool managedEditor(QObject* object);
bool managedLayout(QObject* object);
bool childAllowed(QWidget* parent, const QString& type, QString* error);
}
