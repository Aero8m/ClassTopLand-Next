#include "PluginPreview.h"
#include "../../Ui/Plugins/PluginCenterWindow.h"
#include "../../Ui/Plugins/PluginNativeView.h"
#include "../../Ui/Plugins/PluginUiHost.h"
#include "../ConfigManager/ConfigManager.h"
#include "../ProfileManager/ProfileManager.h"
#include "PluginManager.h"
#include "PluginManifest.h"
#include <ElaApplication.h>
#include <QApplication>
#include <QDate>
#include <QDirIterator>
#include <QEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScreen>
#include <QTimer>
#include <iostream>
#include <thread>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace
{
// Keep normal layout and show/hide lifecycle events, without mapping any preview
// window (including popups) onto the user's desktop. QWidget::grab still renders it.
class PreviewWindowFilter final : public QObject
{
  protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Polish || event->type() == QEvent::Show)
            if (auto *widget = qobject_cast<QWidget *>(object); widget && widget->isWindow())
            {
                widget->setAttribute(Qt::WA_DontShowOnScreen);
                widget->setAttribute(Qt::WA_ShowWithoutActivating);
            }
        return false;
    }
};
}

PluginPreview::PluginPreview(QObject *parent) : QObject(parent)
{
#ifdef Q_OS_WIN
    process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    connect(&process_, &QProcess::readyReadStandardOutput, this, &PluginPreview::consume);
    connect(&process_, &QProcess::readyReadStandardError, this, [this] { process_.readAllStandardError(); });
    connect(&process_, &QProcess::finished, this, [this] { failAll(QStringLiteral("预览进程已结束")); });
    connect(&process_, &QProcess::errorOccurred, this, [this] { failAll(process_.errorString()); });
}
PluginPreview::~PluginPreview()
{
    stop();
}
void PluginPreview::failAll(const QString &message)
{
    auto start = std::move(started_);
    const auto requests = std::move(pending_);
    pending_.clear();
    const auto error = Plugins::error(QStringLiteral("PREVIEW_STOPPED"), message);
    if (start)
        start(error, {});
    for (const auto &completion : requests)
        completion(error, {});
}
void PluginPreview::stop()
{
    ++generation_;
    if (running())
    {
        process_.write("{\"operation\":\"shutdown\"}\n");
        process_.closeWriteChannel();
        if (!process_.waitForFinished(1000))
        {
            process_.kill();
            process_.waitForFinished(1000);
        }
    }
    failAll(QStringLiteral("预览已停止"));
    buffer_.clear();
    directory_.reset();
    plugin_.clear();
    fingerprint_.clear();
}
void PluginPreview::start(const QString &source, Completion completion)
{
    stop();
    auto fail = [&](const QString &message) {
        completion(Plugins::error(QStringLiteral("INVALID_ARGUMENT"), message), {});
    };
    const auto manifest = Plugins::Manifest::read(source);
    if (!manifest.valid())
    {
        fail(manifest.failure);
        return;
    }
    directory_ =
        std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/ctl-plugin-preview-XXXXXX"));
    if (!directory_->isValid())
    {
        fail(directory_->errorString());
        return;
    }
    const auto target = QDir(directory_->path()).filePath(QStringLiteral("plugins/") + manifest.id);
    QDir().mkpath(target);
    QDirIterator files(source, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                       QDirIterator::Subdirectories);
    int count = 0;
    qint64 bytes = 0;
    while (files.hasNext())
    {
        const auto path = files.next();
        const auto info = files.fileInfo();
        const auto relative = QDir(source).relativeFilePath(path);
        if (info.isSymLink() || info.isJunction() || !Plugins::within(source, path) || ++count > 512 ||
            (info.isFile() && (bytes += info.size()) > 16 * 1024 * 1024))
        {
            fail(QStringLiteral("预览源包包含链接或超限"));
            return;
        }
        const auto destination = QDir(target).filePath(relative);
        if (info.isDir())
        {
            if (!QDir().mkpath(destination))
            {
                fail(QStringLiteral("无法创建预览目录"));
                return;
            }
        }
        else if (!QDir().mkpath(QFileInfo(destination).absolutePath()) || !QFile::copy(path, destination))
        {
            fail(QStringLiteral("无法复制预览源包"));
            return;
        }
    }
    const auto snapshot = Plugins::Manifest::read(target);
    if (!snapshot.valid() || snapshot.fingerprint != manifest.fingerprint)
    {
        fail(QStringLiteral("复制期间插件源文件发生变化"));
        return;
    }
    plugin_ = manifest.id;
    fingerprint_ = snapshot.fingerprint;
    started_ = std::move(completion);
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QT_LOGGING_TO_CONSOLE"), QStringLiteral("1"));
    process_.setProcessEnvironment(environment);
    process_.setWorkingDirectory(target);
    process_.start(QCoreApplication::applicationFilePath(),
                   {QStringLiteral("--plugin-preview"), directory_->path(), manifest.id});
    const auto generation = ++generation_;
    QTimer::singleShot(15000, this, [this, generation] {
        if (generation == generation_ && started_)
        {
            failAll(QStringLiteral("预览启动超时"));
            process_.kill();
        }
    });
}
void PluginPreview::request(const QJsonObject &arguments, Completion completion)
{
    if (!running() || started_)
    {
        completion(Plugins::error(QStringLiteral("PREVIEW_STOPPED"), QStringLiteral("请先启动预览")), {});
        return;
    }
    const auto id = QString::number(++sequence_);
    auto request = arguments;
    request.insert(QStringLiteral("requestId"), id);
    const auto bytes = QJsonDocument(request).toJson(QJsonDocument::Compact);
    if (bytes.size() > Plugins::MaxMessageBytes || pending_.size() >= 16)
    {
        completion(Plugins::error(QStringLiteral("RATE_LIMITED"), QStringLiteral("预览请求过大或过多")), {});
        return;
    }
    pending_.insert(id, std::move(completion));
    process_.write(bytes + '\n');
    QTimer::singleShot(10000, this, [this, id] {
        if (pending_.contains(id))
        {
            auto callback = pending_.take(id);
            callback(Plugins::error(QStringLiteral("TIMEOUT"), QStringLiteral("预览请求超时")), {});
        }
    });
}
void PluginPreview::consume()
{
    buffer_ += process_.readAllStandardOutput();
    if (buffer_.size() > 2 * 1024 * 1024)
    {
        failAll(QStringLiteral("预览输出超额"));
        process_.kill();
        return;
    }
    while (buffer_.contains('\n'))
    {
        const auto line = buffer_.left(buffer_.indexOf('\n'));
        buffer_.remove(0, line.size() + 1);
        const auto result = QJsonDocument::fromJson(line).object();
        if (result.value(QStringLiteral("event")) == QStringLiteral("ready") && started_)
        {
            auto callback = std::move(started_);
            callback(result.value(QStringLiteral("error")).toObject(), result);
            continue;
        }
        const auto id = result.value(QStringLiteral("requestId")).toString();
        if (pending_.contains(id))
        {
            auto callback = pending_.take(id);
            callback(result.value(QStringLiteral("error")).toObject(),
                     result.value(QStringLiteral("result")));
        }
    }
}

int runPluginPreview(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    PreviewWindowFilter windows;
    app.installEventFilter(&windows);
    eApp->init();
    const auto args = app.arguments();
    const int index = args.indexOf(QStringLiteral("--plugin-preview"));
    if (index < 0 || index + 2 >= args.size())
        return 2;
    const auto directory = args.at(index + 1), plugin = args.at(index + 2);
    auto output = [](const QJsonObject &value) {
        const auto bytes = QJsonDocument(value).toJson(QJsonDocument::Compact);
        std::cout.write(bytes.constData(), bytes.size());
        std::cout << std::endl;
    };
    QString error;
    if (!ConfigManager::instance().load(QDir(directory).filePath(QStringLiteral("MainConfig.json")),
                                        &error) ||
        !ConfigManager::instance().save(&error))
    {
        output({{QStringLiteral("event"), QStringLiteral("ready")},
                {QStringLiteral("error"), Plugins::error(QStringLiteral("SAVE_FAILED"), error)}});
        return 2;
    }
    QDir().mkpath(QDir(directory).filePath(QStringLiteral("profiles")));
    const auto profile = QDir(directory).filePath(QStringLiteral("profiles/Default.json"));
    if (!ProfileManager::instance().load(profile, &error) || !ProfileManager::instance().save(&error))
    {
        output({{QStringLiteral("event"), QStringLiteral("ready")},
                {QStringLiteral("error"), Plugins::error(QStringLiteral("SAVE_FAILED"), error)}});
        return 2;
    }
    auto fixture = ProfileManager::instance().profile();
    fixture.name = QStringLiteral("隔离预览");
    fixture.subjects = {Subject(QStringLiteral("语文")), Subject(QStringLiteral("数学"))};
    WeekSchedule week(QStringLiteral("模拟课程"), WeekScheduleMode::All);
    week.initDaySchedules();
    for (auto &day : week.daySchedules)
        day.classes = {Class(QStringLiteral("语文"), QTime(8, 0), QTime(8, 45)),
                       Class(QStringLiteral("数学"), QTime(9, 0), QTime(9, 45))};
    fixture.schedules = {week};
    if (!ProfileManager::instance().commit(fixture, &error))
    {
        output({{QStringLiteral("event"), QStringLiteral("ready")},
                {QStringLiteral("error"), Plugins::error(QStringLiteral("SAVE_FAILED"), error)}});
        return 2;
    }
    PluginManager manager(directory, true);
    manager.setPreviewMode(true);
    PluginUiHost ui(&manager);
    PluginCenterWindow center(&manager, &ui);
    ui.navigator = [&](const QString &id, const QString &page, const QString &instance, QString *error) {
        const bool opened = center.navigate(id, page, instance, error);
        if (opened) center.show();
        return opened;
    };
    manager.start();
    bool ready = false;
    auto initialized = [&] {
        const auto record = manager.record(plugin);
        if (ready || !record || record->state == QStringLiteral("启动中"))
            return;
        if (record->state == QStringLiteral("运行中"))
        {
            ready = true;
            for (const auto &page : record->pages)
                center.navigate(plugin, page.value(QStringLiteral("id")).toString());
            auto candidate = ConfigManager::instance().config();
            for (auto it = record->components.cbegin(); it != record->components.cend(); ++it)
            {
                CourseBarComponentConfig component;
                component.type = CourseBarComponentType::Plugin;
                component.typeId = plugin + QLatin1Char('/') + it.key();
                component.instanceId = QStringLiteral("preview-") + it.key();
                candidate.courseBarConfig.components.append(component);
            }
            QString configError;
            if (!ConfigManager::instance().commit(candidate, &configError))
                output(
                    {{QStringLiteral("event"), QStringLiteral("previewError")},
                     {QStringLiteral("error"), Plugins::error(QStringLiteral("SAVE_FAILED"), configError)}});
            for (auto it = record->components.cbegin(); it != record->components.cend(); ++it)
            {
                const auto instance = QStringLiteral("preview-") + it.key();
                auto *view = ui.createComponent(plugin + QLatin1Char('/') + it.key(), instance, {}, &center);
                if (view)
                {
                    view->setWindowFlag(Qt::Window);
                    if (auto *native = qobject_cast<PluginNativeView *>(view))
                    {
                        native->applyMetrics(CourseBarMetrics(candidate.courseBarConfig));
                        QObject::connect(native, &PluginNativeView::availabilityChanged, native,
                                [native] { native->resize(native->sizeHint()); });
                    }
                    view->resize(view->sizeHint());
                    view->show();
                }
                if (it->contains(QStringLiteral("settingsUi")))
                    center.navigate({}, {}, instance);
            }
            if (!record->pages.isEmpty())
                center.navigate(plugin, {});
            center.show();
            output({{QStringLiteral("event"), QStringLiteral("ready")},
                    {QStringLiteral("plugin"), plugin},
                    {QStringLiteral("views"), ui.snapshots()}});
        }
        else if (!record->failure.isEmpty())
        {
            ready = true;
            output(
                {{QStringLiteral("event"), QStringLiteral("ready")},
                 {QStringLiteral("error"), Plugins::error(QStringLiteral("PLUGIN_ERROR"), record->failure)}});
        }
    };
    QObject::connect(&manager, &PluginManager::changed, &app, initialized);
    if (!manager.setEnabled(plugin, true, &error))
    {
        output({{QStringLiteral("event"), QStringLiteral("ready")},
                {QStringLiteral("error"), Plugins::error(QStringLiteral("PLUGIN_ERROR"), error)}});
        return 2;
    }
    // stdin is blocking on Windows; only JSON commands cross into the GUI thread.
    std::thread reader([&] {
        std::string line;
        while (std::getline(std::cin, line))
        {
            const auto request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
            const bool shutdown = request.value(QStringLiteral("operation")) == QStringLiteral("shutdown");
            QMetaObject::invokeMethod(
                &app,
                [&, request] {
                    const auto operation = request.value(QStringLiteral("operation")).toString();
                    if (operation == QStringLiteral("shutdown"))
                    {
                        QCoreApplication::exit(0);
                        return;
                    }
                    QJsonValue result;
                    QString failure;
                    if (operation == QStringLiteral("tree"))
                        result = ui.snapshots();
                    else if (operation == QStringLiteral("logs"))
                    {
                        const auto record = manager.record(plugin);
                        result = record ? QJsonArray::fromStringList(record->logs) : QJsonArray();
                    }
                    else if (operation == QStringLiteral("screenshot"))
                    {
                        const auto path = QDir(directory).filePath(QStringLiteral("preview.png"));
                        if (center.grab().save(path))
                            result = QJsonObject{{QStringLiteral("path"), path},
                                                 {QStringLiteral("width"), center.width()},
                                                 {QStringLiteral("height"), center.height()}};
                        else
                            failure = QStringLiteral("截图保存失败");
                    }
                    else if (operation == QStringLiteral("navigate"))
                    {
                        result = center.navigate(plugin, request.value(QStringLiteral("page")).toString(),
                                                 request.value(QStringLiteral("componentInstance")).toString(), &failure);
                    }
                    else if (QStringList{QStringLiteral("get"), QStringLiteral("set"),
                                         QStringLiteral("invoke")}
                                 .contains(operation))
                        result = ui.inspect(request.value(QStringLiteral("instanceId")).toString(), operation,
                                            request.value(QStringLiteral("arguments")).toObject(), &failure);
                    else
                        failure = QStringLiteral("未知预览操作");
                    output(
                        {{QStringLiteral("requestId"), request.value(QStringLiteral("requestId"))},
                         {QStringLiteral("result"), result},
                         {QStringLiteral("error"),
                          failure.isEmpty() ? QJsonObject()
                                            : Plugins::error(QStringLiteral("INVALID_ARGUMENT"), failure)}});
                },
                Qt::QueuedConnection);
            if (shutdown)
                return;
        }
        QMetaObject::invokeMethod(&app, [] { QCoreApplication::exit(0); }, Qt::QueuedConnection);
    });
    const int code = app.exec();
    reader.join();
    manager.stopAll();
    return code;
}
