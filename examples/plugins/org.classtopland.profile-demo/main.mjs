export function activate(ctx) {
    ctx.ui.registerSettingsPage("profile", { name: "写入演示", tree: { type: "column", children: [
        { type: "text", text: "点击下方按钮会立即修改并保存活动档案：添加“插件示例科目”。请先备份，建议仅在测试档案使用。" },
        { type: "button", id: "addSubject", text: "添加示例科目并保存档案" },
        { type: "text", id: "status", text: "尚未修改档案" }
    ] } });
    ctx.events.on("ui.action", function (event) {
        if (event.actionId !== "addSubject") return;
        ctx.profiles.getCurrent(function (error, snapshot) {
            if (error) { ctx.ui.update(event.instanceId, { status: { text: error.message } }); return; }
            ctx.profiles.applyChanges({ session: snapshot.session, expectedRevision: snapshot.revision,
                operations: [{ op: "subject.add", value: { name: "插件示例科目", simplifiedName: "示", teacher: "" } }]
            }, function (error) {
                ctx.ui.update(event.instanceId, { status: { text: error ? error.code + ": " + error.message : "科目已添加并保存" } });
            });
        });
    });
}
