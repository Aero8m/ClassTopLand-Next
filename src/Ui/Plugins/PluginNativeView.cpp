#include "PluginNativeView.h"
#include "../../Core/Plugins/PluginContracts.h"
#include "../../Core/Plugins/PluginManifest.h"
#include "PluginWidgetRegistry.h"
#include "PluginPageWidgets.h"
#include "../UiWidgets.h"
#include <ElaPushButton.h>
#include "../../CourseBar/CourseBarItem.h"
#include "../../Core/ThemeManager/ThemeManager.h"
#include <ElaScrollArea.h>
#include <ElaTheme.h>
#include <QBuffer>
#include <QFontInfo>
#include <QFrame>
#include <QGroupBox>
#include <QScrollArea>
#include <ElaToggleSwitch.h>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QComboBox>
#include <QColor>
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
#include <QMap>
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
QString cssId(const QString &name);
class Loader final : public QUiLoader
{
  public:
    explicit Loader(bool component) : component_(component)
    {
        clearPluginPaths();
    }
    bool component_;
    QString failure;
    QHash<QString, QString> nativeStyles;
    QHash<QString, QPointer<QObject>> objects;
    QWidget *createWidget(const QString &type, QWidget *parent, const QString &name) override
    {
        auto *widget = PluginWidgetRegistry::create(type, parent);
        if (!widget)
            failure = QStringLiteral("未注册控件：%1").arg(type);
        else
        {
            auto style = widget->styleSheet();
            if (!widget->objectName().isEmpty()) style.replace(QLatin1Char('#') + widget->objectName(), QLatin1Char('#') + cssId(name));
            nativeStyles.insert(name, style);
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
        if (layout) layout->setObjectName(name);
        if (layout)
            objects.insert(name, layout);
        return layout;
    }
};
} // namespace
PluginNativeView::PluginNativeView(const QString &package, const QString &file, QWidget *parent,
                                   Plugins::UiKind kind, const QString &layoutPreset, const QString &title)
    : QWidget(parent), package_(package), layoutPreset_(layoutPreset), kind_(kind)
{
    const auto prepared = Plugins::prepareUi(QDir(package).filePath(file), package, kind, layoutPreset);
    failure_ = prepared.error;
    diagnostics_ = prepared.diagnostics;
    if (!failure_.isEmpty()) return;
    QBuffer input;
    input.setData(prepared.xml);
    input.open(QIODevice::ReadOnly);
    Loader loader(component());
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
    nativeStyles_ = loader.nativeStyles;
    for (auto it = objects_.cbegin(); it != objects_.cend(); ++it)
    {
        auto *toggle = qobject_cast<PluginToggleSwitch *>(it.value().data());
        if (toggle && toggle->restoreMinimumSize())
            diagnostics_.append(QJsonObject{{QStringLiteral("code"), QStringLiteral("UI_CONTROL_SIZE_NORMALIZED")},
                {QStringLiteral("severity"), QStringLiteral("warning")}, {QStringLiteral("object"), it.key()},
                {QStringLiteral("property"), QStringLiteral("size")}, {QStringLiteral("file"), file},
                {QStringLiteral("message"), QStringLiteral("开关的尺寸约束小于原生尺寸，已按原生尺寸调整")}});
    }
    if (!component() && !finalizePageWidgets()) return;
    if (component())
    {
        auto *old = root_->layout();
        const auto name = old->objectName();
        QList<QLayoutItem *> entries;
        while (auto *item = old->takeAt(0)) entries.append(item);
        delete old;
        auto *row = new CourseBarRowLayout(root_);
        row->setObjectName(name);
        for (auto *entry : entries) row->addItem(entry);
        objects_.insert(name, row);
    }
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->setSizeConstraint(QLayout::SetNoConstraint);
    root_->layout()->setContentsMargins(0, 0, 0, 0);
    protectedObjects_.insert(root_->objectName());
    protectedObjects_.insert(root_->layout()->objectName());
    if (component()) layout->addWidget(root_);
    else
    {
        if (layoutPreset_ == QStringLiteral("settings")) root_->layout()->setSpacing(16);
        auto *body = new QWidget(this);
        auto *bodyLayout = AppUi::pageContent(body, layoutPreset_.isEmpty() ? QString() : title);
        bodyLayout->addWidget(root_, layoutPreset_ == QStringLiteral("content") ? 1 : 0);
        if (layoutPreset_ != QStringLiteral("content")) bodyLayout->addStretch();
        QScrollArea *scroll = nullptr;
        if (layoutPreset_.isEmpty() && root_->layout()->count() == 1)
            scroll = qobject_cast<QScrollArea *>(root_->layout()->itemAt(0)->widget());
        if (scroll)
        {
            layout->addWidget(body);
            protectedObjects_.insert(scroll->objectName());
            if (scroll->widget())
            {
                protectedObjects_.insert(scroll->widget()->objectName());
                if (scroll->widget()->layout()) protectedObjects_.insert(scroll->widget()->layout()->objectName());
            }
        }
        else
        {
            scroll = new ElaScrollArea(this);
            scroll->setWidget(body);
            layout->addWidget(scroll);
        }
        if (layoutPreset_ != QStringLiteral("content")) root_->layout()->setAlignment(Qt::AlignTop);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setAutoFillBackground(false);
        scroll->viewport()->setAutoFillBackground(false);
        body->setAutoFillBackground(false);
        AppUi::enableTouchScrolling(scroll);
        PluginWidgetRegistry::bindTheme(this);
    }
    for (auto it = objects_.cbegin(); it != objects_.cend(); ++it)
    {
        auto *widget = qobject_cast<QWidget *>(it.value().data());
        if (!widget) continue;
        for (const auto &property : widget->dynamicPropertyNames())
        {
            const auto native = QByteArray("p") + property;
            if (widget->metaObject()->indexOfProperty(native) >= 0)
            {
                const auto value = widget->property(property);
                widget->setProperty(property, QVariant());
                QString error;
                if (!PluginWidgetRegistry::write(widget, QString::fromUtf8(property), QJsonValue::fromVariant(value), &error))
                { failure_ = error; return; }
            }
        }
        if (!component())
        {
            // QUiLoader applies the sanitized plugin sheet after widget construction.
            const auto style = widget->styleSheet();
            pluginStyles_.insert(it.key(), style == nativeStyles_.value(it.key()) ? QString() : style);
            applyStyle(widget, pluginStyles_.value(it.key()));
            wire(widget);
        }
        else registerItem(widget);
    }
    if (component()) applyMetrics(metrics_);
    else
    {
        applyPageBackgrounds();
        auto refreshTheme = [this] {
            // Discard Qt's cached pre-stylesheet palettes before resolving
            // palette(...) against the new host colors.
            for (auto it = objects_.cbegin(); it != objects_.cend(); ++it)
                if (auto *widget = qobject_cast<QWidget *>(it.value().data()))
                    widget->setStyleSheet({});
            PluginWidgetRegistry::applyTheme(this);
            applyPageBackgrounds();
        };
        connect(eTheme, &ElaTheme::themeModeChanged, this, refreshTheme);
        connect(&ThemeManager::instance(), &ThemeManager::themeColorChanged, this, refreshTheme);
    }
}
bool PluginNativeView::standardRootChild(const QString &type) const
{
    return type == QStringLiteral("PluginSection") || type == QStringLiteral("PluginActionBar");
}
bool PluginNativeView::finalizePageWidgets()
{
    // Only declarative objects are public. Composite internals never enter objects_.
    for (auto it = objects_.cbegin(); it != objects_.cend(); ++it)
    {
        auto *container = qobject_cast<QWidget *>(it.value().data());
        if (!container || !PluginPageWidgets::composite(container)) continue;
        for (auto *child : container->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly))
        {
            if (objects_.value(child->objectName()) != child) continue;
            bool attached = true;
            if (auto *section = qobject_cast<PluginSection *>(container))
                attached = qobject_cast<PluginContentSlot *>(child) && section->attachSlot(qobject_cast<PluginContentSlot *>(child));
            else if (auto *row = qobject_cast<PluginSettingRow *>(container)) attached = row->attachEditor(child);
            else if (auto *bar = qobject_cast<PluginActionBar *>(container))
                attached = qobject_cast<ElaPushButton *>(child) && bar->insertAction(qobject_cast<ElaPushButton *>(child));
            if (!attached) { failure_ = QStringLiteral("标准组件内容槽无效：%1").arg(container->objectName()); return false; }
        }
    }
    for (auto it = objects_.cbegin(); it != objects_.cend(); ++it)
        if (PluginPageWidgets::managedLayout(it.value())) protectedObjects_.insert(it.key());
    return true;
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
namespace
{
QString cssId(const QString &name)
{
    QString result;
    for (auto ch : name)
        if (ch.isLetterOrNumber() || ch == QLatin1Char('_') || ch == QLatin1Char('-')) result += ch;
        else result += QLatin1Char('\\') + QString::number(ch.unicode(), 16) + QLatin1Char(' ');
    return result;
}
QString resolvedThemeStyle(QString style)
{
    const auto mode = eTheme->getThemeMode();
    const QMap<QString, QColor> colors{
        {QStringLiteral("window-text"), ElaThemeColor(mode, BasicText)},
        {QStringLiteral("text"), ElaThemeColor(mode, BasicText)},
        {QStringLiteral("button-text"), ElaThemeColor(mode, BasicText)},
        {QStringLiteral("highlight"), ElaThemeColor(mode, PrimaryNormal)},
        {QStringLiteral("highlighted-text"), ElaThemeColor(mode, BasicTextInvert)},
        {QStringLiteral("mid"), ElaThemeColor(mode, BasicBaseLine)},
        {QStringLiteral("dark"), ElaThemeColor(mode, BasicBaseDeep)},
        {QStringLiteral("light"), ElaThemeColor(mode, BasicBase)}};
    const QRegularExpression expression(QStringLiteral("palette\\(\\s*([a-z-]+)\\s*\\)"));
    auto matches = expression.globalMatch(style);
    QList<QRegularExpressionMatch> replacements;
    while (matches.hasNext()) replacements.append(matches.next());
    for (auto it = replacements.crbegin(); it != replacements.crend(); ++it)
        if (colors.contains(it->captured(1)))
            style.replace(it->capturedStart(), it->capturedLength(), colors.value(it->captured(1)).name(QColor::HexArgb));
    return style;
}
}
void PluginNativeView::applyStyle(QWidget *widget, const QString &style)
{
    const auto name = widget->objectName();
    pluginStyles_.insert(name, style);
    // Explicit, self-scoped host defaults also cover Qt's stylesheet palette
    // cache. Resolve permitted theme roles ourselves instead of the OS palette.
    QString combined = nativeStyles_.value(name) + QStringLiteral(
        "\n#%1{color:palette(text);selection-color:palette(highlighted-text);"
        "selection-background-color:palette(highlight);placeholder-text-color:%2;}"
        "#%1:disabled{color:%2;}\n").arg(cssId(name),
            ElaThemeColor(eTheme->getThemeMode(), BasicTextDisable).name(QColor::HexArgb)) + style;
    combined = resolvedThemeStyle(combined);
    const auto type = PluginWidgetRegistry::typeName(widget);
    if (widget == root_ || PluginPageWidgets::composite(widget) || QStringList{QStringLiteral("QWidget"), QStringLiteral("QFrame"),
        QStringLiteral("QGroupBox"), QStringLiteral("QScrollArea"), QStringLiteral("ElaScrollArea")}.contains(type))
    {
        widget->setAutoFillBackground(false);
        combined += QStringLiteral("\n#%1{background:transparent;}").arg(cssId(name));
    }
    if (qobject_cast<QTabWidget *>(widget))
        combined += QStringLiteral("\n#%1::pane{background:transparent;}").arg(cssId(name));
    if (combined.trimmed().isEmpty()) combined.clear();
    if (widget->styleSheet() != combined)
    {
        widget->setStyleSheet({});
        PluginWidgetRegistry::applyTheme(widget, false);
        widget->setStyleSheet(combined);
    }
}
void PluginNativeView::applyPageBackgrounds()
{
    if (component()) return;
    setAutoFillBackground(false);
    for (auto it = objects_.cbegin(); it != objects_.cend(); ++it)
        if (auto *widget = qobject_cast<QWidget *>(it.value().data()))
        {
            applyStyle(widget, pluginStyles_.value(it.key()));
            if (auto *scroll = qobject_cast<QScrollArea *>(widget))
            {
                scroll->setAutoFillBackground(false);
                scroll->viewport()->setAutoFillBackground(false);
                if (scroll->widget()) scroll->widget()->setAutoFillBackground(false);
            }
        }
}
QSize PluginNativeView::sizeHint() const
{
    return component() ? (root_ && root_->layout() ? root_->layout()->sizeHint() : QSize(0, metrics_.height))
                       : QWidget::sizeHint();
}
QSize PluginNativeView::minimumSizeHint() const
{
    return component() ? QSize(0, 0) : QWidget::minimumSizeHint();
}
void PluginNativeView::applyMetrics(const CourseBarMetrics &metrics)
{
    metrics_ = metrics;
    if (!component()) return;
    setFixedHeight(metrics.height);
    if (root_)
    {
        root_->setFixedHeight(metrics.height);
        if (auto *row = qobject_cast<CourseBarRowLayout *>(root_->layout())) row->setMetrics(metrics);
    }
    refreshComponent();
}
void PluginNativeView::refreshComponent()
{
    if (!component() || !root_) return;
    if (auto *row = root_->layout()) { row->invalidate(); row->activate(); }
    root_->updateGeometry();
    updateGeometry();
    emit availabilityChanged();
}
void PluginNativeView::registerItem(QWidget *widget)
{
    if (auto *item = qobject_cast<CourseBarItem *>(widget))
    {
        item->setMetrics(metrics_);
        item->installEventFilter(this);
        connect(item, &CourseBarItem::preferredSizeChanged, this, &PluginNativeView::refreshComponent);
    }
}
bool PluginNativeView::eventFilter(QObject *watched, QEvent *event)
{
    if (component() && (event->type() == QEvent::Show || event->type() == QEvent::Hide))
        refreshComponent();
    return QWidget::eventFilter(watched, event);
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
    {
        const auto property = args.value(QStringLiteral("property")).toString();
        if (component())
            if (auto *item = qobject_cast<CourseBarItem *>(object))
            {
                if (property == QStringLiteral("TextPixelSize")) return item->textFont().pixelSize();
                if (property == QStringLiteral("TextPointSize")) return QFontInfo(item->textFont()).pointSizeF();
            }
        return PluginWidgetRegistry::read(object, property, error);
    }
    if (method == QStringLiteral("set"))
    {
        const auto values = args.value(QStringLiteral("properties")).toObject();
        QJsonObject originals, normalized;
        for (auto it = values.begin(); it != values.end(); ++it)
        {
            if (!component() && ((layoutPreset_ == QStringLiteral("settings") && object == root_->layout()) || PluginPageWidgets::managedLayout(object)))
            { *error = QStringLiteral("标准布局的间距和尺寸由宿主管理"); return {}; }
            if (PluginPageWidgets::managedEditor(object) && QStringList{QStringLiteral("minimumWidth"), QStringLiteral("minimumHeight"),
                QStringLiteral("maximumWidth"), QStringLiteral("maximumHeight")}.contains(it.key()))
            { *error = QStringLiteral("标准设置行和操作栏的控件尺寸由宿主管理"); return {}; }
            if (component() && !qobject_cast<CourseBarItem *>(object))
            { *error = QStringLiteral("课程条根节点及布局由宿主管理"); return {}; }
            if (!component() && ((protectedObjects_.contains(object->objectName()) && QStringList{QStringLiteral("minimumWidth"),
                    QStringLiteral("minimumHeight"), QStringLiteral("maximumWidth"), QStringLiteral("maximumHeight")}.contains(it.key())) ||
                (protectedObjects_.contains(object->objectName()) && qobject_cast<QLayout *>(object) && it.key().endsWith(QStringLiteral("Margin")))))
            { *error = QStringLiteral("内容根尺寸和外边距由宿主管理"); return {}; }
            QJsonValue value = it.value();
            if (it.key() == QStringLiteral("styleSheet"))
            {
                if (layoutPreset_ == QStringLiteral("settings") && object == root_)
                { *error = QStringLiteral("标准设置根不接受自定义样式"); return {}; }
                if (component() || !value.isString())
                { *error = QStringLiteral("当前视图不接受此样式"); return {}; }
                value = Plugins::restrictedStyle(value.toString(), object->objectName(), false, error);
                if (!error->isEmpty()) return {};
                if (PluginPageWidgets::managedEditor(object) && Plugins::hasLayoutStyle(value.toString()))
                { *error = QStringLiteral("标准控件不接受改变尺寸的样式"); return {}; }
            }
            if (!PluginWidgetRegistry::write(object, it.key(), value, error, true)) return {};
            originals.insert(it.key(), it.key() == QStringLiteral("styleSheet") ? QJsonValue(pluginStyles_.value(object->objectName())) :
                PluginWidgetRegistry::read(object, it.key(), error));
            normalized.insert(it.key(), value);
        }
        for (auto it = normalized.begin(); it != normalized.end(); ++it)
        {
            if (it.key() == QStringLiteral("styleSheet")) applyStyle(qobject_cast<QWidget *>(object), it.value().toString());
            else if (it.key() == QStringLiteral("visible") && pending_.contains(object->objectName()))
            {
                if (it.value().toBool()) hiddenOnInsert_.remove(object->objectName());
                else hiddenOnInsert_.insert(object->objectName());
            }
            else if (!PluginWidgetRegistry::write(object, it.key(), it.value(), error))
            {
                for (auto old = originals.begin(); old != originals.end(); ++old)
                {
                    QString ignored;
                    if (old.key() == QStringLiteral("styleSheet")) applyStyle(qobject_cast<QWidget *>(object), old.value().toString());
                    else PluginWidgetRegistry::write(object, old.key(), old.value(), &ignored);
                }
                return {};
            }
        }
        if (auto *button = qobject_cast<ElaPushButton *>(object); button && qobject_cast<PluginActionBar *>(button->parentWidget()))
            AppUi::configureActionButton(button, button->property("primary").toBool());
        if (component()) refreshComponent();
        else applyPageBackgrounds();
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
        if (!parent || name.isEmpty() || name.size() > 128 || name.startsWith(QStringLiteral("_ctl")) || find(name) || objects_.size() >= 2000)
        {
            *error = QStringLiteral("父控件、名称或控件数无效");
            return {};
        }
        if (!component() && ((layoutPreset_ == QStringLiteral("settings") && parent == root_ && !standardRootChild(type)) ||
            !PluginPageWidgets::childAllowed(parent, type, error)))
        { if (error->isEmpty()) *error = QStringLiteral("标准设置根只能创建分组或操作栏"); return {}; }
        if (layoutPreset_ == QStringLiteral("settings"))
            if (auto *slot = qobject_cast<PluginContentSlot *>(parent); slot && slot->locked() &&
                type != QStringLiteral("PluginSettingRow") && type != QStringLiteral("QVBoxLayout"))
            { *error = QStringLiteral("设置分组只接受 PluginSettingRow"); return {}; }
        if (component() && (parent != root_ || type != QStringLiteral("CourseBarItem")))
        { *error = QStringLiteral("课程条只能在内容根下创建 CourseBarItem"); return {}; }
        if (!component() && type == QStringLiteral("CourseBarItem"))
        { *error = QStringLiteral("CourseBarItem 仅供课程条使用"); return {}; }
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
            if (auto *slot = qobject_cast<PluginContentSlot *>(parent); slot && slot->locked()) {
                slot->lockLayout(); protectedObjects_.insert(name);
            }
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
        auto native = created->styleSheet();
        if (!created->objectName().isEmpty()) native.replace(QLatin1Char('#') + created->objectName(), QLatin1Char('#') + cssId(name));
        nativeStyles_.insert(name, native);
        created->setObjectName(name);
        created->hide();
        pending_.insert(name);
        if (!component()) PluginWidgetRegistry::applyTheme(created);
        objects_.insert(name, created);
        connect(created, &QObject::destroyed, this, [this, name] {
            if (objects_.value(name).isNull())
                objects_.remove(name);
        });
        if (component()) registerItem(created);
        else { applyStyle(created, {}); wire(created); }
        return name;
    }
    if (method == QStringLiteral("remove"))
    {
        if (protectedObjects_.contains(object->objectName()))
        {
            *error = QStringLiteral("不能删除宿主管理的内容根或根布局");
            return {};
        }
        if (auto *layout = qobject_cast<QLayout *>(object); layout && layout->count() > 0)
        { *error = QStringLiteral("请先移除布局内容，再删除空布局"); return {}; }
        if (auto *widget = qobject_cast<QWidget *>(object)) {
            if (auto *row = qobject_cast<PluginSettingRow *>(widget->parentWidget())) row->detachEditor(widget);
            if (auto *bar = qobject_cast<PluginActionBar *>(widget->parentWidget()))
                if (auto *button = qobject_cast<ElaPushButton *>(widget)) bar->removeAction(button);
            if (auto *slot = qobject_cast<PluginContentSlot *>(widget))
                for (auto it = objects_.cbegin(); it != objects_.cend(); ++it)
                    if (auto *section = qobject_cast<PluginSection *>(it.value().data()); section && section->slot() == slot) section->detachSlot(slot);
        }
        const auto descendants = object->findChildren<QObject *>();
        for (auto *child : descendants)
        {
            const auto prefix = child->objectName() + QLatin1Char('/');
            subscriptions_.removeIf([&](const QString &key) { return key.startsWith(prefix); });
            protectedObjects_.remove(child->objectName());
            pending_.remove(child->objectName());
            hiddenOnInsert_.remove(child->objectName());
            nativeStyles_.remove(child->objectName());
            pluginStyles_.remove(child->objectName());
            objects_.remove(child->objectName());
            child->setObjectName({});
        }
        const auto prefix = object->objectName() + QLatin1Char('/');
        subscriptions_.removeIf([&](const QString &key) { return key.startsWith(prefix); });
        protectedObjects_.remove(object->objectName());
        pending_.remove(object->objectName());
        hiddenOnInsert_.remove(object->objectName());
        nativeStyles_.remove(object->objectName());
        pluginStyles_.remove(object->objectName());
        objects_.remove(object->objectName());
        object->setObjectName({});
        if (auto *widget = qobject_cast<QWidget *>(object))
            widget->hide();
        if (auto *widget = qobject_cast<QWidget *>(object); widget && widget->parentWidget() && widget->parentWidget()->layout())
            widget->parentWidget()->layout()->removeWidget(widget);
        object->deleteLater();
        if (component()) refreshComponent();
        return true;
    }
    if (method == QStringLiteral("insert"))
    {
        auto *widget = qobject_cast<QWidget *>(find(args.value(QStringLiteral("child")).toString()));
        auto *layout = qobject_cast<QLayout *>(object);
        if (!layout)
            if (auto *container = qobject_cast<QWidget *>(object))
                layout = container->layout();
        auto *container = qobject_cast<QWidget *>(object);
        auto *section = qobject_cast<PluginSection *>(container);
        auto *settingRow = qobject_cast<PluginSettingRow *>(container);
        auto *actions = qobject_cast<PluginActionBar *>(container);
        const bool semantic = section || settingRow || actions;
        if (!widget || widget == root_ || (!layout && !semantic) || widget == container ||
            (container && widget->isAncestorOf(container)) || (layout && (widget == layout->parentWidget() || widget->isAncestorOf(layout->parentWidget()))))
        { *error = QStringLiteral("插入对象或布局无效"); return {}; }
        if (!semantic && PluginPageWidgets::managedEditor(widget))
        { *error = QStringLiteral("受管理的编辑控件不能移出标准内容槽"); return {}; }
        const bool standardLayout = semantic || PluginPageWidgets::managedLayout(layout) ||
            (layoutPreset_ == QStringLiteral("settings") && layout == root_->layout());
        if (standardLayout) {
            for (const auto &key : args.keys())
                if (!QStringList{QStringLiteral("instanceId"), QStringLiteral("object"), QStringLiteral("child"), QStringLiteral("index"), QStringLiteral("_guard")}.contains(key))
                { *error = QStringLiteral("标准布局插入只接受 index"); return {}; }
            if (args.contains(QStringLiteral("index")) && (!args.value(QStringLiteral("index")).isDouble() ||
                args.value(QStringLiteral("index")).toDouble() != args.value(QStringLiteral("index")).toInt(-2) || args.value(QStringLiteral("index")).toInt() < -1))
            { *error = QStringLiteral("index 必须为 -1 或非负整数"); return {}; }
        }
        if (layoutPreset_ == QStringLiteral("settings") && layout == root_->layout()) {
            if (!standardRootChild(PluginWidgetRegistry::typeName(widget))) { *error = QStringLiteral("设置页只接受分组和末尾操作栏"); return {}; }
            QList<QWidget *> order;
            for (int i = 0; i < layout->count(); ++i) if (auto *item = layout->itemAt(i)->widget(); item && item != widget) order.append(item);
            const int index = args.value(QStringLiteral("index")).toInt(-1);
            if (index > order.size()) { *error = QStringLiteral("index 越界"); return {}; }
            order.insert(index < 0 ? order.size() : index, widget);
            bool found = false;
            for (auto *item : order) { if (found) { *error = QStringLiteral("操作栏必须唯一且位于末尾"); return {}; } found = qobject_cast<PluginActionBar *>(item); }
        }
        if (PluginPageWidgets::managedLayout(layout) && layoutPreset_ == QStringLiteral("settings") && !qobject_cast<PluginSettingRow *>(widget))
        { *error = QStringLiteral("设置分组只接受 PluginSettingRow"); return {}; }
        if (semantic) {
            const int index = args.value(QStringLiteral("index")).toInt(-1);
            if ((actions && index > actions->actionCount()) || (!actions && index > 0))
            { *error = QStringLiteral("内容槽 index 越界"); return {}; }
            if (PluginPageWidgets::managedEditor(widget) && widget->parentWidget() != container)
            { *error = QStringLiteral("编辑控件已属于其他标准组件，请先移除后重新创建"); return {}; }
            if (section) {
                auto *slot = qobject_cast<PluginContentSlot *>(widget);
                for (auto it = objects_.cbegin(); it != objects_.cend(); ++it)
                    if (auto *owner = qobject_cast<PluginSection *>(it.value().data()); owner && owner != section && owner->slot() == slot)
                    { *error = QStringLiteral("内容槽已属于其他分组"); return {}; }
                if (layoutPreset_ == QStringLiteral("settings") && slot && slot->layout())
                    for (int i = 0; i < slot->layout()->count(); ++i)
                        if (!qobject_cast<PluginSettingRow *>(slot->layout()->itemAt(i)->widget()))
                        { *error = QStringLiteral("标准设置内容槽只接受设置行"); return {}; }
            }
            const auto type = PluginWidgetRegistry::typeName(widget);
            if (!PluginPageWidgets::childAllowed(container, type, error)) return {};
            bool attached = section ? section->attachSlot(qobject_cast<PluginContentSlot *>(widget)) :
                settingRow ? settingRow->attachEditor(widget) : actions->insertAction(qobject_cast<ElaPushButton *>(widget), args.value(QStringLiteral("index")).toInt(-1));
            if (!attached) { *error = QStringLiteral("内容槽已占用或操作栏已有主按钮"); return {}; }
            if (auto *slot = qobject_cast<PluginContentSlot *>(widget); slot && slot->layout()) protectedObjects_.insert(slot->layout()->objectName());
        }
        else if (component())
        {
            auto *row = qobject_cast<CourseBarRowLayout *>(layout);
            auto *item = qobject_cast<CourseBarItem *>(widget);
            if (!row || layout != root_->layout() || !item)
            { *error = QStringLiteral("课程条仅允许标准展示项插入根布局"); return {}; }
            for (const auto &key : args.keys())
                if (!QStringList{QStringLiteral("instanceId"), QStringLiteral("object"), QStringLiteral("child"), QStringLiteral("index"), QStringLiteral("_guard")}.contains(key))
                { *error = QStringLiteral("课程条插入只接受 index，不支持 stretch 或网格坐标"); return {}; }
            if (args.contains(QStringLiteral("index")) && (!args.value(QStringLiteral("index")).isDouble() ||
                args.value(QStringLiteral("index")).toDouble() != args.value(QStringLiteral("index")).toInt(-2) ||
                args.value(QStringLiteral("index")).toInt() < -1 || args.value(QStringLiteral("index")).toInt() > row->count()))
            { *error = QStringLiteral("展示项 index 越界或不是整数"); return {}; }
            row->insertWidget(args.value(QStringLiteral("index")).toInt(-1), item);
        }
        else if (auto *box = qobject_cast<QBoxLayout *>(layout))
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
        pending_.remove(widget->objectName());
        widget->setVisible(!hiddenOnInsert_.remove(widget->objectName()));
        if (component()) refreshComponent();
        else applyPageBackgrounds();
        return true;
    }
    if (method == QStringLiteral("invoke"))
    {
        if (component()) { *error = QStringLiteral("课程条展示项不提供交互操作"); return {}; }
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
            int index = -1;
            if (auto *tabs = qobject_cast<QTabWidget *>(widget))
                index = tabs->addTab(child, options.value(QStringLiteral("title")).toString());
            else if (auto *stack = qobject_cast<QStackedWidget *>(widget)) index = stack->addWidget(child);
            if (index >= 0)
            {
                pending_.remove(child->objectName());
                if (hiddenOnInsert_.remove(child->objectName())) child->hide();
                applyPageBackgrounds();
                return index;
            }
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
    auto diagnostics = diagnostics_;
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
            if (auto *widget = qobject_cast<QWidget *>(object))
            {
                const auto font = qobject_cast<CourseBarItem *>(widget) ? qobject_cast<CourseBarItem *>(widget)->textFont() : widget->font();
                node.insert(QStringLiteral("font"), QJsonObject{{QStringLiteral("family"), font.family()},
                    {QStringLiteral("pixelSize"), font.pixelSize()}, {QStringLiteral("weight"), font.weight()},
                    {QStringLiteral("italic"), font.italic()}});
                node.insert(QStringLiteral("geometry"), QJsonObject{{QStringLiteral("x"), widget->x()},
                    {QStringLiteral("y"), widget->y()}, {QStringLiteral("width"), widget->width()}, {QStringLiteral("height"), widget->height()}});
                node.insert(QStringLiteral("backgroundManaged"), true);
                node.insert(QStringLiteral("pending"), pending_.contains(object->objectName()));
                auto *parentLayout = widget->parentWidget() ? widget->parentWidget()->layout() : nullptr;
                node.insert(QStringLiteral("layout"), parentLayout && parentLayout->indexOf(widget) >= 0
                    ? parentLayout->objectName() : QString());
            }
            if (pending_.contains(object->objectName()))
                diagnostics.append(QJsonObject{{QStringLiteral("code"), QStringLiteral("UI_UNATTACHED_WIDGET")},
                    {QStringLiteral("severity"), QStringLiteral("warning")}, {QStringLiteral("object"), object->objectName()},
                    {QStringLiteral("property"), QStringLiteral("layout")}, {QStringLiteral("message"), QStringLiteral("动态控件尚未挂入布局或页面容器")}});
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
            const auto role = qobject_cast<PluginSection *>(object) ? QStringLiteral("section") :
                qobject_cast<PluginContentSlot *>(object) ? QStringLiteral("contentSlot") :
                qobject_cast<PluginSettingRow *>(object) ? QStringLiteral("settingRow") :
                qobject_cast<PluginActionBar *>(object) ? QStringLiteral("actionBar") :
                PluginPageWidgets::managedEditor(object) ? QStringLiteral("editor") : QString();
            node.insert(QStringLiteral("layoutRole"), role);
            node.insert(QStringLiteral("hostManagedLayout"), PluginPageWidgets::composite(object) || PluginPageWidgets::managedLayout(object));
            if ((qobject_cast<PluginSection *>(object) && !qobject_cast<PluginSection *>(object)->slot()) ||
                (qobject_cast<PluginSettingRow *>(object) && !qobject_cast<PluginSettingRow *>(object)->editor()))
                diagnostics.append(QJsonObject{{QStringLiteral("code"), QStringLiteral("UI_CONTENT_SLOT_EMPTY")},
                    {QStringLiteral("severity"), QStringLiteral("warning")}, {QStringLiteral("object"), object->objectName()},
                    {QStringLiteral("message"), QStringLiteral("标准组件尚未挂入必要内容")}});
            nodes.append(node);
        }
    }
    return {{QStringLiteral("nodes"), nodes},
            {QStringLiteral("dirty"), dirty_},
            {QStringLiteral("available"), available()},
            {QStringLiteral("mode"), component() ? QStringLiteral("component") : kind_ == Plugins::UiKind::ComponentSettings ? QStringLiteral("componentSettings") : QStringLiteral("page")},
            {QStringLiteral("diagnostics"), diagnostics},
            {QStringLiteral("layoutPreset"), layoutPreset_.isEmpty() ? QStringLiteral("legacy") : layoutPreset_},
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
