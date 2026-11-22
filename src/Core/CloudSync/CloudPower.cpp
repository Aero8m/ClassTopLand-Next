#include "CloudPower.h"

#ifdef Q_OS_WIN
#include <QCryptographicHash>
#include <QProcess>
#include <QTimer>
#include <windows.h>
#endif

bool CloudPower::supported()
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

void CloudPower::readBootId(QObject* owner, std::function<void(const QString&)> done)
{
#ifdef Q_OS_WIN
    wchar_t systemDirectory[MAX_PATH]{};
    const auto length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    auto* process = new QProcess(owner);
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    QObject::connect(process, &QProcess::finished, process, [process, done = std::move(done)](int code, QProcess::ExitStatus status) {
        const auto output = process->readAllStandardOutput().trimmed();
        bool validTime = false;
        const auto timestamp = output.toULongLong(&validTime);
        if (status == QProcess::NormalExit && code == 0 && validTime && timestamp > 0) {
            const auto digest = QCryptographicHash::hash(QByteArray::number(timestamp), QCryptographicHash::Sha256);
            done(QString::fromLatin1(digest.toHex().left(32)));
        }
        process->deleteLater();
    });
    QObject::connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) process->deleteLater();
    });
    QTimer::singleShot(5000, process, &QProcess::kill);
    process->start(QString::fromWCharArray(systemDirectory, length)
                       + QStringLiteral("\\WindowsPowerShell\\v1.0\\powershell.exe"),
                   {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                    QStringLiteral("-Command"),
                    QStringLiteral("[Console]::Write((Get-CimInstance -ClassName Win32_OperatingSystem "
                                   "-Property LastBootUpTime -ErrorAction Stop).LastBootUpTime.ToFileTimeUtc())")});
#else
    Q_UNUSED(owner);
    Q_UNUSED(done);
#endif
}

bool CloudPower::request(bool restart, QString* error)
{
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    DWORD code = ERROR_SUCCESS;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        code = GetLastError();
    } else {
        TOKEN_PRIVILEGES privileges{};
        TOKEN_PRIVILEGES previous{};
        DWORD previousSize = sizeof(previous);
        privileges.PrivilegeCount = 1;
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (!LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &privileges.Privileges[0].Luid) ||
            !AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(previous), &previous, &previousSize) ||
            GetLastError() == ERROR_NOT_ALL_ASSIGNED) {
            code = GetLastError();
        } else {
            if (!InitiateSystemShutdownExW(nullptr, nullptr, 0, FALSE, restart,
                                          SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_FLAG_PLANNED))
                code = GetLastError();
            AdjustTokenPrivileges(token, FALSE, &previous, 0, nullptr, nullptr);
        }
        CloseHandle(token);
    }
    if (code == ERROR_SUCCESS) return true;
    if (error) {
        if (code == ERROR_ACCESS_DENIED || code == ERROR_NOT_ALL_ASSIGNED || code == ERROR_PRIVILEGE_NOT_HELD)
            *error = QObject::tr("运行客户端的 Windows 账户没有关闭系统权限");
        else
            *error = QObject::tr("Windows 拒绝了电源请求（错误 %1）").arg(code);
    }
#else
    Q_UNUSED(restart);
    if (error) *error = QStringLiteral("Windows power control is unavailable on this platform");
#endif
    return false;
}
