export function activate(ctx) {
    let updates=0;
    ctx.events.on("courses.stateChanged",function(){updates++;});
    ctx.actions.register("status",function(args,done){done(null,{updates:updates});});
    ctx.log.info("后台任务已启动，关闭插件中心不会停止订阅。");
}
