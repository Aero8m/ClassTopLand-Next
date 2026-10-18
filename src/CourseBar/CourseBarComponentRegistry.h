#pragma once
#include <QObject>
#include <QMap>
#include "../Core/Model/CourseBarConfig.h"
class PluginManager;
class PluginUiHost;
class ICourseBarComponent;
class CourseRefreshService;
class QWidget;
class CourseBarComponentRegistry final : public QObject
{
    Q_OBJECT
  public:
    CourseBarComponentRegistry(PluginManager *manager, PluginUiHost *ui, QObject *parent = nullptr);
    QMap<QString, QString> types() const;
    QString name(const CourseBarComponentConfig &config) const;
    QString previewText(const CourseBarComponentConfig &config) const;
    static QString typeId(const CourseBarComponentConfig &config);
    static CourseBarComponentConfig configuration(const QString &typeId);
    ICourseBarComponent *create(const CourseBarComponentConfig &config, CourseRefreshService *courses,
                                QWidget *parent) const;
    void openSettings(const QString &instance, QWidget *parent);
    bool prepareRemoval(const QString &instance);
  signals:
    void changed();

  private:
    PluginManager *manager_;
    PluginUiHost *ui_;
};
