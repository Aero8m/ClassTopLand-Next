export function activate(ctx) {
    let instances = {}, prefix = "课程助手", snapshot = null;
    ctx.ui.registerSettingsPage("general", {
        name: "常规",
        tree: { type: "column", children: [
            { type: "text", text: "这里的设置保存在本插件的私有存储，保存后生效。" },
            { type: "input", id: "prefix", label: "显示前缀", value: prefix, required: true, maxLength: 40 },
            { type: "toggle", id: "showName", label: "显示课表名称", value: true }
        ] }
    });
    ctx.ui.registerSettingsPage("display", {
        name: "显示",
        tree: { type: "column", children: [
            { type: "number", id: "maxCourses", label: "详情最多显示课程数", value: 8, min: 1, max: 32 },
            { type: "select", id: "style", label: "展示方式", value: "compact", items: [
                { label: "紧凑", value: "compact" }, { label: "详细", value: "full" }
            ] }
        ] }
    });
    ctx.ui.registerComponent("status", {
        name: "课程状态", previewText: "课程助手 · 示例课表", displayPolicy: "expandedOnly",
        tree: { type: "row", children: [
            { type: "text", id: "status", text: "课程助手" },
            { type: "button", id: "details", text: "详情", action: "openDetails" }
        ] },
        settings: { tree: { type: "column", children: [
            { type: "input", id: "label", label: "该组件的显示名称", value: "", maxLength: 40 }
        ] } }
    });
    ctx.ui.registerWindow("details", {
        name: "课程详情",
        tree: { type: "column", children: [{ type: "text", id: "summary", text: "等待读取课程…" }] }
    });
    function refresh() {
        Object.keys(instances).forEach(function (id) {
            const instance = instances[id];
            if (instance.kind === "component") {
                const label = instance.settings.label || prefix;
                ctx.ui.update(id, { status: { text: label + (snapshot ? " · " + (snapshot.name || "无课表") : "") } });
            } else if (instance.kind === "window") {
                const courses = snapshot ? snapshot.classes.map(function (course) { return course.startTime + "  " + course.subject; }).join("\n") : "暂无课程";
                ctx.ui.update(id, { summary: { text: courses } });
            }
        });
    }
    ctx.events.on("ui.created", function (event) { instances[event.instanceId] = event; refresh(); });
    ctx.events.on("ui.destroyed", function (event) { delete instances[event.instanceId]; });
    ctx.events.on("ui.action", function (event) { if (event.actionId === "openDetails") ctx.ui.openWindow("details"); });
    ctx.events.on("settings.saved", function (values) {
        if (values.general) { prefix = values.general.prefix; refresh(); }
    });
    ctx.storage.get("settings", function (error, values) {
        if (error) { ctx.log.warn(error.message); return; }
        if (values && values.general) prefix = values.general.prefix;
        refresh();
    });
    ctx.events.on("courses.tableChanged", function (value) { snapshot = value; refresh(); });
    ctx.courses.getSnapshot(function (error, value) { if (!error) { snapshot = value; refresh(); } });
    ctx.timers.every(300000, function () { ctx.log.info("后台任务仍在运行，打开或关闭设置弹窗不影响此任务。 "); });
}
