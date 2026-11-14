# API v2 公共服务

具体参数以 [api.d.ts](api.d.ts) 为准。全部异步服务使用 `(error,result)`，返回 cancel/dispose 句柄；取消不撤销已经完成的原子提交。不要自动重试会改变业务含义的冲突操作。

- courses.getSnapshot：需要 courses=read；返回当前已解析课表及当前／下一节课程状态，不依赖课程条是否开启。订阅 courses.stateChanged/tableChanged 或 course.started/ended/startingSoon 获取变化。
- profiles.getCurrent/applyChanges：需要 read-current/write-current；使用 session、expectedRevision 和当前快照 ref。支持档案信息、科目、时间线、周课表、日课表、课程及调休／恢复／换课操作。每批 1～64 个操作，整批验证并提交。编辑器脏时 EDITOR_DIRTY，过期 CONFLICT，原子保存失败 SAVE_FAILED。复用 Core 来源引用及日期边界规则。
- settings.get/update：需要 settings=read/write；expectedRevision 对应主配置。只允许 themeColor、themeMode、courseBarEnabled、courseBarHeight（24～200）、componentSpacing（0～100）。不提供退出、重启、UIAccess 或文件路径写入。
- theme.changed：广播当前色彩和主题模式；Ela 控件自行跟随宿主主题。
- storage.get/set/remove：插件私有 JSON key/value；损坏或未知 schema 报错并保留文件，不自动恢复默认值覆盖。
- config.read(key)/commit(key,expectedRevision,value)：私有存储的版本检查接口。revision 为字符串，任何私有存储写入推进 revision。旧 schemaVersion=1 缺少 revision 时按 0 读取，首次写入原子添加字段。缺失 key 返回 exists=false，存储 null 返回 exists=true/value=null。
- view.config：页面操作私有 settings key；组件操作主配置对应实例，业务数据结构由插件定义。
- http.request：需要 network.origins；支持 GET/POST/PUT/PATCH/DELETE/HEAD。最多 8 并发、100ms～60s、5 次重定向，重定向重新授权，跨 origin 移除认证头；非 2xx 通过 status 表达，不忽略 TLS 错误。
- notifications.show：需要 notifications=true，每插件至少间隔 3 秒。预览中禁止 HTTP 和系统通知。
- timers、events、log：后台 activate 中创建的资源随插件停止释放；页面 factory 中创建的资源随视图销毁释放。

插件 ID 为小写反向域名，最长 128 字符；局部贡献 ID 为字母开头的字母、数字、下划线或连字符，最长 64 字符。权限扩大需要重新授权。服务只提供当前档案及现有设置白名单，不开放任意宿主对象、原生方法或文件系统 API。各文件之间无跨文件事务。
