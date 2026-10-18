#include "PluginManifest.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <functional>

namespace Plugins
{
QJsonObject error(const QString &code, const QString &message)
{
    return {{QStringLiteral("code"), code}, {QStringLiteral("message"), message}};
}
bool validId(const QString &id)
{
    static const QRegularExpression pattern(QStringLiteral("\\A[a-z][a-z0-9-]*(?:\\.[a-z][a-z0-9-]*)+\\z"));
    static const QRegularExpression reserved(QStringLiteral("\\A(?:con|prn|aux|nul|com[0-9]|lpt[0-9])\\z"));
    if (id.size() > 128 || !pattern.match(id).hasMatch())
        return false;
    for (const auto &part : id.split('.'))
        if (reserved.match(part).hasMatch())
            return false;
    return true;
}
bool within(const QString &root, const QString &path)
{
    const auto base = QFileInfo(root).canonicalFilePath();
    const auto target = QFileInfo(path).canonicalFilePath();
    return !base.isEmpty() && !target.isEmpty() &&
           target.startsWith(base + QLatin1Char('/'),
#ifdef Q_OS_WIN
                             Qt::CaseInsensitive
#else
                             Qt::CaseSensitive
#endif
           );
}
QString origin(const QUrl &url)
{
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
        (url.scheme() != QStringLiteral("https") && url.scheme() != QStringLiteral("http")))
        return {};
    QUrl base;
    base.setScheme(url.scheme().toLower());
    base.setHost(url.host().toLower());
    const int port = url.port(url.scheme() == QStringLiteral("https") ? 443 : 80);
    if (port != (url.scheme() == QStringLiteral("https") ? 443 : 80))
        base.setPort(port);
    return base.toString(QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment);
}
Manifest Manifest::read(const QString &directory)
{
    Manifest m;
    m.directory = QFileInfo(directory).canonicalFilePath();
    m.name = QFileInfo(directory).fileName();
    auto bad = [&](const QString &why)
    {
        m.failure = why;
        return m;
    };
    const auto path = QDir(directory).filePath(QStringLiteral("manifest.json"));
    if (!within(directory, path))
        return bad(QStringLiteral("清单不存在或路径越界"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024)
        return bad(QStringLiteral("清单无法读取或超过 64 KiB"));
    const auto bytes = file.readAll();
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return bad(QStringLiteral("清单不是有效 JSON 对象"));
    const auto value = doc.object();
    m.id = value.value(QStringLiteral("id")).toString();
    m.name = value.value(QStringLiteral("name")).toString();
    m.version = value.value(QStringLiteral("version")).toString();
    m.description = value.value(QStringLiteral("description")).toString();
    m.permissions = value.value(QStringLiteral("permissions")).toObject();
    m.contributions = value.value(QStringLiteral("contributions")).toObject();
    static const QRegularExpression versionPattern(
        QStringLiteral("\\A[0-9]+\\.[0-9]+\\.[0-9]+(?:-[a-zA-Z0-9.-]+)?\\z"));
    if (!validId(m.id) || m.name.trimmed().isEmpty() || m.name.size() > 128 ||
        !versionPattern.match(m.version).hasMatch())
        return bad(QStringLiteral("插件 ID、名称或版本无效"));
    if (value.value(QStringLiteral("manifestVersion")).toInt() != 1 ||
        value.value(QStringLiteral("apiVersion")).toInt() != 1)
        return bad(QStringLiteral("不支持的 manifestVersion 或 apiVersion"));
    if ((!value.value(QStringLiteral("permissions")).isUndefined() &&
         !value.value(QStringLiteral("permissions")).isObject()) ||
        (!value.value(QStringLiteral("contributions")).isUndefined() &&
         !value.value(QStringLiteral("contributions")).isObject()))
        return bad(QStringLiteral("权限或贡献必须是对象"));
    for (auto it = m.permissions.begin(); it != m.permissions.end(); ++it)
    {
        const auto key = it.key();
        if (key == QStringLiteral("courses") && it.value() == QJsonValue(QStringLiteral("read")))
            continue;
        if (key == QStringLiteral("profiles") && (it.value() == QJsonValue(QStringLiteral("read-current")) ||
                                                  it.value() == QJsonValue(QStringLiteral("write-current"))))
            continue;
        if (key == QStringLiteral("settings") && (it.value() == QJsonValue(QStringLiteral("read")) ||
                                                  it.value() == QJsonValue(QStringLiteral("write"))))
            continue;
        if (key == QStringLiteral("notifications") && it.value().isBool())
            continue;
        if (key == QStringLiteral("network") && it.value().isObject())
        {
            const auto origins = it.value().toObject().value(QStringLiteral("origins"));
            if (!origins.isArray() || origins.toArray().size() > 32)
                return bad(QStringLiteral("network.origins 无效"));
            for (const auto &item : origins.toArray())
                if (!item.isString() || origin(QUrl(item.toString())) != item.toString())
                    return bad(QStringLiteral("网络授权必须使用规范化 origin"));
            continue;
        }
        return bad(QStringLiteral("未知或无效权限：%1").arg(key));
    }
    static const QRegularExpression localId(QStringLiteral("\\A[a-zA-Z][a-zA-Z0-9_-]{0,63}\\z"));
    for (auto it = m.contributions.begin(); it != m.contributions.end(); ++it)
    {
        if (!QStringList{QStringLiteral("settingsPages"), QStringLiteral("courseBarComponents"),
                         QStringLiteral("windows")}
                 .contains(it.key()) ||
            !it.value().isArray() || it.value().toArray().size() > 32)
            return bad(QStringLiteral("无效的贡献列表"));
        QSet<QString> ids;
        for (const auto &item : it.value().toArray())
        {
            const auto object = item.toObject();
            const auto id = object.value(QStringLiteral("id")).toString();
            if (!item.isObject() || !localId.match(id).hasMatch() || ids.contains(id) ||
                object.value(QStringLiteral("name")).toString().trimmed().isEmpty())
                return bad(QStringLiteral("贡献 ID 或名称无效／重复"));
            ids.insert(id);
        }
    }
    const auto entry = value.value(QStringLiteral("entry")).toString();
    m.entry = QFileInfo(QDir(directory).filePath(entry)).canonicalFilePath();
    if (entry.isEmpty() || !within(directory, m.entry) || !QFileInfo(m.entry).isFile() ||
        QFileInfo(m.entry).size() > 2 * 1024 * 1024)
        return bad(QStringLiteral("入口不存在、越界或超过 2 MiB"));
    QFile script(m.entry);
    if (!script.open(QIODevice::ReadOnly))
        return bad(script.errorString());
    m.fingerprint = QCryptographicHash::hash(bytes + script.readAll(), QCryptographicHash::Sha256).toHex();
    return m;
}
} // namespace Plugins

QString Plugins::validateView(const QJsonObject &definition)
{
    if (QJsonDocument(definition).toJson(QJsonDocument::Compact).size() > 256 * 1024)
        return QStringLiteral("界面描述超过 256 KiB");
    int count = 0;
    QSet<QString> ids;
    QString failure;
    std::function<void(const QJsonObject &, int)> visit = [&](const QJsonObject &node, int depth)
    {
        if (!failure.isEmpty())
            return;
        if (depth > 16 || ++count > 256)
        {
            failure = QStringLiteral("界面深度或节点数超限");
            return;
        }
        const auto type = node.value(QStringLiteral("type")).toString();
        const auto id = node.value(QStringLiteral("id")).toString();
        for (const auto &key :
             {QStringLiteral("enabled"), QStringLiteral("visible"), QStringLiteral("required")})
            if (node.contains(key) && !node.value(key).isBool())
            {
                failure = QStringLiteral("控件状态必须是布尔值");
                return;
            }
        for (const auto &key : {QStringLiteral("min"), QStringLiteral("max"), QStringLiteral("maxLength")})
            if (node.contains(key) &&
                (!node.value(key).isDouble() || node.value(key).toDouble() != node.value(key).toInt()))
            {
                failure = QStringLiteral("控件范围必须是整数");
                return;
            }
        if (type == QStringLiteral("number") &&
            (node.value(QStringLiteral("min")).toInt(-1000000) < -1000000 ||
             node.value(QStringLiteral("max")).toInt(1000000) > 1000000 ||
             node.value(QStringLiteral("min")).toInt(-1000000) >
                 node.value(QStringLiteral("max")).toInt(1000000)))
        {
            failure = QStringLiteral("数值范围无效");
            return;
        }
        if (!QStringList{QStringLiteral("column"), QStringLiteral("row"), QStringLiteral("group"),
                         QStringLiteral("scroll"), QStringLiteral("text"), QStringLiteral("icon"),
                         QStringLiteral("button"), QStringLiteral("separator"), QStringLiteral("progress"),
                         QStringLiteral("input"), QStringLiteral("multiline"), QStringLiteral("toggle"),
                         QStringLiteral("select"), QStringLiteral("number"), QStringLiteral("list")}
                 .contains(type))
        {
            failure = QStringLiteral("未知控件：%1").arg(type);
            return;
        }
        if (!id.isEmpty())
        {
            if (id.size() > 128 || ids.contains(id))
            {
                failure = QStringLiteral("节点 ID 重复或过长");
                return;
            }
            ids.insert(id);
        }
        if (QStringList{QStringLiteral("input"), QStringLiteral("multiline"), QStringLiteral("toggle"),
                        QStringLiteral("select"), QStringLiteral("number")}
                .contains(type) &&
            id.isEmpty())
        {
            failure = QStringLiteral("输入控件必须声明 id");
            return;
        }
        for (const auto &key :
             {QStringLiteral("text"), QStringLiteral("label"), QStringLiteral("description")})
            if (node.value(key).toString().size() > 4096)
            {
                failure = QStringLiteral("控件文本过长");
                return;
            }
        if (node.value(QStringLiteral("items")).toArray().size() > 64)
        {
            failure = QStringLiteral("列表最多 64 项");
            return;
        }
        const auto children = node.value(QStringLiteral("children"));
        if (!children.isUndefined() && !children.isArray())
        {
            failure = QStringLiteral("children 必须是数组");
            return;
        }
        for (const auto &child : children.toArray())
        {
            if (!child.isObject())
            {
                failure = QStringLiteral("子节点必须是对象");
                return;
            }
            visit(child.toObject(), depth + 1);
        }
    };
    const auto tree = definition.value(QStringLiteral("tree")).toObject();
    if (tree.isEmpty())
        return QStringLiteral("缺少 tree 描述");
    visit(tree, 0);
    return failure;
}
