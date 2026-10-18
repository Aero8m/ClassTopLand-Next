# JavaScript 插件 API v1

插件采用 Qt 6.9.3 QJSEngine，在每插件工作线程运行。界面由 GUI 线程中的 QWidget/Ela 渲染。仅安装可信插件：工作线程不是进程或文件系统沙箱，UIAccess 模式下插件共享宿主进程权限。

## 安装、更新和恢复

1. 在“设置 → 插件 → 更多 → 打开插件目录”进入实际数据目录的 `plugins/`。
2. 将包含 `manifest.json`、`main.mjs` 的插件文件夹复制进去；可使用 `examples/plugins/` 中四个示例。
3. 重新扫描，查看权限，手动启用。扫描只读取清单和入口，不执行 JS。
4. 点击“设置”打开独立非模态 Ela 弹窗；重复点击激活已有窗口。多设置页通过顶部 ElaPivot 页签切换，支持键盘导航和横向滚动，长标题悬停显示完整内容；单页隐藏页签。保存按钮一次提交全部页面。
5. 在“设置 → 课程条”添加插件组件，其“设置”操作打开对应实例弹窗。全局设置与实例设置独立保存。

更新前停用插件，备份旧文件夹，再替换并重启；扫描发现运行中文件发生变化时标记待重启，不热重载。增加或改变权限后需重新确认授权。卸载时先停用，再将文件夹移出 `plugins/`；`plugin-data/<id>/storage.json` 默认保留，删除前单独备份并确认。第一版没有压缩包安装或在线市场。

启动参数 `--disable-plugins` 跳过第三方插件；管理菜单也可请求下一次启动进入恢复模式。恢复模式不清除原启用偏好，允许手动启用。故障插件持久化故障记录，下次不自动运行，通过详情“重新授权并重试”恢复。保存故障记录失败会显示原因；此时可使用启动参数恢复。

## 包与清单

```json
{
  "manifestVersion": 1,
  "id": "org.example.my-plugin",
  "name": "示例插件",
  "version": "1.0.0",
  "apiVersion": 1,
  "entry": "main.mjs",
  "permissions": { "courses": "read" },
  "contributions": { "settingsPages": [{ "id": "general", "name": "常规" }] }
}
```

清单格式见 [manifest.schema.json](manifest.schema.json)，接口见 [api.d.ts](api.d.ts)。插件 ID 为小写反向域名，最多 128 字符；各段以字母开头，不使用 Windows 保留名称。贡献局部 ID 使用字母开头的字母、数字、下划线、连字符，最多 64 字符。重复插件 ID 不加载。

本次实际执行的构建、验收场景和未验证环境见 [验证记录](VALIDATION.md)。

权限字段：`courses: "read"`，`profiles: "read-current" | "write-current"`，`settings: "read" | "write"`，`notifications: true`，`network: { origins: ["https://example.com"] }`。未知或错误声明拒绝加载。origin 为协议、主机和非默认端口，不包含路径、认证、查询或片段；每次重定向重新检查，跨 origin 移除认证头。

推荐将纯 JS 依赖打包到单个 `.mjs`。入口和包资源路径检查不约束 QJSEngine 的全部传递导入能力，不能据此声称文件系统隔离。无 DOM、Node.js、require、fetch 或原生 npm。实测支持 Promise 对象，但不支持 `async function` 语法；生命周期、事件和定时器函数使用同步返回，异步服务使用 callback。

## 生命周期与服务

导出 `activate(ctx)`，可选导出 `deactivate(ctx)`。激活成功后发布贡献；激活阶段允许注册、订阅、定时器和存储读取，网络、通知及持久化写入应在后续事件或定时任务中执行。停用期间不再接受服务请求，资源由宿主清理，无需依靠 deactivate 自行回收。

异步回调为 `(error, result)`，请求返回带 `cancel()/dispose()` 的句柄。事件和定时任务可 dispose。取消不撤销已开始的原子提交；已停止运行代次的迟到结果不再交付。课程快照使用唯一默认课表源，不依赖课程条是否开启；新订阅者收到当前快照，不重放旧上下课提醒。

资源限制：最多同时运行 16 个插件；脚本每次执行预算 2 秒；每插件最多 128 个待完成请求、每秒 200 个服务请求、64 个定时器（最短 100ms）和 128 个待交付事件。课程状态合并，事件积压超限隔离插件。单条消息（含快照结果）、请求及文本响应上限 256 KiB；HTTP 并发最多 8、整体超时 100ms～60s、重定向最多 5 次。HTTP 非 2xx 通过 `status` 表达，传输及 TLS 失败返回错误，不忽略证书检查。网络服务使用 Qt 的默认代理策略，不提供插件自定义代理。私有存储总量 1 MiB，损坏或未知 schema 返回错误并保留原文件。

## 原生界面与表单

清单声明贡献后，在激活时调用 `ctx.ui.registerSettingsPage/registerComponent/registerWindow(id, definition)`。定义包含 `tree`；组件可声明 `settings` 表单、`previewText` 和 `displayPolicy`。界面属性更新每插件每秒最多 30 次，结构更新每秒最多 2 次。界面最多 256 节点、16 层、每段文本 4096 字符、列表 64 项。输入控件必须有唯一 `id`，该 ID 同时作为存储字段；`required/min/max/maxLength` 由宿主校验。`icon` 使用文本符号，不接受任意样式表、原生指针或远程图片 URL；`scroll` 参与宿主页面滚动，不增加一层滚动区。

`ui.created` 包含 `instanceId/kind/contributionId/settings`，`ui.action` 包含 `instanceId/actionId/value`，`ui.destroyed` 包含 `instanceId`。普通字段编辑的 action 默认等于字段 ID；按钮 action 可自定义。使用 `ctx.ui.update(instanceId, { nodeId: { text/value/enabled/visible } })` 增量更新；dirty 字段拒绝后台值覆盖。结构变化使用 `ctx.ui.setTree(instanceId, tree)`，同 ID 字段保留草稿、焦点及选择；删除的字段值仍保留到明确保存，避免静默丢失输入。关闭一个实例不影响其他实例。

插件设置存于私有存储 `settings` key，结构为 `{ pageId: { fieldId: value } }`，宿主以比较原值的方式防止覆盖后台写入。组件设置存于主配置对应实例；业务窗口表单存于 `window/<贡献ID>` key。`settings.saved` 携带该次保存的各页字段对象；仅在成功落盘后触发。窗口关闭、插件停用及应用退出使用保存／放弃／取消守卫。业务窗口和插件设置均可调整大小；后台 `ui.openWindow` 仅允许紧接用户交互调用，管理页也提供声明窗口的打开入口。

## 档案和应用设置写入

先调用 `profiles.getCurrent`，使用返回的 `session`、字符串 `revision` 和临时 `ref` 组装 `applyChanges({session, expectedRevision, operations})`。科目、时间线和课程的 ref 绑定当前快照，修改后重新读取；周课表使用稳定 `id`。不接受任意档案路径或整个档案覆盖。

业务操作见类型声明：科目和时间线 add/update/remove，周课表 add/update/remove，`day.set` 设置一个日课表，课程 add/update/remove，以及 `date.reschedule/date.restore/date.swap`。`value` 使用原生档案相应实体的字段；`week.add` 的 ID 由宿主生成。科目改名同步更新课程引用；被引用科目不能直接删除。`date.swap` 的序号对应当前日期解析后的排序课程，必须使用同一快照版本。每批 1～64 个操作，全部验证后提交一次；成功返回 `session/revision/canceledDates`，需要新实体引用时重新读取快照；由宿主提示来源变化。

编辑器有草稿时返回 `EDITOR_DIRTY`，版本过期返回 `CONFLICT`，原子保存失败返回 `SAVE_FAILED`。不自动重试会改变含义的操作。

`settings.get/update` 使用主配置字符串 revision，changes 白名单为 `themeColor`、`themeMode`（0 跟随系统、1 浅色、2 深色）、`courseBarEnabled`、`courseBarHeight`（24～200）和 `componentSpacing`（0～100）。UIAccess、退出重启、窗口拦截和文件路径不在白名单中。档案、主配置和插件存储之间没有跨文件事务。

## 兼容与排错

主配置 v1 在加载时原子迁移为 v2，新组件使用稳定实例 ID；旧版程序会拒绝 v2。迁移失败不覆盖原文件。缺失插件的配置和未知字段保留，重新安装后可恢复。

启用失败查看详情错误；运行日志仅保留最近 200 条，每条最多 2048 字符。不要在日志中写入 token、认证头、完整档案等敏感信息。插件数据迁移应生成候选 JSON，成功后再 storage.set，旧数据读取失败时不要自动覆盖。API 变化使用新 apiVersion；当前只支持 v1，不隐式降级。
