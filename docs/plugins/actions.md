# 插件业务动作

在清单 contributions.actions 中声明 id/name/description、inputSchema/outputSchema 和 requiredPermissions（权限 key 数组）；activate 中同步 `ctx.actions.register(id,handler)`。requiredPermissions 必须引用插件自身已声明的权限。

schema 支持显式 type（object/array/string/number/integer/boolean/null）、properties、required、additionalProperties（默认 false）、items、enum、maxItems、maxLength、minimum、maximum。每个 schema 最多 16 KiB；禁止使用未实现的 JSON Schema 关键字。输入与输出均由宿主校验。

```js
ctx.actions.register("summary", function(args,done,call) {
    ctx.courses.getSnapshot(function(error,snapshot) {
        done(error,error?null:{text:snapshot.name});
    });
});
// 页面复用相同动作，不复制业务规则：
ctx.actions.invoke("summary",{},function(error,result){});
```

handler 接受 `(args,done,call)`，使用 done(error,result) 完成一次。call 提供 requestId、origin、isCanceled() 和 onCancel(fn)，可解除网络请求等资源。默认 30 秒超时，清单 timeoutMs 可设 100～60000；宿主最多 64 个并发动作。停用、重载、调用者销毁或取消后旧结果不再交付，不自动重放。

Agent 用 list_plugin_actions 分页发现摘要（offset/limit，默认 20、最多 50），指定 plugin/action 获取单个动作完整 schema，再用 call_plugin_action 执行，arguments_json 是符合动作输入 schema 的 JSON 对象或数组。ReadOnly 只发现动作；Ask 每次确认；Yolo 可调用已授权插件，新插件启用或权限扩大仍须确认。插件不会得到 Agent shell、文件工具或供应商密钥；返回值只是工具数据。页面调用仅能访问本插件动作，不提供跨插件服务。
