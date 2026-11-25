#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QProcess>
#include <QTimer>
#include <ElaApplication.h>
#include "src/Core/AppComposer/AppComposer.h"
#include "src/Core/Plugins/PluginPreview.h"
#include "src/TaskbarTrayMenu/TaskbarTrayMenu.h"
#include "src/Utils/SingleInstance/SingleInstance.h"
#include "src/Utils/UiAccess/UiAccess.h"
#ifdef Q_OS_WIN
#include "src/Utils/Update/UpdatePlatform.h"
#endif

namespace {
bool acquireInstance(SingleInstance& instance, int& argc, char* argv[])
{
    QString error;
    const auto result = instance.acquire(&error);
    if (result == SingleInstance::Result::Acquired) return true;

    // Only rejected cold launches need a temporary QApplication for the dialog.
    std::unique_ptr<QApplication> dialogApplication;
    if (!QCoreApplication::instance()) {
        dialogApplication = std::make_unique<QApplication>(argc, argv);
        QApplication::setWindowIcon(QIcon(QStringLiteral(":/res/images/icon.png")));
    }
    if (result == SingleInstance::Result::AlreadyRunning)
        QMessageBox::information(nullptr, QStringLiteral("程序已经运行"),
                                 QStringLiteral("ClassTopLand Next 已经运行，请在系统托盘中打开已有程序。"));
    else
        QMessageBox::critical(nullptr, QStringLiteral("启动失败"), error);
    return false;
}
}

int main(int argc, char* argv[])
{
    for(int index=1;index<argc;++index)
        if(QString::fromLocal8Bit(argv[index])==QStringLiteral("--plugin-preview")) return runPluginPreview(argc,argv);
#ifdef Q_OS_WIN
    if (UpdatePlatform::installationBusy()) {
        QApplication application(argc, argv);
        QMessageBox::information(nullptr, QStringLiteral("正在更新"),
                                 QStringLiteral("请等待 ClassTopLand Next 安装完成。"));
        return 1;
    }
#endif
    const auto startup = UiAccess::inspectStartup(argc, argv, DATA_PATH);
    if (startup.exitRequested) return startup.exitCode;
    SingleInstance instance(startup.dataDirectory);
    // A prepared successor must acknowledge initialization and wait for its
    // predecessor to exit before acquiring the predecessor's data lock.
    if (!startup.handshake && !acquireInstance(instance, argc, argv)) return 1;
    QString startupError;
    std::optional<bool> startupUiAccessOverride;
    const QString configPath = QDir(startup.dataDirectory).filePath(QStringLiteral("MainConfig.json"));
    // Bootstrap before Qt creates native windows. Malformed config is reported
    // by AppComposer's existing startup path, without prompting for elevation.
    if (UiAccess::supported() && !startup.handshake && ConfigManager::instance().load(configPath)) {
        const bool requested = ConfigManager::instance().config().courseBarConfig.uiAccessEnabled;
        if (requested != UiAccess::enabled()) {
            UiAccess::Restart restart;
            if (restart.prepare(requested, startup.dataDirectory, startup.arguments, &startupError)) {
                restart.commit();
                return 0;
            }
            Config fallback = ConfigManager::instance().config();
            fallback.courseBarConfig.uiAccessEnabled = UiAccess::enabled();
            startupUiAccessOverride = fallback.courseBarConfig.uiAccessEnabled;
            QString saveError;
            if (!ConfigManager::instance().commit(fallback, &saveError))
                startupError += QStringLiteral("\n无法保存回退设置：%1\n本次运行仍使用实际可用的置顶模式，未修改磁盘配置。")
                                    .arg(saveError);
        }
    }
    QApplication a(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setApplicationName(QStringLiteral("ClassTopLand Next"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/res/images/icon.png")));
    eApp->init();
    QString handoffError;
    if (!UiAccess::finishStartup(startup, &handoffError)) return 1;
    if (startup.handshake && !acquireInstance(instance, argc, argv)) return 1;

    const QString workingDirectory = QDir::currentPath();
    std::unique_ptr<UiAccess::Restart> preparedRestart;
#ifdef Q_OS_WIN
    UpdatePlatform::Handoff preparedUpdate;
    QTimer::singleShot(3000, &a, [] { UpdatePlatform::cleanCompleted(); });
#endif
    int exitCode;
    {
        AppComposer app(startup.dataDirectory);
        app.setPluginsDisabled(startup.arguments.contains(QStringLiteral("--disable-plugins")));
        if (startupUiAccessOverride) app.setStartupUiAccessOverride(*startupUiAccessOverride);
        if (UiAccess::supported()) {
            app.setRestartHandler([&](const Config& config, const QString& directory, QString* error) {
                preparedRestart = std::make_unique<UiAccess::Restart>();
                return preparedRestart->prepare(config.courseBarConfig.uiAccessEnabled,
                                                directory, startup.arguments, error);
            }, [&] { preparedRestart.reset(); });
        }
#ifdef Q_OS_WIN
        app.setUpdateHandler([&](const UpdateInstaller& installer, QString* error) {
            return preparedUpdate.prepare(installer, startup.arguments, workingDirectory, error);
        });
#endif
        if (!app.startApp()) return 1;
        if (!startupError.isEmpty()) {
            Logger::instance().log(Logger::Level::Warning,
                                   QStringLiteral("Could not start in the configured UIAccess mode: %1").arg(startupError));
            Logger::instance().flush();
            QTimer::singleShot(0, &a, [startupError] {
                QMessageBox::warning(nullptr, QStringLiteral("增强置顶启动失败"), startupError);
            });
        }
        exitCode = QApplication::exec();
        // Tear down windows and persist state while the application object is
        // still alive, instead of relying on scope-exit destructor ordering.
        app.shutdown();
    }
#ifdef Q_OS_WIN
    if (exitCode == TaskbarTrayMenu::UpdateExitCode) {
        instance.release();
        preparedUpdate.commit();
        return 0;
    }
#endif
    if (exitCode == TaskbarTrayMenu::RestartExitCode) {
        if (preparedRestart) {
            preparedRestart->commit();
            return 0;
        }
        QStringList arguments = QCoreApplication::arguments();
        arguments.removeFirst();
        // The fallback restart has no handoff protocol; business teardown and
        // persistence are complete, so release before launching its successor.
        instance.release();
        if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments, workingDirectory)) {
            const QString error = QStringLiteral("无法重启 ClassTopLand-Next，请手动重新启动应用。");
            Logger::instance().log(Logger::Level::Error, "Could not restart ClassTopLand-Next. Please restart the application manually.");
            Logger::instance().flush();
            QMessageBox::critical(nullptr, QStringLiteral("重启失败"), error);
            return 1;
        }
        return 0;
    }
    return exitCode;
}
