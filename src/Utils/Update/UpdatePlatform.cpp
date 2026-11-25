#include "UpdatePlatform.h"
#include "UpdateWindows.h"
#include "../../Core/Update/UpdateService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <vector>

namespace {
bool registeredApp()
{
    const auto directory = UpdateWindows::registeredDirectory();
    if (directory.empty()) return false;

    const auto installed = directory + L"\\" + UpdateWindows::AppName;
    const auto current = UpdateWindows::modulePath();
    if (current.empty() || CompareStringOrdinal(current.c_str(), -1, installed.c_str(), -1, TRUE) != CSTR_EQUAL)
        return false;
    return QFileInfo(QDir(QString::fromStdWString(directory)).filePath(
        QString::fromWCharArray(UpdateWindows::LauncherName))).isFile();
}

QString winError(const QString& message, DWORD code)
{
    return message + QStringLiteral(" (Windows %1)").arg(code);
}
}

bool UpdatePlatform::installed()
{
#if defined(_M_X64) || defined(__x86_64__)
    return registeredApp();
#else
    return false;
#endif
}

bool UpdatePlatform::installationBusy()
{
    using namespace UpdateWindows;
    const auto path = modulePath();
    const auto directory = registeredDirectory();
    if (directory.empty() || path.empty()
        || CompareStringOrdinal(path.c_str(), -1, (directory + L"\\" + AppName).c_str(), -1, TRUE) != CSTR_EQUAL)
        return false;

    Handle gate(OpenEventW(SYNCHRONIZE, FALSE, GateName));
    return gate || GetLastError() == ERROR_ACCESS_DENIED;
}

void UpdatePlatform::cleanCompleted()
{
    QDir temp = QDir::temp();
    const auto jobs = temp.entryInfoList({QStringLiteral("ClassTopLand-Next-update-*")}, QDir::Dirs | QDir::NoSymLinks);
    for (const auto& job : jobs) {
        const QDir directory(job.absoluteFilePath());
        const auto marker = directory.filePath(QString::fromWCharArray(UpdateWindows::Completed));
        if (!QFileInfo(marker).isFile()) continue;
        const auto helper = directory.filePath(QString::fromWCharArray(UpdateWindows::LauncherName));
        if (QFile::exists(helper) && !QFile::remove(helper)) continue;
        for (const auto& name : directory.entryList({QStringLiteral("ClassTopLand-Next-setup_build_*-amd64.exe")},
                                                    QDir::Files | QDir::NoSymLinks))
            QFile::remove(directory.filePath(name));
        QFile::remove(marker);
        temp.rmdir(job.fileName());
    }
}

UpdatePlatform::Handoff::~Handoff()
{
    if (commit_) CloseHandle(commit_);
}

bool UpdatePlatform::Handoff::prepare(const UpdateInstaller& installer, const QStringList& arguments,
                                      const QString& workingDirectory, QString* error)
{
    using namespace UpdateWindows;
    if (commit_ || !installed() || installer.sha256.size() != 32 || installer.size <= 0
        || !QFileInfo(installer.path).isFile()) {
        *error = QStringLiteral("当前无法准备安装。");
        return false;
    }

    const QFileInfo package(installer.path);
    const auto helperName = QString::fromWCharArray(LauncherName);
    const auto helper = package.dir().filePath(helperName);
    const auto source = QDir(QString::fromStdWString(registeredDirectory())).filePath(helperName);
    if (!QFile::exists(helper) && !QFile::copy(source, helper)) {
        *error = QStringLiteral("无法准备更新辅助程序。");
        return false;
    }

    SECURITY_ATTRIBUTES inheritable{sizeof(inheritable), nullptr, TRUE};
    Handle gate(CreateEventW(&inheritable, TRUE, FALSE, GateName));
    const DWORD gateError = GetLastError();
    if (!gate || gateError == ERROR_ALREADY_EXISTS) {
        *error = gateError == ERROR_ALREADY_EXISTS || gateError == ERROR_ACCESS_DENIED
            ? QStringLiteral("另一个安装正在进行。")
            : winError(QStringLiteral("无法锁定安装目录。"), gateError);
        return false;
    }
    Handle parent(OpenProcess(SYNCHRONIZE, TRUE, GetCurrentProcessId()));
    Handle commit(CreateEventW(&inheritable, TRUE, FALSE, nullptr));
    if (!parent || !commit) {
        *error = winError(QStringLiteral("无法准备退出交接。"), GetLastError());
        return false;
    }

    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    if (!bytes) {
        *error = winError(QStringLiteral("无法准备安装进程。"), GetLastError());
        return false;
    }
    std::vector<unsigned char> buffer(bytes);
    auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buffer.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &bytes)) {
        *error = winError(QStringLiteral("无法准备安装进程。"), GetLastError());
        return false;
    }

    const auto nativeHelper = QDir::toNativeSeparators(helper).toStdWString();
    std::wstring command = quote(nativeHelper);
    HANDLE handles[]{parent.get(), commit.get(), gate.get()};
    for (const HANDLE handle : handles) command += L" " + std::to_wstring(reinterpret_cast<quintptr>(handle));
    command += L" " + quote(package.fileName().toStdWString())
            + L" " + std::to_wstring(installer.size)
            + L" " + QString::fromLatin1(installer.sha256.toHex()).toStdWString();
    command += L" " + quote(QDir::toNativeSeparators(workingDirectory).toStdWString());
    for (const auto& argument : arguments) command += L" " + quote(argument.toStdWString());

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = list;
    PROCESS_INFORMATION process{};
    const auto directory = QDir::toNativeSeparators(package.absolutePath()).toStdWString();
    const bool started = UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                                   handles, sizeof(handles), nullptr, nullptr)
        && CreateProcessW(nativeHelper.c_str(), command.data(), nullptr, nullptr, TRUE,
                          EXTENDED_STARTUPINFO_PRESENT, nullptr, directory.c_str(), &startup.StartupInfo, &process);
    const DWORD startError = GetLastError();
    DeleteProcThreadAttributeList(list);
    if (!started) {
        *error = winError(QStringLiteral("无法启动更新辅助程序。"), startError);
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    commit_ = commit.release();
    return true;
}

void UpdatePlatform::Handoff::commit()
{
    if (commit_) SetEvent(commit_);
}
