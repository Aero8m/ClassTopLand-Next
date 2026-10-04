#include "AppComposer.h"
#include "../../TaskbarTrayMenu/TaskbarTrayMenu.h"

#include <QApplication>
#include <QFileInfo>

namespace {
bool reportStartupError(const QString& message)
{
    Logger::instance().log(Logger::Level::Error, message);
    Logger::instance().flush();
    QMessageBox::critical(nullptr, QStringLiteral("错误"), message);
    return false;
}
}

AppComposer::AppComposer(QObject* parent) : QObject(parent)
{

}

AppComposer::~AppComposer()
{
    dropInstances();
}

bool AppComposer::initInstances()
{
    if (initialized_) return true;
    // init logger
    Logger::instance().addConsoleOutput(true);
    printLogo();
    Logger::instance().log(Logger::Level::Info, "Initializing AppComposer...");
    Logger::instance().flush();

    // init config manager
    QString error;
    const QString configPath = CONFIG_FILE_PATH;
    const bool createConfig = !QFileInfo::exists(configPath);
    if (!ConfigManager::instance().load(configPath, &error)) {
        return reportStartupError(QStringLiteral("配置加载失败：%1\n%2").arg(configPath, error));
    }
    if (createConfig && !ConfigManager::instance().save(&error)) {
        return reportStartupError(QStringLiteral("默认配置保存失败：%1\n%2").arg(configPath, error));
    }
    // init profile manager
    const QString profilePath = GET_PROFILE_PATH(ConfigManager::instance().config().profileName);
    const bool createProfile = !QFileInfo::exists(profilePath);
    if (!ProfileManager::instance().load(profilePath, &error)) {
        return reportStartupError(QStringLiteral("档案加载失败：%1\n%2").arg(profilePath, error));
    }
    if (createProfile && !ProfileManager::instance().save(&error)) {
        return reportStartupError(QStringLiteral("默认档案保存失败：%1\n%2").arg(profilePath, error));
    }
    // init course bar
    courseBar = std::make_unique<CourseBar>();
    trayMenu_ = std::make_unique<TaskbarTrayMenu>();
    QApplication::setQuitOnLastWindowClosed(false);

    initialized_ = true;
    Logger::instance().log(Logger::Level::Info,
                           QStringLiteral("AppComposer initialized. Config: %1; Profile: %2")
                               .arg(configPath, profilePath));
    Logger::instance().flush();
    return true;
}

void AppComposer::dropInstances()
{
    if (!initialized_) return;
    trayMenu_.reset();
    courseBar.reset();
    QString error;
    if (!ConfigManager::instance().save(&error))
        Logger::instance().log(Logger::Level::Error, QStringLiteral("配置保存失败：%1").arg(error));
    if (!ProfileManager::instance().save(&error))
        Logger::instance().log(Logger::Level::Error, QStringLiteral("档案保存失败：%1").arg(error));
    Logger::instance().log(Logger::Level::Info, "AppComposer dropped, Application will be stopped");
    Logger::instance().flush();
    initialized_ = false;
}

bool AppComposer::startApp()
{
    if (!initInstances()) return false;
    courseBar->show();
    return initialized_;
}

void AppComposer::printLogo()
{
    QFile logoFile(":/res/logo.txt");
    if (logoFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        Logger::instance().print(Logger::Level::Debug, QString(logoFile.readAll()));
        Logger::instance().print(Logger::Level::Debug, "----------------------------------------------------------------------------------------\n");
        Logger::instance().print(Logger::Level::Debug, "                                Powered By Aero8m\n");
        Logger::instance().flush();
    }
    logoFile.close();
}
