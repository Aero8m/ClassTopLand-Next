#include "PluginUiPolicy.h"
#include "PluginContracts.h"
#include "../Model/CourseBarConfig.h"
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <memory>
#include <functional>
#include <vector>

namespace Plugins
{
QString validateUiSafety(const QString &path, const QString &package);
namespace
{
void warning(QJsonArray *out, const QString &object, const QString &property,
             const QString &message, const QString &code = QStringLiteral("UI_PROPERTY_IGNORED"))
{
    if (out && out->size() >= 128)
    {
        out->replace(127, QJsonObject{{QStringLiteral("code"), QStringLiteral("UI_DIAGNOSTICS_TRUNCATED")},
            {QStringLiteral("severity"), QStringLiteral("warning")},
            {QStringLiteral("message"), QStringLiteral("兼容诊断过多，仅保留前 127 项；请修复后重新检查")}});
        return;
    }
    if (out) out->append(QJsonObject{{QStringLiteral("code"), code},
        {QStringLiteral("severity"), QStringLiteral("warning")},
        {QStringLiteral("object"), object}, {QStringLiteral("property"), property},
        {QStringLiteral("message"), message}});
}
bool cssValue(const QString &property, const QString &value)
{
    if (property == QStringLiteral("font-weight"))
        return QRegularExpression(QStringLiteral("\\A(?:normal|bold|[1-9]00)\\z")).match(value).hasMatch();
    if (property == QStringLiteral("font-style"))
        return value == QStringLiteral("normal") || value == QStringLiteral("italic");
    if (property == QStringLiteral("text-decoration"))
        return QStringList{QStringLiteral("none"), QStringLiteral("underline"),
                           QStringLiteral("line-through")}.contains(value);
    if (property == QStringLiteral("border-style"))
        return QStringList{QStringLiteral("none"), QStringLiteral("solid"), QStringLiteral("dashed"),
                           QStringLiteral("dotted")}.contains(value);
    if (property == QStringLiteral("color") || property == QStringLiteral("border-color"))
        return QRegularExpression(QStringLiteral(
            "\\Apalette\\(\\s*(?:window-text|text|button-text|highlight|highlighted-text|mid|dark|light)\\s*\\)\\z"))
            .match(value).hasMatch();
    const bool padding = property == QStringLiteral("padding") ||
        QStringList{QStringLiteral("padding-left"), QStringLiteral("padding-top"),
                    QStringLiteral("padding-right"), QStringLiteral("padding-bottom")}.contains(property);
    const int max = padding ? 256 : property == QStringLiteral("border-width") ? 8 :
                    property == QStringLiteral("border-radius") ? 64 : -1;
    if (max < 0) return false;
    const auto parts = value.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (parts.isEmpty() || parts.size() > (property == QStringLiteral("padding") ? 4 : 1)) return false;
    for (const auto &part : parts)
    {
        const auto match = QRegularExpression(QStringLiteral("\\A([0-9]{1,3})(?:px)?\\z")).match(part);
        if (!match.hasMatch() || match.captured(1).toInt() > max) return false;
    }
    return true;
}
struct Node
{
    QString tag, text;
    QMap<QString, QString> attributes;
    std::vector<std::unique_ptr<Node>> children;
    qint64 line = 0, column = 0;
    QString name() const { return attributes.value(QStringLiteral("name")); }
    QString type() const { return attributes.value(QStringLiteral("class")); }
};
std::unique_ptr<Node> readNode(QXmlStreamReader &reader)
{
    auto node = std::make_unique<Node>();
    node->tag = reader.name().toString();
    node->line = reader.lineNumber();
    node->column = reader.columnNumber();
    for (const auto &attr : reader.attributes()) node->attributes.insert(attr.name().toString(), attr.value().toString());
    while (!reader.atEnd())
    {
        reader.readNext();
        if (reader.isStartElement()) node->children.push_back(readNode(reader));
        else if (reader.isCharacters()) node->text += reader.text();
        else if (reader.isEndElement()) break;
    }
    return node;
}
void writeNode(QXmlStreamWriter &writer, const Node &node)
{
    writer.writeStartElement(node.tag);
    for (auto it = node.attributes.cbegin(); it != node.attributes.cend(); ++it) writer.writeAttribute(it.key(), it.value());
    if (!node.text.trimmed().isEmpty() || node.children.empty()) writer.writeCharacters(node.text);
    for (const auto &child : node.children) writeNode(writer, *child);
    writer.writeEndElement();
}
QList<Node *> children(Node &node, const QString &tag)
{
    QList<Node *> result;
    for (const auto &child : node.children) if (child->tag == tag) result.append(child.get());
    return result;
}
QString propertyText(const Node &node)
{
    return node.children.empty() ? QString() : node.children.front()->text;
}
void setPropertyText(Node &node, const QString &value)
{
    if (!node.children.empty()) node.children.front()->text = value;
}
QString location(const QString &path, const Node &node, const QString &message)
{
    return QStringLiteral("%1:%2:%3 [%4] %5").arg(path).arg(node.line).arg(node.column).arg(node.name(), message);
}
}
bool hasLayoutStyle(const QString &style)
{
    return QRegularExpression(QStringLiteral("(?:[;{])\\s*(?:padding(?:-[a-z]+)?|border-width|border-radius)\\s*:"))
        .match(style).hasMatch();
}
bool singleLineText(const QString &text)
{
    return text.size() <= 32768 && !text.contains(QLatin1Char('\n')) && !text.contains(QLatin1Char('\r')) &&
        !text.contains(QChar(0x2028)) && !text.contains(QChar(0x2029));
}
QString restrictedStyle(const QString &style, const QString &object, bool compatibility,
                        QString *error, QJsonArray *diagnostics)
{
    auto invalid = [&](const QString &message) -> QString {
        if (compatibility) warning(diagnostics, object, QStringLiteral("styleSheet"), message,
                                   QStringLiteral("UI_STYLE_IGNORED"));
        else *error = message;
        return {};
    };
    if (style.trimmed().isEmpty()) return {};
    if (style.size() > 32768) return invalid(QStringLiteral("样式超过 32 KiB"));
    // Tokenize comments and rule boundaries before inspecting individual declarations.
    QString source;
    for (qsizetype i = 0; i < style.size(); ++i)
    {
        if (style.mid(i, 2) == QStringLiteral("/*"))
        {
            const auto end = style.indexOf(QStringLiteral("*/"), i + 2);
            if (end < 0) return invalid(QStringLiteral("样式注释未闭合"));
            source += QLatin1Char(' ');
            i = end + 1;
        }
        else source += style[i];
    }
    struct Rule { QString selector; QStringList declarations; };
    QList<Rule> rules;
    qsizetype offset = 0;
    while (offset < source.size())
    {
        if (source.mid(offset).trimmed().isEmpty()) break;
        const auto open = source.indexOf(QLatin1Char('{'), offset);
        const auto close = source.indexOf(QLatin1Char('}'), open + 1);
        if (open < 0 || close < 0 || source.mid(open + 1, close - open - 1).contains(QLatin1Char('{')) ||
            source.mid(offset, open - offset).contains(QLatin1Char('}')))
            return invalid(QStringLiteral("样式必须由完整的对象选择器规则组成"));
        Rule rule{source.mid(offset, open - offset).trimmed(), {}};
        const auto selector = QRegularExpression(QStringLiteral("\\A#%1(?::(?:hover|pressed|checked|disabled|focus))*\\z")
            .arg(QRegularExpression::escape(object)));
        const bool safeName = QRegularExpression(QStringLiteral("\\A[A-Za-z_][A-Za-z0-9_-]*\\z")).match(object).hasMatch();
        if (!safeName || !selector.match(rule.selector).hasMatch())
        {
            if (!compatibility) return invalid(QStringLiteral("样式只允许当前对象 #objectName 及受支持状态"));
            warning(diagnostics, object, QStringLiteral("styleSheet"), QStringLiteral("忽略跨对象或不受支持的样式选择器"), QStringLiteral("UI_STYLE_IGNORED"));
        }
        else
        {
            const auto body = source.mid(open + 1, close - open - 1);
            for (const auto &entry : body.split(QLatin1Char(';')))
            {
                if (entry.trimmed().isEmpty()) continue;
                const auto colon = entry.indexOf(QLatin1Char(':'));
                const auto property = entry.left(colon).trimmed().toLower();
                const auto value = entry.mid(colon + 1).trimmed().toLower();
                if (colon <= 0 || !cssValue(property, value))
                {
                    if (!compatibility) return invalid(QStringLiteral("样式属性或值未开放：%1").arg(property));
                    warning(diagnostics, object, property, QStringLiteral("忽略不受支持的样式声明"), QStringLiteral("UI_STYLE_IGNORED"));
                }
                else rule.declarations.append(property + QLatin1Char(':') + value + QLatin1Char(';'));
            }
            rules.append(rule);
        }
        offset = close + 1;
    }
    QString result;
    for (const auto &rule : rules)
        if (!rule.declarations.isEmpty()) result += rule.selector + QLatin1Char('{') + rule.declarations.join(QString()) + QLatin1Char('}');
    return result;
}
PreparedUi prepareUi(const QString &path, const QString &package, UiKind kind, const QString &layoutPreset)
{
    PreparedUi result;
    if ((!layoutPreset.isEmpty() && layoutPreset != QStringLiteral("settings") && layoutPreset != QStringLiteral("content")) ||
        (kind == UiKind::Component && !layoutPreset.isEmpty()) ||
        (kind == UiKind::ComponentSettings && layoutPreset == QStringLiteral("content")))
    { result.error = QStringLiteral("UI_LAYOUT_PRESET_INVALID：版式与视图用途不匹配"); return result; }
    result.error = validateUiSafety(path, package);
    if (!result.error.isEmpty()) return result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { result.error = file.errorString(); return result; }
    QXmlStreamReader reader(file.readAll());
    reader.readNextStartElement();
    auto document = readNode(reader);
    const auto roots = children(*document, QStringLiteral("widget"));
    if (document->tag != QStringLiteral("ui") || roots.size() != 1)
    { result.error = QStringLiteral("UI 必须包含一个内容根 QWidget"); return result; }
    auto *root = roots.first();
    const bool component = kind == UiKind::Component;
    auto fail = [&](Node &node, const QString &message) { result.error = location(path, node, message); };
    auto warn = [&](Node &node, const QString &property, const QString &message) {
        warning(&result.diagnostics, node.name(), property, message);
        auto diagnostic = result.diagnostics.last().toObject();
        diagnostic.insert(QStringLiteral("file"), path);
        diagnostic.insert(QStringLiteral("line"), node.line);
        diagnostic.insert(QStringLiteral("column"), node.column);
        result.diagnostics.replace(result.diagnostics.size() - 1, diagnostic);
    };
    if (children(*root, QStringLiteral("layout")).size() != 1)
    { fail(*root, QStringLiteral("内容根必须有一个布局")); return result; }
    Node *row = children(*root, QStringLiteral("layout")).first();
    const bool settings = layoutPreset == QStringLiteral("settings");
    const QSet<QString> composites{QStringLiteral("PluginSection"), QStringLiteral("PluginContentSlot"),
        QStringLiteral("PluginSettingRow"), QStringLiteral("PluginActionBar")};
    QSet<QString> managedControls, managedLayouts, actionButtons;
    if (!component && layoutPreset.isEmpty())
        warning(&result.diagnostics, root->name(), QStringLiteral("layoutPreset"),
                QStringLiteral("当前按旧布局兼容加载；新页面请声明 settings/content 标准版式"), QStringLiteral("UI_LAYOUT_LEGACY"));
    auto itemWidgets = [&](Node &layout) {
        QList<Node *> widgets;
        for (auto *item : children(layout, QStringLiteral("item"))) {
            auto entries = children(*item, QStringLiteral("widget"));
            if (entries.size() != 1 || item->children.size() != 1) {
                fail(*item, QStringLiteral("标准设置布局只接受组件，不接受嵌套布局或 spacer")); return QList<Node *>();
            }
            widgets.append(entries.first());
        }
        return widgets;
    };
    if (settings) {
        if (row->type() != QStringLiteral("QVBoxLayout")) { fail(*row, QStringLiteral("标准设置页根布局必须为 QVBoxLayout")); return result; }
        managedLayouts.insert(row->name());
        const auto items = itemWidgets(*row);
        bool actions = false;
        for (auto *item : items) {
            if (actions || (item->type() != QStringLiteral("PluginSection") && item->type() != QStringLiteral("PluginActionBar"))) {
                fail(*item, QStringLiteral("设置页只能排列分组和一个末尾操作栏")); return result;
            }
            actions = item->type() == QStringLiteral("PluginActionBar");
        }
    }
    std::function<void(Node &)> structure = [&](Node &node) {
        if (!result.error.isEmpty()) return;
        if (!layoutPreset.isEmpty() && (node.tag == QStringLiteral("widget") || node.tag == QStringLiteral("layout")) && node.name().startsWith(QStringLiteral("_ctl")))
        { fail(node, QStringLiteral("标准版式保留 _ctl 内部名称前缀")); return; }
        if (node.tag == QStringLiteral("widget")) {
            const auto widgets = children(node, QStringLiteral("widget"));
            const auto layouts = children(node, QStringLiteral("layout"));
            const auto type = node.type();
            if (composites.contains(type) && component) { fail(node, QStringLiteral("标准页面组件不能用于课程条")); return; }
            if (type == QStringLiteral("PluginSection")) {
                if (!layouts.isEmpty() || widgets.size() != 1 || widgets.first()->type() != QStringLiteral("PluginContentSlot"))
                { fail(node, QStringLiteral("PluginSection 必须直接声明一个 PluginContentSlot，不能另设布局")); return; }
                auto *slot = widgets.first();
                const auto slotLayouts = children(*slot, QStringLiteral("layout"));
                if (slotLayouts.size() != 1 || slotLayouts.first()->type() != QStringLiteral("QVBoxLayout"))
                { fail(*slot, QStringLiteral("分组内容槽必须使用一个 QVBoxLayout")); return; }
                managedLayouts.insert(slotLayouts.first()->name());
                if (settings) for (auto *entry : itemWidgets(*slotLayouts.first()))
                    if (entry->type() != QStringLiteral("PluginSettingRow")) { fail(*entry, QStringLiteral("标准设置分组只接受 PluginSettingRow")); return; }
            } else if (type == QStringLiteral("PluginSettingRow")) {
                if (!layouts.isEmpty() || widgets.size() != 1 || composites.contains(widgets.first()->type()))
                { fail(node, QStringLiteral("PluginSettingRow 必须直接声明一个业务编辑控件")); return; }
                managedControls.insert(widgets.first()->name());
            } else if (type == QStringLiteral("PluginActionBar")) {
                if (!layouts.isEmpty()) { fail(node, QStringLiteral("操作栏布局由宿主管理")); return; }
                int primary = 0;
                for (auto *button : widgets) {
                    if (button->type() != QStringLiteral("ElaPushButton")) { fail(*button, QStringLiteral("操作栏只接受 ElaPushButton")); return; }
                    managedControls.insert(button->name());
                    actionButtons.insert(button->name());
                    for (auto *property : children(*button, QStringLiteral("property"))) if (property->name() == QStringLiteral("primary")) {
                        if (property->children.size() != 1 || property->children.front()->tag != QStringLiteral("bool") ||
                            (propertyText(*property) != QStringLiteral("true") && propertyText(*property) != QStringLiteral("false")))
                        { fail(*button, QStringLiteral("primary 必须为 bool")); return; }
                        if (propertyText(*property) == QStringLiteral("true")) ++primary;
                    }
                }
                if (primary > 1) { fail(node, QStringLiteral("每个操作栏最多一个主按钮")); return; }
            } else if (type == QStringLiteral("PluginContentSlot") && layouts.size() != 1) {
                fail(node, QStringLiteral("静态内容槽必须声明一个布局")); return;
            }
        }
        for (const auto &child : node.children) structure(*child);
    };
    structure(*root);
    if (!result.error.isEmpty()) return result;
    if (component)
    {
        const auto items = children(*row, QStringLiteral("item"));
        const bool single = items.size() == 1 && children(*items.first(), QStringLiteral("widget")).size() == 1;
        if (row->type() == QStringLiteral("QVBoxLayout") && single)
        {
            row->attributes[QStringLiteral("class")] = QStringLiteral("QHBoxLayout");
            warn(*row, QStringLiteral("class"), QStringLiteral("单文字纵向布局已按标准横向布局加载"));
        }
        if (row->type() != QStringLiteral("QHBoxLayout"))
        { fail(*row, QStringLiteral("课程条只能使用标准 QHBoxLayout")); return result; }
        if (!children(*root, QStringLiteral("widget")).isEmpty())
        { fail(*root, QStringLiteral("课程条展示项必须位于根布局内")); return result; }
        for (auto *item : items)
        {
            const auto widgets = children(*item, QStringLiteral("widget"));
            if (widgets.size() != 1 || item->children.size() != 1)
            { fail(*item, QStringLiteral("课程条不允许嵌套布局或 spacer")); return result; }
            auto *widget = widgets.first();
            if (widget->type() == QStringLiteral("QLabel") || widget->type() == QStringLiteral("ElaText"))
            {
                for (auto *property : children(*widget, QStringLiteral("property")))
                    if (property->name() == QStringLiteral("pixmap"))
                    { fail(*widget, QStringLiteral("图片控件不能自动转换为课程条文字项")); return result; }
                widget->attributes[QStringLiteral("class")] = QStringLiteral("CourseBarItem");
                warn(*widget, QStringLiteral("class"), QStringLiteral("旧文字控件按标准 CourseBarItem 加载"));
            }
            if (widget->type() != QStringLiteral("CourseBarItem") ||
                !children(*widget, QStringLiteral("widget")).isEmpty() || !children(*widget, QStringLiteral("layout")).isEmpty())
            { fail(*widget, QStringLiteral("课程条仅允许 CourseBarItem；复杂交互请放入设置页")); return result; }
        }
    }
    std::function<void(Node &, bool)> visit = [&](Node &node, bool isRoot) {
        if (!result.error.isEmpty()) return;
        const bool widget = node.tag == QStringLiteral("widget");
        const bool layout = node.tag == QStringLiteral("layout");
        if (widget && !component && node.type() == QStringLiteral("CourseBarItem"))
        { fail(node, QStringLiteral("CourseBarItem 仅供课程条组件使用")); return; }
        if (widget && !component && QStringList{QStringLiteral("QWidget"), QStringLiteral("QFrame"), QStringLiteral("QGroupBox")}.contains(node.type()))
        {
            if (!children(node, QStringLiteral("widget")).isEmpty())
            { fail(node, QStringLiteral("普通子控件必须通过布局 item 管理")); return; }
            if (children(node, QStringLiteral("layout")).size() > 1)
            { fail(node, QStringLiteral("控件不能有多个布局")); return; }
        }
        if (component && layout && &node != row)
        { fail(node, QStringLiteral("课程条不允许嵌套布局")); return; }
        if (component && (node.tag == QStringLiteral("spacer") || node.tag == QStringLiteral("action") || node.tag == QStringLiteral("addaction")))
        { fail(node, QStringLiteral("课程条不允许 spacer")); return; }
        if (component && (layout || node.tag == QStringLiteral("item")))
        {
            for (const auto &key : {QStringLiteral("stretch"), QStringLiteral("alignment")})
                if (node.attributes.remove(key)) warn(node, key, QStringLiteral("课程条布局参数由宿主管理"));
        }
        if (managedLayouts.contains(node.name())) {
            for (const auto &key : {QStringLiteral("stretch"), QStringLiteral("alignment")})
                if (node.attributes.remove(key)) warn(node, key, QStringLiteral("标准布局参数由宿主管理"));
            for (auto *item : children(node, QStringLiteral("item")))
                if (item->attributes.remove(QStringLiteral("alignment"))) warn(node, QStringLiteral("alignment"), QStringLiteral("设置项对齐由宿主管理"));
        }
        for (auto it = node.children.begin(); it != node.children.end();)
        {
            auto &child = **it;
            bool remove = false;
            if ((widget || layout) && child.tag == QStringLiteral("property"))
            {
                const auto name = child.name();
                if (!component && node.type() == QStringLiteral("ElaPushButton") && name == QStringLiteral("primary") && !actionButtons.contains(node.name())) {
                    fail(node, QStringLiteral("primary 仅可用于 PluginActionBar 内的按钮")); return;
                }
                if (settings && isRoot && (name == QStringLiteral("styleSheet") || name == QStringLiteral("font"))) {
                    warn(node, name, QStringLiteral("标准设置根外观由宿主管理")); it = node.children.erase(it); continue;
                }
                const bool composite = widget && composites.contains(node.type());
                const bool managed = managedControls.contains(node.name()) || managedLayouts.contains(node.name());
                const QSet<QString> geometry{QStringLiteral("geometry"), QStringLiteral("minimumSize"), QStringLiteral("maximumSize"),
                    QStringLiteral("sizePolicy"), QStringLiteral("minimumWidth"), QStringLiteral("maximumWidth"),
                    QStringLiteral("minimumHeight"), QStringLiteral("maximumHeight")};
                if (composite) {
                    const auto properties = widgetCatalog().value(node.type()).toObject().value(QStringLiteral("properties")).toArray();
                    if (!properties.contains(name)) {
                        warn(node, name, QStringLiteral("标准组件的外观和布局由宿主管理"));
                        it = node.children.erase(it); continue;
                    }
                    if (QStringList{QStringLiteral("title"), QStringLiteral("description"), QStringLiteral("message")}.contains(name) &&
                        (child.children.size() != 1 || child.children.front()->tag != QStringLiteral("string"))) {
                        fail(node, QStringLiteral("标准组件文字属性必须为 string")); return;
                    }
                }
                if (managed && (geometry.contains(name) || managedLayouts.contains(node.name()))) {
                    warn(node, name, QStringLiteral("标准布局参数由宿主管理"));
                    it = node.children.erase(it); continue;
                }
                if (component)
                {
                    const QSet<QString> allowed{QStringLiteral("text"), QStringLiteral("toolTip"), QStringLiteral("visible"),
                                               QStringLiteral("active"), QStringLiteral("progressValue"),
                                               QStringLiteral("leftMargin"), QStringLiteral("rightMargin")};
                    remove = isRoot || layout || !allowed.contains(name);
                    if (!remove)
                    {
                        const auto scalar = child.children.size() == 1 ? child.children.front()->tag : QString();
                        const auto expected = name == QStringLiteral("visible") || name == QStringLiteral("active")
                            ? QStringLiteral("bool") : QStringList{QStringLiteral("progressValue"), QStringLiteral("leftMargin"), QStringLiteral("rightMargin")}.contains(name)
                                ? QStringLiteral("number") : QStringLiteral("string");
                        if (scalar != expected || (expected == QStringLiteral("bool") && propertyText(child) != QStringLiteral("true") && propertyText(child) != QStringLiteral("false")))
                        { fail(node, QStringLiteral("展示项属性类型无效：%1").arg(name)); return; }
                    }
                    if (!remove && name == QStringLiteral("text") && !singleLineText(propertyText(child)))
                    {
                        auto text = propertyText(child);
                        if (text.size() > 32768) { fail(node, QStringLiteral("文字超过 32 Ki 字符")); return; }
                        text.replace(QRegularExpression(QStringLiteral("[\\r\\n\\x{2028}\\x{2029}]")), QStringLiteral(" "));
                        setPropertyText(child, text);
                        warn(node, name, QStringLiteral("课程条换行已替换为空格"));
                    }
                    if (!remove && name == QStringLiteral("progressValue"))
                    {
                        bool ok = false;
                        const int value = propertyText(child).toInt(&ok);
                        if (!ok || value < 0 || value > 10000) { fail(node, QStringLiteral("progressValue 必须为 0～10000 整数")); return; }
                    }
                    if (!remove && (name == QStringLiteral("leftMargin") || name == QStringLiteral("rightMargin")))
                    {
                        bool ok = false;
                        const int value = propertyText(child).toInt(&ok);
                        if (!ok || value < -1 || value > CourseBarConfig::MaximumSpacing)
                        { fail(node, QStringLiteral("左右边距必须为 -1 或 0～100 整数")); return; }
                    }
                }
                else
                {
                    remove = widget && (QStringList{QStringLiteral("objectName"), QStringLiteral("palette"), QStringLiteral("autoFillBackground"),
                        QStringLiteral("backgroundRole"), QStringLiteral("foregroundRole")}.contains(name) ||
                        (isRoot && QStringList{QStringLiteral("geometry"), QStringLiteral("minimumSize"),
                            QStringLiteral("maximumSize"), QStringLiteral("sizePolicy")}.contains(name)));
                    if (name == QStringLiteral("styleSheet"))
                    {
                        QString error;
                        const auto before = result.diagnostics.size();
                        const auto style = restrictedStyle(propertyText(child), node.name(), true, &error, &result.diagnostics);
                        if (managedControls.contains(node.name()) && hasLayoutStyle(style)) {
                            setPropertyText(child, {});
                            warn(node, name, QStringLiteral("标准编辑控件和按钮不接受改变尺寸的样式"));
                        } else setPropertyText(child, style);
                        for (qsizetype i = before; i < result.diagnostics.size(); ++i)
                        {
                            auto entry = result.diagnostics[i].toObject();
                            entry.insert(QStringLiteral("file"), path);
                            entry.insert(QStringLiteral("line"), child.line);
                            entry.insert(QStringLiteral("column"), child.column);
                            result.diagnostics.replace(i, entry);
                        }
                    }
                    if (layout && &node == row && QStringList{QStringLiteral("leftMargin"), QStringLiteral("topMargin"),
                        QStringLiteral("rightMargin"), QStringLiteral("bottomMargin"), QStringLiteral("margin")}.contains(name))
                        remove = true;
                }
                if (remove) warn(node, name, QStringLiteral("此属性由宿主管理，已忽略"));
            }
            if (remove) it = node.children.erase(it);
            else { visit(child, false); ++it; }
        }
    };
    visit(*root, true);
    if (!result.error.isEmpty()) return result;
    QXmlStreamWriter writer(&result.xml);
    writer.writeStartDocument();
    writeNode(writer, *document);
    writer.writeEndDocument();
    return result;
}
QString validateUi(const QString &path, const QString &package, UiKind kind)
{
    return prepareUi(path, package, kind).error;
}
}
