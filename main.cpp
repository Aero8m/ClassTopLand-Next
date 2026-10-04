#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QProcess>
#include <ElaApplication.h>
#include "src/Core/AppComposer/AppComposer.h"
#include "src/TaskbarTrayMenu/TaskbarTrayMenu.h"
int main(int argc, char* argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ClassTopLand-Next"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/res/images/icon.png")));
    eApp->init();

    const QString workingDirectory = QDir::currentPath();
    int exitCode;
    {
        AppComposer app;
        if (!app.startApp()) return 1;
        exitCode = QApplication::exec();
        // Tear down windows and persist state while the application object is
        // still alive, instead of relying on scope-exit destructor ordering.
        app.shutdown();
    }
    if (exitCode == TaskbarTrayMenu::RestartExitCode) {
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
