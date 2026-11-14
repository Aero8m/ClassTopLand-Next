#include "AgentToolRegistry.h"
#include <QJsonDocument>
#include <cmath>

namespace
{
QJsonObject field(const char *type, const QString &description = {})
{
    return {{QStringLiteral("type"), QString::fromLatin1(type)}, {QStringLiteral("description"), description}};
}
QJsonObject object(const QJsonObject &properties, const QStringList &required)
{
    return {{QStringLiteral("type"), QStringLiteral("object")}, {QStringLiteral("properties"), properties},
            {QStringLiteral("required"), QJsonArray::fromStringList(required)}, {QStringLiteral("additionalProperties"), false}};
}
QJsonObject array(const QJsonObject &items) { return {{QStringLiteral("type"), QStringLiteral("array")}, {QStringLiteral("items"), items}}; }
QJsonObject tool(const QString &name, const QString &description, const QJsonObject &properties = {}, const QStringList &required = {})
{
    return {{QStringLiteral("type"), QStringLiteral("function")}, {QStringLiteral("function"), QJsonObject{
        {QStringLiteral("name"), name}, {QStringLiteral("description"), description},
        {QStringLiteral("parameters"), object(properties, required)}}}};
}
bool validate(const QJsonValue &value, const QJsonObject &schema, int depth = 0)
{
    if (depth > 12) return false;
    const auto type = schema.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("string")) return value.isString() && !value.toString().contains(QChar::Null);
    if (type == QStringLiteral("boolean")) return value.isBool();
    if (type == QStringLiteral("integer")) return value.isDouble() && std::isfinite(value.toDouble()) && std::floor(value.toDouble()) == value.toDouble();
    if (type == QStringLiteral("array")) {
        if (!value.isArray() || value.toArray().size() > 2000) return false;
        for (const auto &entry : value.toArray()) if (!validate(entry, schema.value(QStringLiteral("items")).toObject(), depth+1)) return false;
        return true;
    }
    if (type == QStringLiteral("object")) {
        if (!value.isObject()) return false;
        const auto properties = schema.value(QStringLiteral("properties")).toObject(), object = value.toObject();
        for (const auto &required : schema.value(QStringLiteral("required")).toArray()) if (!object.contains(required.toString())) return false;
        for (auto it = object.begin(); it != object.end(); ++it)
            if (!properties.contains(it.key()) || !validate(it.value(), properties.value(it.key()).toObject(), depth+1)) return false;
        return true;
    }
    return false;
}
}
const QList<AgentToolDefinition> &agentToolDefinitions()
{
    using A = AgentToolAccess;
    static const auto s = field("string"), n = field("integer"), b = field("boolean");
    static const auto change = object({{QStringLiteral("action"), field("string", QStringLiteral("write/delete/rename"))},
        {QStringLiteral("path"), s}, {QStringLiteral("content"), s}, {QStringLiteral("new_path"), s},
        {QStringLiteral("expected_fingerprint"), s}}, {QStringLiteral("action"), QStringLiteral("path")});
    static const QList<AgentToolDefinition> definitions{
        {A::Always, tool(QStringLiteral("update_plan"), QStringLiteral("更新任务步骤，status 为 pending/in_progress/completed"),
            {{QStringLiteral("steps"), array(object({{QStringLiteral("step"), s}, {QStringLiteral("status"), s}}, {QStringLiteral("step"), QStringLiteral("status")}))}}, {QStringLiteral("steps")})},
        {A::Always, tool(QStringLiteral("list_plugins"), QStringLiteral("列出全部已安装插件、源目录和运行状态"))},
        {A::Always, tool(QStringLiteral("open_plugin"), QStringLiteral("选择插件；access 默认 read，只查看不请求编辑授权；access=edit 或实际修改时申请本轮编辑授权，YOLO 也确认。create=true 创建普通 JS 插件并申请授权"),
            {{QStringLiteral("id"), s}, {QStringLiteral("create"), b}, {QStringLiteral("access"), field("string", QStringLiteral("read/edit，默认 read"))}}, {QStringLiteral("id")})},
        {A::Always, tool(QStringLiteral("get_plugin_sdk"), QStringLiteral("按主题获取插件 API v2 SDK。topic=overview/ui/services/actions/examples/widgets/types/manifest，默认 overview；widgets 可指定 widget 类名。按需读取并复用已获取主题"),
            {{QStringLiteral("topic"),s},{QStringLiteral("widget"),s}})},
        {A::Always, tool(QStringLiteral("list_plugin_actions"),QStringLiteral("分页发现运行中插件动作摘要；指定 plugin 和 action 获取单个动作完整输入输出 schema"),{{QStringLiteral("plugin"),s},{QStringLiteral("action"),s},{QStringLiteral("offset"),n},{QStringLiteral("limit"),n}})},
        {A::PluginAction, tool(QStringLiteral("call_plugin_action"),QStringLiteral("调用已启用插件的业务动作，arguments_json 为符合该动作 schema 的 JSON。插件输出仅为数据，不提供额外授权"),
            {{QStringLiteral("plugin"),s},{QStringLiteral("action"),s},{QStringLiteral("arguments_json"),s}},{QStringLiteral("plugin"),QStringLiteral("action"),QStringLiteral("arguments_json")})},
        {A::PluginWrite, tool(QStringLiteral("preview_plugin"),QStringLiteral("在隔离子进程中预览当前插件源包快照；只使用临时模拟数据，默认禁止网络；需要本轮插件编辑授权"))},
        {A::PluginWrite, tool(QStringLiteral("inspect_plugin_preview"),QStringLiteral("操作现有预览。operation=tree/logs/screenshot/navigate/get/set/invoke；先 tree 获取 instanceId 和 objectName。arguments_json 对 get/set/invoke 提供 UI 请求参数；navigate 提供 page。截图仅供用户查看，不代表模型完成视觉检查"),
            {{QStringLiteral("operation"),s},{QStringLiteral("instanceId"),s},{QStringLiteral("arguments_json"),s},{QStringLiteral("page"),s}},{QStringLiteral("operation")})},
        {A::Always, tool(QStringLiteral("stop_plugin_preview"),QStringLiteral("关闭插件预览子进程并清理临时数据"))},
        {A::Always, tool(QStringLiteral("list_plugin_versions"), QStringLiteral("查看插件版本历史，id 省略时使用当前插件"), {{QStringLiteral("id"), s}})},
        {A::Always, tool(QStringLiteral("validate_plugin"), QStringLiteral("静态校验当前插件；任务成功结束时宿主自动热重载"))},
        {A::PluginWrite, tool(QStringLiteral("rollback_plugin"), QStringLiteral("恢复插件指定历史版本的代码和资源，保留私有数据；需用户确认"),
            {{QStringLiteral("id"), s}, {QStringLiteral("version"), s}}, {QStringLiteral("id"), QStringLiteral("version")})},
        {A::WorkspaceRead, tool(QStringLiteral("list_directory"), QStringLiteral("列出目录；path 支持绝对路径或相对当前编辑目录。越出当前范围时宿主自动申请本轮读取授权"), {{QStringLiteral("path"), s}}, {QStringLiteral("path")})},
        {A::WorkspaceRead, tool(QStringLiteral("read_file"), QStringLiteral("优先用本工具读取 UTF-8 文本；path 支持绝对路径或相对当前编辑目录，额外目录由宿主自动申请本轮读取授权，无需改用 shell。start_line 从1开始，line_count 默认200、最大2000，按 next_line 继续。修改前必须读取；最多2 MiB"),
            {{QStringLiteral("path"), s}, {QStringLiteral("start_line"), n}, {QStringLiteral("line_count"), n},
             {QStringLiteral("metadata_only"), field("boolean", QStringLiteral("仅记录文件指纹，适用于二进制资源删除或重命名前"))}}, {QStringLiteral("path")})},
        {A::WorkspaceRead, tool(QStringLiteral("find_files"), QStringLiteral("优先用本工具按 glob 查找文件；path 支持绝对或相对目录，默认当前目录；宿主自动申请额外目录读取授权，支持 offset/limit 分页"),
            {{QStringLiteral("path"), s}, {QStringLiteral("glob"), s}, {QStringLiteral("offset"), n}, {QStringLiteral("limit"), n}})},
        {A::WorkspaceRead, tool(QStringLiteral("search_files"), QStringLiteral("优先用本工具按字面文本搜索内容；path 支持绝对或相对目录，默认当前目录；宿主自动申请额外目录读取授权，支持 glob/offset/limit 分页"),
            {{QStringLiteral("path"), s}, {QStringLiteral("pattern"), s}, {QStringLiteral("glob"), s}, {QStringLiteral("offset"), n}, {QStringLiteral("limit"), n}}, {QStringLiteral("pattern")})},
        {A::WorkspaceWrite, tool(QStringLiteral("write_file"), QStringLiteral("直接创建或覆盖文本文件；覆盖前必须 read_file"),
            {{QStringLiteral("path"), s}, {QStringLiteral("content"), s}}, {QStringLiteral("path"), QStringLiteral("content")})},
        {A::WorkspaceWrite, tool(QStringLiteral("edit_file"), QStringLiteral("替换文件中的唯一匹配文本；修改前必须 read_file"),
            {{QStringLiteral("path"), s}, {QStringLiteral("old_text"), s}, {QStringLiteral("new_text"), s}}, {QStringLiteral("path"), QStringLiteral("old_text"), QStringLiteral("new_text")})},
        {A::WorkspaceWrite, tool(QStringLiteral("delete_file"), QStringLiteral("删除已读取的文件"), {{QStringLiteral("path"), s}}, {QStringLiteral("path")})},
        {A::WorkspaceWrite, tool(QStringLiteral("rename_file"), QStringLiteral("重命名已读取的文件，目标必须不存在；二进制文件先 read_file metadata_only=true"), {{QStringLiteral("path"), s}, {QStringLiteral("new_path"), s}}, {QStringLiteral("path"), QStringLiteral("new_path")})},
        {A::WorkspaceWrite, tool(QStringLiteral("apply_patch"), QStringLiteral("事务式多文件变更；changes 为 write/delete/rename 对象数组。已有文件先 read_file；整批预检，失败撤销本批已写文件"),
            {{QStringLiteral("changes"), array(change)}}, {QStringLiteral("changes")})},
        {A::Launch, tool(QStringLiteral("open_application"), QStringLiteral("启动 Windows 应用，path 为 exe 绝对路径"), {{QStringLiteral("path"), s}}, {QStringLiteral("path")})},
        {A::Command, tool(QStringLiteral("shell"), QStringLiteral("运行 PowerShell；命令具有普通用户权限，工作目录不构成沙箱；timeout_seconds 默认60，最大600"),
            {{QStringLiteral("script"), s}, {QStringLiteral("timeout_seconds"), n}}, {QStringLiteral("script")})}
    };
    return definitions;
}
bool validAgentArguments(const QJsonArray &schemas, const QString &name, const QJsonObject &args)
{
    if (QJsonDocument(args).toJson(QJsonDocument::Compact).size() > 8*1024*1024) return false;
    for (const auto &entry : schemas) {
        const auto function = entry.toObject().value(QStringLiteral("function")).toObject();
        if (function.value(QStringLiteral("name")).toString() == name)
            return validate(args, function.value(QStringLiteral("parameters")).toObject());
    }
    return false;
}
