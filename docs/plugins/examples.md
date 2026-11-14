# 示例与 Agent 开发闭环

仓库四个示例全部为 API v2：

- org.classtopland.example-tools：课程详情表格、动态创建控件、两页共享设置、课程条与实例设置、课程摘要动作。settings.mjs 演示配置冲突合并和 dirty 守卫。
- org.classtopland.background-demo：无页面后台订阅与查询动作；关闭插件中心不影响后台。
- org.classtopland.http-demo：页面与 Agent 共用网络动作，支持取消；隔离预览中明确拒绝外部网络。
- org.classtopland.profile-demo：页面与 Agent 共用档案写入；请在隔离或测试档案中体验。

Agent 先 list_plugins/open_plugin；只查看用 access=read。首次修改或 preview_plugin 请求本轮、本插件编辑授权，Yolo 也需确认。源文件修改前 read_file，首次写入前建立包检查点，运行版本在编辑期间不变。

get_plugin_sdk 按 overview/ui/services/actions/examples/widgets/types/manifest 获取文档；widgets 默认返回类名列表，指定 widget 获取该类属性、信号和操作。上下文压缩按主题保留最新 SDK；空间不足时暂停，避免反复获取或丢失最新工具结果。

validate_plugin 检查清单、UI XML、控件类别、唯一 objectName、包内资源和静态贡献契约。激活时进一步核对 JS 注册契约。静态校验不会证明 JS 业务正确或 UI 视觉正确。

preview_plugin 将源包冻结到临时目录，通过当前应用的独立预览进程执行，使用临时主配置、档案、存储和模拟课程，禁止宿主 HTTP／系统通知，不连接真实用户档案。预览同样运行可信代码，不声称提供操作系统沙箱。

inspect_plugin_preview 支持 tree/logs/navigate/get/set/invoke/screenshot。先读取 tree 取得 instanceId/objectName，再用 arguments_json 操作。invoke click 执行按钮，set 输入属性触发插件信号。截图显示在聊天中，供用户人工检查；当前文本模型不接收截图，不应声称完成视觉验收。截图保留在 agent/previews，进程临时数据由 stop_plugin_preview 或停止任务清理。

预览不替换当前运行版本；预览后源文件变化必须重新预览，再发布。任务完成时静态校验并自动热发布，保留已有停用状态，首次启用及新增权限仍确认。失败运行旧版，源文件保留供修复。版本历史／撤销本轮修改恢复代码和资源，保留私有数据。
