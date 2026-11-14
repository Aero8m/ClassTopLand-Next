#include "PluginWidgetRegistry.h"
#include "../../Core/Plugins/PluginContracts.h"
#include "../../Core/ThemeManager/ThemeManager.h"
#include <ElaCheckBox.h>
#include <ElaComboBox.h>
#include <ElaDoubleSpinBox.h>
#include <ElaLineEdit.h>
#include <ElaListView.h>
#include <ElaPlainTextEdit.h>
#include <ElaProgressBar.h>
#include <ElaPushButton.h>
#include <ElaRadioButton.h>
#include <ElaScrollArea.h>
#include <ElaSlider.h>
#include <ElaSpinBox.h>
#include <ElaTabWidget.h>
#include <ElaTableView.h>
#include <ElaText.h>
#include <ElaTheme.h>
#include <ElaToggleSwitch.h>
#include <ElaToolButton.h>
#include <ElaTreeView.h>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMetaProperty>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimeEdit>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>
void PluginWidgetRegistry::bindTheme(QWidget *widget)
{
    auto update = [widget] {
        const auto mode = eTheme->getThemeMode();
        auto palette = widget->palette();
        palette.setColor(QPalette::Window, ElaThemeColor(mode, WindowBase));
        palette.setColor(QPalette::Base, ElaThemeColor(mode, BasicBase));
        palette.setColor(QPalette::AlternateBase, ElaThemeColor(mode, BasicAlternating));
        palette.setColor(QPalette::Text, ElaThemeColor(mode, BasicText));
        palette.setColor(QPalette::WindowText, ElaThemeColor(mode, BasicText));
        palette.setColor(QPalette::ButtonText, ElaThemeColor(mode, BasicText));
        palette.setColor(QPalette::Button, ElaThemeColor(mode, BasicBase));
        palette.setColor(QPalette::Highlight, ElaThemeColor(mode, PrimaryNormal));
        palette.setColor(QPalette::HighlightedText, ElaThemeColor(mode, BasicTextInvert));
        palette.setColor(QPalette::Disabled, QPalette::Text, ElaThemeColor(mode, BasicTextDisable));
        widget->setPalette(palette);
        // Native Qt viewports may have an explicit platform palette. Apply the
        // host colors after construction and on theme changes as well.
        for (auto *child : widget->findChildren<QWidget *>())
            child->setPalette(palette);
    };
    update();
    QTimer::singleShot(0, widget, update);
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, widget, update);
    QObject::connect(&ThemeManager::instance(), &ThemeManager::themeColorChanged, widget, update);
}
QWidget *PluginWidgetRegistry::create(const QString &type, QWidget *parent)
{
    if (type == QStringLiteral("QWidget"))
        return new QWidget(parent);
    if (type == QStringLiteral("QLabel"))
        return new QLabel(parent);
    if (type == QStringLiteral("QPushButton"))
        return new QPushButton(parent);
    if (type == QStringLiteral("QToolButton"))
        return new QToolButton(parent);
    if (type == QStringLiteral("QCheckBox"))
        return new QCheckBox(parent);
    if (type == QStringLiteral("QRadioButton"))
        return new QRadioButton(parent);
    if (type == QStringLiteral("QGroupBox"))
        return new QGroupBox(parent);
    if (type == QStringLiteral("QFrame"))
        return new QFrame(parent);
    if (type == QStringLiteral("QLineEdit"))
        return new QLineEdit(parent);
    if (type == QStringLiteral("QTextEdit"))
        return new QTextEdit(parent);
    if (type == QStringLiteral("QPlainTextEdit"))
        return new QPlainTextEdit(parent);
    if (type == QStringLiteral("QSpinBox"))
        return new QSpinBox(parent);
    if (type == QStringLiteral("QDoubleSpinBox"))
        return new QDoubleSpinBox(parent);
    if (type == QStringLiteral("QComboBox"))
        return new QComboBox(parent);
    if (type == QStringLiteral("QSlider"))
        return new QSlider(parent);
    if (type == QStringLiteral("QProgressBar"))
        return new QProgressBar(parent);
    if (type == QStringLiteral("QScrollArea"))
        return new QScrollArea(parent);
    if (type == QStringLiteral("QTabWidget"))
        return new QTabWidget(parent);
    if (type == QStringLiteral("QStackedWidget"))
        return new QStackedWidget(parent);
    if (type == QStringLiteral("QListWidget"))
        return new QListWidget(parent);
    if (type == QStringLiteral("QTreeWidget"))
        return new QTreeWidget(parent);
    if (type == QStringLiteral("QTableWidget"))
        return new QTableWidget(parent);
    if (type == QStringLiteral("QDateEdit"))
        return new QDateEdit(parent);
    if (type == QStringLiteral("QTimeEdit"))
        return new QTimeEdit(parent);
    if (type == QStringLiteral("QDateTimeEdit"))
        return new QDateTimeEdit(parent);
    if (type == QStringLiteral("QSplitter"))
        return new QSplitter(parent);
    if (type == QStringLiteral("QListView"))
        return new QListView(parent);
    if (type == QStringLiteral("QTreeView"))
        return new QTreeView(parent);
    if (type == QStringLiteral("QTableView"))
        return new QTableView(parent);
    if (type == QStringLiteral("ElaText"))
    {
        auto *text = new ElaText(parent);
        text->setFont(QFont());
        return text;
    }
    if (type == QStringLiteral("ElaPushButton"))
        return new ElaPushButton(parent);
    if (type == QStringLiteral("ElaToolButton"))
        return new ElaToolButton(parent);
    if (type == QStringLiteral("ElaCheckBox"))
        return new ElaCheckBox(parent);
    if (type == QStringLiteral("ElaRadioButton"))
        return new ElaRadioButton(parent);
    if (type == QStringLiteral("ElaLineEdit"))
        return new ElaLineEdit(parent);
    if (type == QStringLiteral("ElaPlainTextEdit"))
        return new ElaPlainTextEdit(parent);
    if (type == QStringLiteral("ElaSpinBox"))
        return new ElaSpinBox(parent);
    if (type == QStringLiteral("ElaDoubleSpinBox"))
        return new ElaDoubleSpinBox(parent);
    if (type == QStringLiteral("ElaComboBox"))
        return new ElaComboBox(parent);
    if (type == QStringLiteral("ElaSlider"))
        return new ElaSlider(parent);
    if (type == QStringLiteral("ElaProgressBar"))
        return new ElaProgressBar(parent);
    if (type == QStringLiteral("ElaScrollArea"))
        return new ElaScrollArea(parent);
    if (type == QStringLiteral("ElaTabWidget"))
        return new ElaTabWidget(parent);
    if (type == QStringLiteral("ElaToggleSwitch"))
        return new PluginToggleSwitch(parent);
    if (type == QStringLiteral("ElaListView"))
        return new ElaListView(parent);
    if (type == QStringLiteral("ElaTreeView"))
        return new ElaTreeView(parent);
    if (type == QStringLiteral("ElaTableView"))
        return new ElaTableView(parent);
    return nullptr;
}
QString PluginWidgetRegistry::typeName(QObject *object)
{
    const auto catalog = Plugins::widgetCatalog();
    for (auto meta = object->metaObject(); meta; meta = meta->superClass())
        if (catalog.contains(QString::fromLatin1(meta->className())))
            return QString::fromLatin1(meta->className());
    return {};
}
QJsonValue PluginWidgetRegistry::read(QObject *object, const QString &name, QString *error)
{
    const auto properties = Plugins::widgetCatalog()
                                .value(typeName(object))
                                .toObject()
                                .value(QStringLiteral("properties"))
                                .toArray();
    if (!properties.contains(name))
    {
        *error = QStringLiteral("属性未开放：%1").arg(name);
        return {};
    }
    if (auto toggle = qobject_cast<ElaToggleSwitch *>(object); toggle && name == QStringLiteral("checked"))
        return toggle->getIsToggled();
    const auto propertyName =
        object->metaObject()->indexOfProperty(name.toUtf8()) >= 0 ? name : QStringLiteral("p") + name;
    const auto value = object->property(propertyName.toUtf8());
    if (value.metaType().id() == QMetaType::QDate)
        return value.toDate().toString(Qt::ISODate);
    if (value.metaType().id() == QMetaType::QTime)
        return value.toTime().toString(Qt::ISODate);
    if (value.metaType().id() == QMetaType::QDateTime)
        return value.toDateTime().toString(Qt::ISODate);
    return QJsonValue::fromVariant(value);
}
bool PluginWidgetRegistry::write(QObject *object, const QString &name, const QJsonValue &value,
                                 QString *error)
{
    read(object, name, error);
    if (!error->isEmpty())
        return false;
    if (auto toggle = qobject_cast<ElaToggleSwitch *>(object); toggle && name == QStringLiteral("checked"))
    {
        if (!value.isBool())
        {
            *error = QStringLiteral("checked 必须为 boolean");
            return false;
        }
        toggle->setIsToggled(value.toBool());
        return true;
    }
    const auto propertyName =
        object->metaObject()->indexOfProperty(name.toUtf8()) >= 0 ? name : QStringLiteral("p") + name;
    const auto index = object->metaObject()->indexOfProperty(propertyName.toUtf8());
    if (index < 0)
    {
        *error = QStringLiteral("控件未实现此属性");
        return false;
    }
    const auto prop = object->metaObject()->property(index);
    auto converted = value.toVariant();
    if (prop.metaType().id() == QMetaType::QDate)
        converted = QDate::fromString(value.toString(), Qt::ISODate);
    if (prop.metaType().id() == QMetaType::QTime)
        converted = QTime::fromString(value.toString(), Qt::ISODate);
    if (prop.metaType().id() == QMetaType::QDateTime)
        converted = QDateTime::fromString(value.toString(), Qt::ISODate);
    if (!prop.isWritable() || !converted.convert(prop.metaType()) || !prop.write(object, converted))
    {
        *error = QStringLiteral("属性类型错误或不可写：%1").arg(name);
        return false;
    }
    return true;
}
QJsonValue PluginWidgetRegistry::invoke(QWidget *object, const QString &operation, const QJsonObject &args,
                                        QString *error)
{
    const auto operations = Plugins::widgetCatalog()
                                .value(typeName(object))
                                .toObject()
                                .value(QStringLiteral("operations"))
                                .toArray();
    if (!operations.contains(operation))
    {
        *error = QStringLiteral("操作未开放：%1").arg(operation);
        return {};
    }
    if (operation == QStringLiteral("click"))
    {
        qobject_cast<QAbstractButton *>(object)->click();
        return true;
    }
    if (operation == QStringLiteral("select"))
    {
        auto *view = qobject_cast<QAbstractItemView *>(object);
        const auto row = args.value(QStringLiteral("row")).toInt(-1),
                   col = args.value(QStringLiteral("column")).toInt(0);
        if (!view || !view->model() || row < 0 || col < 0 || row >= view->model()->rowCount() ||
            col >= view->model()->columnCount())
        {
            *error = QStringLiteral("选中位置无效");
            return {};
        }
        view->setCurrentIndex(view->model()->index(row, col));
        return true;
    }
    if (operation == QStringLiteral("setItems"))
    {
        const auto items = args.value(QStringLiteral("items")).toArray();
        if (items.size() > 2000)
        {
            *error = QStringLiteral("最多 2000 项，请分页展示");
            return {};
        }
        if (auto combo = qobject_cast<QComboBox *>(object))
        {
            combo->clear();
            for (const auto &item : items)
                combo->addItem(item.isString() ? item.toString()
                                               : item.toObject().value(QStringLiteral("text")).toString(),
                               item.toObject().value(QStringLiteral("value")).toVariant());
        }
        else if (auto list = qobject_cast<QListWidget *>(object))
        {
            list->clear();
            for (const auto &item : items)
                list->addItem(item.isString() ? item.toString()
                                              : item.toObject().value(QStringLiteral("text")).toString());
        }
        else if (auto table = qobject_cast<QTableWidget *>(object))
        {
            const auto columns = args.value(QStringLiteral("columns")).toArray();
            if (columns.size() > 64)
            {
                *error = QStringLiteral("最多 64 列");
                return {};
            }
            table->clear();
            table->setColumnCount(columns.size());
            table->setRowCount(items.size());
            QStringList headers;
            for (const auto &column : columns)
                headers.append(column.toString());
            table->setHorizontalHeaderLabels(headers);
            for (int row = 0; row < items.size(); ++row)
                for (int col = 0; col < columns.size(); ++col)
                    table->setItem(
                        row, col,
                        new QTableWidgetItem((col < items[row].toArray().size()
                                                  ? items[row].toArray().at(col).toVariant().toString()
                                                  : QString())));
        }
        else if (auto tree = qobject_cast<QTreeWidget *>(object))
        {
            tree->clear();
            tree->setColumnCount(1);
            std::function<void(const QJsonArray &, QTreeWidgetItem *, int)> add =
                [&](const QJsonArray &values, QTreeWidgetItem *parent, int depth) {
                    if (depth > 16)
                        return;
                    for (const auto &value : values)
                    {
                        auto item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree);
                        item->setText(0, value.isString()
                                             ? value.toString()
                                             : value.toObject().value(QStringLiteral("text")).toString());
                        add(value.toObject().value(QStringLiteral("children")).toArray(), item, depth + 1);
                    }
                };
            add(items, nullptr, 0);
        }
        else if (auto view = qobject_cast<QAbstractItemView *>(object))
        {
            auto model = new QStandardItemModel(view);
            const auto columns = args.value(QStringLiteral("columns")).toArray();
            if (columns.size() > 64)
            {
                delete model;
                *error = QStringLiteral("最多 64 列");
                return {};
            }
            QStringList headers;
            for (const auto &column : columns)
                headers.append(column.toString());
            model->setHorizontalHeaderLabels(headers);
            std::function<void(const QJsonArray &, QStandardItem *, int)> add = [&](const QJsonArray &values,
                                                                                    QStandardItem *parent,
                                                                                    int depth) {
                if (depth > 16)
                    return;
                for (const auto &value : values)
                {
                    QList<QStandardItem *> row;
                    if (value.isArray())
                    {
                        for (const auto &cell : value.toArray())
                            row.append(new QStandardItem(cell.toVariant().toString()));
                    }
                    else
                        row.append(new QStandardItem(
                            value.isString() ? value.toString()
                                             : value.toObject().value(QStringLiteral("text")).toString()));
                    if (row.isEmpty())
                        row.append(new QStandardItem);
                    parent->appendRow(row);
                    add(value.toObject().value(QStringLiteral("children")).toArray(), row.first(), depth + 1);
                }
            };
            add(items, model->invisibleRootItem(), 0);
            auto previous = view->model();
            auto previousSelection = view->selectionModel();
            view->setModel(model);
            if (previousSelection && previousSelection->parent() == view)
                previousSelection->deleteLater();
            if (previous && previous->parent() == view)
                previous->deleteLater();
        }
        return true;
    }
    *error = QStringLiteral("此操作由视图会话执行");
    return {};
}
