#include "PluginContracts.h"
#include "PluginManifest.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>
#include <cmath>

namespace Plugins
{
QJsonObject widgetCatalog()
{
    static const QJsonObject catalog = [] {
        QFile file(QStringLiteral(":/agent/plugins/widgets.json"));
        return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object()
                                              : QJsonObject();
    }();
    return catalog;
}
QString validateUi(const QString &path, const QString &package)
{
    QFile file(path);
    if (!within(package, path) || !file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return QStringLiteral("UI 文件不存在、越界或超过 1 MiB：%1").arg(path);
    QXmlStreamReader xml(file.readAll());
    const auto catalog = widgetCatalog();
    QSet<QString> names;
    int nodes = 0, depth = 0;
    bool rootWidget = false;
    QString property;
    while (!xml.atEnd())
    {
        xml.readNext();
        if (xml.isDTD() || xml.isEntityReference())
            return QStringLiteral("UI 不接受 DTD 或实体");
        if (xml.isStartElement())
        {
            if (++depth > 64 || ++nodes > 20000)
                return QStringLiteral("UI 结构过大");
            const auto tag = xml.name();
            if (tag == QStringLiteral("property"))
                property = xml.attributes().value(QStringLiteral("name")).toString();
            if (tag == QStringLiteral("widget") || tag == QStringLiteral("layout") ||
                tag == QStringLiteral("action"))
            {
                const auto name = xml.attributes().value(QStringLiteral("name")).toString();
                const auto type = xml.attributes().value(QStringLiteral("class")).toString();
                if (name.isEmpty() || names.contains(name))
                    return QStringLiteral("objectName 缺失或重复：%1").arg(name);
                if (names.size() >= 2000)
                    return QStringLiteral("UI 控件和布局最多 2000 个");
                names.insert(name);
                if (tag != QStringLiteral("action") && !catalog.contains(type))
                    return QStringLiteral("未注册控件或布局：%1").arg(type);
                if (tag == QStringLiteral("widget") && !rootWidget)
                {
                    if (type != QStringLiteral("QWidget"))
                        return QStringLiteral("UI 根控件必须为 QWidget");
                    rootWidget = true;
                }
            }
            // Connections belong to JS controllers. Never load Designer plugins, scripts or qrc files.
            if (tag == QStringLiteral("connection") || tag == QStringLiteral("script") ||
                tag == QStringLiteral("include"))
                return QStringLiteral("UI 不支持 connection、script 或外部 resource include");
            if (tag == QStringLiteral("pixmap") || tag == QStringLiteral("normaloff") ||
                tag == QStringLiteral("normalon") || tag == QStringLiteral("disabledoff") ||
                tag == QStringLiteral("disabledon") || tag == QStringLiteral("activeoff") ||
                tag == QStringLiteral("activeon") || tag == QStringLiteral("selectedoff") ||
                tag == QStringLiteral("selectedon"))
            {
                const auto resource = xml.readElementText();
                --depth;
                if (!resource.isEmpty() &&
                    (!within(package, QDir(package).filePath(resource)) || QDir::isAbsolutePath(resource)))
                    return QStringLiteral("UI 资源必须位于插件包内：%1").arg(resource);
            }
            if (tag == QStringLiteral("string") && property == QStringLiteral("styleSheet"))
            {
                const auto style = xml.readElementText();
                --depth;
                const QRegularExpression urls(QStringLiteral("url\\s*\\(\\s*['\"]?([^)'\"]+)"),
                                              QRegularExpression::CaseInsensitiveOption);
                auto matches = urls.globalMatch(style);
                while (matches.hasNext())
                {
                    const auto resource = matches.next().captured(1).trimmed();
                    if (QDir::isAbsolutePath(resource) || !within(package, QDir(package).filePath(resource)))
                        return QStringLiteral("样式资源必须位于插件包内：%1").arg(resource);
                }
            }
        }
        else if (xml.isEndElement())
        {
            if (xml.name() == QStringLiteral("property"))
                property.clear();
            --depth;
        }
    }
    if (xml.hasError())
        return QStringLiteral("UI XML 错误（%1:%2）：%3")
            .arg(xml.lineNumber())
            .arg(xml.columnNumber())
            .arg(xml.errorString());
    return rootWidget ? QString() : QStringLiteral("UI 缺少根控件");
}
QString validateSchema(const QJsonObject &schema, int depth)
{
    if (depth > 16)
        return QStringLiteral("schema 嵌套过深");
    static const QSet<QString> keywords{QStringLiteral("type"),        QStringLiteral("properties"),
                                        QStringLiteral("required"),    QStringLiteral("additionalProperties"),
                                        QStringLiteral("items"),       QStringLiteral("enum"),
                                        QStringLiteral("maxItems"),    QStringLiteral("maxLength"),
                                        QStringLiteral("minimum"),     QStringLiteral("maximum"),
                                        QStringLiteral("description"), QStringLiteral("title")};
    for (auto it = schema.begin(); it != schema.end(); ++it)
        if (!keywords.contains(it.key()))
            return QStringLiteral("不支持的 schema 关键字：%1").arg(it.key());
    for (const auto &key : {QStringLiteral("maxItems"), QStringLiteral("maxLength")})
        if (schema.contains(key) &&
            (!schema.value(key).isDouble() || schema.value(key).toDouble() != schema.value(key).toInt(-1) ||
             schema.value(key).toInt(-1) < 0))
            return QStringLiteral("schema 范围必须为非负整数");
    for (const auto &key : {QStringLiteral("minimum"), QStringLiteral("maximum")})
        if (schema.contains(key) && !schema.value(key).isDouble())
            return QStringLiteral("schema 边界必须为数值");
    if (schema.contains(QStringLiteral("required")) && !schema.value(QStringLiteral("required")).isArray())
        return QStringLiteral("required 必须为数组");
    if (schema.contains(QStringLiteral("properties")) &&
        !schema.value(QStringLiteral("properties")).isObject())
        return QStringLiteral("properties 必须为对象");
    if (schema.contains(QStringLiteral("enum")) && (!schema.value(QStringLiteral("enum")).isArray() ||
                                                    schema.value(QStringLiteral("enum")).toArray().isEmpty()))
        return QStringLiteral("enum 必须为非空数组");
    if (schema.contains(QStringLiteral("additionalProperties")) &&
        !schema.value(QStringLiteral("additionalProperties")).isBool())
        return QStringLiteral("additionalProperties 必须为 boolean");
    const auto type = schema.value(QStringLiteral("type")).toString();
    if (!QStringList{QStringLiteral("object"), QStringLiteral("array"), QStringLiteral("string"),
                     QStringLiteral("number"), QStringLiteral("integer"), QStringLiteral("boolean"),
                     QStringLiteral("null")}
             .contains(type))
        return QStringLiteral("schema 需要受支持的 type");
    // Keep schemas bounded before recursive inspection.
    if (QJsonDocument(schema).toJson(QJsonDocument::Compact).size() > 16384)
        return QStringLiteral("schema 过大");
    if (type == QStringLiteral("object"))
    {
        const auto properties = schema.value(QStringLiteral("properties")).toObject();
        for (auto it = properties.begin(); it != properties.end(); ++it)
        {
            if (!it.value().isObject())
                return QStringLiteral("properties 必须包含 schema 对象");
            const auto error = validateSchema(it.value().toObject(), depth + 1);
            if (!error.isEmpty())
                return error;
        }
        for (const auto &name : schema.value(QStringLiteral("required")).toArray())
            if (!name.isString() || !properties.contains(name.toString()))
                return QStringLiteral("required 引用了未知属性");
    }
    if (type == QStringLiteral("array"))
        return validateSchema(schema.value(QStringLiteral("items")).toObject(), depth + 1);
    return {};
}
QString validateJson(const QJsonValue &value, const QJsonObject &schema, int depth)
{
    if (depth > 16)
        return QStringLiteral("数据嵌套过深");
    const auto type = schema.value(QStringLiteral("type")).toString();
    const bool valid =
        (type == QStringLiteral("object") && value.isObject()) ||
        (type == QStringLiteral("array") && value.isArray()) ||
        (type == QStringLiteral("string") && value.isString()) ||
        (type == QStringLiteral("boolean") && value.isBool()) ||
        (type == QStringLiteral("null") && value.isNull()) ||
        ((type == QStringLiteral("number") || type == QStringLiteral("integer")) && value.isDouble() &&
         std::isfinite(value.toDouble()) &&
         (type != QStringLiteral("integer") || std::floor(value.toDouble()) == value.toDouble()));
    if (!valid)
        return QStringLiteral("数据类型不符合 schema：%1").arg(type);
    if (schema.contains(QStringLiteral("enum")) &&
        !schema.value(QStringLiteral("enum")).toArray().contains(value))
        return QStringLiteral("值不在 enum 中");
    if (value.isObject())
    {
        const auto object = value.toObject(),
                   properties = schema.value(QStringLiteral("properties")).toObject();
        for (const auto &required : schema.value(QStringLiteral("required")).toArray())
            if (!object.contains(required.toString()))
                return QStringLiteral("缺少必填属性：%1").arg(required.toString());
        for (auto it = object.begin(); it != object.end(); ++it)
        {
            if (!properties.contains(it.key()))
            {
                if (!schema.value(QStringLiteral("additionalProperties")).toBool(false))
                    return QStringLiteral("未知属性：%1").arg(it.key());
            }
            else
            {
                const auto error = validateJson(it.value(), properties.value(it.key()).toObject(), depth + 1);
                if (!error.isEmpty())
                    return it.key() + QStringLiteral(": ") + error;
            }
        }
    }
    if (value.isArray())
    {
        if (value.toArray().size() > schema.value(QStringLiteral("maxItems")).toInt(2000))
            return QStringLiteral("数组过长");
        for (const auto &item : value.toArray())
        {
            const auto error =
                validateJson(item, schema.value(QStringLiteral("items")).toObject(), depth + 1);
            if (!error.isEmpty())
                return error;
        }
    }
    if (value.isString() && value.toString().size() > schema.value(QStringLiteral("maxLength")).toInt(32768))
        return QStringLiteral("字符串过长");
    if (value.isDouble() && ((schema.contains(QStringLiteral("minimum")) &&
                              value.toDouble() < schema.value(QStringLiteral("minimum")).toDouble()) ||
                             (schema.contains(QStringLiteral("maximum")) &&
                              value.toDouble() > schema.value(QStringLiteral("maximum")).toDouble())))
        return QStringLiteral("数值越界");
    return {};
}
QString contributionRegistrationError(const QJsonObject &declared, const QMap<QString, QJsonObject> &pages,
                                      const QMap<QString, QJsonObject> &components,
                                      const QMap<QString, QJsonObject> &actions)
{
    for (const auto &kind :
         {QStringLiteral("pages"), QStringLiteral("courseBarComponents"), QStringLiteral("actions")})
    {
        const auto &registered = kind == QStringLiteral("pages")     ? pages
                                 : kind == QStringLiteral("actions") ? actions
                                                                     : components;
        for (const auto &item : declared.value(kind).toArray())
            if (!registered.contains(item.toObject().value(QStringLiteral("id")).toString()))
                return QStringLiteral("未注册贡献：%1/%2")
                    .arg(kind, item.toObject().value(QStringLiteral("id")).toString());
    }
    return {};
}
} // namespace Plugins
