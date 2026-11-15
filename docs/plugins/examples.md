# 示例与 Agent 开发闭环

全部示例使用清单 v3、JS API v2。课程助手的 mainPage 为 details，常规／显示设置通过宿主 ElaPivot 切换；HTTP 和档案示例各有一个主页面，隐藏页签；后台示例不声明 mainPage。Agent 创建模板默认无页面，新增 pages 时必须同时声明 mainPage。validate_plugin、正式加载和隔离预览共用清单校验；预览初始化后定位 mainPage，navigate 的无效目标或加载错误会返回失败。旧清单迁移及不兼容历史版本的回滚规则见 README。


仓库四个示例全部为 API v2：

- org.classtopland.example-tools：课程详情表格、动态创建控件、两页共享设置、课程条与实例设置、课程摘要动作。general.ui、display.ui 和 component-settings.ui 演示标准分组、设置行及操作栏；settings.mjs 演示配置冲突合并和 dirty 守卫，使用 ActionBar.message 展示状态。
- org.classtopland.background-demo：无页面后台订阅与查询动作；关闭插件中心不影响后台。
- org.classtopland.http-demo：页面与 Agent 共用网络动作，支持取消；隔离预览中明确拒绝外部网络。
- org.classtopland.profile-demo：页面与 Agent 共用档案写入；请在隔离或测试档案中体验。

Agent 先 list_plugins/open_plugin；只查看用 access=read。首次修改或 preview_plugin 请求本轮、本插件编辑授权，Yolo 也需确认。源文件修改前 read_file，首次写入前建立包检查点，运行版本在编辑期间不变。

get_plugin_sdk 按 overview/ui/services/actions/examples/widgets/types/manifest 获取文档；widgets 默认返回类名列表，指定 widget 获取该类属性、信号和操作。上下文压缩按主题保留最新 SDK；空间不足时暂停，避免反复获取或丢失最新工具结果。

validate_plugin 按页面／组件设置／课程条用途和 layoutPreset 检查清单、UI XML、布局、控件类别、唯一 objectName、包内资源和静态贡献契约。新设置页使用 settings 标准结构，新功能页使用 content 统一页面容器；课程条设置页使用 settingsLayoutPreset。旧布局报告 UI_LAYOUT_LEGACY，旧样式的兼容诊断记录到插件日志；课程条示例仅使用根 QHBoxLayout 和 CourseBarItem。激活时进一步核对 JS 注册契约。静态校验不会证明 JS 业务正确或 UI 视觉正确。

preview_plugin 将源包冻结到临时目录，通过当前应用的独立后台预览进程执行，不显示插件中心、课程条组件或弹出窗口，也不激活窗口或抢占焦点；控件树检查、属性读写、调用、页面导航和截图仍可使用。预览使用临时主配置、档案、存储和模拟课程，禁止宿主 HTTP／系统通知，不连接真实用户档案。预览同样运行可信代码，不声称提供操作系统沙箱。内部布局及 show/hide 生命周期仍然执行，tree 中的 visible 表示内部控件可见状态，不代表窗口显示在桌面上。

inspect_plugin_preview 支持 tree/logs/navigate/get/set/invoke/screenshot。navigate 传 page 定位页面，空字符串定位 mainPage；也可传 componentInstance 定位组件实例设置。先读取 tree 取得 instanceId/objectName，核对 mode、layoutPreset、diagnostics、font、geometry、layout、layoutRole、hostManagedLayout、backgroundManaged 和 pending，再用 arguments_json 操作。UI_UNATTACHED_WIDGET 表示动态控件尚未插入布局或原生页面容器，UI_CONTENT_SLOT_EMPTY 表示标准组件还缺少业务内容。invoke click 执行按钮，set 输入属性触发插件信号。截图显示在聊天中，供用户人工检查；当前文本模型不接收截图，不应声称完成视觉验收。截图保留在 agent/previews，进程临时数据由 stop_plugin_preview 或停止任务清理。

预览不替换当前运行版本；预览后源文件变化必须重新预览，再发布。任务完成时静态校验并自动热发布，保留已有停用状态，首次启用及新增权限仍确认。失败运行旧版，源文件保留供修复。版本历史／撤销本轮修改恢复代码和资源，保留私有数据。
