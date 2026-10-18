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
    let callbacks={},subscriptions={},serial=0;
    function request(method,args,callback,retain) {
        let id=host.send(method,JSON.stringify(args||{},function(key,value){
            if(typeof value==='function'||typeof value==='symbol'||typeof value==='undefined'||(typeof value==='number'&&!isFinite(value)))throw new Error('Only finite JSON-compatible values are accepted');
            return value;
        }));
        if(!id) throw new Error('Request rejected: plugin stopped or resource limit exceeded');
        if(typeof callback==='function') callbacks[id]={fn:callback,retain:!!retain};
        return {dispose:function(){delete callbacks[id];host.cancel(id);},cancel:function(){delete callbacks[id];host.cancel(id);}};
    }
    let ui={};
    ['registerSettingsPage','registerComponent','registerWindow'].forEach(function(name){ui[name]=function(id,definition){return request('ui.'+name,{id:id,definition:definition});};});
    ui.update=function(instanceId,patch){return request('ui.update',{instanceId:instanceId,patch:patch});};
    ui.setTree=function(instanceId,tree){return request('ui.setTree',{instanceId:instanceId,tree:tree});};
    ui.openWindow=function(id){return request('ui.openWindow',{id:id});};
    let ctx={
      courses:{getSnapshot:function(cb){return request('courses.getSnapshot',{},cb);}},
      profiles:{getCurrent:function(cb){return request('profiles.getCurrent',{},cb);},applyChanges:function(args,cb){return request('profiles.applyChanges',args,cb);}},
      settings:{get:function(cb){return request('settings.get',{},cb);},update:function(args,cb){return request('settings.update',args,cb);}},
      storage:{get:function(key,cb){return request('storage.get',{key:key},cb);},set:function(key,value,cb){return request('storage.set',{key:key,value:value},cb);},remove:function(key,cb){return request('storage.remove',{key:key},cb);}},
      timers:{after:function(ms,fn){return request('timer.after',{ms:ms},function(e){if(!e)fn();});},every:function(ms,fn){return request('timer.every',{ms:ms},function(e){if(!e)fn();},true);}},
      http:{request:function(options,cb){return request('http.request',options,cb);}},
      notifications:{show:function(options){return request('notifications.show',options);}},
      log:{},ui:ui,
      events:{on:function(name,fn){let id=String(++serial);if(typeof fn!=='function')throw new Error('Handler required'); subscriptions[id]={name:name,fn:fn};request('events.subscribe',{name:name});return {dispose:function(){delete subscriptions[id];}};}}
    };
    ['debug','info','warn','error'].forEach(function(level){ctx.log[level]=function(message){request('log',{level:level,message:String(message)});};});
    return {context:ctx,
      complete:function(id,error,result){let item=callbacks[id];if(!item)return;if(!item.retain)delete callbacks[id];item.fn(error,result);},
      dispatch:function(name,value){Object.keys(subscriptions).forEach(function(id){let item=subscriptions[id];if(item&&item.name===name)item.fn(value);});},
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
