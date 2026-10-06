#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QProcess>
#include <QTimer>
#include <ElaApplication.h>
#include "src/Core/AppComposer/AppComposer.h"
#include "src/TaskbarTrayMenu/TaskbarTrayMenu.h"
#include "src/Utils/UiAccess/UiAccess.h"
int main(int argc, char* argv[])
{
    const auto startup = UiAccess::inspectStartup(argc, argv, DATA_PATH);
    if (startup.exitRequested) return startup.exitCode;
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
    eApp->setWindowDisplayMode(ElaApplicationType::Mica);
    QString handoffError;
    if (!UiAccess::finishStartup(startup, &handoffError)) return 1;

    const QString workingDirectory = QDir::currentPath();
    std::unique_ptr<UiAccess::Restart> preparedRestart;
    int exitCode;
    {
        AppComposer app(startup.dataDirectory);
        if (startupUiAccessOverride) app.setStartupUiAccessOverride(*startupUiAccessOverride);
        if (UiAccess::supported()) {
            app.setRestartHandler([&](const Config& config, const QString& directory, QString* error) {
                preparedRestart = std::make_unique<UiAccess::Restart>();
                return preparedRestart->prepare(config.courseBarConfig.uiAccessEnabled,
                                                directory, startup.arguments, error);
            }, [&] { preparedRestart.reset(); });
        }
        if (!app.startApp()) return 1;
        if (!startupError.isEmpty()) {
            Logger::instance().log(Logger::Level::Warning, startupError);
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
    if (exitCode == TaskbarTrayMenu::RestartExitCode) {
        if (preparedRestart) {
            preparedRestart->commit();
            return 0;
        }
        QStringList arguments = QCoreApplication::arguments();
        arguments.removeFirst();
        if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments, workingDirectory)) {
            const QString error = QStringLiteral("无法重启 ClassTopLand-Next，请手动重新启动应用。");
            Logger::instance().log(Logger::Level::Error, error);
            Logger::instance().flush();
            QMessageBox::critical(nullptr, QStringLiteral("重启失败"), error);
            return 1;
        }
        return 0;
    }
    return exitCode;
}
