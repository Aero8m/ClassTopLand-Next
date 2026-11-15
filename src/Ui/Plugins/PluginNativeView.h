#pragma once
#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QSet>
#include <QWidget>
#include "../../Core/Plugins/PluginUiPolicy.h"
#include "../../CourseBar/CourseBarMetrics.h"

class PluginNativeView final : public QWidget
{
    Q_OBJECT
  public:
    PluginNativeView(const QString &package, const QString &file, QWidget *parent = nullptr,
                     Plugins::UiKind kind = Plugins::UiKind::Page,
                     const QString &layoutPreset = {}, const QString &title = {});
    ~PluginNativeView() override;
    QString failure() const
    {
        return failure_;
    }
    bool dirty() const
    {
        return dirty_;
    }
    void setDirty(bool value)
    {
        dirty_ = value;
    }
    bool available() const
    {
        return available_ && (!component() || sizeHint().width() > 0);
    }
    void setAvailable(bool value)
    {
        available_ = value;
        emit availabilityChanged();
    }
    QJsonValue request(const QString &method, const QJsonObject &args, QString *error);
    QJsonObject snapshot() const;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    void applyMetrics(const CourseBarMetrics &metrics);
    QJsonArray diagnostics() const { return diagnostics_; }
  signals:
    void controlSignal(const QString &object, const QString &signal, const QJsonArray &arguments);
    void availabilityChanged();

  protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
  signals:
    void visibilityChanged(bool visible);

  private:
    QObject *find(const QString &name) const;
    void wire(QWidget *widget);
    void selectionSignals(QWidget *widget);
    bool component() const { return kind_ == Plugins::UiKind::Component; }
    void applyPageBackgrounds();
    void applyStyle(QWidget *widget, const QString &style);
    void refreshComponent();
    void registerItem(QWidget *widget);
    QString package_, failure_, layoutPreset_;
    bool finalizePageWidgets();
    bool standardRootChild(const QString &type) const;
    QWidget *root_ = nullptr;
    bool dirty_ = false, available_ = true;
    QSet<QString> subscriptions_;
    QHash<QString, QPointer<QObject>> objects_;
    Plugins::UiKind kind_;
    CourseBarMetrics metrics_;
    QJsonArray diagnostics_;
    QHash<QString, QString> nativeStyles_, pluginStyles_;
    QSet<QString> pending_, hiddenOnInsert_, protectedObjects_;
};
