export function activate(ctx) {
    ctx.events.on("course.started", function (event) { ctx.log.info("开始上课：" + event.subject); });
    ctx.events.on("course.ended", function (event) { ctx.log.info("下课：" + event.subject); });
    ctx.timers.every(60000, function () {
        ctx.courses.getSnapshot(function (error, snapshot) {
            if (error) ctx.log.warn(error.message);
            else ctx.log.info("当前课程状态：" + snapshot.phase);
        });
    });
}
