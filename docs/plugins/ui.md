# 原生 UI 与控制器

## 插件级导航

清单使用 manifestVersion=3、apiVersion=2。有 pages 时必须用 contributions.mainPage 引用其中一个页面，无页面时省略 mainPage。宿主只为每个插件创建一个侧栏入口，在内部以 ElaPivot + QStackedWidget 组织页面：主页面最先，其余按清单顺序，实例设置最后；单页隐藏页签。

插件可在 `manifest.json` 根节点设置 `"icon": "Clock"`，指定左侧插件入口的图标。值为宿主所带 `ElaIconType::IconName` 的准确枚举名，区分大小写，不带 `ElaIconType::` 前缀；例如 `Clock`、`ArrowsRotate`、`CalendarDays` 或 `ScrewdriverWrench`，完整名称见 [ElaWidgetToolsDef.h](../../third_party/ElaWidgetTools-Plus/ElaWidgetTools/ElaWidgetToolsDef.h) 的 IconName。省略时沿用 `PuzzlePiece`，`None` 表示不显示图标；空值、未知名称及非字符串会导致清单校验失败。每个插件只有一个入口，所有页面及临时组件设置入口共用该图标；不需要新增权限。修改清单后重新扫描并热重载即可生效，停用和失败回滚沿用原有生命周期。图标由 Ela 绘制，跟随宿主主题与缩放。

页面继续分别使用 registerPage(id, factory) 注册；ctx.ui.openPage(id, callback) 选择所属插件和指定页签，失败通过 callback 返回 HostError。无需在 .ui 中声明宿主的 ElaPivot。页面按需加载，跨页签和跨插件切换保留实例与草稿。侧栏恢复上次页签，“打开插件”定位主页面。隐藏页参与关闭、停用、重载和退出的保存守卫；守卫期间导航禁用，宿主定位有未保存内容的页面。

组件实例设置在所属插件内部动态打开，同实例复用视图，不同实例使用名称和实例 ID 区分。无普通页面的插件允许只运行后台能力；打开实例设置时才创建临时入口，实例删除或中心关闭后移除。


页面及组件设置的 `.ui` 以 QWidget 为内容根，必须安装布局；普通控件通过 `<layout>/<item>` 排布，不能依靠绝对坐标。页签、局部滚动区域和分隔器按 Qt 原生结构使用。Ela 控件通过提升为对应 Ela 类使用，无需 Designer DLL。加载器清除 Designer 插件路径，未知类明确报错。输入、动态控件和布局必须使用唯一 objectName；不允许 UI 内嵌 script、connection 或外部 qrc include，事件在 JS 中连接。

页面由宿主提供透明内容容器、水平 24／垂直 20 个逻辑像素的页面外边距及按需滚动，与应用的 AppUi 页面共用实现。内容根布局外边距固定为 0；新页面不自行添加全页面滚动区域。旧 UI 的唯一全页面滚动区域可复用，保留对象名称；局部滚动区域仍可使用。普通容器背景由宿主管理，输入框、按钮、列表和表格保留主题外观。根尺寸和普通容器调色板不能覆盖宿主规则。

## 页面标准版式

新页面在清单中声明版式：`purpose:"settings"` 对应 `layoutPreset:"settings"`，`purpose:"function"` 对应 `layoutPreset:"content"`。课程条组件的设置页声明 `settingsUi` 和 `settingsLayoutPreset:"settings"`。未声明版式的旧页面继续兼容加载，报告 `UI_LAYOUT_LEGACY`，不会改写源文件或猜测表单含义。Agent 新建页面必须声明匹配版式；原有未迁移页面可以继续编辑。

宿主从贡献的 name 生成 24 像素、中等字重的页面标题，标题与内容间距 16；不要在 `.ui` 中重复添加同一标题。设置页结构固定如下，业务控件仍保留自己的 objectName、属性和信号：

```text
QWidget root / QVBoxLayout pageLayout
├─ PluginSection（title）
│  └─ PluginContentSlot / QVBoxLayout
│     ├─ PluginSettingRow（title、description）
│     │  └─ 一个业务编辑控件，如 ElaLineEdit
│     └─ PluginSettingRow / 一个业务编辑控件
└─ PluginActionBar（可选，唯一且位于末尾，message）
   ├─ ElaPushButton（text、primary:true）
   └─ ElaPushButton（text）
```

Section、SettingRow、ActionBar 内部不声明布局；上图中的内容槽、编辑控件和按钮使用直接 `<widget>` 子节点。只有根和 ContentSlot 使用 `<layout>/<item>`。完整 XML 见仓库示例 `org.classtopland.example-tools/general.ui`。

| 组件 | 可设置的业务属性 | 宿主提供的布局 |
|---|---|---|
| PluginSection | title | 16 像素分组标题，标题与卡片间距 8；圆角 8，卡片内边距水平 16／垂直 14 |
| PluginContentSlot | enabled、visible、toolTip | 一个 VBox，边距 0，间距 12；settings 版式仅接受 SettingRow |
| PluginSettingRow | title、description | 标题 14、说明 12，文字间距 6；左右间距 20；窄宽度下上下排列 |
| PluginActionBar | message | 内容末尾随页面滚动，按钮靠右，按钮间距 8；最多一个 primary 按钮 |

所有标准组件还支持 enabled、visible、toolTip；不开放其内部标题、卡片和布局句柄，也不允许自定义样式或尺寸。`_ctl` 为宿主内部对象名称前缀，插件不得使用。行编辑控件的尺寸由宿主按类别管理：开关使用自然尺寸，下拉框 180、数值框 150、日期时间框 180、单行输入框最小 240 并伸展；窄宽度时调整以适应内容区域。按钮高 44，最小宽度为 112 与文字宽度加 40 中的较大值。

标准根布局、分组内容布局的间距及边距不开放修改；行编辑控件和操作栏按钮不能设置尺寸或改变尺寸的 QSS。静态旧属性忽略并提示，动态请求报错且不应用整批属性。功能页和旧版页面上的 ElaToggleSwitch 也保留原生最小尺寸。已有 .ui 中过小的约束会在加载时调整，并报告 UI_CONTROL_SIZE_NORMALIZED。动态请求将开关设得过小会报错。设计开关行时使用布局和原生尺寸即可。`primary` 仅在 PluginActionBar 的 ElaPushButton 上开放，可通过 view.set 切换；先关闭现有主按钮，再开启另一个。

功能页 `content` 版式共用标题、外边距、背景和滚动，可使用表格、页签、网格等业务布局，也可使用上述标准分组。功能页分组的内容槽仍使用宿主管理的 VBox，但槽内允许任意受支持的业务控件和带布局的业务容器；不要求全部为 SettingRow。局部业务布局仍可调整间距。不要用 QSS 模拟卡片、分组和操作栏。

动态创建标准组件沿用原接口：`view.create(section,"PluginContentSlot",name,cb)` 后为槽创建 VBox，再 `view.insert(section,slot,{})`；`view.insert(row,editor,{})` 和 `view.insert(actionBar,button,{index:0})` 分别挂载编辑控件与按钮。插入标准根／槽／组件只接受顺序 index，不接受 stretch 或网格坐标；新控件成功挂入布局前保持隐藏。移除标准组件会同时销毁其业务子控件；已被行或操作栏接管的编辑控件不能直接移动到其他容器。

图片和图标使用已有原生属性及相对插件包根目录的路径，如 `assets/icon.png`；不能引用包外路径、远程 URL 或宿主资源。QSS 不提供图片或 URL 入口。运行时变更不写回 `.ui`。

## 页面受限 QSS

`styleSheet` 只允许当前对象 `#objectName` 的规则，可附加 `:hover/:pressed/:checked/:disabled/:focus`，不允许类型、全局、后代、属性、子控件或其他对象选择器。使用样式的 objectName 应为字母或下划线开头的 ASCII 字母、数字、下划线、连字符组合。

允许：`font-weight`（normal/bold/100～900）、`font-style`（normal/italic）、`text-decoration`（none/underline/line-through）、`border-style`（none/solid/dashed/dotted）、`border-width`（0～8）、`border-radius`（0～64）、`padding`（1～4 个值）及四侧 padding（0～256），长度使用逻辑像素，可附 px。`color` 和 `border-color` 仅接受 `palette(role)`，role 为 window-text、text、button-text、highlight、highlighted-text、mid、dark 或 light。

禁止背景、图片、URL、定位、尺寸、`qproperty-*` 和固定颜色。例如：

```css
#note { font-weight: bold; padding: 2px 4px; color: palette(text); }
#localButton:hover { border-style: solid; border-width: 1px; border-color: palette(highlight); }
```

旧 `.ui` 的违规样式在内存中忽略并记录兼容警告；无法解析的旧样式整段忽略。动态 `view.set` 则明确返回错误，不应用该批属性。清空插件样式保留宿主／Ela 必要样式。

```js
export function activate(ctx) {
    ctx.ui.registerPage("main", function(view) {
        view.on("button", "clicked", function() {
            view.set("label", {text:"你好"});
        });
        return { show:function(){}, hide:function(){}, destroy:function(){} };
    });
}
```

一个 factory 对应一个视图实例；每次打开新实例重新调用。组件使用 `registerComponent(id,{create,createSettings})`，有 settingsUi 时必须提供 createSettings。view 含 instanceId、kind、contributionId、componentInstance、initialSettings 和 config。不把 QWidget 指针传入工作线程。

`get/set/on/create/remove/insert/invoke` 均通过队列操作 GUI。get/set 使用控件目录中列出的属性；ElaToggleSwitch 统一使用 checked。create 同样支持为尚无布局的 QWidget 创建 QVBoxLayout/QHBoxLayout/QGridLayout/QFormLayout。调用 create(parent,className,objectName,cb) 后控件先隐藏，可设置属性，再 insert(layout,child,options) 挂入布局后展示。空容器允许随后创建布局；未挂入布局或原生页面容器的控件会在预览中报告 UI_UNATTACHED_WIDGET。insert 支持盒布局 index/stretch、网格 row/column，以及表单行；invoke addPage 使用 `{child,title}` 添加原生页签。页面布局开放 leftMargin/topMargin/rightMargin/bottomMargin/spacing；QGridLayout 和 QFormLayout 另支持 horizontalSpacing/verticalSpacing。值为 -1（Qt 默认）或 0～256 的整数。内容根 margin 由宿主管理，不可动态修改。动态 set 先校验整个属性批次，再应用；失败保留原状态。删除或重载后旧句柄失效。

`invoke(object,"setItems",{items,columns})`：ComboBox/ListWidget 的项为字符串或 `{text,value}`；TableWidget/TableView 使用二维行数组和 columns 标题；TreeWidget/TreeView 项为 `{text,children}`。ListView 使用字符串数组。支持原生 Qt 与注册的 Ela View；单次数据受消息大小和行列配额限制。`invoke(object,"select",{row,column})` 选择列表或表格项。预览控件树额外返回 model 摘要，包含总行列数、当前选择及前 10 行／6 列数据。信号参数见控件目录；activated 和 selectionChanged 返回行、列。

## 课程条严格展示模式

`courseBarComponents[].ui` 使用专用规则，`settingsUi` 继续使用普通页面规则；同一文件被不同用途引用时分别校验。课程条只允许根 QWidget、一个直接的 QHBoxLayout 和若干直接 CourseBarItem，不允许其他控件、嵌套布局、spacer、stretch、自定义 QSS 或布局／尺寸参数。

```xml
<ui version="4.0">
  <class>CourseStatus</class>
  <widget class="QWidget" name="root">
    <layout class="QHBoxLayout" name="row">
      <item><widget class="CourseBarItem" name="status">
        <property name="text"><string>等待课程…</string></property>
      </widget></item>
    </layout>
  </widget>
</ui>
```

CourseBarItem 仅允许 text（单行纯文本，最多 32768 字符）、toolTip、visible、active（默认 false）、progressValue（0～10000 的整数，默认 10000）及 leftMargin/rightMargin（-1 或 0～100 整数，默认 -1）。active 为 true 时显示内置课程块相同的高亮及进度装饰；进度值直接表示填充比例，时间业务由插件负责。内部文字及进度对象不暴露给 JS。

```js
view.set("status", {text:"语文", active:true, progressValue:5000});
view.create("root", "CourseBarItem", "extra", function(error) {
    if (error) return;
    view.set("extra", {text:"下一节数学"});
    view.insert("row", "extra", {index:1});
});
```

宿主共用内置 CourseBarMetrics、CourseBarLabel 和课程块排布：字体族、字号、字重、黑色文字、视觉居中、字形宽度、保护边距、栏高、高亮扩展和进度外观均由宿主管理。展示项可单独设置 leftMargin/rightMargin；未设置时沿用原排布。普通项透明；组件之间的间距由课程条统一提供。隐藏项不占间距，空组件不可用。不做页面主题的文字颜色转换。复杂交互全部放设置页。

左右边距可在 `.ui` 的 CourseBarItem 上声明 `<property name="leftMargin"><number>0</number></property>`，也可通过 `view.set()` 动态设置；`view.get()` 和预览 tree 可读取。只接受 `-1` 或 `0～100` 的整数：`-1` 为默认，继承相邻宿主间距的一半（奇数间距的余数归前一项右侧），最外侧继承 0；显式值以 49px 栏高为基准，随栏高和 DPI 缩放。相邻项间距为前一项 rightMargin 与后一项 leftMargin 相加，包含在组件总宽度内；隐藏项及其边距均不占空间。

例如时间和日期间距改为 8：

```javascript
view.set("time", {rightMargin: 4});
view.set("date", {leftMargin: 4});
// 两侧都设为 0 可取消该间距；都设为 -1 恢复宿主默认排布。
```

动态创建只能在根下创建 CourseBarItem；插入只允许 index（-1 表示末尾），不允许 stretch/row/column。根及根布局不能删除或修改。字体旧属性 TextPixelSize/TextPointSize 可读取实际值，不能写入。

兼容旧 UI：根横向布局的 QLabel/ElaText 可转为标准项；仅有一个文字项的根纵向布局可转为横向。保留名称、文字、提示和可见状态，忽略旧外观，静态换行转为空格并提示。其他结构报错，不自动推断布局或进度关联，不改写源文件。运行时换行或违规属性直接返回错误。

## 诊断与预览

控件目录的 contexts 描述适用视图，stylePolicy 描述受限样式，layoutPolicy 描述标准槽规则。预览 tree 返回 mode（page/component/componentSettings）、layoutPreset（legacy/settings/content）、diagnostics，以及节点的 font、geometry、layout、layoutRole、hostManagedLayout、backgroundManaged、pending。动态标准分组缺少内容槽、设置行缺少编辑控件时报告 UI_CONTENT_SLOT_EMPTY。诊断含 code/severity/object/property/message，静态诊断附 file/line/column；同样的规则用于清单检查、正式加载和预览。

兼容警告进入已有插件日志；动态接口通过回调返回违规原因。交付前处理布局错误、未挂载控件和本次新增的兼容警告。截图仅供人工检查，文本模型取得截图不代表完成视觉验收。密码内容继续从快照中移除。

## 配置与保存

插件实现输入校验、保存按钮和错误提示。view.config.read(cb) 返回 `{exists,value?,revision}`，页面共享插件私有 settings key，组件／组件设置使用该实例的配置。view.config.commit(expectedRevision,value,cb) 原子保存，失败保留原值。缺失值与 null 明确区分。共享设置页应合并自己负责的字段；示例 settings.mjs 演示重新读取、比较基线和提交，避免不同页相互覆盖。

```js
// factory 内维护自己的 draft、revision；保存按钮使用 view.save()。
return {
    save:function(done) {
        view.config.commit(revision, draft, function(error,result) {
            if (!error) revision=result.revision;
            done(error); // 成功落盘后才报告成功
        });
    },
    discard:function(done) { /* 重新读取并恢复表单后 */ done(null); }
};
```

输入事件处理器应立即调用 setDirty(true)，再发起异步工作。宿主在关闭检查前先等待已经发出的控件事件被工作线程处理，避免快速输入后立即关闭遗漏草稿。view.save/discard 在成功回调后清除 dirty，失败保留。关闭中心、停用、重载、删除组件、退出／重启时宿主依次请求 save/discard；取消或 10 秒超时取消整个操作。页面必须向用户展示校验与保存错误。宿主暂停新业务写入，允许当前守卫及其异步回调完成必要保存，不使用阻塞式 JS 保存。

与页面关联的请求、定时器和事件订阅随销毁自动解除；destroy 仍可用于显式释放自己的资源。后台 activate 中创建的订阅不受页面关闭影响。课程条使用 setAvailable 和宿主 metrics；标准展示项的字体与绘制完全由宿主管理。内容、布局或可用状态变化通知课程条重新计算尺寸。
