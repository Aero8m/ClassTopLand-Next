#include "PluginView.h"
#include "../../Core/Plugins/PluginManifest.h"
#include "../UiWidgets.h"
#include <ElaText.h>
#include <ElaLineEdit.h>
#include <ElaPushButton.h>
#include <ElaToggleSwitch.h>
#include <ElaComboBox.h>
#include <ElaSpinBox.h>
#include <QPlainTextEdit>
#include <ElaPlainTextEdit.h>
#include <QProgressBar>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QRegularExpression>
#include <QApplication>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QPointer>
#include <QTimer>
#include <QTextCursor>

QString PluginView::check(const QJsonObject &definition)
{
    return Plugins::validateView(definition);
}

PluginView::PluginView(const QJsonObject &definition, const QJsonObject &values, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("pluginNativeView"));
    setStyleSheet(QStringLiteral("#pluginNativeView { background: transparent; }"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);
    failure_ = check(definition);
    if (!failure_.isEmpty())
    {
        layout->addWidget(AppUi::text(failure_, this));
        return;
    }
    rootId_ = definition.value(QStringLiteral("tree")).toObject().value(QStringLiteral("id")).toString();
    available_ =
        definition.value(QStringLiteral("tree")).toObject().value(QStringLiteral("visible")).toBool(true);
    layout->addWidget(build(definition.value(QStringLiteral("tree")).toObject(), this));
    layout->addStretch();
    auto initial = defaults_;
    for (auto it = values.begin(); it != values.end(); ++it)
        initial.insert(it.key(), it.value());
    setValues(initial);
    baseline_ = values_;
}
QWidget *PluginView::build(const QJsonObject &node, QWidget *parent)
{
    const auto type = node.value(QStringLiteral("type")).toString();
    const auto id = node.value(QStringLiteral("id")).toString();
    const auto label = node.value(QStringLiteral("label")).toString();
    QWidget *control = nullptr;
    if (type == QStringLiteral("column") || type == QStringLiteral("row") ||
        type == QStringLiteral("group") || type == QStringLiteral("scroll"))
    {
        auto *body = new QWidget(parent);
        QBoxLayout *layout = type == QStringLiteral("row") ? static_cast<QBoxLayout *>(new QHBoxLayout(body))
                                                           : new QVBoxLayout(body);
        layout->setContentsMargins(type == QStringLiteral("group") ? 12 : 0, 0, 0, 0);
        layout->setSpacing(12);
        if (!label.isEmpty())
            layout->addWidget(AppUi::text(label, body, 16));
        for (const auto &child : node.value(QStringLiteral("children")).toArray())
            layout->addWidget(build(child.toObject(), body));
        control = body;
    }
    else if (type == QStringLiteral("input"))
    {
        auto *edit = new ElaLineEdit(parent);
        edit->setMaxLength(qBound(1, node.value(QStringLiteral("maxLength")).toInt(4096), 4096));
        edit->setPlaceholderText(node.value(QStringLiteral("placeholder")).toString());
        connect(edit, &ElaLineEdit::textChanged, this,
                [this, id](const QString &value) { fieldChanged(id, value); });
        control = edit;
    }
    else if (type == QStringLiteral("multiline"))
    {
        auto *edit = new ElaPlainTextEdit(parent);
        edit->setMinimumHeight(80);
        edit->setMaximumHeight(180);
        AppUi::enableTouchScrolling(edit);
        connect(edit, &QPlainTextEdit::textChanged, this,
                [this, id, edit] { fieldChanged(id, edit->toPlainText()); });
        control = edit;
    }
    else if (type == QStringLiteral("toggle"))
    {
        auto *toggle = new ElaToggleSwitch(parent);
        connect(toggle, &ElaToggleSwitch::toggled, this, [this, id](bool value) { fieldChanged(id, value); });
        control = toggle;
    }
    else if (type == QStringLiteral("select"))
    {
        auto *combo = new ElaComboBox(parent);
        for (const auto &value : node.value(QStringLiteral("items")).toArray())
        {
            if (value.isObject())
            {
                const auto item = value.toObject();
                combo->addItem(item.value(QStringLiteral("label")).toString(),
                               item.value(QStringLiteral("value")).toVariant());
            }
            else
                combo->addItem(value.toString(), value.toVariant());
        }
        connect(combo, &ElaComboBox::currentIndexChanged, this,
                [this, id, combo] { fieldChanged(id, QJsonValue::fromVariant(combo->currentData())); });
        control = combo;
    }
    else if (type == QStringLiteral("number"))
    {
        auto *spin = new ElaSpinBox(parent);
        spin->setRange(node.value(QStringLiteral("min")).toInt(-1000000),
                       node.value(QStringLiteral("max")).toInt(1000000));
        spin->setKeyboardTracking(false);
        connect(spin, &ElaSpinBox::valueChanged, this, [this, id](int value) { fieldChanged(id, value); });
        control = spin;
    }
    else if (type == QStringLiteral("button"))
    {
        auto *button = AppUi::button(node.value(QStringLiteral("text")).toString(), parent);
        const auto actionId = node.value(QStringLiteral("action")).toString(id);
        connect(button, &ElaPushButton::clicked, this, [this, actionId] { emit action(actionId, values_); });
        control = button;
    }
    else if (type == QStringLiteral("progress"))
    {
        auto *progress = new QProgressBar(parent);
        progress->setRange(0, 100);
        progress->setValue(qBound(0, node.value(QStringLiteral("value")).toInt(), 100));
        control = progress;
    }
    else if (type == QStringLiteral("separator"))
    {
        auto *frame = new QFrame(parent);
        frame->setFrameShape(QFrame::HLine);
        control = frame;
    }
    else
    {
        QString content = node.value(QStringLiteral("text")).toString();
        if (type == QStringLiteral("list"))
        {
            QStringList items;
            for (const auto &value : node.value(QStringLiteral("items")).toArray())
                items.append(value.toString());
            content = items.join(QLatin1Char('\n'));
        }
        auto *text = AppUi::text(content, parent);
        text->setTextFormat(Qt::PlainText);
        text->setWordWrap(true);
        text->setMinimumWidth(0);
        control = text;
    }
    if (!id.isEmpty())
    {
        widgets_[id] = control;
        control->setObjectName(QStringLiteral("pluginNode_") + id);
        control->setAccessibleName(label.isEmpty() ? id : label);
    }
    const bool field =
        QStringList{QStringLiteral("input"), QStringLiteral("multiline"), QStringLiteral("toggle"),
                    QStringLiteral("select"), QStringLiteral("number")}
            .contains(type);
    if (field)
    {
        fields_[id] = node;
        auto value = node.value(QStringLiteral("value"));
        if (value.isUndefined())
        {
            if (type == QStringLiteral("toggle"))
                value = false;
            else if (type == QStringLiteral("number"))
                value = 0;
            else if (type == QStringLiteral("select"))
                value = QJsonValue::fromVariant(qobject_cast<ElaComboBox *>(control)->currentData());
            else
                value = QString();
        }
        defaults_.insert(id, value);
    }
    control->setEnabled(node.value(QStringLiteral("enabled")).toBool(true));
    control->setVisible(node.value(QStringLiteral("visible")).toBool(true));
    if ((field && !label.isEmpty()) || !node.value(QStringLiteral("description")).toString().isEmpty())
    {
        auto *wrapper = new QWidget(parent);
        auto *layout = new QVBoxLayout(wrapper);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);
        if (field && !label.isEmpty())
            layout->addWidget(AppUi::text(label, wrapper));
        layout->addWidget(control);
        const auto description = node.value(QStringLiteral("description")).toString();
        if (!description.isEmpty())
        {
            auto *text = AppUi::text(description, wrapper, 12);
            text->setWordWrap(true);
            layout->addWidget(text);
        }
        return wrapper;
    }
    return control;
}
void PluginView::fieldChanged(const QString &id, const QJsonValue &value)
{
    if (refreshing_)
        return;
    values_.insert(id, value);
    emit changed();
    emit action(fields_.value(id).value(QStringLiteral("action")).toString(id), value);
}
void PluginView::setValues(const QJsonObject &values)
{
    refreshing_ = true;
    values_ = values;
    for (auto it = fields_.begin(); it != fields_.end(); ++it)
    {
        auto *widget = widgets_.value(it.key());
        const auto value = values_.value(it.key());
        if (auto *edit = qobject_cast<ElaLineEdit *>(widget)) {
            if (edit->text() != value.toString()) {
                const int cursor = edit->cursorPosition(), start = edit->selectionStart();
                const int length = edit->selectedText().size();
                edit->setText(value.toString());
                if (edit->hasFocus()) {
                    if (start >= 0) edit->setSelection(qMin(start, edit->text().size()), length);
                    else edit->setCursorPosition(qMin(cursor, edit->text().size()));
                }
            }
        }
        else if (auto *edit = qobject_cast<QPlainTextEdit *>(widget)) {
            if (edit->toPlainText() != value.toString()) {
                const auto previous = edit->textCursor();
                const int scroll = edit->verticalScrollBar()->value();
                edit->setPlainText(value.toString());
                if (edit->hasFocus()) {
                    auto cursor = edit->textCursor();
                    cursor.setPosition(qMin(previous.anchor(), edit->toPlainText().size()));
                    cursor.setPosition(qMin(previous.position(), edit->toPlainText().size()), QTextCursor::KeepAnchor);
                    edit->setTextCursor(cursor);
                    edit->verticalScrollBar()->setValue(scroll);
                }
            }
        }
        else if (auto *toggle = qobject_cast<ElaToggleSwitch *>(widget))
            toggle->setIsToggled(value.toBool());
        else if (auto *combo = qobject_cast<ElaComboBox *>(widget))
            combo->setCurrentIndex(combo->findData(value.toVariant()));
        else if (auto *spin = qobject_cast<ElaSpinBox *>(widget))
            spin->setValue(value.toInt());
    }
    refreshing_ = false;
    emit changed();
}
void PluginView::markSaved()
{
    baseline_ = values_;
    emit changed();
}
QString PluginView::validate() const
{
    if (!failure_.isEmpty())
        return failure_;
    for (auto it = fields_.begin(); it != fields_.end(); ++it)
    {
        const auto value = values_.value(it.key());
        const auto node = it.value();
        const auto type = node.value(QStringLiteral("type")).toString();
        QString error;
        if (type == QStringLiteral("input") || type == QStringLiteral("multiline"))
        {
            if (!value.isString())
                error = QStringLiteral("必须是文本");
            else if (node.value(QStringLiteral("required")).toBool() && value.toString().trimmed().isEmpty())
                error = QStringLiteral("不能为空");
            else if (value.toString().size() >
                     qBound(1, node.value(QStringLiteral("maxLength")).toInt(4096), 4096))
                error = QStringLiteral("文本过长");
        }
        else if (type == QStringLiteral("toggle") && !value.isBool())
            error = QStringLiteral("必须是开关值");
        else if (type == QStringLiteral("number") &&
                 (!value.isDouble() || value.toDouble() != value.toInt() ||
                  value.toInt() < node.value(QStringLiteral("min")).toInt(-1000000) ||
                  value.toInt() > node.value(QStringLiteral("max")).toInt(1000000)))
            error = QStringLiteral("数值超出范围");
        else if (type == QStringLiteral("select") &&
                 qobject_cast<ElaComboBox *>(widgets_.value(it.key()))->findData(value.toVariant()) < 0)
            error = QStringLiteral("请选择有效选项");
        if (!error.isEmpty())
            return node.value(QStringLiteral("label")).toString(it.key()) + QStringLiteral("：") + error;
    }
    return {};
}
void PluginView::patch(const QJsonObject &patch)
{
    for (auto it = patch.begin(); it != patch.end(); ++it)
    {
        auto *widget = widgets_.value(it.key());
        if (!widget || !it.value().isObject())
            continue;
        const auto attributes = it.value().toObject();
        if (it.key() == rootId_ && attributes.value(QStringLiteral("visible")).isBool())
            available_ = attributes.value(QStringLiteral("visible")).toBool();
        if (attributes.value(QStringLiteral("enabled")).isBool())
            widget->setEnabled(attributes.value(QStringLiteral("enabled")).toBool());
        if (attributes.value(QStringLiteral("visible")).isBool())
            widget->setVisible(attributes.value(QStringLiteral("visible")).toBool());
        if (attributes.value(QStringLiteral("text")).isString())
        {
            if (auto *text = qobject_cast<ElaText *>(widget))
                text->setText(attributes.value(QStringLiteral("text")).toString().left(4096));
        }
        if (attributes.contains(QStringLiteral("value")))
        {
            if (fields_.contains(it.key()))
            {
                if (values_.value(it.key()) == baseline_.value(it.key()))
                {
                    auto values = values_;
                    values.insert(it.key(), attributes.value(QStringLiteral("value")));
                    const bool wasDirty = dirty();
                    setValues(values);
                    baseline_.insert(it.key(), attributes.value(QStringLiteral("value")));
                    if (!wasDirty)
                        baseline_ = values_;
                }
            }
            else if (auto *progress = qobject_cast<QProgressBar *>(widget))
                progress->setValue(qBound(0, attributes.value(QStringLiteral("value")).toInt(), 100));
        }
    }
    emit changed();
}
void PluginView::setTree(const QJsonObject &tree)
{
    if (!check(QJsonObject{{QStringLiteral("tree"), tree}}).isEmpty())
        return;
    auto *focus = QApplication::focusWidget();
    const auto focusId = focus && isAncestorOf(focus) ? focus->objectName() : QString();
    int cursor = -1, selection = -1, selectionLength = 0;
    if (auto *edit = qobject_cast<ElaLineEdit *>(focus))
    {
        cursor = edit->cursorPosition();
        selection = edit->selectionStart();
        selectionLength = edit->selectedText().size();
    }
    QTextCursor textCursor;
    if (auto *edit = qobject_cast<QPlainTextEdit *>(focus))
        textCursor = edit->textCursor();
    QPointer<QAbstractScrollArea> scroll;
    for (auto *parent = parentWidget(); parent; parent = parent->parentWidget())
        if (auto *area = qobject_cast<QAbstractScrollArea *>(parent))
        {
            scroll = area;
            break;
        }
    const int scrollPosition = scroll ? scroll->verticalScrollBar()->value() : 0;
    const auto oldValues = values_, oldBaseline = baseline_;
    while (auto *item = layout()->takeAt(0))
    {
        if (auto *widget = item->widget())
        {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    widgets_.clear();
    fields_.clear();
    defaults_ = QJsonObject();
    rootId_ = tree.value(QStringLiteral("id")).toString();
    available_ = tree.value(QStringLiteral("visible")).toBool(true);
    auto *body = qobject_cast<QVBoxLayout *>(layout());
    body->addWidget(build(tree, this));
    body->addStretch();
    auto values = defaults_;
    for (auto it = oldValues.begin(); it != oldValues.end(); ++it)
        values.insert(it.key(), it.value());
    setValues(values);
    baseline_ = oldBaseline;
    for (auto it = defaults_.begin(); it != defaults_.end(); ++it)
        if (!baseline_.contains(it.key()))
            baseline_.insert(it.key(), it.value());
    const auto id = focusId.startsWith(QStringLiteral("pluginNode_")) ? focusId.mid(11) : QString();
    if (auto *next = widgets_.value(id); next && !focusId.isEmpty())
    {
        next->setFocus();
        if (auto *edit = qobject_cast<ElaLineEdit *>(next))
        {
            if (selection >= 0)
                edit->setSelection(selection, selectionLength);
            else if (cursor >= 0)
                edit->setCursorPosition(cursor);
        }
        else if (auto *edit = qobject_cast<QPlainTextEdit *>(next); edit && !textCursor.isNull())
        {
            auto position = edit->textCursor();
            position.setPosition(qMin(textCursor.anchor(), edit->toPlainText().size()));
            position.setPosition(qMin(textCursor.position(), edit->toPlainText().size()),
                                 QTextCursor::KeepAnchor);
            edit->setTextCursor(position);
        }
    }
    if (scroll)
        QTimer::singleShot(0, this,
                           [scroll, scrollPosition]
                           {
                               if (scroll)
                                   scroll->verticalScrollBar()->setValue(scrollPosition);
                           });
    emit changed();
}
