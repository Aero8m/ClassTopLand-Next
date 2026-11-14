#include "PluginRuntime.h"
#include <QJSEngine>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QElapsedTimer>
#include <QSet>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

class PluginWorker;
class PluginBridge final : public QObject
{
    Q_OBJECT
  public:
    explicit PluginBridge(PluginWorker *worker);
    Q_INVOKABLE QString send(const QString &method, const QString &json);
    Q_INVOKABLE void cancel(const QString &id);

  private:
    PluginWorker *worker_;
};

class PluginWorker final : public QObject
{
    Q_OBJECT
  public:
    explicit PluginWorker(const Plugins::Manifest &manifest) : manifest_(manifest) {}
    ~PluginWorker() override
    {
        shutdown();
    }
    void load();
    void shutdown();
    QString send(const QString &method, const QString &json);
    void cancel(const QString &id);
    void deliver(const QString &id, const QJsonObject &error, const QJsonValue &result, bool retain = false);
    void event(const QString &name, const QJsonValue &value);
    void interrupt()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (engine_)
            engine_->setInterrupted(true);
    }
  signals:
    void started();
    void failed(const QString &message);
    void request(const QString &id, const QString &method, const QJsonObject &args);
    void cancelRequest(const QString &id);
    void eventProcessed();

  private:
    bool call(const QJSValue &fn, const QJSValueList &args = {});
    Plugins::Manifest manifest_;
    std::unique_ptr<QJSEngine> engine_;
    QJSValue module_, invoke_, complete_, dispatch_, context_;
    QHash<QString, QTimer *> timers_;
    QSet<QString> pending_;
    quint64 serial_ = 0;
    QElapsedTimer rateClock_;
    int requests_ = 0;
    bool failed_ = false, stopping_ = false;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::thread watchdog_;
    bool watchStop_ = false, executing_ = false, timedOut_ = false;
    std::chrono::steady_clock::time_point deadline_;
    std::chrono::steady_clock::time_point heartbeatDeadline_;
    QTimer *heartbeat_ = nullptr;
};

PluginBridge::PluginBridge(PluginWorker *worker) : QObject(worker), worker_(worker) {}
QString PluginBridge::send(const QString &method, const QString &json)
{
    return worker_->send(method, json);
}
void PluginBridge::cancel(const QString &id)
{
    worker_->cancel(id);
}

static const char bootstrap[] = R"JS(
(function(host) {
    let callbacks={},subscriptions={},serial=0,pages={},components={},views={},actions={},activeActions={},guard='',currentView='';
    function scoped(token,fn,args,view) {let previous=guard,previousView=currentView;guard=token;currentView=view||'';try{return fn.apply(undefined,args);}finally{guard=previous;currentView=previousView;}}
    function request(method,args,callback,retain) {
        args=args||{};if(guard)args._guard=guard;
        let id=host.send(method,JSON.stringify(args,function(key,value){
            if(typeof value==='function'||typeof value==='symbol'||typeof value==='undefined'||(typeof value==='number'&&!isFinite(value)))throw new Error('Only finite JSON-compatible values are accepted');
            return value;
        }));
        if(!id)throw new Error('Request rejected: plugin stopped or resource limit exceeded');
        callbacks[id]={fn:typeof callback==='function'?callback:function(e){if(e&&method!=='log')ctx.log.error(e.code+': '+e.message);},retain:!!retain,guard:guard,view:args.instanceId||currentView};
        return {dispose:function(){delete callbacks[id];host.cancel(id);},cancel:function(){delete callbacks[id];host.cancel(id);}};
    }
    function viewRequest(view,method,args,cb) {if(!view.active)throw new Error('View destroyed');args=args||{};args.instanceId=view.instanceId;return request('ui.'+method,args,cb);}
    function runLifecycle(item,operation,callback) {
        let done=false,version=item.view.dirtyVersion;
        function finish(error){if(done)return;done=true;if(!error&&version===item.view.dirtyVersion)item.view.setDirty(false);if(callback)callback(error||null);}
        if(typeof item.controller[operation]!=='function'){finish({code:'NOT_SUPPORTED',message:'Page has no '+operation+' handler'});return;}
        try{item.controller[operation](finish);}catch(e){finish({code:'PLUGIN_ERROR',message:String(e)});}
    }
    function makeView(event) {
        let v={instanceId:event.instanceId,kind:event.kind,contributionId:event.contributionId,componentInstance:event.componentInstance,initialSettings:event.settings,active:true,dirtyVersion:0};
        v.get=function(object,property,cb){return viewRequest(v,'get',{object:object,property:property},cb);};
        v.set=function(object,properties,cb){return viewRequest(v,'set',{object:object,properties:properties},cb);};
        v.create=function(parent,type,name,cb){return viewRequest(v,'create',{object:parent,'class':type,name:name},cb);};
        v.remove=function(object,cb){return viewRequest(v,'remove',{object:object},cb);};
        v.insert=function(layout,child,options,cb){let args=Object.assign({},options||{},{object:layout,child:child});return viewRequest(v,'insert',args,cb);};
        v.invoke=function(object,operation,args,cb){return viewRequest(v,'invoke',{object:object,operation:operation,arguments:args||{}},cb);};
        v.setDirty=function(value,cb){if(value)v.dirtyVersion++;return viewRequest(v,'setDirty',{value:!!value},cb);};
        v.setAvailable=function(value,cb){return viewRequest(v,'setAvailable',{value:!!value},cb);};
        v.config={read:function(cb){return viewRequest(v,'config.read',{},cb);},commit:function(revision,value,cb){return viewRequest(v,'config.commit',{expectedRevision:revision,value:value},cb);}};
        v.on=function(object,signal,fn){
            if(typeof fn!=='function')throw new Error('Signal handler required');
            let key=object+'/'+signal,id=String(++serial),item=views[v.instanceId];
            if(!item.signals[key]){item.signals[key]={};viewRequest(v,'subscribe',{object:object,signal:signal});}
            item.signals[key][id]=fn;
            return {dispose:function(){if(!v.active)return;let handlers=item.signals[key];if(!handlers)return;delete handlers[id];if(!Object.keys(handlers).length){delete item.signals[key];viewRequest(v,'unsubscribe',{object:object,signal:signal});}}};
        };
        v.save=function(cb){runLifecycle(views[v.instanceId],'save',cb);};
        v.discard=function(cb){runLifecycle(views[v.instanceId],'discard',cb);};
        return v;
    }
    let ui={
        registerPage:function(id,factory){if(pages[id]||typeof factory!=='function')throw new Error('Page factory required or duplicate registration');pages[id]=factory;return request('ui.registerPage',{id:id},function(e){if(e)throw new Error(e.message);});},
        registerComponent:function(id,controller){if(components[id]||!controller||typeof controller.create!=='function')throw new Error('Component create factory required or duplicate registration');components[id]=controller;return request('ui.registerComponent',{id:id,hasSettings:typeof controller.createSettings==='function'},function(e){if(e)throw new Error(e.message);});},
        openPage:function(id,cb){return request('ui.openPage',{id:id},cb);},
        catalog:function(cb){return request('ui.catalog',{},cb);}
    };
    let ctx={
        apiVersion:2,
        courses:{getSnapshot:function(cb){return request('courses.getSnapshot',{},cb);}},
        profiles:{getCurrent:function(cb){return request('profiles.getCurrent',{},cb);},applyChanges:function(args,cb){return request('profiles.applyChanges',args,cb);}},
        settings:{get:function(cb){return request('settings.get',{},cb);},update:function(args,cb){return request('settings.update',args,cb);}},
        storage:{get:function(key,cb){return request('storage.get',{key:key},cb);},set:function(key,value,cb){return request('storage.set',{key:key,value:value},cb);},remove:function(key,cb){return request('storage.remove',{key:key},cb);}},
        config:{read:function(key,cb){return request('storage.read',{key:key},cb);},commit:function(key,revision,value,cb){return request('storage.commit',{key:key,expectedRevision:revision,value:value},cb);}},
        timers:{after:function(ms,fn){return request('timer.after',{ms:ms},function(e){if(!e)fn();});},every:function(ms,fn){return request('timer.every',{ms:ms},function(e){if(!e)fn();},true);}},
        http:{request:function(options,cb){return request('http.request',options,cb);}},
        notifications:{show:function(options,cb){return request('notifications.show',options,cb);}},
        actions:{register:function(id,handler){if(actions[id]||typeof handler!=='function')throw new Error('Action handler required or duplicate registration');actions[id]=handler;return request('actions.register',{id:id},function(e){if(e)throw new Error(e.message);});},invoke:function(id,args,cb){return request('actions.invoke',{id:id,arguments:args},cb);}},
        log:{},ui:ui,
        events:{on:function(name,fn){let id=String(++serial);if(typeof fn!=='function')throw new Error('Handler required');subscriptions[id]={name:name,fn:fn,view:currentView};request('events.subscribe',{name:name});return {dispose:function(){delete subscriptions[id];}};}}
    };
    ['debug','info','warn','error'].forEach(function(level){ctx.log[level]=function(message){request('log',{level:level,message:String(message)});};});
    function dispatch(name,event) {
        if(name==='ui.barrier') {request('ui.barrierReady',{instanceId:event.instanceId,token:event.token},function(){});}
        else if(name==='ui.created') {
            let item={signals:{},controller:{},view:null};views[event.instanceId]=item;item.view=makeView(event);
            let factory=event.kind==='page'?pages[event.contributionId]:(components[event.contributionId]||{})[event.kind==='componentSettings'?'createSettings':'create'];
            if(typeof factory!=='function')throw new Error('Missing controller factory: '+event.contributionId+'/'+event.kind);
            item.controller=scoped('',factory,[item.view],event.instanceId)||{};
        } else if(name==='ui.signal') {
            let item=views[event.instanceId],key=event.object+'/'+event.signal;
            if(item&&item.signals[key])Object.keys(item.signals[key]).forEach(function(id){let fn=item.signals[key][id];if(fn)scoped('',fn,event.arguments||[],event.instanceId);});
        } else if(name==='ui.visibility') {
            let item=views[event.instanceId],method=event.visible?'show':'hide';if(item&&typeof item.controller[method]==='function')scoped('',item.controller[method],[],event.instanceId);
        } else if(name==='ui.guard') {
            let item=views[event.instanceId];if(item)scoped(event.token,function(){runLifecycle(item,event.operation,function(error){request('ui.guardComplete',{instanceId:event.instanceId,error:error||null},function(){});});},[],event.instanceId);
        } else if(name==='ui.destroyed') {
            let item=views[event.instanceId];if(item){if(typeof item.controller.destroy==='function')scoped('',item.controller.destroy,[],event.instanceId);item.view.active=false;delete views[event.instanceId];Object.keys(callbacks).forEach(function(id){if(callbacks[id].view===event.instanceId){delete callbacks[id];host.cancel(id);}});Object.keys(subscriptions).forEach(function(id){if(subscriptions[id].view===event.instanceId)delete subscriptions[id];});}
        } else if(name==='actions.invoke') {
            let canceled=false,done=false,listeners=[],handler=actions[event.actionId];
            let call={requestId:event.requestId,origin:event.origin,isCanceled:function(){return canceled;},onCancel:function(fn){listeners.push(fn);}};
            activeActions[event.requestId]=function(){canceled=true;listeners.forEach(function(fn){fn();});delete activeActions[event.requestId];};
            function complete(error,result){if(done||canceled)return;done=true;delete activeActions[event.requestId];request('actions.complete',{requestId:event.requestId,error:error||null,result:typeof result==='undefined'?null:result},function(){});}
            try{if(!handler)throw new Error('Action not registered');handler(event.arguments,complete,call);}catch(e){complete({code:'PLUGIN_ERROR',message:String(e)},null);}
        } else if(name==='actions.cancel') {if(activeActions[event.requestId])activeActions[event.requestId]();}
        Object.keys(subscriptions).forEach(function(id){let item=subscriptions[id];if(item&&item.name===name&&(!item.view||views[item.view]))scoped('',item.fn,[event],item.view);});
    }
    return {context:ctx,
        complete:function(id,error,result){let item=callbacks[id];if(!item)return;if(!item.retain)delete callbacks[id];if(!item.view||views[item.view])scoped(item.guard,item.fn,[error,result],item.view);},
        dispatch:dispatch,
        invoke:function(fn,args){try{let result=fn.apply(undefined,args);if(result&&typeof result.then==='function')throw new Error('Async callbacks/lifecycle are not supported');return JSON.stringify({ok:true});}catch(e){return JSON.stringify({ok:false,message:String(e),stack:e&&String(e.stack||'')});}}
    };
})
)JS";

void PluginWorker::load()
{
    rateClock_.start();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        engine_ = std::make_unique<QJSEngine>();
        heartbeatDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    }
    heartbeat_ = new QTimer(this);
    heartbeat_->setInterval(100);
    connect(heartbeat_, &QTimer::timeout, this,
            [this]
            {
                std::lock_guard<std::mutex> lock(mutex_);
                heartbeatDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                wake_.notify_all();
            });
    heartbeat_->start();
    watchdog_ = std::thread(
        [this]
        {
            std::unique_lock<std::mutex> lock(mutex_);
            while (!watchStop_)
            {
                const auto limit = executing_ ? deadline_ : heartbeatDeadline_;
                if (wake_.wait_until(lock, limit) == std::cv_status::timeout && !watchStop_)
                {
                    const auto now = std::chrono::steady_clock::now();
                    if (executing_ && now >= deadline_)
                    {
                        timedOut_ = true;
                        executing_ = false;
                        heartbeatDeadline_ = now + std::chrono::seconds(2);
                        engine_->setInterrupted(true);
                    }
                    else if (!executing_ && now >= heartbeatDeadline_)
                    {
                        heartbeatDeadline_ = now + std::chrono::seconds(2);
                        engine_->setInterrupted(true);
                        // Promise jobs also execute through the engine's event loop, outside call().
                        QMetaObject::invokeMethod(
                            this,
                            [this]
                            {
                                if (!failed_ && !stopping_)
                                {
                                    failed_ = true;
                                    emit failed(QStringLiteral("脚本事件循环超过 2 秒未响应"));
                                }
                            },
                            Qt::QueuedConnection);
                    }
                }
            }
        });
    auto *bridge = new PluginBridge(this);
    QJSEngine::setObjectOwnership(bridge, QJSEngine::CppOwnership);
    auto boot = engine_->evaluate(QString::fromUtf8(bootstrap)).call({engine_->newQObject(bridge)});
    invoke_ = boot.property(QStringLiteral("invoke"));
    complete_ = boot.property(QStringLiteral("complete"));
    dispatch_ = boot.property(QStringLiteral("dispatch"));
    context_ = boot.property(QStringLiteral("context"));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        timedOut_ = false;
        executing_ = true;
        deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        wake_.notify_all();
    }
    module_ = engine_->importModule(manifest_.entry);
    bool timeout;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        timeout = timedOut_;
        executing_ = false;
        heartbeatDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        wake_.notify_all();
    }
    auto activate = module_.property(QStringLiteral("activate"));
    if (timeout || module_.isError() || !activate.isCallable())
    {
        failed_ = true;
        emit failed(timeout
                        ? QStringLiteral("模块加载超时")
                        : QStringLiteral("模块加载失败或缺少 activate：%1")
                              .arg(module_.isError() ? module_.toString() : QStringLiteral("无有效导出")));
        return;
    }
    if (call(activate, {context_}))
        emit started();
}
bool PluginWorker::call(const QJSValue &fn, const QJSValueList &args)
{
    if (failed_ || !engine_ || !fn.isCallable())
        return false;
    auto array = engine_->newArray(args.size());
    for (int i = 0; i < args.size(); ++i)
        array.setProperty(i, args[i]);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        timedOut_ = false;
        executing_ = true;
        deadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        wake_.notify_all();
    }
    auto result = invoke_.call({fn, array});
    const auto text = result.toString();
    bool timeout;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        timeout = timedOut_;
        executing_ = false;
        heartbeatDeadline_ = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        wake_.notify_all();
    }
    const auto status = QJsonDocument::fromJson(text.toUtf8()).object();
    if (timeout || !status.value(QStringLiteral("ok")).toBool())
    {
        failed_ = true;
        emit failed(timeout ? QStringLiteral("脚本执行超过 2 秒")
                            : status.value(QStringLiteral("message")).toString() + QLatin1Char('\n') +
                                  status.value(QStringLiteral("stack")).toString());
        return false;
    }
    return true;
}
QString PluginWorker::send(const QString &method, const QString &json)
{
    if (stopping_ || failed_ || json.toUtf8().size() > Plugins::MaxMessageBytes || pending_.size() >= 128)
        return {};
    if (rateClock_.elapsed() >= 1000)
    {
        rateClock_.restart();
        requests_ = 0;
    }
    if (++requests_ > 200)
        return {};
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(json.toUtf8(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    const auto id = QString::number(++serial_);
    const auto args = doc.object();
    if (method.startsWith(QStringLiteral("timer.")))
    {
        const int ms = args.value(QStringLiteral("ms")).toInt();
        if (ms < 100 || timers_.size() >= 64)
            return {};
        auto *timer = new QTimer(this);
        timer->setInterval(ms);
        timer->setSingleShot(method == QStringLiteral("timer.after"));
        timers_.insert(id, timer);
        connect(timer, &QTimer::timeout, this,
                [this, id, timer]
                {
                    bool keep = !timer->isSingleShot();
                    deliver(id, {}, QJsonValue(), keep);
                    if (!keep)
                    {
                        timers_.remove(id);
                        timer->deleteLater();
                    }
                });
        timer->start();
    }
    else
    {
        pending_.insert(id);
        emit request(id, method, args);
    }
    return id;
}
void PluginWorker::cancel(const QString &id)
{
    if (auto *timer = timers_.take(id))
    {
        timer->stop();
        timer->deleteLater();
    }
    if (pending_.remove(id))
        emit cancelRequest(id);
}
void PluginWorker::deliver(const QString &id, const QJsonObject &error, const QJsonValue &result, bool)
{
    if (stopping_ || failed_ || !engine_)
        return;
    pending_.remove(id);
    auto scopedError = error;
    if (!scopedError.isEmpty())
        scopedError.insert(QStringLiteral("requestId"), id);
    call(complete_, {QJSValue(id),
                     scopedError.isEmpty() ? QJSValue(QJSValue::NullValue)
                                           : engine_->toScriptValue(scopedError.toVariantMap()),
                     engine_->toScriptValue(result.toVariant())});
}
void PluginWorker::event(const QString &name, const QJsonValue &value)
{
    if (!stopping_ && !failed_ && engine_)
        call(dispatch_, {QJSValue(name), engine_->toScriptValue(value.toVariant())});
    emit eventProcessed();
}
void PluginWorker::shutdown()
{
    if (stopping_)
        return;
    stopping_ = true;
    if (heartbeat_)
        heartbeat_->stop();
    for (auto *timer : timers_)
        timer->stop();
    timers_.clear();
    pending_.clear();
    if (engine_ && !failed_)
    {
        engine_->setInterrupted(false);
        call(module_.property(QStringLiteral("deactivate")), {context_});
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        watchStop_ = true;
        executing_ = false;
        wake_.notify_all();
    }
    if (watchdog_.joinable())
        watchdog_.join();
    module_ = invoke_ = complete_ = dispatch_ = context_ = QJSValue();
    std::lock_guard<std::mutex> lock(mutex_);
    engine_.reset();
}
PluginRuntime::PluginRuntime(const Plugins::Manifest &manifest, quint64, QObject *parent)
    : QObject(parent), worker_(new PluginWorker(manifest))
{
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(&thread_, &QThread::finished, this, &PluginRuntime::stopped);
    connect(worker_, &PluginWorker::started, this, &PluginRuntime::started);
    connect(worker_, &PluginWorker::failed, this, &PluginRuntime::failed);
    connect(worker_, &PluginWorker::request, this, &PluginRuntime::request);
    connect(worker_, &PluginWorker::cancelRequest, this, &PluginRuntime::cancelRequest);
    connect(worker_, &PluginWorker::eventProcessed, this,
            [this]
            {
                eventPending_ = false;
                nextEvent();
            });
    connect(&thread_, &QThread::started, worker_, &PluginWorker::load);
}
PluginRuntime::~PluginRuntime()
{
    stop();
}
void PluginRuntime::start()
{
    thread_.start();
}
void PluginRuntime::beginStop()
{
    if (stopping_)
        return;
    stopping_ = true;
    if (!thread_.isRunning())
        return;
    worker_->interrupt();
    QMetaObject::invokeMethod(
        worker_,
        [this]
        {
            worker_->shutdown();
            thread_.quit();
        },
        Qt::QueuedConnection);
}
void PluginRuntime::stop()
{
    beginStop();
    if (thread_.isRunning())
        thread_.wait();
}
void PluginRuntime::deliver(const QString &id, const QJsonObject &error, const QJsonValue &result)
{
    if (QJsonDocument(QJsonArray{result}).toJson(QJsonDocument::Compact).size() > Plugins::MaxMessageBytes)
    {
        deliver(id, Plugins::error(QStringLiteral("RATE_LIMITED"), QStringLiteral("宿主结果超过 256 KiB")), QJsonValue());
        return;
    }
    if (!stopping_)
        QMetaObject::invokeMethod(
            worker_, [=] { worker_->deliver(id, error, result); }, Qt::QueuedConnection);
}
void PluginRuntime::event(const QString &name, const QJsonValue &value)
{
    if (stopping_)
        return;
    if (QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact).size() > Plugins::MaxMessageBytes)
    { emit failed(QStringLiteral("事件数据超过 256 KiB，已停止插件事件流")); return; }
    if (name == QStringLiteral("courses.tableChanged"))
        events_.removeIf([](const auto &item)
                         { return item.first == QStringLiteral("courses.stateChanged"); });
    if (name == QStringLiteral("courses.stateChanged"))
        for (auto &event : events_)
            if (event.first == name)
            {
                event.second = value;
                return;
            }
    if (events_.size() >= 128)
    {
        emit failed(QStringLiteral("事件积压超过 128 条，插件已隔离"));
        return;
    }
    events_.enqueue(qMakePair(name, value));
    nextEvent();
}
void PluginRuntime::nextEvent()
{
    if (stopping_ || eventPending_ || events_.isEmpty())
        return;
    const auto event = events_.dequeue();
    eventPending_ = true;
    QMetaObject::invokeMethod(
        worker_, [this, event] { worker_->event(event.first, event.second); }, Qt::QueuedConnection);
}
#include "PluginRuntime.moc"
