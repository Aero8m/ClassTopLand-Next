# 原生 UI 与控制器

`.ui` 以 QWidget 为根，使用 Qt Designer 布局、属性、页签、滚动区域和 promoted widgets。Ela 控件通过提升为对应 Ela 类使用，无需 Designer DLL。加载器清除 Designer 插件路径，未知类明确报错。输入、动态控件和布局必须使用唯一 objectName；不允许 UI 内嵌 script、connection 或外部 qrc include，事件在 JS 中连接。

图片、图标和样式 url 使用相对插件包根目录的路径，如 `assets/icon.png`；不能引用包外路径、远程 URL 或宿主资源。可直接使用控件 styleSheet，但建议沿用 Ela 主题，避免固定前景／背景色。运行时变更不写回 `.ui`。

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

`get/set/on/create/remove/insert/invoke` 均通过队列操作 GUI。get/set 使用控件目录中列出的属性；ElaToggleSwitch 统一使用 checked。create 同样支持为尚无布局的 QWidget 创建 QVBoxLayout/QHBoxLayout/QGridLayout/QFormLayout。调用 create(parent,className,objectName,cb) 后，可设置属性，再 insert(layout,child,options)。insert 支持盒布局 index/stretch、网格 row/column，以及表单行；invoke addPage 使用 `{child,title}` 添加原生页签。删除或重载后旧句柄失效。

`invoke(object,"setItems",{items,columns})`：ComboBox/ListWidget 的项为字符串或 `{text,value}`；TableWidget/TableView 使用二维行数组和 columns 标题；TreeWidget/TreeView 项为 `{text,children}`。ListView 使用字符串数组。支持原生 Qt 与注册的 Ela View；单次数据受消息大小和行列配额限制。`invoke(object,"select",{row,column})` 选择列表或表格项。预览控件树额外返回 model 摘要，包含总行列数、当前选择及前 10 行／6 列数据。信号参数见控件目录；activated 和 selectionChanged 返回行、列。

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

与页面关联的请求、定时器和事件订阅随销毁自动解除；destroy 仍可用于显式释放自己的资源。后台 activate 中创建的订阅不受页面关闭影响。组件使用 setAvailable 和宿主 metrics；初始创建与栏高变化时，宿主统一缩放组件控件字体，保留字体族和样式。内容、布局或可用状态变化通知课程条重新计算尺寸。
