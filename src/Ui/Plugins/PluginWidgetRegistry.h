#pragma once
#include <ElaToggleSwitch.h>
#include <QJsonObject>
#include <QWidget>
class PluginToggleSwitch final : public ElaToggleSwitch
{
    Q_OBJECT
    Q_PROPERTY(bool checked READ checked WRITE setChecked NOTIFY toggled)
  public:
    explicit PluginToggleSwitch(QWidget *parent = nullptr) : ElaToggleSwitch(parent)
    {
    }
    bool checked() const
    {
        return getIsToggled();
    }
    void setChecked(bool value)
    {
        setIsToggled(value);
    }
};
class PluginWidgetRegistry
{
  public:
    static void applyTheme(QWidget *widget, bool descendants = true);
    static void bindTheme(QWidget *widget);
    static QWidget *create(const QString &type, QWidget *parent);
    static QString typeName(QObject *object);
    static QJsonValue read(QObject *object, const QString &name, QString *error);
    static bool write(QObject *object, const QString &name, const QJsonValue &value, QString *error, bool validateOnly = false);
    static QJsonValue invoke(QWidget *object, const QString &operation, const QJsonObject &args,
                             QString *error);
};
