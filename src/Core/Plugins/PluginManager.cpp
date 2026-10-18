#include "PluginManager.h"
#include "PluginRuntime.h"
#include "PluginIo.h"
#include "../ConfigManager/ConfigManager.h"
#include "../ProfileManager/ProfileManager.h"
#include "../ThemeManager/ThemeManager.h"
#include "../Logger/Logger.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>
#include <QUrl>
#include <QRegularExpression>

PluginManager::Record::~Record() = default;
PluginManager::PluginManager(const QString &directory, bool recovery, QObject *parent)
    : QObject(parent), directory_(directory), io_(new PluginIo(directory)),
      courses_(new CourseRefreshService(this)), recovery_(recovery)
{
    io_->moveToThread(&ioThread_);
    connect(&ioThread_, &QThread::finished, io_, &QObject::deleteLater);
    ioThread_.start();
    connect(io_, &PluginIo::completed, this,
            [this](const QString &plugin, quint64 generation, const QString &id, const QJsonObject &error,
                   const QJsonValue &result)
            {
                auto record = records_.value(plugin);
                if (!record || record->generation != generation)
                    return;
                const auto token = plugin + QLatin1Char('/') + id;
                if (internal_.contains(token))
                {
                    auto request = internal_.take(token);
                    if (request.context)
                        request.callback(error, result);
                    return;
                }
                if (record->runtime)
                    record->runtime->deliver(id, error, result);
            });
    connect(courses_, &CourseRefreshService::tableChanged, this,
            [this] { broadcast(QStringLiteral("courses.tableChanged"), courseSnapshot()); });
    connect(courses_, &CourseRefreshService::stateChanged, this,
            [this] { broadcast(QStringLiteral("courses.stateChanged"), courseSnapshot()); });
    connect(courses_, &CourseRefreshService::eventOccurred, this,
            [this](const CourseRefreshService::Event &event)
            {
                QString name;
                switch (event.type)
                {
                case CourseRefreshService::EventType::ClassStarted:
                    name = QStringLiteral("course.started");
                    break;
                case CourseRefreshService::EventType::ClassEnded:
                    name = QStringLiteral("course.ended");
                    break;
                case CourseRefreshService::EventType::ClassStartingSoon:
                    name = QStringLiteral("course.startingSoon");
                    break;
                }
                auto data = courseSnapshot();
                data.insert(QStringLiteral("subject"), event.subject);
                data.insert(QStringLiteral("scheduledTime"),
                            event.scheduledTime.toOffsetFromUtc(event.scheduledTime.offsetFromUtc())
                                .toString(Qt::ISODate));
                broadcast(name, data);
            });
    connect(&ProfileManager::instance(), &ProfileManager::committed, this,
            [this]
            {
                courses_->reloadTable();
                broadcast(QStringLiteral("profile.changed"),
                          QJsonObject{{QStringLiteral("revision"),
                                       QString::number(ProfileManager::instance().revision())}});
            });
    auto theme = [this]
    {
        broadcast(
            QStringLiteral("theme.changed"),
            QJsonObject{{QStringLiteral("color"), ThemeManager::instance().themeColor().name()},
                        {QStringLiteral("mode"), static_cast<int>(ThemeManager::instance().themeMode())}});
    };
    connect(&ThemeManager::instance(), &ThemeManager::themeColorChanged, this, theme);
    connect(&ThemeManager::instance(), &ThemeManager::themeModeChanged, this, theme);
}
PluginManager::~PluginManager()
{
    stopAll();
    ioThread_.quit();
    ioThread_.wait();
}
QString PluginManager::pluginsDirectory() const
{
    return QDir(directory_).filePath(QStringLiteral("plugins"));
}
void PluginManager::start()
{
    auto candidate = ConfigManager::instance().config();
    if (candidate.pluginConfig.recoveryNextStart)
    {
        recovery_ = true;
        candidate.pluginConfig.recoveryNextStart = false;
        QString error;
        if (!ConfigManager::instance().commit(candidate, &error))
            Logger::instance().log(Logger::Level::Warning, error);
    }
    scan();
    courses_->start();
    if (!recovery_)
        for (const auto &record : records_)
        {
            const auto preference =
                ConfigManager::instance().config().pluginConfig.plugins.value(record->manifest.id);
            if (preference.enabled && preference.fault.isEmpty())
                startRecord(record);
        }
}
void PluginManager::scan()
{
    if (stopped_)
        return;
    if (!QDir().mkpath(pluginsDirectory()) || !Plugins::within(directory_, pluginsDirectory()))
    {
        emit notification(QStringLiteral("插件扫描失败"), QStringLiteral("无法访问插件目录，或目录路径越界"));
        return;
    }
    QMap<QString, Plugins::Manifest> discovered;
    for (const auto &entry :
         QDir(pluginsDirectory()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
        auto manifest = Plugins::Manifest::read(entry.absoluteFilePath());
        if (!Plugins::within(pluginsDirectory(), entry.absoluteFilePath()))
            manifest.failure = QStringLiteral("插件目录路径越界");
        auto key = manifest.id.isEmpty() ? entry.fileName() : manifest.id;
        if (discovered.contains(key))
        {
            auto original = discovered.value(key);
            original.failure = QStringLiteral("存在重复插件 ID");
            manifest.failure = original.failure;
            discovered[key] = original;
            discovered.insert(key + QLatin1Char('@') + entry.fileName(), manifest);
        }
        else
            discovered.insert(key, manifest);
    }
    for (auto it = discovered.begin(); it != discovered.end(); ++it)
    {
        if (auto existing = records_.value(it.key());
            existing && (existing->runtime || existing->updatePending ||
                         existing->manifest.fingerprint != it->fingerprint))
        {
            if (existing->manifest.fingerprint != it->fingerprint || !it->valid())
            {
                existing->updatePending = true;
                existing->failure = QStringLiteral("检测到版本 %1；当前版本 %2，更新需重启。%3")
                                        .arg(it->version, existing->manifest.version, it->failure);
            }
            continue;
        }
        auto record = std::make_shared<Record>();
        record->key = it.key();
        record->manifest = it.value();
        record->failure = it->failure;
        if (!it->valid())
            record->state = QStringLiteral("不可用");
        else
        {
            auto preference = ConfigManager::instance().config().pluginConfig.plugins.value(it->id);
            if (!preference.fault.isEmpty())
            {
                record->state = QStringLiteral("运行故障");
                record->failure = preference.fault;
            }
        }
        records_[it.key()] = record;
    }
    const auto keys = records_.keys();
    for (const auto &key : keys)
        if (!discovered.contains(key))
        {
            auto record = records_[key];
            if (record->runtime)
            {
                record->updatePending = true;
                record->failure = QStringLiteral("插件目录已移除；重启后卸载");
            }
            else
                records_.remove(key);
        }
    emit changed();
}
bool PluginManager::setEnabled(const QString &id, bool enabled, QString *error)
{
    auto record = records_.value(id);
    auto fail = [&](const QString &message)
    {
        if (error)
            *error = message;
        return false;
    };
    if (!record || !record->manifest.valid() || record->key != record->manifest.id)
        return fail(QStringLiteral("插件不可用"));
    if (writesBlocked_ || stopped_)
        return fail(QStringLiteral("应用正在退出"));
    if (enabled && record->updatePending)
        return fail(QStringLiteral("插件文件已更新，请先重启"));
    if (enabled && record->runtime)
        return true;
    if (enabled)
    {
        int count = 0;
        for (const auto &item : records_)
            if (item->runtime)
                ++count;
        if (count >= Plugins::MaxPlugins)
            return fail(QStringLiteral("最多同时运行 16 个插件"));
    }
    if (!enabled && preparePluginStop && !preparePluginStop(id))
        return false;
    auto candidate = ConfigManager::instance().config();
    auto &preference = candidate.pluginConfig.plugins[id];
    preference.enabled = enabled;
    if (enabled)
    {
        preference.grantedPermissions = record->manifest.permissions;
        preference.approvedVersion = record->manifest.version;
        preference.fault.clear();
    }
    if (!ConfigManager::instance().commit(candidate, error))
        return false;
    if (enabled)
        startRecord(record);
    else
        stopRecord(record, false);
    emit changed();
    return true;
}
void PluginManager::startRecord(const std::shared_ptr<Record> &record)
{
    if (!record->manifest.valid() || record->runtime || record->key != record->manifest.id)
        return;
    const auto preference =
        ConfigManager::instance().config().pluginConfig.plugins.value(record->manifest.id);
    if (preference.grantedPermissions != record->manifest.permissions)
    {
        record->state = QStringLiteral("待重新授权");
        record->failure = QStringLiteral("插件权限已变化，请重新确认并启用");
        emit changed();
        return;
    }
    int running = 0;
    for (const auto &item : records_)
        if (item->runtime)
            ++running;
    if (running >= Plugins::MaxPlugins)
    {
        record->failure = QStringLiteral("最多同时运行 16 个插件");
        record->state = QStringLiteral("已停用");
        emit changed();
        return;
    }
    record->generation = ++generation_;
    record->state = QStringLiteral("启动中");
    record->failure.clear();
    const auto id = record->manifest.id;
    const auto generation = record->generation;
    const auto permissions = preference.grantedPermissions;
    QMetaObject::invokeMethod(io_, [=] { io_->attach(id, generation, permissions); }, Qt::QueuedConnection);
    record->runtime = std::make_unique<PluginRuntime>(record->manifest, generation);
    connect(
        record->runtime.get(), &PluginRuntime::request, this,
        [this, record, generation](const QString &requestId, const QString &method, const QJsonObject &args)
        {
            if (record->generation == generation && record->runtime)
                request(record, requestId, method, args);
        });
    connect(record->runtime.get(), &PluginRuntime::cancelRequest, this,
            [this, id, generation](const QString &requestId)
            {
                QMetaObject::invokeMethod(
                    io_, [=] { io_->cancel(id, generation, requestId); }, Qt::QueuedConnection);
            });
    connect(record->runtime.get(), &PluginRuntime::started, this,
            [this, record, generation]
            {
                if (record->generation != generation || !record->runtime ||
                    record->state != QStringLiteral("启动中"))
                    return;
                record->state = QStringLiteral("运行中");
                emit changed();
                emit contributionsChanged();
            });
    connect(record->runtime.get(), &PluginRuntime::failed, this,
            [this, record, generation](const QString &error)
            {
                if (record->generation == generation && record->runtime)
                    fault(record, error);
            });
    record->runtime->start();
    emit changed();
}
void PluginManager::fault(const std::shared_ptr<Record> &record, const QString &message)
{
    record->failure = message.left(8192);
    record->logs.append(QStringLiteral("ERROR ") + record->failure);
    auto candidate = ConfigManager::instance().config();
    candidate.pluginConfig.plugins[record->manifest.id].fault = record->failure;
    QString error;
    if (!ConfigManager::instance().commit(candidate, &error))
        record->failure += QStringLiteral("\n故障记录保存失败：") + error;
    stopRecord(record, true);
    emit changed();
}
void PluginManager::stopRecord(const std::shared_ptr<Record> &record, bool fault)
{
    if (record->state == QStringLiteral("停止中"))
        return;
    record->stopFault = fault;
    record->state = QStringLiteral("停止中");
    emit changed();
    emit pluginStopped(record->manifest.id, fault);
    const auto id = record->manifest.id;
    const auto generation = record->generation;
    QMetaObject::invokeMethod(io_, [=] { io_->detach(id, generation); }, Qt::QueuedConnection);
    if (record->runtime)
    {
        record->runtime->disconnect(this);
        connect(record->runtime.get(), &PluginRuntime::failed, this,
                [this, record](const QString &message)
                {
                    record->failure = QStringLiteral("停用时发生异常：") + message.left(4096);
                    record->logs.append(record->failure);
                    emit changed();
                });
        connect(record->runtime.get(), &PluginRuntime::stopped, this,
                [this, record, generation]
                {
                    if (record->generation == generation && record->runtime)
                        finishStop(record);
                });
        record->runtime->beginStop();
    }
    else
        finishStop(record);
    emit contributionsChanged();
}
void PluginManager::finishStop(const std::shared_ptr<Record> &record)
{
    if (record->runtime)
    {
        record->runtime->disconnect(this);
        record->runtime.reset();
    }
    const auto id = record->manifest.id;
    record->settingsPages.clear();
    record->components.clear();
    record->windows.clear();
    record->subscriptions.clear();
    const auto instances = views_.keys();
    for (const auto &view : instances)
        if (views_[view] == id)
            views_.remove(view);
    const auto pending = internal_.keys();
    for (const auto &token : pending)
        if (token.startsWith(id + QLatin1Char('/')))
        {
            auto item = internal_.take(token);
            if (item.context)
                item.callback(Plugins::error(QStringLiteral("PLUGIN_STOPPED"), QStringLiteral("插件已停止")),
                              {});
        }
    record->state = record->stopFault ? QStringLiteral("运行故障") : QStringLiteral("已停用");
    emit contributionsChanged();
    emit changed();
}
void PluginManager::stopAll()
{
    if (stopped_)
        return;
    stopped_ = true;
    writesBlocked_ = true;
    courses_->stop();
    for (const auto &record : records_)
        if (record->runtime)
        {
            stopRecord(record, false);
            if (record->runtime)
                record->runtime->stop();
            finishStop(record);
        }
    QMetaObject::invokeMethod(io_, [] {}, Qt::BlockingQueuedConnection);
}
bool PluginManager::requestRecovery(QString *error)
{
    auto candidate = ConfigManager::instance().config();
    candidate.pluginConfig.recoveryNextStart = true;
    if (!ConfigManager::instance().commit(candidate, error))
        return false;
    emit restartRequested();
    return true;
}
QJsonObject PluginManager::courseSnapshot() const
{
    const auto &table = courses_->table();
    const auto &state = courses_->state();
    QJsonArray classes;
    for (const auto &course : table.classes)
        classes.append(
            QJsonObject{{QStringLiteral("subject"), course.subject},
                        {QStringLiteral("startTime"), course.startTime.toString(QStringLiteral("HH:mm:ss"))},
                        {QStringLiteral("endTime"), course.endTime.toString(QStringLiteral("HH:mm:ss"))}});
    return {
        {QStringLiteral("profileSession"), mutations_.session()},
        {QStringLiteral("profileRevision"), QString::number(ProfileManager::instance().revision())},
        {QStringLiteral("tableRevision"), QString::number(table.revision)},
        {QStringLiteral("date"), table.date.toString(Qt::ISODate)},
        {QStringLiteral("name"), table.name},
        {QStringLiteral("valid"), table.valid},
        {QStringLiteral("error"), table.error},
        {QStringLiteral("classes"), classes},
        {QStringLiteral("phase"), static_cast<int>(state.phase)},
        {QStringLiteral("currentClassIndex"), state.currentClassIndex},
        {QStringLiteral("nextClassIndex"), state.nextClassIndex},
        {QStringLiteral("remainingSeconds"), double(state.remainingSeconds)},
        {QStringLiteral("countdownTarget"),
         state.countdownTarget.toOffsetFromUtc(state.countdownTarget.offsetFromUtc()).toString(Qt::ISODate)}};
}
void PluginManager::broadcast(const QString &name, const QJsonValue &value)
{
    auto data = value.toObject();
    data.insert(QStringLiteral("sequence"), QString::number(++sequence_));
    for (const auto &record : records_)
        if (record->runtime && record->state == QStringLiteral("运行中") &&
            record->subscriptions.contains(name))
            record->runtime->event(name, data);
}
QJsonObject PluginManager::definition(const QString &plugin, const QString &kind, const QString &id) const
{
    const auto record = records_.value(plugin);
    if (!record || record->state != QStringLiteral("运行中"))
        return {};
    return kind == QStringLiteral("settings")    ? record->settingsPages.value(id)
           : kind == QStringLiteral("component") ? record->components.value(id)
                                                 : record->windows.value(id);
}
QMap<QString, QString> PluginManager::componentTypes() const
{
    QMap<QString, QString> types;
    for (const auto &record : records_)
        if (record->state == QStringLiteral("运行中"))
            for (auto it = record->components.begin(); it != record->components.end(); ++it)
                types.insert(record->manifest.id + QLatin1Char('/') + it.key(),
                             record->manifest.name + QStringLiteral(" · ") +
                                 it->value(QStringLiteral("name")).toString(it.key()));
    return types;
}
void PluginManager::createView(const QString &plugin, const QString &kind, const QString &contribution,
                               const QString &instance, const QJsonObject &settings)
{
    auto record = records_.value(plugin);
    if (!record || !record->runtime || record->state != QStringLiteral("运行中"))
        return;
    views_[instance] = plugin;
    record->runtime->event(QStringLiteral("ui.created"),
                           QJsonObject{{QStringLiteral("instanceId"), instance},
                                       {QStringLiteral("kind"), kind},
                                       {QStringLiteral("contributionId"), contribution},
                                       {QStringLiteral("settings"), settings}});
}
void PluginManager::releaseView(const QString &instance)
{
    auto record = records_.value(views_.take(instance));
    if (record && record->runtime)
        record->runtime->event(QStringLiteral("ui.destroyed"),
                               QJsonObject{{QStringLiteral("instanceId"), instance}});
}
void PluginManager::action(const QString &instance, const QString &action, const QJsonValue &value)
{
    auto record = records_.value(views_.value(instance));
    if (record && record->runtime)
    {
        record->lastUserAction = QDateTime::currentMSecsSinceEpoch();
        record->runtime->event(QStringLiteral("ui.action"),
                               QJsonObject{{QStringLiteral("instanceId"), instance},
                                           {QStringLiteral("actionId"), action},
                                           {QStringLiteral("value"), value}});
    }
}
void PluginManager::saved(const QString &id, const QJsonObject &values)
{
    auto record = records_.value(id);
    if (record && record->runtime)
        record->runtime->event(QStringLiteral("settings.saved"), values);
}
void PluginManager::storage(const QString &plugin, const QString &method, const QJsonObject &args,
                            QObject *context, Completion callback)
{
    auto record = records_.value(plugin);
    if (!record || !record->runtime)
    {
        callback(Plugins::error(QStringLiteral("PLUGIN_STOPPED"), QStringLiteral("插件已停止")), {});
        return;
    }
    const auto requestId = QStringLiteral("host-") + QString::number(++internalRequest_);
    internal_.insert(plugin + QLatin1Char('/') + requestId, {context, std::move(callback)});
    const auto generation = record->generation;
    QMetaObject::invokeMethod(
        io_, [=] { io_->request(plugin, generation, requestId, method, args); }, Qt::QueuedConnection);
}

void PluginManager::request(const std::shared_ptr<Record> &record, const QString &id, const QString &method,
                            const QJsonObject &args)
{
    auto reply = [&](const QJsonObject &error = {}, const QJsonValue &result = QJsonValue())
    {
        if (method == QStringLiteral("profiles.applyChanges") || method == QStringLiteral("settings.update"))
        {
            const auto before = args.value(QStringLiteral("expectedRevision")).toString();
            const auto after = QString::number(method == QStringLiteral("profiles.applyChanges")
                                                   ? ProfileManager::instance().revision()
                                                   : ConfigManager::instance().revision());
            const auto audit =
                QStringLiteral("Plugin %1 %2 #%3 revision %4 -> %5: %6")
                    .arg(record->manifest.id, method, id,
                         QRegularExpression(QStringLiteral("\\A[0-9]{1,20}\\z")).match(before).hasMatch()
                             ? before
                             : QStringLiteral("invalid"),
                         after, error.value(QStringLiteral("code")).toString(QStringLiteral("OK")));
            record->logs.append(audit);
            while (record->logs.size() > 200)
                record->logs.removeFirst();
            Logger::instance().log(Logger::Level::Info, audit);
        }
        if (record->runtime)
            record->runtime->deliver(id, error, result);
    };
    const auto permissions =
        ConfigManager::instance().config().pluginConfig.plugins.value(record->manifest.id).grantedPermissions;
    auto denied = [&]
    { reply(Plugins::error(QStringLiteral("PERMISSION_DENIED"), QStringLiteral("未授予此能力"))); };
    const bool starting = record->state == QStringLiteral("启动中");
    if (stopped_ || record->state == QStringLiteral("停止中"))
    {
        reply(Plugins::error(QStringLiteral("PLUGIN_STOPPED"), QStringLiteral("宿主或插件正在停止")));
        return;
    }
    if (method == QStringLiteral("log"))
    {
        const auto line = QDateTime::currentDateTime().toString(Qt::ISODate) + QLatin1Char(' ') +
                          args.value(QStringLiteral("level")).toString() + QLatin1Char(' ') +
                          args.value(QStringLiteral("message")).toString().left(2048);
        record->logs.append(line);
        while (record->logs.size() > 200)
            record->logs.removeFirst();
        reply();
        return;
    }
    if (method == QStringLiteral("events.subscribe"))
    {
        const auto name = args.value(QStringLiteral("name")).toString();
        bool allowed = false;
        if (name.startsWith(QStringLiteral("course.")) || name.startsWith(QStringLiteral("courses.")))
            allowed = permissions.value(QStringLiteral("courses")).toString() == QStringLiteral("read");
        else if (name == QStringLiteral("profile.changed"))
            allowed = permissions.contains(QStringLiteral("profiles"));
        else
            allowed = QStringList{QStringLiteral("theme.changed"), QStringLiteral("ui.created"),
                                  QStringLiteral("ui.destroyed"), QStringLiteral("ui.action"),
                                  QStringLiteral("settings.saved")}
                          .contains(name);
        if (!allowed)
        {
            denied();
            return;
        }
        record->subscriptions.insert(name);
        reply();
        if (name.startsWith(QStringLiteral("courses.")))
            record->runtime->event(name, courseSnapshot());
        return;
    }
    if (method == QStringLiteral("courses.getSnapshot"))
    {
        if (permissions.value(QStringLiteral("courses")).toString() != QStringLiteral("read"))
        {
            denied();
            return;
        }
        reply({}, courseSnapshot());
        return;
    }
    if (method == QStringLiteral("profiles.getCurrent"))
    {
        if (!permissions.contains(QStringLiteral("profiles")))
        {
            denied();
            return;
        }
        reply({}, mutations_.snapshot());
        return;
    }
    if (method == QStringLiteral("profiles.applyChanges"))
    {
        if (permissions.value(QStringLiteral("profiles")).toString() != QStringLiteral("write-current"))
        {
            denied();
            return;
        }
        if (starting || writesBlocked_)
        {
            reply(Plugins::error(QStringLiteral("CANCELED"),
                                 QStringLiteral("激活或退出准备期间不可修改业务数据")));
            return;
        }
        if (editorDirty && editorDirty())
        {
            reply(Plugins::error(QStringLiteral("EDITOR_DIRTY"), QStringLiteral("档案编辑器存在未保存草稿")));
            return;
        }
        QJsonValue result;
        const auto error = mutations_.apply(args, result);
        reply(error, result);
        if (error.isEmpty())
            emit profileWritten(result.toObject().value(QStringLiteral("canceledDates")).toArray());
        record->logs.append(QStringLiteral("profiles.applyChanges: ") +
                            error.value(QStringLiteral("code")).toString(QStringLiteral("OK")));
        return;
    }
    if (method == QStringLiteral("settings.get"))
    {
        if (!permissions.contains(QStringLiteral("settings")))
        {
            denied();
            return;
        }
        const auto &config = ConfigManager::instance().config();
        reply({},
              QJsonObject{{QStringLiteral("revision"), QString::number(ConfigManager::instance().revision())},
                          {QStringLiteral("themeColor"), config.appearanceConfig.themeColor},
                          {QStringLiteral("themeMode"), static_cast<int>(config.appearanceConfig.themeMode)},
                          {QStringLiteral("courseBarEnabled"), config.courseBarConfig.enable},
                          {QStringLiteral("courseBarHeight"), config.courseBarConfig.height},
                          {QStringLiteral("componentSpacing"), config.courseBarConfig.componentSpacing}});
        return;
    }
    if (method == QStringLiteral("settings.update"))
    {
        if (permissions.value(QStringLiteral("settings")).toString() != QStringLiteral("write"))
        {
            denied();
            return;
        }
        if (starting || writesBlocked_)
        {
            reply(Plugins::error(QStringLiteral("CANCELED"), QStringLiteral("当前不可写入")));
            return;
        }
        if (args.value(QStringLiteral("expectedRevision")).toString() !=
            QString::number(ConfigManager::instance().revision()))
        {
            reply(Plugins::error(QStringLiteral("CONFLICT"), QStringLiteral("设置已变化")));
            return;
        }
        if (!args.value(QStringLiteral("changes")).isObject())
        {
            reply(Plugins::error(QStringLiteral("INVALID_ARGUMENT"), QStringLiteral("changes 必须是对象")));
            return;
        }
        auto candidate = ConfigManager::instance().config();
        const auto changes = args.value(QStringLiteral("changes")).toObject();
        for (auto it = changes.begin(); it != changes.end(); ++it)
        {
            bool valid = true;
            if (it.key() == QStringLiteral("themeColor") && it.value().isString() &&
                QRegularExpression(QStringLiteral("\\A#[0-9a-fA-F]{6}\\z"))
                    .match(it.value().toString())
                    .hasMatch())
                candidate.appearanceConfig.themeColor = it.value().toString();
            else if (it.key() == QStringLiteral("themeMode") && it.value().isDouble() &&
                     it.value().toDouble() == it.value().toInt() && it.value().toInt(-1) >= 0 &&
                     it.value().toInt() <= 2)
                candidate.appearanceConfig.themeMode = static_cast<AppearanceThemeMode>(it.value().toInt());
            else if (it.key() == QStringLiteral("courseBarEnabled") && it.value().isBool())
                candidate.courseBarConfig.enable = it.value().toBool();
            else if (it.key() == QStringLiteral("courseBarHeight") && it.value().isDouble() &&
                     it.value().toDouble() == it.value().toInt() && it.value().toInt() >= 24 &&
                     it.value().toInt() <= 200)
                candidate.courseBarConfig.height = it.value().toInt();
            else if (it.key() == QStringLiteral("componentSpacing") && it.value().isDouble() &&
                     it.value().toDouble() == it.value().toInt() && it.value().toInt() >= 0 &&
                     it.value().toInt() <= CourseBarConfig::MaximumSpacing)
                candidate.courseBarConfig.componentSpacing = it.value().toInt(-1);
            else
                valid = false;
            if (!valid)
            {
                reply(Plugins::error(QStringLiteral("INVALID_ARGUMENT"),
                                     QStringLiteral("无效或不允许的设置字段：%1").arg(it.key())));
                return;
            }
        }
        QString error;
        if (!ConfigManager::instance().commit(candidate, &error))
        {
            reply(Plugins::error(QStringLiteral("SAVE_FAILED"), error));
            return;
        }
        ThemeManager::instance().applyConfiguredTheme();
        reply({}, QJsonObject{
                      {QStringLiteral("revision"), QString::number(ConfigManager::instance().revision())}});
        return;
    }
    if (method.startsWith(QStringLiteral("storage.")) || method == QStringLiteral("http.request"))
    {
        if (starting && method != QStringLiteral("storage.get"))
        {
            reply(Plugins::error(QStringLiteral("CANCELED"),
                                 QStringLiteral("激活阶段仅允许读取存储；在定时任务或事件中发起其他操作")));
            return;
        }
        const auto plugin = record->manifest.id;
        const auto generation = record->generation;
        QMetaObject::invokeMethod(
            io_, [=] { io_->request(plugin, generation, id, method, args); }, Qt::QueuedConnection);
        return;
    }
    if (method == QStringLiteral("notifications.show"))
    {
        if (!permissions.value(QStringLiteral("notifications")).toBool())
        {
            denied();
            return;
        }
        const auto now = QDateTime::currentMSecsSinceEpoch();
        if (starting || now - record->lastNotification < 3000)
        {
            reply(Plugins::error(QStringLiteral("RATE_LIMITED"), QStringLiteral("通知限速或插件仍在激活")));
            return;
        }
        record->lastNotification = now;
        emit notification(args.value(QStringLiteral("title")).toString().left(128),
                          args.value(QStringLiteral("message")).toString().left(512));
        reply();
        return;
    }
    if (method.startsWith(QStringLiteral("ui.register")))
    {
        const auto local = args.value(QStringLiteral("id")).toString();
        const auto definition = args.value(QStringLiteral("definition")).toObject();
        const auto kind =
            method == QStringLiteral("ui.registerSettingsPage") ? QStringLiteral("settingsPages")
            : method == QStringLiteral("ui.registerComponent")  ? QStringLiteral("courseBarComponents")
            : method == QStringLiteral("ui.registerWindow")     ? QStringLiteral("windows")
                                                                : QString();
        bool declared = false;
        for (const auto &contribution : record->manifest.contributions.value(kind).toArray())
            if (contribution.toObject().value(QStringLiteral("id")).toString() == local)
                declared = true;
        if (kind.isEmpty() || !declared || !args.value(QStringLiteral("definition")).isObject())
        {
            reply(Plugins::error(QStringLiteral("INVALID_ARGUMENT"),
                                 QStringLiteral("视图未在清单声明或定义无效")));
            return;
        }
        auto validation = Plugins::validateView(definition);
        if (validation.isEmpty() && definition.contains(QStringLiteral("settings")))
            validation = Plugins::validateView(definition.value(QStringLiteral("settings")).toObject());
        if (!validation.isEmpty())
        {
            reply(Plugins::error(QStringLiteral("INVALID_ARGUMENT"), validation));
            fault(record, validation);
            return;
        }
        auto *target = kind == QStringLiteral("settingsPages") ? &record->settingsPages
                       : kind == QStringLiteral("windows")     ? &record->windows
                                                               : &record->components;
        if (target->contains(local))
        {
            reply(Plugins::error(QStringLiteral("CONFLICT"), QStringLiteral("贡献已注册")));
            return;
        }
        target->insert(local, definition);
        reply();
        if (!starting)
            emit contributionsChanged();
        return;
    }
    if (method == QStringLiteral("ui.update"))
    {
        if (!record->uiClock.isValid() || record->uiClock.elapsed() >= 1000)
        {
            record->uiClock.start();
            record->uiUpdates = record->uiTrees = 0;
        }
        if (++record->uiUpdates > 30)
        {
            reply(
                Plugins::error(QStringLiteral("RATE_LIMITED"), QStringLiteral("界面属性更新每秒最多 30 次")));
            return;
        }
        const auto instance = args.value(QStringLiteral("instanceId")).toString();
        if (views_.value(instance) != record->manifest.id || !args.value(QStringLiteral("patch")).isObject())
        {
            reply(Plugins::error(QStringLiteral("NOT_FOUND"), QStringLiteral("视图实例不存在")));
            return;
        }
        emit viewUpdated(instance, args.value(QStringLiteral("patch")).toObject());
        reply();
        return;
    }
    if (method == QStringLiteral("ui.setTree"))
    {
        if (!record->uiClock.isValid() || record->uiClock.elapsed() >= 1000)
        {
            record->uiClock.start();
            record->uiUpdates = record->uiTrees = 0;
        }
        if (++record->uiTrees > 2)
        {
            reply(
                Plugins::error(QStringLiteral("RATE_LIMITED"), QStringLiteral("界面结构更新每秒最多 2 次")));
            return;
        }
        const auto instance = args.value(QStringLiteral("instanceId")).toString();
        if (views_.value(instance) != record->manifest.id)
        {
            reply(Plugins::error(QStringLiteral("NOT_FOUND"), QStringLiteral("视图实例不存在")));
            return;
        }
        const auto tree = args.value(QStringLiteral("tree")).toObject();
        const auto error = Plugins::validateView(QJsonObject{{QStringLiteral("tree"), tree}});
        if (!error.isEmpty())
        {
            reply(Plugins::error(QStringLiteral("INVALID_ARGUMENT"), error));
            return;
        }
        emit viewTreeReplaced(instance, tree);
        reply();
        return;
    }
    if (method == QStringLiteral("ui.openWindow"))
    {
        const auto local = args.value(QStringLiteral("id")).toString();
        if (starting || !record->windows.contains(local))
        {
            reply(Plugins::error(QStringLiteral("NOT_FOUND"), QStringLiteral("窗口不可用")));
            return;
        }
        if (QDateTime::currentMSecsSinceEpoch() - record->lastUserAction > 3000)
        {
            reply(
                Plugins::error(QStringLiteral("PERMISSION_DENIED"), QStringLiteral("打开窗口需要用户交互")));
            return;
        }
        record->lastUserAction = 0;
        emit windowRequested(record->manifest.id, local);
        reply();
        return;
    }
    reply(Plugins::error(QStringLiteral("NOT_FOUND"), method));
}
