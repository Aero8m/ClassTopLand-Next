#include "PluginManifest.h"
#include "PluginContracts.h"
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMetaEnum>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <functional>
#include <utility>

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
    if (value.value(QStringLiteral("manifestVersion")) != QJsonValue(3) ||
        value.value(QStringLiteral("apiVersion")) != QJsonValue(2))
        return bad(QStringLiteral("需要升级：仅支持插件 manifestVersion=3、apiVersion=2；有页面的插件必须声明 contributions.mainPage"));
    if (value.contains(QStringLiteral("icon")))
    {
        const auto icon = value.value(QStringLiteral("icon"));
        static const QRegularExpression iconPattern(QStringLiteral("\\A[A-Za-z][A-Za-z0-9]{0,127}\\z"));
        bool known = false;
        const int code = QMetaEnum::fromType<ElaIconType::IconName>().keyToValue(icon.toString().toUtf8().constData(), &known);
        if (!icon.isString() || !iconPattern.match(icon.toString()).hasMatch() || !known)
            return bad(QStringLiteral("icon 必须为有效的 ElaIconType 图标名称，例如 Clock 或 ArrowsRotate"));
        m.icon = static_cast<ElaIconType::IconName>(code);
    }
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
        if (it.key() == QStringLiteral("mainPage"))
            continue;
        if (!QStringList{QStringLiteral("pages"), QStringLiteral("courseBarComponents"),
                         QStringLiteral("actions")}
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
            if (it.key() == QStringLiteral("actions")) {
                if (object.value(QStringLiteral("description")).toString().trimmed().isEmpty()) return bad(QStringLiteral("动作缺少说明"));
                for (const auto &schema : {QStringLiteral("inputSchema"), QStringLiteral("outputSchema")}) {
                    const auto error = validateSchema(object.value(schema).toObject());
                    if (!error.isEmpty()) return bad(id + QStringLiteral(": ") + error);
                }
                if(object.contains(QStringLiteral("timeoutMs")) && (!object.value(QStringLiteral("timeoutMs")).isDouble() || object.value(QStringLiteral("timeoutMs")).toDouble()!=object.value(QStringLiteral("timeoutMs")).toInt() || object.value(QStringLiteral("timeoutMs")).toInt()<100 || object.value(QStringLiteral("timeoutMs")).toInt()>60000)) return bad(QStringLiteral("timeoutMs 必须为 100～60000 的整数"));
                if (!object.value(QStringLiteral("requiredPermissions")).isArray()) return bad(QStringLiteral("动作必须声明 requiredPermissions"));
                for (const auto &permission : object.value(QStringLiteral("requiredPermissions")).toArray())
                    if (!permission.isString() || !m.permissions.contains(permission.toString())) return bad(QStringLiteral("动作引用了未声明权限"));
            } else {
                if (it.key() == QStringLiteral("pages") && !QStringList{QStringLiteral("function"), QStringLiteral("settings")}.contains(object.value(QStringLiteral("purpose")).toString()))
                    return bad(QStringLiteral("页面 purpose 必须为 function 或 settings"));
                const bool page = it.key() == QStringLiteral("pages");
                if (object.contains(QStringLiteral("layoutPreset")) && (!page || !object.value(QStringLiteral("layoutPreset")).isString() ||
                    object.value(QStringLiteral("layoutPreset")).toString() != (object.value(QStringLiteral("purpose")) == QStringLiteral("settings") ? QStringLiteral("settings") : QStringLiteral("content"))))
                    return bad(id + QStringLiteral(": layoutPreset 与页面用途不匹配"));
                if (object.contains(QStringLiteral("settingsLayoutPreset")) && (page || !object.contains(QStringLiteral("settingsUi")) ||
                    object.value(QStringLiteral("settingsLayoutPreset")) != QStringLiteral("settings")))
                    return bad(id + QStringLiteral(": settingsLayoutPreset 仅支持带 settingsUi 的 settings 版式"));
                for (const auto &key : {QStringLiteral("ui"), QStringLiteral("settingsUi")}) {
                    if (key == QStringLiteral("settingsUi") && !object.contains(key)) continue;
                    const auto ui = object.value(key).toString();
                    if (QDir::isAbsolutePath(ui)) return bad(id + QStringLiteral(": UI 必须使用包内相对路径"));
                    const auto prepared = prepareUi(QDir(directory).filePath(ui), directory,
                        it.key() == QStringLiteral("courseBarComponents") && key == QStringLiteral("ui")
                            ? UiKind::Component : key == QStringLiteral("settingsUi") ? UiKind::ComponentSettings : UiKind::Page,
                        object.value(key == QStringLiteral("settingsUi") ? QStringLiteral("settingsLayoutPreset") : QStringLiteral("layoutPreset")).toString());
                    if (!prepared.error.isEmpty()) return bad(id + QStringLiteral(": ") + prepared.error);
                    for (const auto &diagnostic : prepared.diagnostics) m.uiDiagnostics.append(diagnostic);
                }
                if (object.contains(QStringLiteral("displayPolicy")) && !QStringList{QStringLiteral("expandedOnly"), QStringLiteral("whenAvailable")}.contains(object.value(QStringLiteral("displayPolicy")).toString()))
                    return bad(QStringLiteral("无效的 displayPolicy"));
            }
            ids.insert(id);
        }
    }
    const auto pages = m.contributions.value(QStringLiteral("pages")).toArray();
    const auto mainPage = m.contributions.value(QStringLiteral("mainPage"));
    if (pages.isEmpty())
    {
        if (!mainPage.isUndefined())
            return bad(QStringLiteral("无页面插件不得声明 contributions.mainPage"));
    }
    else
    {
        bool found = false;
        for (const auto &page : pages)
            if (page.toObject().value(QStringLiteral("id")) == mainPage)
                found = true;
        if (!mainPage.isString() || !found)
            return bad(QStringLiteral("contributions.mainPage 必须引用 pages 中唯一的主页面 ID"));
    }
    const auto entry = value.value(QStringLiteral("entry")).toString();
    m.entry = QFileInfo(QDir(directory).filePath(entry)).canonicalFilePath();
    if (entry.isEmpty() || !within(directory, m.entry) || !QFileInfo(m.entry).isFile() ||
        QFileInfo(m.entry).size() > 2 * 1024 * 1024)
        return bad(QStringLiteral("入口不存在、越界或超过 2 MiB"));
    const QDir package(m.directory);
    QDirIterator files(m.directory, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks,
                       QDirIterator::Subdirectories);
    QStringList paths;
    qint64 size = 0;
    while (files.hasNext()) {
        const auto filename = files.next();
        if (!within(m.directory, filename) || paths.size() >= 512 ||
            (size += files.fileInfo().size()) > 16 * 1024 * 1024)
            return bad(QStringLiteral("插件包路径越界或超过大小限制"));
        paths.append(package.relativeFilePath(filename));
    }
    paths.sort();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const auto &path : std::as_const(paths)) {
        QFile file(package.filePath(path));
        if (!file.open(QIODevice::ReadOnly)) return bad(file.errorString());
        QCryptographicHash content(QCryptographicHash::Sha256);
        if (!content.addData(&file)) return bad(file.errorString());
        hash.addData(path.toUtf8());
        hash.addData(QByteArrayView("\0", 1));
        hash.addData(content.result());
    }
    m.fingerprint = hash.result().toHex();
    return m;
}
} // namespace Plugins
