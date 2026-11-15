# JavaScript 插件 API v2

插件采用 Qt 6.9.3 QJSEngine，在每插件工作线程执行 JS；原生 QWidget/Ela 页面由 GUI 线程使用 QtUiTools 动态加载 `.ui`，通过异步消息桥接。无需 WebEngine、QML 页面或插件编译器。仅安装可信插件：正式插件与宿主同进程，工作线程和私有路径检查不构成安全沙箱，UIAccess 下共享宿主权限。

## 安装和使用

1. 从托盘打开「插件中心 → 插件管理 → 更多 → 打开插件目录」。
2. 将标准插件包目录放入 `plugins/`，重新扫描并授权启用。四个完整示例见 `examples/plugins/`。
3. 每个有页面的运行中插件只占一个侧栏入口；宿主在内部使用 ElaPivot 切换主页面和其他页面，单页隐藏页签。管理卡的设置按钮定位清单中第一个设置页。
4. 在「设置 → 课程条」添加插件组件；实例设置以所属插件内部的动态页签打开，按稳定实例 ID 独立保存。无普通页面的插件仅在打开实例设置时出现临时入口。

切换页面保留实例与草稿。关闭插件中心处理全部页面草稿，通过后销毁页面；后台插件继续运行。停用、重载、移除组件和退出均保留保存／放弃／取消守卫。主设置窗口不再拥有插件页面。

## 标准包

```text
org.example.tools/
  manifest.json
  main.mjs
  page.ui
  assets/
```

```json
{
  "manifestVersion": 3,
  "apiVersion": 2,
  "id": "org.example.tools",
  "name": "示例工具",
  "icon": "ScrewdriverWrench",
  "version": "2.0.0",
  "entry": "main.mjs",
  "permissions": {},
  "contributions": {
    "mainPage": "main",
    "pages": [{"id":"main","name":"工具","purpose":"function","layoutPreset":"content","ui":"page.ui"}]
  }
}
```

页面 purpose 为 `function` 或 `settings`，新页面分别声明 `layoutPreset:"content"` 或 `"settings"`；组件声明 `ui`、可选 `settingsUi/previewText/displayPolicy`，新组件设置页还声明 `settingsLayoutPreset:"settings"`；动作声明输入／输出 schema 和 requiredPermissions。清单见 [manifest.schema.json](manifest.schema.json)，类型见 [api.d.ts](api.d.ts)，控件能力目录见 [widgets.json](widgets.json)。控件目录同时供静态校验、宿主桥接与 Agent SDK 使用。标准设置页通过 PluginSection、PluginContentSlot、PluginSettingRow 和 PluginActionBar 编排业务内容，共用应用原生布局；功能页在统一页面容器中保留业务布局能力。旧页面缺省版式时兼容加载并提示，不改写源文件。课程条仅允许标准横向 `CourseBarItem`，开放展示项 `leftMargin` / `rightMargin`，其他字体、布局和样式沿用宿主，详见 UI 文档。

仅支持 manifestVersion=3、apiVersion=2；旧清单显示「需要升级」，不执行或自动转换。迁移时将清单版本改为 3，并在有 pages 时声明 contributions.mainPage，引用其中一个页面 ID；没有页面时不得声明 mainPage。旧插件数据、历史、启用偏好和课程条配置保留。JS API 和页面工厂保持 v2，插件应自行读取旧 settings 结构，不得静默覆盖无法识别的数据。无需修改主配置版本。回滚到不兼容的旧清单会在替换源文件前失败。

主页面显示为第一个页签，其余页面按清单声明顺序排列，动态实例设置追加在末尾。首次进入插件显示主页面，侧栏再次进入恢复本窗口内上次页签；“打开插件”明确定位主页面。ctx.ui.openPage(id) 选择所属插件及指定页签，加载失败通过回调返回错误。所有页面按需加载，跨插件切换保留草稿；保存守卫也覆盖隐藏页面，并定位正在处理的页面。

## 开发主题

- [UI 与控制器](ui.md)：Designer、动态控件、信号、保存守卫。
- [公共服务](services.md)：课程、档案、设置、网络与原子配置。
- [动作](actions.md)：页面与 Agent 共用业务入口。
- [示例与开发闭环](examples.md)：隔离预览、校验、发布与回滚。

JS 无 DOM、Node.js、require、fetch 或原生 npm；实测不支持 async function，使用 callback，不在生命周期返回 Promise。推荐将纯 JS 依赖打包为 `.mjs`。

## 生命周期与资源

`activate(ctx)` 中同步注册所有声明的贡献，否则激活失败；可选 `deactivate(ctx)`。激活阶段允许注册、订阅、定时器及配置读取，其他操作放到后续事件。停用后宿主释放资源，旧代次结果不再交付。

保留执行和 IO 预算：最多 16 个运行插件，每次脚本执行 2 秒，128 个待完成请求、每秒 200 个服务请求、64 个定时器（最短 100ms）、128 个待交付事件。单条消息 256 KiB，存储 1 MiB，包 512 项／16 MiB。页面 XML 1 MiB；列表／表格建议分页，单次最多 2000 行、64 列。日志最多 200 条、每条 2048 字符。不要写入密钥、完整认证头或完整档案。

更新替换源包后在管理详情热重载，审阅权限并处理草稿。不可变运行副本位于 `plugin-versions/`，编辑源文件不改变当前版本；激活或配置保存失败恢复旧运行版本。检查点与历史位于 `plugin-history/`，恢复日志位于 `plugin-recovery/`，私有存储位于 `plugin-data/<id>/storage.json`。回滚代码不回滚私有存储或已发生的业务副作用。

`--disable-plugins` 或管理页恢复模式可跳过自动启动；故障记录保留，手动重新授权重试。恢复模式不清除启用偏好。暂不提供在线市场、插件依赖或原生 DLL 插件。
