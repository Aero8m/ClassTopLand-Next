export function activate(ctx) {
    ctx.ui.registerSettingsPage("network", { name: "连接测试", tree: { type: "column", children: [
        { type: "text", text: "点击后访问 https://example.com，仅用于验证 HTTPS；不会发送课程或档案。" },
        { type: "button", id: "request", text: "测试连接" },
        { type: "text", id: "status", text: "尚未请求" }
    ] } });
    ctx.events.on("ui.action", function (event) {
        if (event.actionId !== "request") return;
        ctx.ui.update(event.instanceId, { status: { text: "请求中…" } });
        ctx.http.request({ url: "https://example.com", method: "GET", timeoutMs: 10000 }, function (error, response) {
            ctx.ui.update(event.instanceId, { status: { text: error ? error.code + ": " + error.message : "HTTP " + response.status } });
        });
    });
}
