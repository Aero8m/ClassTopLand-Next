#include "CourseBarComponentRegistry.h"
#include "ICourseBarComponent.h"
#include "BuiltinComponents/Date/Date.h"
#include "BuiltinComponents/CourseView/CourseView.h"
#include "BuiltinComponents/Countdown/Countdown.h"
#include "BuiltinComponents/TextTip/TextTip.h"
#include "../Core/Plugins/PluginManager.h"
#include "../Ui/Plugins/PluginUiHost.h"
#include "../Ui/Plugins/PluginView.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QEvent>
#include <QScreen>

class ScriptCourseBarComponent final : public ICourseBarComponent
{
  public:
    ScriptCourseBarComponent(const CourseBarComponentConfig &config, PluginUiHost *ui, bool collapsed,
                             QWidget *parent)
        : ICourseBarComponent(parent), name_(config.typeId), collapsed_(collapsed)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSizeConstraint(QLayout::SetNoConstraint);
        view_ = ui->createComponent(config.typeId, config.instanceId, config.settings, this);
        if (!view_)
        {
            auto *label = new QLabel(QStringLiteral("插件不可用"), this);
            label->setToolTip(config.typeId);
            view_ = label;
        }
        layout->addWidget(view_);
        view_->installEventFilter(this);
        updateMetrics();
    }
    QString getName() override
    {
        return name_;
    }
    QSize sizeHint() const override
    {
        return QSize(qBound(24, view_->sizeHint().width(), 800), metrics().height);
    }
    bool visibleWhenCollapsed() const override
    {
        return collapsed_;
    }
    bool isAvailable() const override
    {
        const auto *view = qobject_cast<PluginView *>(view_);
        return !view || view->available();
    }

  protected:
    void updateMetrics() override
    {
        setFixedHeight(metrics().height);
        if (view_)
            view_->setFont(metrics().font());
        notifyPreferredSizeChanged();
    }
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == view_ && (event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show ||
                                 event->type() == QEvent::Hide))
            notifyPreferredSizeChanged();
        return ICourseBarComponent::eventFilter(watched, event);
    }

  private:
    QString name_;
    QWidget *view_ = nullptr;
    bool collapsed_ = false;
};
CourseBarComponentRegistry::CourseBarComponentRegistry(PluginManager *manager, PluginUiHost *ui,
                                                       QObject *parent)
    : QObject(parent), manager_(manager), ui_(ui)
{
    connect(manager, &PluginManager::contributionsChanged, this, &CourseBarComponentRegistry::changed);
}
QMap<QString, QString> CourseBarComponentRegistry::types() const
{
    QMap<QString, QString> result{{QStringLiteral("builtin/date"), QStringLiteral("内置 · 日期")},
                                  {QStringLiteral("builtin/courseView"), QStringLiteral("内置 · 课程查看")},
                                  {QStringLiteral("builtin/countdown"), QStringLiteral("内置 · 倒计时")},
                                  {QStringLiteral("builtin/textTip"), QStringLiteral("内置 · 文本提示")}};
    const auto pluginTypes = manager_->componentTypes();
    for (auto it = pluginTypes.begin(); it != pluginTypes.end(); ++it)
        result.insert(it.key(), it.value());
    return result;
}
QString CourseBarComponentRegistry::typeId(const CourseBarComponentConfig &config)
{
    switch (config.type)
    {
    case CourseBarComponentType::Date:
        return QStringLiteral("builtin/date");
    case CourseBarComponentType::CourseView:
        return QStringLiteral("builtin/courseView");
    case CourseBarComponentType::Countdown:
        return QStringLiteral("builtin/countdown");
    case CourseBarComponentType::TextTip:
        return QStringLiteral("builtin/textTip");
    case CourseBarComponentType::Plugin:
        return config.typeId;
    }
    return {};
}
CourseBarComponentConfig CourseBarComponentRegistry::configuration(const QString &type)
{
    CourseBarComponentConfig config;
    config.typeId = type;
    if (type == QStringLiteral("builtin/date"))
        config.type = CourseBarComponentType::Date;
    else if (type == QStringLiteral("builtin/courseView"))
        config.type = CourseBarComponentType::CourseView;
    else if (type == QStringLiteral("builtin/countdown"))
        config.type = CourseBarComponentType::Countdown;
    else if (type == QStringLiteral("builtin/textTip"))
        config.type = CourseBarComponentType::TextTip;
    else
        config.type = CourseBarComponentType::Plugin;
    return config;
}
QString CourseBarComponentRegistry::name(const CourseBarComponentConfig &config) const
{
    return types().value(typeId(config), QStringLiteral("不可用插件 · ") + config.typeId);
}
QString CourseBarComponentRegistry::previewText(const CourseBarComponentConfig &config) const
{
    const auto parts = config.typeId.split('/');
    if (parts.size() != 2)
        return QStringLiteral("插件不可用");
    const auto definition = manager_->definition(parts[0], QStringLiteral("component"), parts[1]);
    return definition.value(QStringLiteral("previewText"))
        .toString(definition.isEmpty() ? QStringLiteral("插件不可用") : QStringLiteral("静态预览"))
        .left(160);
}
ICourseBarComponent *CourseBarComponentRegistry::create(const CourseBarComponentConfig &config,
                                                        CourseRefreshService *courses, QWidget *parent) const
{
    switch (config.type)
    {
    case CourseBarComponentType::Date:
        return new Date(parent);
    case CourseBarComponentType::CourseView:
        return courses ? new CourseView(courses, parent) : new CourseView(parent);
    case CourseBarComponentType::Countdown:
        return courses ? new Countdown(courses, parent) : new Countdown(parent);
    case CourseBarComponentType::TextTip:
    {
        auto *tip = new TextTip(parent);
        tip->setText(config.text);
        return tip;
    }
    case CourseBarComponentType::Plugin:
    {
        const auto parts = config.typeId.split('/');
        const auto definition = parts.size() == 2
                                    ? manager_->definition(parts[0], QStringLiteral("component"), parts[1])
                                    : QJsonObject();
        return new ScriptCourseBarComponent(config, ui_,
                                            definition.value(QStringLiteral("displayPolicy")).toString() ==
                                                QStringLiteral("whenAvailable"),
                                            parent);
    }
    }
    return nullptr;
}
void CourseBarComponentRegistry::openSettings(const QString &instance, QWidget *parent)
{
    ui_->openComponent(instance, parent);
}
bool CourseBarComponentRegistry::prepareRemoval(const QString &instance)
{
    return ui_->prepareComponentRemoval(instance);
}
