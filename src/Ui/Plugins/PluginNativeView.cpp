#include "PluginNativeView.h"
#include "../../Core/Plugins/PluginContracts.h"
#include "../../Core/Plugins/PluginManifest.h"
#include "PluginWidgetRegistry.h"
#include <ElaToggleSwitch.h>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMetaProperty>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QShowEvent>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QUiLoader>
#include <QVBoxLayout>
#include <functional>

namespace
{
class Loader final : public QUiLoader
{
  public:
    Loader()
    {
        clearPluginPaths();
    }
    QString failure;
    QHash<QString, QPointer<QObject>> objects;
    QWidget *createWidget(const QString &type, QWidget *parent, const QString &name) override
    {
        auto *widget = PluginWidgetRegistry::create(type, parent);
        if (!widget)
            failure = QStringLiteral("未注册控件：%1").arg(type);
        else
        {
            widget->setObjectName(name);
            objects.insert(name, widget);
        }
        return widget;
    }
    QLayout *createLayout(const QString &type, QObject *parent, const QString &name) override
    {
        if (!Plugins::widgetCatalog().contains(type))
        {
            failure = QStringLiteral("未注册布局：%1").arg(type);
            return nullptr;
        }
        auto *layout = QUiLoader::createLayout(type, parent, name);
        if (layout)
            objects.insert(name, layout);
        return layout;
    }
};
} // namespace
PluginNativeView::PluginNativeView(const QString &package, const QString &file, QWidget *parent)
    : QWidget(parent), package_(package)
{
    PluginWidgetRegistry::bindTheme(this);
    const auto path = QDir(package).filePath(file);
    failure_ = Plugins::validateUi(path, package);
    if (!failure_.isEmpty())
        return;
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly))
    {
        failure_ = input.errorString();
        return;
    }
    Loader loader;
    loader.setWorkingDirectory(QDir(package));
    root_ = loader.load(&input, this);
    if (!root_ || !loader.failure.isEmpty())
    {
        failure_ = loader.failure.isEmpty() ? loader.errorString() : loader.failure;
        delete root_;
        root_ = nullptr;
        return;
    }
    objects_ = loader.objects;
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(root_);
    const auto widgets = root_->findChildren<QWidget *>();
    auto adapt = [](QWidget *widget) {
        for (const auto &property : widget->dynamicPropertyNames())
        {
            const auto native = QByteArray("p") + property;
            if (widget->metaObject()->indexOfProperty(native) >= 0)
            {
                const auto value = widget->property(property);
                widget->setProperty(property, QVariant());
                QString error;
                PluginWidgetRegistry::write(widget, QString::fromUtf8(property),
                                            QJsonValue::fromVariant(value), &error);
            }
        }
    };
    adapt(root_);
    wire(root_);
    for (auto *widget : widgets)
    {
        if (objects_.value(widget->objectName()) != widget)
            continue;
        if (!widget->styleSheet().isEmpty())
            widget->setStyleSheet(resolveStyle(widget->styleSheet()));
        adapt(widget);
        wire(widget);
    }
    if (!root_->styleSheet().isEmpty())
        root_->setStyleSheet(resolveStyle(root_->styleSheet()));
}
PluginNativeView::~PluginNativeView()
{
    // Child destruction can emit signals connected to the view. Retire those
    // children while the object registry and subscriptions still exist.
    delete root_;
    root_ = nullptr;
}
QObject *PluginNativeView::find(const QString &name) const
{
    if (!root_)
        return nullptr;
    return objects_.value(name);
}
void PluginNativeView::wire(QWidget *widget)
{
    auto send = [this, widget](const QString &signal, const QJsonArray &args) {
        if (subscriptions_.contains(widget->objectName() + QLatin1Char('/') + signal))
            emit controlSignal(widget->objectName(), signal, args);
    };
    if (auto *button = qobject_cast<QAbstractButton *>(widget))
    {
        connect(button, &QAbstractButton::clicked, this,
                [send](bool value) { send(QStringLiteral("clicked"), {value}); });
        connect(button, &QAbstractButton::toggled, this,
                [send](bool value) { send(QStringLiteral("toggled"), {value}); });
    }
    if (auto *toggle = qobject_cast<ElaToggleSwitch *>(widget))
        connect(toggle, &ElaToggleSwitch::toggled, this,
                [send](bool value) { send(QStringLiteral("toggled"), {value}); });
    if (auto *edit = qobject_cast<QLineEdit *>(widget))
    {
        connect(edit, &QLineEdit::textChanged, this,
                [send](const QString &value) { send(QStringLiteral("textChanged"), {value}); });
        connect(edit, &QLineEdit::editingFinished, this,
                [send] { send(QStringLiteral("editingFinished"), {}); });
    }
    if (auto *edit = qobject_cast<QTextEdit *>(widget))
        connect(edit, &QTextEdit::textChanged, this,
                [send, edit] { send(QStringLiteral("textChanged"), {edit->toPlainText()}); });
    if (auto *edit = qobject_cast<QPlainTextEdit *>(widget))
        connect(edit, &QPlainTextEdit::textChanged, this,
                [send, edit] { send(QStringLiteral("textChanged"), {edit->toPlainText()}); });
    if (auto *spin = qobject_cast<QSpinBox *>(widget))
        connect(spin, &QSpinBox::valueChanged, this,
                [send](int value) { send(QStringLiteral("valueChanged"), {value}); });
    if (auto *spin = qobject_cast<QDoubleSpinBox *>(widget))
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [send](double value) { send(QStringLiteral("valueChanged"), {value}); });
    if (auto *slider = qobject_cast<QAbstractSlider *>(widget))
        connect(slider, &QAbstractSlider::valueChanged, this,
                [send](int value) { send(QStringLiteral("valueChanged"), {value}); });
    if (auto *combo = qobject_cast<QComboBox *>(widget))
        connect(combo, &QComboBox::currentIndexChanged, this,
                [send](int value) { send(QStringLiteral("currentIndexChanged"), {value}); });
    if (auto *tabs = qobject_cast<QTabWidget *>(widget))
        connect(tabs, &QTabWidget::currentChanged, this,
                [send](int value) { send(QStringLiteral("currentChanged"), {value}); });
    if (auto *stack = qobject_cast<QStackedWidget *>(widget))
        connect(stack, &QStackedWidget::currentChanged, this,
                [send](int value) { send(QStringLiteral("currentChanged"), {value}); });
    if (auto *date = qobject_cast<QDateTimeEdit *>(widget))
        connect(date, &QDateTimeEdit::dateTimeChanged, this, [send](const QDateTime &value) {
            send(QStringLiteral("dateTimeChanged"), {value.toString(Qt::ISODate)});
        });
    if (auto *view = qobject_cast<QAbstractItemView *>(widget))
    {
        connect(view, &QAbstractItemView::activated, this, [send](const QModelIndex &index) {
            send(QStringLiteral("activated"), {index.row(), index.column()});
        });
        selectionSignals(widget);
    }
}
void PluginNativeView::selectionSignals(QWidget *widget)
{
    auto *view = qobject_cast<QAbstractItemView *>(widget);
    if (!view || !view->selectionModel() || view->selectionModel()->property("pluginWired").toBool())
        return;
    view->selectionModel()->setProperty("pluginWired", true);
    connect(view->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this, view] {
        const auto selection = view->selectionModel()->selectedIndexes();
        const auto index = selection.isEmpty() ? QModelIndex() : selection.first();
        if (subscriptions_.contains(view->objectName() + QStringLiteral("/selectionChanged")))
            emit controlSignal(view->objectName(), QStringLiteral("selectionChanged"),
                               {index.row(), index.column()});
    });
}
bool PluginNativeView::validStyle(const QString &style) const
{
    const QRegularExpression urls(QStringLiteral("url\\s*\\(\\s*['\"]?([^)'\"]+)"),
                                  QRegularExpression::CaseInsensitiveOption);
    auto matches = urls.globalMatch(style);
    while (matches.hasNext())
    {
        const auto resource = matches.next().captured(1).trimmed();
        if (QDir::isAbsolutePath(resource) || !Plugins::within(package_, QDir(package_).filePath(resource)))
            return false;
    }
    return true;
}
QString PluginNativeView::resolveStyle(const QString &style) const
{
    QString resolved = style;
    const QRegularExpression urls(QStringLiteral("url\\s*\\(\\s*['\"]?([^)'\"]+)['\"]?\\s*\\)"),
                                  QRegularExpression::CaseInsensitiveOption);
    auto matches = urls.globalMatch(style);
    QList<QRegularExpressionMatch> replacements;
    while (matches.hasNext())
        replacements.append(matches.next());
    for (auto it = replacements.crbegin(); it != replacements.crend(); ++it)
        resolved.replace(
            it->capturedStart(), it->capturedLength(),
            QStringLiteral("url(\"%1\")").arg(QDir(package_).filePath(it->captured(1).trimmed())));
    return resolved;
}
QJsonValue PluginNativeView::request(const QString &method, const QJsonObject &args, QString *error)
{
    if (method == QStringLiteral("tree"))
        return snapshot();
    auto *object = find(args.value(QStringLiteral("object")).toString());
    if (!object)
    {
        *error = QStringLiteral("找不到控件：%1").arg(args.value(QStringLiteral("object")).toString());
        return {};
    }
    if (method == QStringLiteral("get"))
        return PluginWidgetRegistry::read(object, args.value(QStringLiteral("property")).toString(), error);
    if (method == QStringLiteral("set"))
    {
        const auto values = args.value(QStringLiteral("properties")).toObject();
        for (auto it = values.begin(); it != values.end(); ++it)
        {
            QJsonValue value = it.value();
            if (it.key() == QStringLiteral("styleSheet"))
            {
                if (!validStyle(value.toString()))
                {
                    *error = QStringLiteral("样式资源越界");
                    return {};
                }
                value = resolveStyle(value.toString());
            }
            if (!PluginWidgetRegistry::write(object, it.key(), value, error))
                return {};
        }
        return true;
    }
    if (method == QStringLiteral("subscribe") || method == QStringLiteral("unsubscribe"))
    {
        const auto signal = args.value(QStringLiteral("signal")).toString();
        if (!Plugins::widgetCatalog()
                 .value(PluginWidgetRegistry::typeName(object))
                 .toObject()
                 .value(QStringLiteral("signals"))
                 .toArray()
                 .contains(signal))
        {
            *error = QStringLiteral("信号未开放：%1").arg(signal);
            return {};
        }
        const auto key = object->objectName() + QLatin1Char('/') + signal;
        if (method == QStringLiteral("subscribe"))
            subscriptions_.insert(key);
        else
            subscriptions_.remove(key);
        return true;
    }
    if (method == QStringLiteral("create"))
    {
        auto *parent = qobject_cast<QWidget *>(object);
        const auto name = args.value(QStringLiteral("name")).toString();
        const auto type = args.value(QStringLiteral("class")).toString();
        if (!parent || name.isEmpty() || name.size() > 128 || find(name) || objects_.size() >= 2000)
        {
            *error = QStringLiteral("父控件、名称或控件数无效");
            return {};
        }
        if (Plugins::widgetCatalog().value(type).toObject().value(QStringLiteral("kind")) ==
            QStringLiteral("layout"))
        {
            if (parent->layout())
            {
                *error = QStringLiteral("父控件已有布局");
                return {};
            }
            QLayout *layout = nullptr;
            if (type == QStringLiteral("QVBoxLayout"))
                layout = new QVBoxLayout(parent);
            else if (type == QStringLiteral("QHBoxLayout"))
                layout = new QHBoxLayout(parent);
            else if (type == QStringLiteral("QGridLayout"))
                layout = new QGridLayout(parent);
            else if (type == QStringLiteral("QFormLayout"))
                layout = new QFormLayout(parent);
            if (!layout)
            {
                *error = QStringLiteral("未知布局");
                return {};
            }
            layout->setObjectName(name);
            objects_.insert(name, layout);
            connect(layout, &QObject::destroyed, this, [this, name] {
                if (objects_.value(name).isNull())
                    objects_.remove(name);
            });
            return name;
        }
        auto *created = PluginWidgetRegistry::create(type, parent);
        if (!created)
        {
            *error = QStringLiteral("未知控件：%1").arg(type);
            return {};
        }
        PluginWidgetRegistry::bindTheme(created);
        created->setObjectName(name);
        objects_.insert(name, created);
        connect(created, &QObject::destroyed, this, [this, name] {
            if (objects_.value(name).isNull())
                objects_.remove(name);
        });
        wire(created);
        return name;
    }
    if (method == QStringLiteral("remove"))
    {
        if (object == root_)
        {
            *error = QStringLiteral("不能删除根控件");
            return {};
        }
        const auto descendants = object->findChildren<QObject *>();
        for (auto *child : descendants)
        {
            const auto prefix = child->objectName() + QLatin1Char('/');
            subscriptions_.removeIf([&](const QString &key) { return key.startsWith(prefix); });
            objects_.remove(child->objectName());
            child->setObjectName({});
        }
        const auto prefix = object->objectName() + QLatin1Char('/');
        subscriptions_.removeIf([&](const QString &key) { return key.startsWith(prefix); });
        objects_.remove(object->objectName());
        object->setObjectName({});
        if (auto *widget = qobject_cast<QWidget *>(object))
            widget->hide();
        object->deleteLater();
        return true;
    }
    if (method == QStringLiteral("insert"))
    {
        auto *widget = qobject_cast<QWidget *>(find(args.value(QStringLiteral("child")).toString()));
        auto *layout = qobject_cast<QLayout *>(object);
        if (!layout)
            if (auto *container = qobject_cast<QWidget *>(object))
                layout = container->layout();
        if (!widget || widget == root_ || !layout || widget == layout->parentWidget() ||
            widget->isAncestorOf(layout->parentWidget()))
        {
            *error = QStringLiteral("插入对象或布局无效");
            return {};
        }
        if (auto *box = qobject_cast<QBoxLayout *>(layout))
            box->insertWidget(args.value(QStringLiteral("index")).toInt(-1), widget,
                              args.value(QStringLiteral("stretch")).toInt());
        else if (auto *grid = qobject_cast<QGridLayout *>(layout))
            grid->addWidget(widget, qBound(0, args.value(QStringLiteral("row")).toInt(), 2000),
                            qBound(0, args.value(QStringLiteral("column")).toInt(), 64));
        else if (auto *form = qobject_cast<QFormLayout *>(layout))
            form->addRow(widget);
        else
        {
            *error = QStringLiteral("布局不支持插入");
            return {};
        }
        widget->show();
        return true;
    }
    if (method == QStringLiteral("invoke"))
    {
        auto *widget = qobject_cast<QWidget *>(object);
        if (!widget)
        {
            *error = QStringLiteral("对象不是控件");
            return {};
        }
        const auto operation = args.value(QStringLiteral("operation")).toString();
        const auto options = args.value(QStringLiteral("arguments")).toObject();
        if (operation == QStringLiteral("addPage"))
        {
            auto *child = qobject_cast<QWidget *>(find(options.value(QStringLiteral("child")).toString()));
            if (!child || child == root_ || child == widget || child->isAncestorOf(widget))
            {
                *error = QStringLiteral("页面控件无效");
                return {};
            }
            if (auto *tabs = qobject_cast<QTabWidget *>(widget))
                return tabs->addTab(child, options.value(QStringLiteral("title")).toString());
            if (auto *stack = qobject_cast<QStackedWidget *>(widget))
                return stack->addWidget(child);
            *error = QStringLiteral("控件不是页面容器");
            return {};
        }
        const auto result = PluginWidgetRegistry::invoke(widget, operation, options, error);
        if (error->isEmpty() && operation == QStringLiteral("setItems"))
            selectionSignals(widget);
        return result;
    }
    *error = QStringLiteral("未知 UI 操作：%1").arg(method);
    return {};
}
QJsonObject PluginNativeView::snapshot() const
{
    QJsonArray nodes;
    if (root_)
    {
        QList<QObject *> objects;
        const auto names = objects_.keys();
        for (const auto &name : names)
            if (objects_.value(name))
                objects.append(objects_.value(name));
        for (auto *object : objects)
        {
            const auto type = PluginWidgetRegistry::typeName(object);
            if (type.isEmpty() || object->objectName().isEmpty())
                continue;
            QJsonObject props;
            for (const auto &property : Plugins::widgetCatalog()
                                            .value(type)
                                            .toObject()
                                            .value(QStringLiteral("properties"))
                                            .toArray())
            {
                if (property == QStringLiteral("styleSheet"))
                    continue;
                QString error;
                const auto value = PluginWidgetRegistry::read(object, property.toString(), &error);
                if (error.isEmpty())
                    props.insert(property.toString(), value);
            }
            // Passwords must not enter model context or diagnostic snapshots.
            if (auto *edit = qobject_cast<QLineEdit *>(object); edit && edit->echoMode() != QLineEdit::Normal)
                props.insert(QStringLiteral("text"), QStringLiteral("<redacted>"));
            QJsonObject node{
                {QStringLiteral("object"), object->objectName()},
                {QStringLiteral("class"), type},
                {QStringLiteral("parent"), object->parent() ? object->parent()->objectName() : QString()},
                {QStringLiteral("properties"), props}};
            if (auto *view = qobject_cast<QAbstractItemView *>(object); view && view->model())
            {
                auto *model = view->model();
                QJsonArray rows;
                for (int row = 0; row < qMin(10, model->rowCount()); ++row)
                {
                    QJsonArray cells;
                    for (int col = 0; col < qMin(6, model->columnCount()); ++col)
                        cells.append(model->data(model->index(row, col)).toString().left(128));
                    rows.append(cells);
                }
                node.insert(QStringLiteral("model"),
                            QJsonObject{{QStringLiteral("rowCount"), model->rowCount()},
                                        {QStringLiteral("columnCount"), model->columnCount()},
                                        {QStringLiteral("currentRow"), view->currentIndex().row()},
                                        {QStringLiteral("currentColumn"), view->currentIndex().column()},
                                        {QStringLiteral("rows"), rows}});
            }
            nodes.append(node);
        }
    }
    return {{QStringLiteral("nodes"), nodes},
            {QStringLiteral("dirty"), dirty_},
            {QStringLiteral("available"), available_},
            {QStringLiteral("error"), failure_}};
}
void PluginNativeView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    emit visibilityChanged(true);
}
void PluginNativeView::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    emit visibilityChanged(false);
}
